/*
 * Genesis Render - render scene implementation
 */
#include "renderer/render_scene.hpp"
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
            Math::Mat4 basis = Math::Mat4::fromQuat(rotation);
            forward = -basis.zAxis();
            up = basis.yAxis();
        }

        void CameraState::update()
        {
            Math::Mat4 cam_world = Math::Mat4::compose(position, rotation, Math::Vec3(1.0f));
            forward = -cam_world.zAxis();
            up = cam_world.yAxis();
            view = cam_world.inversed();

            const bool zero_to_one = (backend == RHI::BackendType::Vulkan);
            if (orthographic)
            {
                float h = ortho_size * 0.5f;
                float w = h * aspect;
                projection = Math::Mat4::ortho(-w, w, -h, h, near_plane, far_plane, zero_to_one);
            }
            else
            {
                projection = Math::Mat4::perspective(Math::toRadians(fov_degrees), aspect, near_plane, far_plane, zero_to_one);
            }

            // Orientation
            // -----------
            // The projection is used UNFLIPPED on both backends.  Flipping Y in
            // clip space would also reverse triangle winding, which would force
            // per-target pipeline variants just to keep back-face culling right.
            //
            // Instead we accept the natural orientation of each backend:
            //   * both backends map NDC y = -1 to row 0 of the render target
            //     (GL: window origin bottom-left, Vulkan: y_fb = (ndc+1)/2*h),
            //   * with a GL style (y up) projection the BOTTOM of the view lands
            //     on row 0, so every offscreen image is "row 0 = bottom" and
            //     multi-pass chains stay self consistent on both backends.
            //
            // The single place where orientation is corrected is the final
            // present/tone mapping pass, which flips the sampling V coordinate
            // when needed (see RenderSystem::renderFrame / renderAndCapture).
            (void)target_is_swapchain;
            view_projection = projection * view;
            inv_view_projection = view_projection.inversed();
            inv_view = cam_world;
        }

        Math::Ray CameraState::screenRay(float pixel_x, float pixel_y, float viewport_w, float viewport_h) const
        {
            Math::Ray ray;
            if (viewport_w <= 0.0f || viewport_h <= 0.0f) return ray;
            float ndc_x = Math::clampf(2.0f * (pixel_x / viewport_w) - 1.0f, -1.0f, 1.0f);
            float ndc_y = Math::clampf(2.0f * (pixel_y / viewport_h) - 1.0f, -1.0f, 1.0f);
            float near_z = (backend == RHI::BackendType::Vulkan) ? 0.0f : -1.0f;

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
                RHI::TextureBarrier barrier{};
                barrier.texture = req->texture;
                barrier.old_layout = req->texture->currentLayout();
                barrier.new_layout = RHI::TextureLayout::TransferDstOptimal;
                barrier.base_mip = 0;
                barrier.mip_count = 1;
                barrier.base_layer = 0;
                barrier.layer_count = ~0u;
                cb->pipelineBarrier(&barrier, 1);
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
            {
                if (kv.second.owns)
                {
                    if (kv.second.texture) device_->destroyTexture(kv.second.texture);
                }
            }
            cache_.clear();
            defaults_created_ = false;
            white_ = GpuTexture();
            black_ = GpuTexture();
            normal_ = GpuTexture();
            grey_ = GpuTexture();
        }

        RHI::RHISampler *TextureCache::samplerFor(bool mipmap, RHI::AddressMode address, bool srgb)
        {
            (void)srgb;
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

        GpuTexture TextureCache::createFromImage(const ImageData &image, const std::string &key, bool srgb, bool mipmap)
        {
            auto it = cache_.find(key);
            if (it != cache_.end()) return it->second;

            GpuTexture out;
            out.key = key;
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
            desc.usage = (uint32_t)RHI::TextureUsageFlagBits::Sampled |
                         (uint32_t)RHI::TextureUsageFlagBits::TransferDst |
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
            out.sampler = samplerFor(mipmap, RHI::AddressMode::Repeat, srgb);
            cache_[key] = out;
            return out;
        }

        GpuTexture TextureCache::createCubeFromFaces(const std::vector<std::vector<uint8_t>> &faces, uint32_t size,
                                                     const std::string &key, bool srgb)
        {
            auto it = cache_.find(key);
            if (it != cache_.end()) return it->second;
            if (faces.size() != 6)
            {
                GEN_LOG_ERROR("[TextureCache] createCubeFromFaces needs exactly 6 faces");
                return white();
            }

            GpuTexture out;
            out.key = key;
            out.width = out.height = size;
            out.srgb = srgb;

            RHI::TextureDesc desc;
            desc.type = RHI::TextureType::TexCube;
            desc.format = srgb ? RHI::PixelFormat::RGBA8_SRGB : RHI::PixelFormat::RGBA8_UNORM;
            desc.width = size;
            desc.height = size;
            desc.array_layers = 1;
            desc.mip_levels = mipLevelsFor(size, size);
            desc.generate_mipmaps = true;
            desc.usage = (uint32_t)RHI::TextureUsageFlagBits::Sampled |
                         (uint32_t)RHI::TextureUsageFlagBits::TransferDst |
                         (uint32_t)RHI::TextureUsageFlagBits::TransferSrc;
            desc.name = key;
            out.texture = device_->createTexture(desc);
            if (!out.texture) return white();

            std::vector<uint8_t> all;
            all.reserve((size_t)size * size * 4 * 6);
            for (const auto &f : faces)
            {
                all.insert(all.end(), f.begin(), f.end());
                if (f.size() < (size_t)size * size * 4)
                    all.resize(all.size() + ((size_t)size * size * 4 - f.size()), 0);
            }
            device_->writeTexture(out.texture, all.data(), all.size(), size, size, 0, 0, 6);

            MipRequest req{out.texture};
            device_->submitImmediate(recordMipGeneration, &req);

            out.sampler = samplerFor(true, RHI::AddressMode::ClampToEdge, srgb);
            cache_[key] = out;
            return out;
        }

        GpuTexture TextureCache::acquire(const TextureRef &ref, bool srgb, const std::string &fallback_key)
        {
            if (!ref.valid)
            {
                if (fallback_key == "white") return white();
                if (fallback_key == "black") return black();
                if (fallback_key == "normal") return flatNormal();
                return white();
            }

            std::string key;
            std::shared_ptr<ImageData> image = ref.embedded;
            if (!ref.path.empty())
            {
                key = ref.path;
                auto it = cache_.find(key);
                if (it != cache_.end()) return it->second;
                if (!image) image = ResourceIO::loadImage(ref.path);
            }
            else if (image)
            {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "embedded:%p", (void *)image.get());
                key = image->name.empty() ? buf : (image->name + ":" + buf);
                auto it = cache_.find(key);
                if (it != cache_.end()) return it->second;
            }
            if (!image)
            {
                GEN_LOG_WARN("[TextureCache] cannot resolve texture '%s' - using %s", ref.describe().c_str(), fallback_key.c_str());
                if (fallback_key == "black") return black();
                if (fallback_key == "normal") return flatNormal();
                return white();
            }
            return createFromImage(*image, key, srgb, true);
        }

        const GpuTexture &TextureCache::white()
        {
            if (!white_.texture)
            {
                ImageData img;
                img.width = img.height = 4;
                img.channels = 4;
                img.pixels.assign(64, 255);
                white_ = createFromImage(img, "__default_white", false, false);
                white_.owns = false; // stays in the cache map
            }
            return white_;
        }

        const GpuTexture &TextureCache::black()
        {
            if (!black_.texture)
            {
                ImageData img;
                img.width = img.height = 4;
                img.channels = 4;
                img.pixels.assign(64, 0);
                for (size_t i = 3; i < img.pixels.size(); i += 4) img.pixels[i] = 255;
                black_ = createFromImage(img, "__default_black", false, false);
                black_.owns = false;
            }
            return black_;
        }

        const GpuTexture &TextureCache::flatNormal()
        {
            if (!normal_.texture)
            {
                ImageData img;
                img.width = img.height = 4;
                img.channels = 4;
                img.pixels.resize(64);
                for (size_t i = 0; i < 16; ++i)
                {
                    img.pixels[i * 4 + 0] = 128;
                    img.pixels[i * 4 + 1] = 128;
                    img.pixels[i * 4 + 2] = 255;
                    img.pixels[i * 4 + 3] = 255;
                }
                normal_ = createFromImage(img, "__default_normal", false, false);
                normal_.owns = false;
            }
            return normal_;
        }

        const GpuTexture &TextureCache::grey()
        {
            if (!grey_.texture)
            {
                ImageData img;
                img.width = img.height = 4;
                img.channels = 4;
                img.pixels.resize(64);
                for (size_t i = 0; i < 16; ++i)
                {
                    img.pixels[i * 4 + 0] = 200; // roughness-ish mid grey
                    img.pixels[i * 4 + 1] = 128;
                    img.pixels[i * 4 + 2] = 128;
                    img.pixels[i * 4 + 3] = 255;
                }
                grey_ = createFromImage(img, "__default_grey", false, false);
                grey_.owns = false;
            }
            return grey_;
        }

        //==================================================================
        // RenderScene
        //==================================================================
        RenderScene::RenderScene(RHI::RHIDevice *device, RHI::RHIResourceLayout *material_layout)
            : device_(device), material_layout_(material_layout), texture_cache_(device)
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
                if (m->ubo_buffer) device_->destroyBuffer(m->ubo_buffer);
                if (m->descriptor_set) device_->destroyDescriptorSet(m->descriptor_set);
            }
            materials_.clear();
            for (auto &m : meshes_)
            {
                if (m->vertex_buffer) device_->destroyBuffer(m->vertex_buffer);
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
            vbd.usage = (uint32_t)RHI::BufferUsageFlagBits::Vertex | (uint32_t)RHI::BufferUsageFlagBits::TransferDst;
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

        int32_t RenderScene::uploadMaterial(const MaterialData &material, const std::string &base_dir)
        {
            (void)base_dir;
            auto gpu = std::make_unique<GpuMaterial>();
            gpu->name = material.name;

            // Factors: glTF stores base colour / emissive factors already in
            // linear space, so no conversion is needed here.
            gpu->ubo.base_color_factor = GpuVec4::from(material.base_color_factor);
            gpu->ubo.emissive_factor = GpuVec4(material.emissive_factor.r, material.emissive_factor.g,
                                               material.emissive_factor.b, 1.0f);
            gpu->ubo.params0 = GpuVec4(material.metallic_factor, material.roughness_factor,
                                       material.normal_scale, material.occlusion_strength);
            gpu->ubo.params1 = GpuVec4(material.alpha_cutoff,
                                       (float)material.alpha_mode,
                                       material.double_sided ? 1.0f : 0.0f,
                                       0.0f); // shading mode, set by RenderSystem
            gpu->ubo.texture_mask = GpuVec4(material.base_color.valid ? 1.0f : 0.0f,
                                            material.metallic_roughness.valid ? 1.0f : 0.0f,
                                            material.normal.valid ? 1.0f : 0.0f,
                                            material.occlusion.valid ? 1.0f : 0.0f);
            gpu->ubo.extra = GpuVec4(material.emissive.valid ? 1.0f : 0.0f, 1.0f, 1.0f, 0.0f);

            gpu->base_color = texture_cache_.acquire(material.base_color, true, "white");
            gpu->metallic_roughness = texture_cache_.acquire(material.metallic_roughness, false, "grey");
            gpu->normal = texture_cache_.acquire(material.normal, false, "normal");
            gpu->occlusion = texture_cache_.acquire(material.occlusion, false, "white");
            gpu->emissive = texture_cache_.acquire(material.emissive, true, "black");

            gpu->transparent = (material.alpha_mode == MaterialData::AlphaMode::Blend);
            gpu->alpha_mask = (material.alpha_mode == MaterialData::AlphaMode::Mask);
            gpu->double_sided = material.double_sided;

            // Uniform buffer + descriptor set (set 1)
            RHI::BufferDesc ubd;
            ubd.size = sizeof(MaterialUBO);
            ubd.usage = (uint32_t)RHI::BufferUsageFlagBits::Uniform | (uint32_t)RHI::BufferUsageFlagBits::TransferDst;
            ubd.memory_usage = RHI::MemoryUsage::CpuToGpu;
            ubd.initial_data = &gpu->ubo;
            ubd.initial_data_size = sizeof(MaterialUBO);
            ubd.name = gpu->name + ".material_ubo";
            gpu->ubo_buffer = device_->createBuffer(ubd);

            if (material_layout_ && gpu->ubo_buffer)
            {
                // The material layout is the *combined* mesh layout (sets 0..2);
                // materials live in set 1, so the descriptor set must be
                // allocated from that set's layout, otherwise it would collide
                // with the scene set on the GL uniform block binding points.
                gpu->descriptor_set = device_->createDescriptorSet(material_layout_, 1);
                if (gpu->descriptor_set)
                {
                    RHI::DescriptorBufferInfo buf_info{gpu->ubo_buffer, 0, sizeof(MaterialUBO)};
                    RHI::DescriptorImageInfo images[5] = {
                        {gpu->base_color.texture, gpu->base_color.sampler, RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1},
                        {gpu->metallic_roughness.texture, gpu->metallic_roughness.sampler, RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1},
                        {gpu->normal.texture, gpu->normal.sampler, RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1},
                        {gpu->occlusion.texture, gpu->occlusion.sampler, RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1},
                        {gpu->emissive.texture, gpu->emissive.sampler, RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1},
                    };
                    RHI::WriteDescriptorSet writes[6];
                    writes[0] = RHI::WriteDescriptorSet{0, 0, 1, RHI::ResourceType::UniformBuffer, &buf_info, nullptr};
                    for (int i = 0; i < 5; ++i)
                        writes[i + 1] = RHI::WriteDescriptorSet{(uint32_t)(i + 1), 0, 1, RHI::ResourceType::CombinedImageSampler, nullptr, &images[i]};
                    gpu->descriptor_set->write(writes, 6);
                }
            }

            materials_.push_back(std::move(gpu));
            return (int32_t)materials_.size() - 1;
        }

        GpuMaterial *RenderScene::defaultMaterial()
        {
            if (default_material_index_ >= 0)
                return materials_[(size_t)default_material_index_].get();
            MaterialData m;
            m.name = "__default_material";
            m.base_color_factor = Math::Color(0.8f, 0.8f, 0.8f, 1.0f);
            m.metallic_factor = 0.0f;
            m.roughness_factor = 0.6f;
            default_material_index_ = uploadMaterial(m, "");
            return materials_[(size_t)default_material_index_].get();
        }

        size_t RenderScene::updateShadingMode(RHI::RHIDevice *device, float shading_mode)
        {
            size_t updated = 0;
            for (auto &m : materials_)
            {
                if (!m || !m->ubo_buffer) continue;
                if (m->ubo.params1.w == shading_mode) continue;
                m->ubo.params1.w = shading_mode;
                device->writeBuffer(m->ubo_buffer, &m->ubo, sizeof(MaterialUBO), 0);
                ++updated;
            }
            return updated;
        }

        void RenderScene::setMaterialBaseColor(int32_t index, const Math::Color &color)
        {
            GpuMaterial *m = material(index);
            if (!m) return;
            m->ubo.base_color_factor = GpuVec4::from(color);
            if (m->ubo_buffer) device_->writeBuffer(m->ubo_buffer, &m->ubo, sizeof(MaterialUBO), 0);
        }

        void RenderScene::setMaterialPBR(int32_t index, float metallic, float roughness)
        {
            GpuMaterial *m = material(index);
            if (!m) return;
            GpuVec4 p = m->ubo.params0;
            p.x = metallic;
            p.y = roughness;
            m->ubo.params0 = p;
            if (m->ubo_buffer) device_->writeBuffer(m->ubo_buffer, &m->ubo, sizeof(MaterialUBO), 0);
        }

        void RenderScene::setMaterialEmissive(int32_t index, const Math::Color &color, float strength)
        {
            GpuMaterial *m = material(index);
            if (!m) return;
            m->ubo.emissive_factor = GpuVec4(color.r, color.g, color.b, strength);
            if (m->ubo_buffer) device_->writeBuffer(m->ubo_buffer, &m->ubo, sizeof(MaterialUBO), 0);
        }

        std::vector<uint32_t> RenderScene::uploadModel(const ModelData &model, const std::string &name_hint)
        {
            std::vector<uint32_t> mesh_indices;
            std::vector<int32_t> material_map(model.materials.size(), -1);
            for (size_t i = 0; i < model.materials.size(); ++i)
                material_map[i] = uploadMaterial(model.materials[i], ResourceIO::pathDirectory(model.source_path));

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
                        meshes_[idx]->material_index = defaultMaterial() ? (int32_t)(materials_.size() - 1) : -1;
                }
                mesh_indices.push_back(idx);
            }
            return mesh_indices;
        }

    } // namespace Render
} // namespace Genesis
