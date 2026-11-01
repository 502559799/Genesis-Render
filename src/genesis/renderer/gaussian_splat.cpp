/*
 * Genesis Render - 3D Gaussian splat visualisation implementation
 */
#include "renderer/gaussian_splat.hpp"

#include "foundation_layer/genesis_log.hpp"
#include "renderer/command_buffer.hpp"
#include "renderer/render_system.hpp"
#include "resource/resource_types.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>
#include <unordered_map>

namespace Genesis
{
    namespace Gaussian
    {
        namespace
        {
            constexpr float kSHC0 = 0.28209479177387814f;

            enum class PlyType
            {
                Int8,
                UInt8,
                Int16,
                UInt16,
                Int32,
                UInt32,
                Float32,
                Float64,
                Unknown
            };

            size_t typeSize(PlyType type)
            {
                switch (type)
                {
                case PlyType::Int8:
                case PlyType::UInt8: return 1;
                case PlyType::Int16:
                case PlyType::UInt16: return 2;
                case PlyType::Int32:
                case PlyType::UInt32:
                case PlyType::Float32: return 4;
                case PlyType::Float64: return 8;
                default: return 0;
                }
            }

            PlyType typeFrom(const std::string &name)
            {
                if (name == "char" || name == "int8") return PlyType::Int8;
                if (name == "uchar" || name == "uint8") return PlyType::UInt8;
                if (name == "short" || name == "int16") return PlyType::Int16;
                if (name == "ushort" || name == "uint16") return PlyType::UInt16;
                if (name == "int" || name == "int32") return PlyType::Int32;
                if (name == "uint" || name == "uint32") return PlyType::UInt32;
                if (name == "float" || name == "float32") return PlyType::Float32;
                if (name == "double" || name == "float64") return PlyType::Float64;
                return PlyType::Unknown;
            }

            double readScalar(const uint8_t *data, PlyType type)
            {
                switch (type)
                {
                case PlyType::Int8: { int8_t v; std::memcpy(&v, data, 1); return v; }
                case PlyType::UInt8: { uint8_t v; std::memcpy(&v, data, 1); return v; }
                case PlyType::Int16: { int16_t v; std::memcpy(&v, data, 2); return v; }
                case PlyType::UInt16: { uint16_t v; std::memcpy(&v, data, 2); return v; }
                case PlyType::Int32: { int32_t v; std::memcpy(&v, data, 4); return v; }
                case PlyType::UInt32: { uint32_t v; std::memcpy(&v, data, 4); return v; }
                case PlyType::Float32: { float v; std::memcpy(&v, data, 4); return v; }
                case PlyType::Float64: { double v; std::memcpy(&v, data, 8); return v; }
                default: return 0.0;
                }
            }

            struct Property
            {
                std::string name;
                PlyType type = PlyType::Unknown;
            };

            float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

            float channel(double v)
            {
                return (float)(v > 1.0 ? v / 255.0 : v);
            }
        } // namespace

        void GaussianSplatCloud::clear()
        {
            loaded_ = false;
            source_path_.clear();
            splats_.clear();
            bounds_ = Math::AABB{};
            stats_ = LoadStats{};
        }

        bool GaussianSplatCloud::loadPly(const std::string &path, std::string *error)
        {
            clear();
            std::vector<uint8_t> file;
            if (!ResourceIO::readFile(path, file) || file.size() < 16)
            {
                if (error) *error = "cannot read splat file: " + path;
                return false;
            }
            const std::string text(reinterpret_cast<const char *>(file.data()),
                                   std::min<size_t>(file.size(), 1u << 20));
            if (text.compare(0, 3, "ply") != 0)
            {
                if (error) *error = "not a PLY file: " + path;
                return false;
            }
            const size_t header_end_marker = text.find("end_header");
            if (header_end_marker == std::string::npos)
            {
                if (error) *error = "PLY header has no end_header";
                return false;
            }
            const size_t header_end = text.find('\n', header_end_marker);
            if (header_end == std::string::npos)
            {
                if (error) *error = "truncated PLY header";
                return false;
            }

            bool ascii = false;
            bool little_endian = true;
            uint64_t vertex_count = 0;
            std::vector<Property> properties;
            {
                std::istringstream header(text.substr(0, header_end));
                std::string line;
                bool in_vertex = false;
                while (std::getline(header, line))
                {
                    std::istringstream ls(line);
                    std::string tag;
                    ls >> tag;
                    if (tag == "format")
                    {
                        std::string fmt;
                        ls >> fmt;
                        ascii = fmt == "ascii";
                        little_endian = fmt.find("little") != std::string::npos;
                        stats_.format = fmt;
                    }
                    else if (tag == "element")
                    {
                        std::string name;
                        uint64_t count = 0;
                        ls >> name >> count;
                        in_vertex = name == "vertex";
                        if (in_vertex) vertex_count = count;
                    }
                    else if (tag == "property" && in_vertex)
                    {
                        std::string type_name, prop_name;
                        ls >> type_name;
                        if (type_name == "list")
                        {
                            // Lists do not occur in vertex elements we use.
                            continue;
                        }
                        ls >> prop_name;
                        properties.push_back({prop_name, typeFrom(type_name)});
                    }
                }
            }
            if (vertex_count == 0 || properties.empty())
            {
                if (error) *error = "PLY has no vertex element";
                return false;
            }
            if (!ascii && !little_endian)
            {
                if (error) *error = "big endian PLY is not supported";
                return false;
            }
            stats_.binary = !ascii;
            stats_.vertices = vertex_count;
            splats_.reserve((size_t)vertex_count);

            std::unordered_map<std::string, size_t> index;
            for (size_t i = 0; i < properties.size(); ++i) index[properties[i].name] = i;
            auto has = [&](const char *name) { return index.find(name) != index.end(); };

            auto makeSplat = [&](const std::vector<double> &values) -> bool
            {
                auto value = [&](const char *name, double fallback) -> double
                {
                    auto it = index.find(name);
                    return it == index.end() ? fallback : values[it->second];
                };
                Splat splat;
                splat.position = Math::Vec3((float)value("x", 0.0), (float)value("y", 0.0), (float)value("z", 0.0));
                if (!std::isfinite(splat.position.x) || !std::isfinite(splat.position.y) ||
                    !std::isfinite(splat.position.z))
                {
                    ++stats_.skipped;
                    return false;
                }
                if (has("f_dc_0"))
                {
                    stats_.has_sh = true;
                    const float r = 0.5f + kSHC0 * (float)value("f_dc_0", 0.0);
                    const float g = 0.5f + kSHC0 * (float)value("f_dc_1", 0.0);
                    const float b = 0.5f + kSHC0 * (float)value("f_dc_2", 0.0);
                    splat.color = Math::Color(Math::saturate(r), Math::saturate(g), Math::saturate(b), 1.0f);
                    splat.opacity = sigmoid((float)value("opacity", 0.0));
                    splat.scale = Math::Vec3(std::exp((float)value("scale_0", -3.0)),
                                             std::exp((float)value("scale_1", -3.0)),
                                             std::exp((float)value("scale_2", -3.0)));
                    const Math::Quat q((float)value("rot_1", 0.0), (float)value("rot_2", 0.0),
                                       (float)value("rot_3", 0.0), (float)value("rot_0", 1.0));
                    splat.rotation = q.normalized();
                }
                else
                {
                    splat.color = Math::Color(channel(value("red", 255.0)), channel(value("green", 255.0)),
                                              channel(value("blue", 255.0)), 1.0f);
                    splat.opacity = (float)channel(value("alpha", 255.0));
                    const float s = (float)value("scale", 0.04) * (has("scale_0") ? std::exp((float)value("scale_0", -3.0)) : 1.0f);
                    splat.scale = Math::Vec3(std::max(1e-4f, s));
                }
                if (splat.opacity < 0.002f) { ++stats_.skipped; return false; }
                splats_.push_back(splat);
                return true;
            };

            if (ascii)
            {
                std::istringstream body(std::string(reinterpret_cast<const char *>(file.data()) + header_end + 1,
                                                    file.size() - header_end - 1));
                std::string line;
                std::vector<double> values(properties.size());
                for (uint64_t i = 0; i < vertex_count && std::getline(body, line); ++i)
                {
                    std::istringstream ls(line);
                    bool ok = true;
                    for (size_t p = 0; p < properties.size(); ++p)
                        if (!(ls >> values[p])) { ok = false; break; }
                    if (ok) makeSplat(values);
                    else ++stats_.skipped;
                }
            }
            else
            {
                size_t stride = 0;
                for (const Property &p : properties) stride += typeSize(p.type);
                if (stride == 0)
                {
                    if (error) *error = "PLY vertex has unknown property types";
                    return false;
                }
                const uint8_t *cursor = file.data() + header_end + 1;
                const uint8_t *end = file.data() + file.size();
                std::vector<double> values(properties.size());
                for (uint64_t i = 0; i < vertex_count; ++i)
                {
                    if (cursor + stride > end)
                    {
                        if (error) *error = "truncated PLY vertex data";
                        return false;
                    }
                    size_t offset = 0;
                    for (size_t p = 0; p < properties.size(); ++p)
                    {
                        values[p] = readScalar(cursor + offset, properties[p].type);
                        offset += typeSize(properties[p].type);
                    }
                    makeSplat(values);
                    cursor += stride;
                }
            }

            if (splats_.empty())
            {
                if (error) *error = "PLY contains no usable splats";
                return false;
            }
            bounds_.valid = true;
            bounds_.minPoint = splats_[0].position;
            bounds_.maxPoint = splats_[0].position;
            for (const Splat &s : splats_)
            {
                bounds_.minPoint = Math::min(bounds_.minPoint, s.position);
                bounds_.maxPoint = Math::max(bounds_.maxPoint, s.position);
            }
            loaded_ = true;
            source_path_ = path;
            GEN_LOG_INFO("[Splat] loaded %zu splats from %s (%s, %s)", splats_.size(), path.c_str(),
                         stats_.binary ? "binary" : "ascii", stats_.has_sh ? "3DGS attributes" : "point cloud");
            return true;
        }

        //==================================================================
        RHI::RHITexture *GaussianSplatRenderer::falloffTexture(Render::RenderSystem &render)
        {
            if (owner_ != &render) { falloff_ = Render::GpuTexture{}; owner_ = &render; }
            if (falloff_.valid()) return falloff_.texture;
            constexpr uint32_t size = 64;
            ImageData image;
            image.width = image.height = size;
            image.channels = 4;
            image.is_hdr = false;
            image.pixels.resize((size_t)size * size * 4);
            for (uint32_t y = 0; y < size; ++y)
                for (uint32_t x = 0; x < size; ++x)
                {
                    const float nx = (x + 0.5f) / size * 2.0f - 1.0f;
                    const float ny = (y + 0.5f) / size * 2.0f - 1.0f;
                    const float r2 = nx * nx + ny * ny;
                    const float a = r2 >= 1.0f ? 0.0f : std::exp(-4.5f * r2) * (1.0f - r2 * r2);
                    const uint8_t alpha = (uint8_t)(Math::saturate(a) * 255.0f);
                    uint8_t *p = &image.pixels[((size_t)y * size + x) * 4];
                    p[0] = p[1] = p[2] = 255;
                    p[3] = alpha;
                }
            falloff_ = render.scene().textures().createFromImage(image, "__gaussian_falloff", false, false);
            return falloff_.texture;
        }

        RHI::RHISampler *GaussianSplatRenderer::falloffSampler(Render::RenderSystem &render)
        {
            if (!falloff_.valid()) falloffTexture(render);
            return falloff_.sampler;
        }

        void GaussianSplatRenderer::record(Render::RenderSystem &render, Render::CommandBuffer &cb,
                                           const GaussianSplatCloud &cloud, const SplatSettings &settings)
        {
            stats_ = SplatRenderStats{};
            if (!cloud.loaded()) return;
            RHI::RHITexture *texture = falloffTexture(render);
            RHI::RHISampler *sampler = falloffSampler(render);
            if (!texture) return;

            const Render::CameraState &camera = render.camera();
            Render::RenderTexturePtr target = render.hdrTarget();
            const float width = target ? (float)target->width() : 1280.0f;
            const float height = target ? (float)target->height() : 720.0f;
            const float focal = 0.5f * height / std::tan(Math::toRadians(camera.fov_degrees) * 0.5f);
            const Math::Vec3 eye = camera.position;

            struct Item
            {
                float depth;
                float sx, sy, radius;
                Math::Color color;
            };
            std::vector<Item> items;
            size_t limit = cloud.count();
            if (settings.max_splats > 0) limit = std::min(limit, (size_t)settings.max_splats);
            items.reserve(limit);
            stats_.total = cloud.count();

            const auto project_start = std::chrono::steady_clock::now();
            for (size_t i = 0; i < limit; ++i)
            {
                const Splat &splat = cloud.splats()[i];
                const Math::Vec3 relative = splat.position - eye;
                const float depth = Math::dot(relative, camera.forward);
                if (depth <= camera.near_plane) continue;
                const Math::Vec4 clip = camera.view_projection * Math::Vec4(splat.position, 1.0f);
                if (clip.w <= 1e-4f) continue;
                const float ndc_x = clip.x / clip.w;
                const float ndc_y = clip.y / clip.w;
                const float sx = (ndc_x * 0.5f + 0.5f) * width;
                const float sy = (1.0f - (ndc_y * 0.5f + 0.5f)) * height;

                const float world_scale = std::max(splat.scale.x, std::max(splat.scale.y, splat.scale.z)) *
                                          settings.size_scale * 2.0f;
                float radius = world_scale * focal / depth;
                if (radius < 0.75f) radius = 0.75f;
                if (radius > settings.max_screen_radius) radius = settings.max_screen_radius;
                if (sx + radius < 0.0f || sx - radius > width || sy + radius < 0.0f || sy - radius > height)
                    continue;

                Item item;
                item.depth = depth;
                item.sx = sx;
                item.sy = sy;
                item.radius = radius;
                Math::Color base = settings.colored ? splat.color : Math::Color(1.0f, 1.0f, 1.0f, 1.0f);
                const float alpha = Math::saturate(splat.opacity * settings.opacity_scale);
                item.color = Math::Color(base.r * settings.tint.r, base.g * settings.tint.g,
                                         base.b * settings.tint.b, alpha * settings.tint.a);
                items.push_back(item);
            }
            stats_.visible = items.size();
            stats_.project_ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - project_start).count();

            const auto sort_start = std::chrono::steady_clock::now();
            if (settings.sort)
            {
                std::sort(items.begin(), items.end(),
                          [](const Item &a, const Item &b) { return a.depth > b.depth; });
            }
            stats_.sort_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - sort_start).count();

            for (const Item &item : items)
            {
                cb.drawImageTexture(texture, sampler, item.sx - item.radius, item.sy - item.radius,
                                    item.radius * 2.0f, item.radius * 2.0f, item.color);
            }
            stats_.drawn = items.size();
        }

    } // namespace Gaussian
} // namespace Genesis
