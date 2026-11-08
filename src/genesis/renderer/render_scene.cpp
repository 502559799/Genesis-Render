/*
 * Genesis Render - render scene implementation
 */
#include "renderer/render_scene.hpp"
#include "renderer/frame_resources.hpp"
#include "foundation_layer/genesis_log.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Genesis
{
    namespace Render
    {
        //==================================================================
        // CameraState
        //==================================================================
        void CameraState::updateFromTransform(const Math::Mat4 &world_matrix)
        {
            Math::Transform t = Math::Transform::fromMatrix(world_matrix);
            position = t.position;
            rotation = t.rotation.normalized();
        }

        void CameraState::lookAt(const Math::Vec3 &eye, const Math::Vec3 &target, const Math::Vec3 &world_up)
        {
            position = eye;
            Math::Vec3 dir = target - eye;
            if (Math::lengthSquared(dir) < 1e-10f) dir = Math::Vec3(0, 0, -1);
            rotation = Math::Quat::lookRotation(Math::normalize(dir), world_up);
        }

        void CameraState::update()
        {
            Math::Mat4 cam_world = Math::Mat4::compose(position, rotation, Math::Vec3(1.0f));
            forward = Math::normalize(-cam_world.zAxis());
            up = Math::normalize(cam_world.yAxis());
            right = Math::normalize(cam_world.xAxis());
            view = cam_world.inversed();

            if (orthographic)
            {
                float h = ortho_size * 0.5f;
                float w = h * aspect;
                projection = Math::Mat4::ortho(-w, w, -h, h, near_plane, far_plane, depth_zero_to_one);
            }
            else
            {
                projection = Math::Mat4::perspective(Math::toRadians(fov_degrees), aspect, near_plane, far_plane,
                                                     depth_zero_to_one);
            }
            view_projection = projection * view;
            inv_view_projection = view_projection.inversed();
            inv_view = cam_world;
        }

        Math::Ray CameraState::screenRay(float pixel_x, float pixel_y, float viewport_w, float viewport_h) const
        {
            Math::Ray ray;
            if (viewport_w <= 0.0f || viewport_h <= 0.0f) return ray;
            float ndc_x = Math::clampf(2.0f * (pixel_x / viewport_w) - 1.0f, -1.0f, 1.0f);
            // Screen y grows downwards, NDC y upwards.
            float ndc_y = Math::clampf(1.0f - 2.0f * (pixel_y / viewport_h), -1.0f, 1.0f);
            float near_z = depth_zero_to_one ? 0.0f : -1.0f;

            Math::Vec4 p_near = inv_view_projection * Math::Vec4(ndc_x, ndc_y, near_z, 1.0f);
            Math::Vec4 p_far = inv_view_projection * Math::Vec4(ndc_x, ndc_y, 1.0f, 1.0f);
            if (std::fabs(p_near.w) < 1e-6f || std::fabs(p_far.w) < 1e-6f) return ray;

            Math::Vec3 a = p_near.xyz() / p_near.w;
            Math::Vec3 b = p_far.xyz() / p_far.w;
            ray.origin = a;
            ray.direction = Math::normalize(b - a);
            return ray;
        }

        //==================================================================
        // TextureCache
        //==================================================================
        namespace
        {
            struct MipRequest
            {
                RHI::RHITexture *texture;
            };

            void recordMipGeneration(RHI::RHICommandBuffer *cb, void *user)
            {
                MipRequest *req = reinterpret_cast<MipRequest *>(user);
                cb->generateMipmaps(req->texture);
            }

            uint32_t mipLevelsFor(uint32_t w, uint32_t h)
            {
                uint32_t levels = 1;
                while (w > 1 || h > 1)
                {
                    w = std::max(1u, w / 2);
                    h = std::max(1u, h / 2);
                    ++levels;
                }
                return levels;
            }
        } // namespace

        TextureCache::TextureCache(RHI::RHIDevice *device) : device_(device) {}

        TextureCache::~TextureCache()
        {
            clear();
            for (auto &kv : samplers_)
                if (kv.second) device_->destroySampler(kv.second);
            samplers_.clear();
        }

        void TextureCache::clear()
        {
            for (auto &kv : cache_)
                if (kv.second.texture) device_->destroyTexture(kv.second.texture);
            cache_.clear();
            embedded_sources_.clear();
            white_ = black_ = normal_ = grey_ = black_cube_ = GpuTexture();
        }

        std::vector<GpuTexture> TextureCache::list() const
        {
            std::vector<GpuTexture> out;
            for (const auto &kv : cache_)
                if (kv.first.rfind("__default", 0) != 0) out.push_back(kv.second);
            std::sort(out.begin(), out.end(), [](const GpuTexture &a, const GpuTexture &b) { return a.key < b.key; });
            return out;
        }

        RHI::RHISampler *TextureCache::samplerFor(bool mipmap, RHI::AddressMode address)
        {
            char key[64];
            std::snprintf(key, sizeof(key), "%d|%d", (int)mipmap, (int)address);
            auto it = samplers_.find(key);
            if (it != samplers_.end()) return it->second;

            RHI::SamplerDesc desc;
            desc.min_filter = RHI::FilterMode::Linear;
            desc.mag_filter = RHI::FilterMode::Linear;
            desc.mipmap_mode = mipmap ? RHI::MipmapMode::Linear : RHI::MipmapMode::Nearest;
            desc.address_u = desc.address_v = desc.address_w = address;
            desc.max_anisotropy = mipmap ? 8.0f : 1.0f;
            desc.min_lod = 0.0f;
            desc.max_lod = mipmap ? 14.0f : 0.0f;
            desc.name = key;
            RHI::RHISampler *sampler = device_->createSampler(desc);
            samplers_[key] = sampler;
            return sampler;
        }

        GpuTexture TextureCache::createFromImage(const ImageData &image, const std::string &source_key, bool srgb, bool mipmap)
        {
            const std::string key = source_key + (srgb ? ":srgb" : ":linear") + (mipmap ? ":mips" : ":single");
            auto it = cache_.find(key);
            if (it != cache_.end()) return it->second;

            GpuTexture out;
            // Public source identity is used by Material::setTexture to detect
            // fallback textures; color space belongs only to the cache key.
            out.key = source_key;
            out.width = image.width;
            out.height = image.height;
            out.srgb = srgb;

            RHI::TextureDesc desc;
            desc.type = RHI::TextureType::Tex2D;
            desc.format = srgb ? RHI::PixelFormat::RGBA8_SRGB : RHI::PixelFormat::RGBA8_UNORM;
            desc.width = std::max(1u, image.width);
            desc.height = std::max(1u, image.height);
            desc.mip_levels = mipmap ? mipLevelsFor(image.width, image.height) : 1;
            desc.generate_mipmaps = mipmap && desc.mip_levels > 1;
            desc.usage = (uint32_t)RHI::TextureUsageFlagBits::Sampled | (uint32_t)RHI::TextureUsageFlagBits::TransferDst |
                         (uint32_t)RHI::TextureUsageFlagBits::TransferSrc;
            desc.name = key;
            out.texture = device_->createTexture(desc);
            if (!out.texture)
            {
                GEN_LOG_ERROR("[TextureCache] failed to create texture '%s'", key.c_str());
                return white();
            }

            std::vector<uint8_t> rgba = image.asRGBA8();
            device_->writeTexture(out.texture, rgba.data(), rgba.size(), image.width, image.height, 0, 0, 1);
            if (desc.generate_mipmaps)
            {
                MipRequest req{out.texture};
                device_->submitImmediate(recordMipGeneration, &req);
            }
            out.sampler = samplerFor(mipmap, RHI::AddressMode::Repeat);
            cache_[key] = out;
            return out;
        }

        GpuTexture TextureCache::acquirePath(const std::string &path, bool srgb)
        {
            TextureRef ref;
            ref.path = path;
            ref.srgb = srgb;
            ref.valid = !path.empty();
            return acquire(ref, srgb, "white");
        }

        GpuTexture TextureCache::acquire(const TextureRef &ref, bool srgb, const std::string &fallback_key)
        {
            auto fallback = [&]() -> GpuTexture
            {
                if (fallback_key == "black") return black();
                if (fallback_key == "normal") return flatNormal();
                if (fallback_key == "grey") return grey();
                return white();
            };
            if (!ref.valid) return fallback();

            std::string key;
            std::shared_ptr<ImageData> image = ref.embedded;
            if (!ref.path.empty())
            {
                key = ref.path;
                auto it = cache_.find(key + (srgb ? ":srgb:mips" : ":linear:mips"));
                if (it != cache_.end()) return it->second;
                if (!image) image = ResourceIO::loadImage(ref.path);
            }
            else if (image)
            {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "embedded:%p", (void *)image.get());
                key = image->name.empty() ? buf : (image->name + ":" + buf);
                auto it = cache_.find(key + (srgb ? ":srgb:mips" : ":linear:mips"));
                if (it != cache_.end()) return it->second;
            }
            if (!image)
            {
                GEN_LOG_WARN("[TextureCache] cannot resolve texture '%s' - using %s", ref.describe().c_str(), fallback_key.c_str());
                return fallback();
            }
            GpuTexture texture = createFromImage(*image, key, srgb, true);
            if (ref.embedded) embedded_sources_[texture.key] = ref.embedded;
            return texture;
        }

        static GpuTexture makeSolid(TextureCache &cache, const char *key, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            ImageData img;
            img.width = img.height = 4;
            img.channels = 4;
            img.pixels.resize(64);
            for (size_t i = 0; i < 16; ++i)
            {
                img.pixels[i * 4 + 0] = r;
                img.pixels[i * 4 + 1] = g;
                img.pixels[i * 4 + 2] = b;
                img.pixels[i * 4 + 3] = a;
            }
            return cache.createFromImage(img, key, false, false);
        }

        const GpuTexture &TextureCache::white()
        {
            if (!white_.texture) white_ = makeSolid(*this, "__default_white", 255, 255, 255, 255);
            return white_;
        }

        const GpuTexture &TextureCache::black()
        {
            if (!black_.texture) black_ = makeSolid(*this, "__default_black", 0, 0, 0, 255);
            return black_;
        }

        const GpuTexture &TextureCache::flatNormal()
        {
            if (!normal_.texture) normal_ = makeSolid(*this, "__default_normal", 128, 128, 255, 255);
            return normal_;
        }

        const GpuTexture &TextureCache::grey()
        {
            // glTF metallic-roughness convention: G = roughness, B = metallic.
            if (!grey_.texture) grey_ = makeSolid(*this, "__default_grey", 255, 255, 255, 255);
            return grey_;
        }

        const GpuTexture &TextureCache::blackCube()
        {
            if (black_cube_.texture) return black_cube_;
            RHI::TextureDesc desc;
            desc.type = RHI::TextureType::TexCube;
            desc.format = RHI::PixelFormat::RGBA8_UNORM;
            desc.width = desc.height = 1;
            desc.mip_levels = 1;
            desc.usage = (uint32_t)RHI::TextureUsageFlagBits::Sampled | (uint32_t)RHI::TextureUsageFlagBits::TransferDst;
            desc.name = "__default_black_cube";
            black_cube_.texture = device_->createTexture(desc);
            if (black_cube_.texture)
            {
                const uint8_t faces[6 * 4] = {0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
                device_->writeTexture(black_cube_.texture, faces, sizeof(faces), 1, 1, 0, 0, 6);
            }
            black_cube_.sampler = samplerFor(false, RHI::AddressMode::ClampToEdge);
            black_cube_.key = desc.name;
            black_cube_.width = black_cube_.height = 1;
            cache_[desc.name] = black_cube_;
            return black_cube_;
        }

        //==================================================================
        // RenderScene
        //==================================================================
        RenderScene::RenderScene(RHI::RHIDevice *device, RHI::RHIResourceLayout *mesh_layout, FrameAllocator *frames)
            : device_(device), mesh_layout_(mesh_layout), frames_(frames), texture_cache_(device)
        {
        }

        RenderScene::~RenderScene()
        {
            releaseAll();
        }

        void RenderScene::releaseAll()
        {
            for (auto &m : materials_)
            {
                if (m->descriptor_set) device_->destroyDescriptorSet(m->descriptor_set);
                m->descriptor_set = nullptr;
                m->owner = nullptr;
                m->bound_revision = 0;
            }
            materials_.clear();
            for (auto &m : meshes_)
            {
                if (m->vertex_buffer) device_->destroyBuffer(m->vertex_buffer);
                if (m->animated_vertex_buffer && m->animated_vertex_buffer != m->vertex_buffer)
                    device_->destroyBuffer(m->animated_vertex_buffer);
                if (m->index_buffer) device_->destroyBuffer(m->index_buffer);
            }
            meshes_.clear();
            default_material_index_ = -1;
            texture_cache_.clear();
        }

        uint32_t RenderScene::uploadMesh(const MeshData &mesh, const std::string &name)
        {
            auto gpu = std::make_unique<GpuMesh>();
            gpu->name = name.empty() ? mesh.name : name;
            gpu->vertex_count = (uint32_t)mesh.vertices.size();
            gpu->index_count = (uint32_t)mesh.indices.size();
            gpu->bounds = mesh.bounds;
            gpu->material_index = mesh.material_index;

            RHI::BufferDesc vbd;
            vbd.size = (uint64_t)mesh.vertices.size() * sizeof(Vertex);
            // Storage is required so compute kernels can animate the stream.
            vbd.usage = (uint32_t)RHI::BufferUsageFlagBits::Vertex |
                        (uint32_t)RHI::BufferUsageFlagBits::Storage |
                        (uint32_t)RHI::BufferUsageFlagBits::TransferDst;
            vbd.memory_usage = RHI::MemoryUsage::GpuOnly;
            vbd.initial_data = mesh.vertices.data();
            vbd.initial_data_size = vbd.size;
            vbd.name = gpu->name + ".vb";
            gpu->vertex_buffer = device_->createBuffer(vbd);

            const bool use_32bit = gpu->vertex_count > 65535u;
            gpu->index_type = use_32bit ? RHI::IndexType::UInt32 : RHI::IndexType::UInt16;
            std::vector<uint16_t> indices16;
            const void *index_data = mesh.indices.data();
            uint64_t index_bytes = (uint64_t)mesh.indices.size() * 4;
            if (!use_32bit)
            {
                indices16.resize(mesh.indices.size());
                for (size_t i = 0; i < mesh.indices.size(); ++i) indices16[i] = (uint16_t)mesh.indices[i];
                index_data = indices16.data();
                index_bytes = (uint64_t)indices16.size() * 2;
            }
            RHI::BufferDesc ibd;
            ibd.size = std::max<uint64_t>(index_bytes, 4);
            ibd.usage = (uint32_t)RHI::BufferUsageFlagBits::Index | (uint32_t)RHI::BufferUsageFlagBits::TransferDst;
            ibd.memory_usage = RHI::MemoryUsage::GpuOnly;
            ibd.initial_data = index_data;
            ibd.initial_data_size = index_bytes;
            ibd.name = gpu->name + ".ib";
            gpu->index_buffer = device_->createBuffer(ibd);

            if (!gpu->vertex_buffer || !gpu->index_buffer)
            {
                GEN_LOG_ERROR("[RenderScene] failed to upload mesh '%s'", gpu->name.c_str());
                return 0xFFFFFFFFu;
            }
            meshes_.push_back(std::move(gpu));
            return (uint32_t)meshes_.size() - 1;
        }

        int32_t RenderScene::addMaterial(const MaterialPtr &material)
        {
            if (!material) return -1;
            if (material->owner == this && material->index >= 0) return material->index;
            material->owner = this;
            material->index = (int32_t)materials_.size();
            materials_.push_back(material);
            return material->index;
        }

        MaterialPtr RenderScene::createMaterial(const std::string &name)
        {
            MaterialPtr m = std::make_shared<Material>(name);
            addMaterial(m);
            return m;
        }

        MaterialPtr RenderScene::cloneMaterial(const Material &source, const std::string &new_name)
        {
            MaterialPtr m = std::make_shared<Material>(new_name);
            addMaterial(m);
            m->copyFrom(source);
            return m;
        }

        MaterialPtr RenderScene::findMaterial(const std::string &name) const
        {
            for (const auto &m : materials_)
                if (m->name == name) return m;
            return nullptr;
        }

        int32_t RenderScene::createMaterial(const MaterialData &data)
        {
            MaterialPtr m = std::make_shared<Material>(data.name);
            addMaterial(m);
            // glTF stores base colour / emissive factors in linear space.
            m->base_color = data.base_color_factor;
            m->emissive = Math::Color(data.emissive_factor.r, data.emissive_factor.g, data.emissive_factor.b, 1.0f);
            m->emissive_strength = 1.0f;
            m->metallic = data.metallic_factor;
            m->roughness = data.roughness_factor;
            m->normal_scale = data.normal_scale;
            m->occlusion_strength = data.occlusion_strength;
            m->alpha_cutoff = data.alpha_cutoff;
            m->double_sided = data.double_sided;
            m->blend = data.alpha_mode == MaterialData::AlphaMode::Blend  ? MaterialBlend::Transparent
                       : data.alpha_mode == MaterialData::AlphaMode::Mask ? MaterialBlend::Masked
                                                                          : MaterialBlend::Opaque;

            auto assign = [&](TextureSlot slot, const TextureRef &ref, bool srgb)
            {
                if (!ref.valid) return;
                GpuTexture tex = texture_cache_.acquire(ref, srgb, "white");
                Material::Slot &s = m->slots[(size_t)slot];
                s.texture = tex;
                s.source = ref.path.empty() ? tex.key : ref.path;
                s.enabled = true;
            };
            assign(TextureSlot::BaseColor, data.base_color, true);
            assign(TextureSlot::MetallicRoughness, data.metallic_roughness, false);
            assign(TextureSlot::Normal, data.normal, false);
            assign(TextureSlot::Occlusion, data.occlusion, false);
            assign(TextureSlot::Emissive, data.emissive, true);
            ++m->texture_revision;
            return m->index;
        }

        Material *RenderScene::defaultMaterial()
        {
            if (default_material_index_ >= 0) return materials_[(size_t)default_material_index_].get();
            MaterialPtr m = createMaterial("__default_material");
            m->base_color = Math::Color(0.8f, 0.8f, 0.8f, 1.0f);
            m->metallic = 0.0f;
            m->roughness = 0.6f;
            default_material_index_ = m->index;
            return m.get();
        }

        bool RenderScene::prepareMaterial(Material &m)
        {
            // Render textures may have been resized (new GPU objects).
            for (auto &slot : m.slots)
            {
                if (slot.render_texture && slot.render_texture->generation() != slot.rt_generation)
                {
                    slot.rt_generation = slot.render_texture->generation();
                    ++m.texture_revision;
                }
            }
            if (m.descriptor_set && m.bound_revision == m.texture_revision) return true;
            if (!mesh_layout_ || !frames_ || !frames_->uniformBuffer()) return false;

            RHI::RHIDescriptorSet *set = device_->createDescriptorSet(mesh_layout_, 1);
            if (!set) return false;

            auto pick = [&](TextureSlot slot, const GpuTexture &fallback, RHI::RHITexture *&tex, RHI::RHISampler *&samp)
            {
                const Material::Slot &s = m.slots[(size_t)slot];
                tex = fallback.texture;
                samp = fallback.sampler;
                if (!s.enabled) return;
                if (s.render_texture && s.render_texture->valid())
                {
                    tex = s.render_texture->color();
                    samp = s.render_texture->sampler();
                }
                else if (s.texture.valid())
                {
                    tex = s.texture.texture;
                    samp = s.texture.sampler;
                }
            };

            RHI::DescriptorBufferInfo ubo{frames_->uniformBuffer(), 0, sizeof(MaterialUBO)};
            RHI::DescriptorImageInfo images[5];
            const GpuTexture *fallbacks[5] = {&texture_cache_.white(), &texture_cache_.grey(), &texture_cache_.flatNormal(),
                                              &texture_cache_.white(), &texture_cache_.black()};
            for (uint32_t i = 0; i < 5; ++i)
            {
                RHI::RHITexture *tex = nullptr;
                RHI::RHISampler *samp = nullptr;
                pick((TextureSlot)i, *fallbacks[i], tex, samp);
                images[i] = RHI::DescriptorImageInfo{tex, samp, RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1};
            }
            RHI::WriteDescriptorSet writes[6];
            writes[0] = RHI::WriteDescriptorSet{0, 0, 1, RHI::ResourceType::UniformBufferDynamic, &ubo, nullptr};
            for (uint32_t i = 0; i < 5; ++i)
                writes[i + 1] = RHI::WriteDescriptorSet{i + 1, 0, 1, RHI::ResourceType::CombinedImageSampler, nullptr, &images[i]};
            set->write(writes, 6);

            if (m.descriptor_set) frames_->release(m.descriptor_set);
            m.descriptor_set = set;
            m.bound_revision = m.texture_revision;
            return true;
        }

        void RenderScene::setMaterialBaseColor(int32_t index, const Math::Color &color)
        {
            if (Material *m = material(index)) { m->base_color = color; m->touch(); }
        }

        void RenderScene::setMaterialPBR(int32_t index, float metallic, float roughness)
        {
            if (Material *m = material(index))
            {
                m->metallic = metallic;
                m->roughness = roughness;
                m->touch();
            }
        }

        void RenderScene::setMaterialEmissive(int32_t index, const Math::Color &color, float strength)
        {
            if (Material *m = material(index))
            {
                m->emissive = color;
                m->emissive_strength = strength;
                m->touch();
            }
        }

        std::vector<uint32_t> RenderScene::uploadModel(const ModelData &model, const std::string &name_hint)
        {
            std::vector<uint32_t> mesh_indices;
            std::vector<int32_t> material_map(model.materials.size(), -1);
            for (size_t i = 0; i < model.materials.size(); ++i) material_map[i] = createMaterial(model.materials[i]);

            for (size_t i = 0; i < model.meshes.size(); ++i)
            {
                std::string name = model.meshes[i].name.empty()
                                       ? (name_hint.empty() ? model.name : name_hint) + "_mesh" + std::to_string(i)
                                       : model.meshes[i].name;
                uint32_t idx = uploadMesh(model.meshes[i], name);
                if (idx != 0xFFFFFFFFu)
                {
                    int32_t mi = model.meshes[i].material_index;
                    if (mi >= 0 && (size_t)mi < material_map.size() && material_map[mi] >= 0)
                        meshes_[idx]->material_index = material_map[mi];
                    else
                        meshes_[idx]->material_index = defaultMaterial()->index;
                }
                mesh_indices.push_back(idx);
            }
            return mesh_indices;
        }

    } // namespace Render
} // namespace Genesis
