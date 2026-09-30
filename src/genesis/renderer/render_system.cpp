/*
 * Genesis Render - render system: lifetime, layouts, targets, registration.
 *
 * Descriptor / binding plan (mirrors shaders/common/genesis_shader.h):
 *
 *   mesh pipelines : set 0 = scene (PerFrame, Lights, Environment = dynamic
 *                    UBOs in the frame ring + IBL textures),
 *                    set 1 = material (dynamic UBO + 5 textures),
 *                    set 2 = per draw (dynamic UBO)
 *   skybox         : set 0 = scene, set 1 = sky textures
 *   post / blit    : set 0 = PostParams (dynamic) + source texture
 *   batch          : set 0 = BatchParams (dynamic) + texture
 *   IBL bakes      : set 0 = IBLParams (dynamic) + source textures
 *   debug lines    : push constants only
 *   ImGui          : push constants + texture
 *
 * Every uniform that changes per frame / view / draw is a dynamic offset into
 * FrameAllocator's ring, so descriptor sets are written once and never
 * touched while a frame that uses them is in flight.
 */
#include "renderer/render_system.hpp"
#include "renderer/render_internal.hpp"
#include "foundation_layer/genesis_log.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Genesis
{
    namespace Render
    {
        namespace
        {
            constexpr uint64_t kUniformRingPerFrame = 8ull * 1024 * 1024;
            constexpr uint64_t kVertexRingPerFrame = 16ull * 1024 * 1024;

            RHI::ResourceBinding ubo(uint32_t set, uint32_t binding, const char *name, bool dynamic = true)
            {
                RHI::ResourceBinding b;
                b.set = set;
                b.binding = binding;
                b.name = name;
                b.stages = (uint32_t)RHI::ShaderStageFlagBits::Vertex | (uint32_t)RHI::ShaderStageFlagBits::Fragment;
                b.type = dynamic ? RHI::ResourceType::UniformBufferDynamic : RHI::ResourceType::UniformBuffer;
                return b;
            }

            RHI::ResourceBinding tex(uint32_t set, uint32_t binding, const char *name)
            {
                RHI::ResourceBinding b;
                b.set = set;
                b.binding = binding;
                b.name = name;
                b.stages = (uint32_t)RHI::ShaderStageFlagBits::Vertex | (uint32_t)RHI::ShaderStageFlagBits::Fragment;
                b.type = RHI::ResourceType::CombinedImageSampler;
                return b;
            }

            std::vector<RHI::ResourceBinding> sceneBindings()
            {
                return {ubo(0, 0, "PerFrame"), ubo(0, 1, "Lights"), ubo(0, 2, "Environment"),
                        tex(0, 3, "u_irradiance_map"), tex(0, 4, "u_prefilter_map"), tex(0, 5, "u_brdf_lut")};
            }
        } // namespace

        //==================================================================
        // Lifetime
        //==================================================================
        RenderSystem::RenderSystem(RHI::RHIDevice *device) : device_(device) {}

        RenderSystem::~RenderSystem()
        {
            shutdown();
        }

        bool RenderSystem::initialize(uint32_t width, uint32_t height, const std::string &ttf_path)
        {
            if (!device_)
            {
                GEN_LOG_ERROR("[Render] no RHI device");
                return false;
            }
            camera_.depth_zero_to_one = device_->info().clip_depth_zero_to_one;
            settings_.hdr_width = width;
            settings_.hdr_height = height;

            if (!frames_.initialize(device_, kUniformRingPerFrame, kVertexRingPerFrame)) return false;
            pipelines_.initialize(device_);
            if (!createLayouts()) return false;

            // Fullscreen quad + 1x1 white texture.
            {
                RHI::BufferDesc d;
                d.name = "fullscreen-quad";
                d.size = sizeof(detail::QuadVertex) * 6;
                d.usage = (uint32_t)RHI::BufferUsageFlagBits::Vertex | (uint32_t)RHI::BufferUsageFlagBits::TransferDst;
                d.memory_usage = RHI::MemoryUsage::GpuOnly;
                d.initial_data = detail::quadVertices();
                d.initial_data_size = d.size;
                quad_vb_ = device_->createBuffer(d);

                RHI::TextureDesc wt;
                wt.format = RHI::PixelFormat::RGBA8_UNORM;
                wt.width = wt.height = 1;
                wt.usage = (uint32_t)RHI::TextureUsageFlagBits::Sampled | (uint32_t)RHI::TextureUsageFlagBits::TransferDst;
                wt.name = "batch.white";
                white_ = device_->createTexture(wt);
                const uint32_t pixel = 0xFFFFFFFFu;
                if (white_) device_->writeTexture(white_, &pixel, 4, 1, 1, 0, 0, 1);

                RHI::SamplerDesc sd;
                sd.min_filter = sd.mag_filter = RHI::FilterMode::Linear;
                sd.mipmap_mode = RHI::MipmapMode::Linear;
                sd.address_u = sd.address_v = sd.address_w = RHI::AddressMode::ClampToEdge;
                sd.max_lod = 16.0f;
                sd.name = "ibl-sampler";
                ibl_sampler_ = device_->createSampler(sd);
                sd.mipmap_mode = RHI::MipmapMode::Nearest;
                sd.max_lod = 0.0f;
                sd.name = "linear-clamp";
                linear_clamp_ = device_->createSampler(sd);
                if (!quad_vb_ || !white_ || !ibl_sampler_ || !linear_clamp_) return false;
            }

            scene_ = std::make_unique<RenderScene>(device_, mesh_layout_, &frames_);

            scene_set_ = device_->createDescriptorSet(mesh_layout_, 0);
            per_draw_set_ = device_->createDescriptorSet(mesh_layout_, 2);
            skybox_set_ = device_->createDescriptorSet(skybox_layout_, 1);
            if (!scene_set_ || !per_draw_set_ || !skybox_set_)
            {
                GEN_LOG_ERROR("[Render] failed to allocate descriptor sets");
                return false;
            }
            {
                RHI::DescriptorBufferInfo info{frames_.uniformBuffer(), 0, sizeof(PerDrawUBO)};
                RHI::WriteDescriptorSet w{0, 0, 1, RHI::ResourceType::UniformBufferDynamic, &info, nullptr};
                per_draw_set_->write(&w, 1);
            }
            writeSceneDescriptorSet();
            writeSkyboxDescriptorSet();

            if (!createTargets(width, height)) return false;

            if (!ttf_path.empty())
            {
                if (!font_.load(device_, ttf_path))
                    GEN_LOG_WARN("[Render] SDF font '%s' unavailable - text rendering disabled", ttf_path.c_str());
            }

            initialized_ = true;
            GEN_LOG_INFO("[Render] initialised (%ux%u, backend=%s, shaders=%zu, depth fmt %u, %u frame(s) in flight, text=%d)",
                         width, height, RHI::backendTypeName(device_->backend()), Shaders::shaderCount(),
                         (unsigned)device_->info().depth_format, frames_.slotCount(), (int)font_.ready());
            return true;
        }

        void RenderSystem::shutdown()
        {
            if (!device_) return;
            device_->waitIdle();

            clearCommandBuffers();
            for (auto &cb : immediate_) cb.clear();
            clearDebugLines();

            // Render textures still referenced by scripts / materials lose
            // their GPU objects now; the handles stay valid but empty.
            for (auto &weak : render_textures_)
                if (RenderTexturePtr rt = weak.lock()) rt->releaseGpu();
            render_textures_.clear();
            hdr_target_.reset();
            capture_target_.reset();

            releaseIBL();
            for (auto &fb : face_framebuffers_)
                if (fb.second) device_->destroyFramebuffer(fb.second);
            face_framebuffers_.clear();

            font_.release();
            if (scene_) scene_->releaseAll();
            scene_.reset();

            for (auto &kv : texture_sets_)
                if (kv.second.set) device_->destroyDescriptorSet(kv.second.set);
            texture_sets_.clear();
            for (RHI::RHIDescriptorSet **s : {&scene_set_, &per_draw_set_, &skybox_set_, &ibl_set_equirect_, &ibl_set_cube_})
            {
                if (*s) device_->destroyDescriptorSet(*s);
                *s = nullptr;
            }

            frames_.flushReleases();
            pipelines_.shutdown();
            if (quad_vb_) { device_->destroyBuffer(quad_vb_); quad_vb_ = nullptr; }
            if (white_) { device_->destroyTexture(white_); white_ = nullptr; }
            if (ibl_sampler_) { device_->destroySampler(ibl_sampler_); ibl_sampler_ = nullptr; }
            if (linear_clamp_) { device_->destroySampler(linear_clamp_); linear_clamp_ = nullptr; }

            for (RHI::RHIResourceLayout **l : {&mesh_layout_, &skybox_layout_, &post_layout_, &ibl_layout_, &line_layout_,
                                               &batch_layout_, &ui_layout_})
            {
                if (*l) device_->destroyResourceLayout(*l);
                *l = nullptr;
            }
            frames_.shutdown();
            initialized_ = false;
            device_ = nullptr;
        }

        //==================================================================
        // Layouts
        //==================================================================
        bool RenderSystem::createLayouts()
        {
            RHI::ResourceLayoutDesc mesh;
            mesh.name = "mesh";
            mesh.bindings = sceneBindings();
            for (const auto &b : {ubo(1, 0, "Material"), tex(1, 1, "u_base_color"), tex(1, 2, "u_metallic_roughness"),
                                  tex(1, 3, "u_normal"), tex(1, 4, "u_occlusion"), tex(1, 5, "u_emissive"), ubo(2, 0, "PerDraw")})
                mesh.bindings.push_back(b);
            mesh_layout_ = device_->createResourceLayout(mesh);

            RHI::ResourceLayoutDesc sky;
            sky.name = "skybox";
            sky.bindings = sceneBindings();
            sky.bindings.push_back(tex(1, 0, "u_sky_equirect"));
            sky.bindings.push_back(tex(1, 1, "u_sky_cube"));
            skybox_layout_ = device_->createResourceLayout(sky);

            RHI::ResourceLayoutDesc post;
            post.name = "post";
            post.bindings = {ubo(0, 0, "PostParams"), tex(0, 1, "u_scene_color")};
            post_layout_ = device_->createResourceLayout(post);

            RHI::ResourceLayoutDesc ibl;
            ibl.name = "ibl";
            ibl.bindings = {ubo(0, 0, "IBLParams"), tex(0, 1, "u_src_equirect"), tex(0, 2, "u_src_cube")};
            ibl_layout_ = device_->createResourceLayout(ibl);

            RHI::ResourceLayoutDesc line;
            line.name = "debug-line";
            line.push_constant_size = 80; // mat4 + vec4
            line_layout_ = device_->createResourceLayout(line);

            RHI::ResourceLayoutDesc batch;
            batch.name = "batch";
            batch.bindings = {ubo(0, 0, "BatchParams"), tex(0, 1, "u_batch_texture")};
            batch_layout_ = device_->createResourceLayout(batch);

            RHI::ResourceLayoutDesc ui;
            ui.name = "imgui";
            ui.bindings = {tex(0, 0, "u_ui_texture")};
            ui.push_constant_size = 16;
            ui_layout_ = device_->createResourceLayout(ui);

            if (!mesh_layout_ || !skybox_layout_ || !post_layout_ || !ibl_layout_ || !line_layout_ || !batch_layout_ || !ui_layout_)
            {
                GEN_LOG_ERROR("[Render] failed to create the resource layouts");
                return false;
            }
            return true;
        }

        void RenderSystem::writeSceneDescriptorSet()
        {
            if (!scene_set_) return;
            TextureCache &tc = scene_->textures();
            RHI::RHIBuffer *ring = frames_.uniformBuffer();
            RHI::DescriptorBufferInfo buffers[3] = {
                {ring, 0, sizeof(PerFrameUBO)},
                {ring, 0, sizeof(LightsUBO)},
                {ring, 0, sizeof(EnvironmentUBO)},
            };
            const GpuTexture &cube = tc.blackCube();
            const GpuTexture &black = tc.black();
            RHI::DescriptorImageInfo images[3] = {
                {ibl_ready_ ? irradiance_cubemap_ : cube.texture, ibl_ready_ ? ibl_sampler_ : cube.sampler,
                 RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1},
                {ibl_ready_ ? prefiltered_cubemap_ : cube.texture, ibl_ready_ ? ibl_sampler_ : cube.sampler,
                 RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1},
                {ibl_ready_ ? brdf_lut_ : black.texture, ibl_ready_ ? linear_clamp_ : black.sampler,
                 RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1},
            };
            RHI::WriteDescriptorSet writes[6];
            for (uint32_t i = 0; i < 3; ++i)
                writes[i] = RHI::WriteDescriptorSet{i, 0, 1, RHI::ResourceType::UniformBufferDynamic, &buffers[i], nullptr};
            for (uint32_t i = 0; i < 3; ++i)
                writes[3 + i] = RHI::WriteDescriptorSet{3 + i, 0, 1, RHI::ResourceType::CombinedImageSampler, nullptr, &images[i]};
            scene_set_->write(writes, 6);
        }

        void RenderSystem::writeSkyboxDescriptorSet()
        {
            if (!skybox_set_) return;
            TextureCache &tc = scene_->textures();
            RHI::DescriptorImageInfo equirect{env_equirect_ ? env_equirect_ : tc.black().texture,
                                              env_equirect_ ? ibl_sampler_ : tc.black().sampler,
                                              RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1};
            RHI::DescriptorImageInfo cube{env_cubemap_ ? env_cubemap_ : tc.blackCube().texture,
                                          env_cubemap_ ? ibl_sampler_ : tc.blackCube().sampler,
                                          RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 6};
            RHI::WriteDescriptorSet writes[2];
            writes[0] = RHI::WriteDescriptorSet{0, 0, 1, RHI::ResourceType::CombinedImageSampler, nullptr, &equirect};
            writes[1] = RHI::WriteDescriptorSet{1, 0, 1, RHI::ResourceType::CombinedImageSampler, nullptr, &cube};
            skybox_set_->write(writes, 2);
        }

        //==================================================================
        // Texture descriptor sets (batch / post / UI)
        //==================================================================
        RHI::RHIDescriptorSet *RenderSystem::textureSet(RHI::RHIResourceLayout *layout, RHI::RHITexture *texture,
                                                        RHI::RHISampler *sampler, bool dynamic_ubo_at_binding0)
        {
            if (!layout || !texture) return nullptr;
            if (!sampler) sampler = linear_clamp_;
            uint64_t key = 1469598103934665603ull;
            for (uint64_t v : {(uint64_t)(uintptr_t)layout, (uint64_t)(uintptr_t)texture, (uint64_t)(uintptr_t)sampler})
            {
                key ^= v;
                key *= 1099511628211ull;
            }
            auto it = texture_sets_.find(key);
            if (it != texture_sets_.end()) return it->second.set;

            RHI::RHIDescriptorSet *set = device_->createDescriptorSet(layout, 0);
            if (!set) return nullptr;
            RHI::DescriptorBufferInfo ubo_info{frames_.uniformBuffer(), 0, 80};
            RHI::DescriptorImageInfo img{texture, sampler, RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1};
            RHI::WriteDescriptorSet writes[2];
            uint32_t count = 0;
            if (dynamic_ubo_at_binding0)
                writes[count++] = RHI::WriteDescriptorSet{0, 0, 1, RHI::ResourceType::UniformBufferDynamic, &ubo_info, nullptr};
            writes[count++] = RHI::WriteDescriptorSet{dynamic_ubo_at_binding0 ? 1u : 0u, 0, 1,
                                                      RHI::ResourceType::CombinedImageSampler, nullptr, &img};
            set->write(writes, count);
            texture_sets_[key] = TextureSetEntry{set, texture};
            return set;
        }

        void RenderSystem::forgetTexture(RHI::RHITexture *texture)
        {
            for (auto it = texture_sets_.begin(); it != texture_sets_.end();)
            {
                if (it->second.texture == texture)
                {
                    frames_.release(it->second.set);
                    it = texture_sets_.erase(it);
                }
                else ++it;
            }
        }

        //==================================================================
        // Render targets
        //==================================================================
        RenderTexturePtr RenderSystem::createRenderTexture(const std::string &name, uint32_t width, uint32_t height,
                                                           bool hdr, bool depth)
        {
            FrameAllocator *frames = &frames_;
            auto rt = std::make_shared<RenderTexture>(
                device_, name, width, height, hdr ? RHI::PixelFormat::RGBA16_SFLOAT : RHI::PixelFormat::RGBA8_UNORM, depth,
                [frames](std::function<void()> fn) { frames->release(std::move(fn)); });
            if (!rt->valid() && hdr)
            {
                GEN_LOG_WARN("[Render] '%s': RGBA16F not renderable, falling back to RGBA8", name.c_str());
                rt = std::make_shared<RenderTexture>(device_, name, width, height, RHI::PixelFormat::RGBA8_UNORM, depth,
                                                     [frames](std::function<void()> fn) { frames->release(std::move(fn)); });
            }
            if (!rt->valid()) return nullptr;
            rt->setReleaseHook([](RenderTexture *self, void *user)
                               { static_cast<RenderSystem *>(user)->forgetTexture(self->color()); },
                               this);
            render_textures_.erase(std::remove_if(render_textures_.begin(), render_textures_.end(),
                                                  [](const std::weak_ptr<RenderTexture> &w) { return w.expired(); }),
                                   render_textures_.end());
            render_textures_.push_back(rt);
            return rt;
        }

        bool RenderSystem::createTargets(uint32_t width, uint32_t height)
        {
            destroyTargets();
            hdr_target_ = createRenderTexture("camera.hdr", width, height, true, true);
            capture_target_ = createRenderTexture("capture", width, height, false, false);
            if (!hdr_target_ || !capture_target_)
            {
                GEN_LOG_ERROR("[Render] failed to create the camera targets");
                return false;
            }
            capture_target_->setRow0Top(true);
            settings_.hdr_width = hdr_target_->width();
            settings_.hdr_height = hdr_target_->height();
            GEN_LOG_INFO("[Render] targets: hdr %ux%u (fmt %u, depth fmt %u), capture %ux%u", hdr_target_->width(),
                         hdr_target_->height(), (unsigned)hdr_target_->colorFormat(),
                         (unsigned)device_->info().depth_format, capture_target_->width(), capture_target_->height());
            return true;
        }

        void RenderSystem::destroyTargets()
        {
            hdr_target_.reset();
            capture_target_.reset();
        }

        void RenderSystem::resize(uint32_t width, uint32_t height)
        {
            if (!device_ || width == 0 || height == 0) return;
            if (hdr_target_ && hdr_target_->width() == width && hdr_target_->height() == height) return;
            device_->waitIdle();
            if (hdr_target_) hdr_target_->resize(width, height);
            if (capture_target_) capture_target_->resize(width, height);
            settings_.hdr_width = width;
            settings_.hdr_height = height;
        }

        RHI::RHIFramebuffer *RenderSystem::faceFramebuffer(RHI::RHITexture *texture, uint32_t mip, uint32_t layer,
                                                           RHI::PixelFormat format, const std::string &name)
        {
            uint64_t key = (uint64_t)(uintptr_t)texture;
            key = key * 1000003u ^ (uint64_t)mip;
            key = key * 1000003u ^ (uint64_t)layer;
            key = key * 1000003u ^ (uint64_t)format;
            auto it = face_framebuffers_.find(key);
            if (it != face_framebuffers_.end()) return it->second;

            RHI::RenderPassDesc rpd;
            rpd.name = name + ".pass";
            rpd.attachment_count = 1;
            rpd.attachments[0].format = format;
            rpd.attachments[0].load_op = RHI::LoadOp::Clear;
            rpd.attachments[0].store_op = RHI::StoreOp::Store;
            rpd.attachments[0].initial_layout = RHI::TextureLayout::Undefined;
            rpd.attachments[0].final_layout = RHI::TextureLayout::ColorAttachmentOptimal;

            RHI::FramebufferDesc fbd;
            fbd.render_pass = rpd;
            fbd.width = std::max(1u, texture->width() >> mip);
            fbd.height = std::max(1u, texture->height() >> mip);
            fbd.name = name;
            fbd.attachments.push_back(RHI::FramebufferDesc::Attachment{texture, mip, layer});
            RHI::RHIFramebuffer *fb = device_->createFramebuffer(fbd);
            if (fb) face_framebuffers_[key] = fb;
            return fb;
        }

        //==================================================================
        // Registration
        //==================================================================
        uint32_t RenderSystem::registerModel(const ModelData &model, const std::string &name_hint)
        {
            if (!scene_) return 0xFFFFFFFFu;
            std::vector<uint32_t> indices = scene_->uploadModel(model, name_hint);
            return indices.empty() ? 0xFFFFFFFFu : indices.front();
        }

        void RenderSystem::bindEntityMeshes(const EntityMeshBinding &b)
        {
            if (!b.entity.valid()) return;
            bindings_[b.entity.id] = b;
        }

        const RenderSystem::EntityMeshBinding *RenderSystem::binding(ECS::Entity e) const
        {
            auto it = bindings_.find(e.id);
            return (it != bindings_.end() && it->second.entity.generation == e.generation) ? &it->second : nullptr;
        }

        void RenderSystem::addCommandBuffer(CameraEvent event, const CommandBufferPtr &cb)
        {
            if (!cb || (uint32_t)event >= (uint32_t)CameraEvent::Count) return;
            auto &list = injected_[(size_t)event];
            if (std::find(list.begin(), list.end(), cb) == list.end()) list.push_back(cb);
        }

        void RenderSystem::removeCommandBuffer(CameraEvent event, const CommandBufferPtr &cb)
        {
            if ((uint32_t)event >= (uint32_t)CameraEvent::Count) return;
            auto &list = injected_[(size_t)event];
            list.erase(std::remove(list.begin(), list.end(), cb), list.end());
        }

        void RenderSystem::clearCommandBuffers()
        {
            for (auto &list : injected_) list.clear();
            once_.clear();
            custom_.clear();
        }

        void RenderSystem::executeCommandBuffer(const CommandBufferPtr &cb)
        {
            if (cb) once_.push_back(cb);
        }

        void RenderSystem::addCustomCommandBuffer(const CommandBufferPtr &cb)
        {
            if (cb && std::find(custom_.begin(), custom_.end(), cb) == custom_.end()) custom_.push_back(cb);
        }

        void RenderSystem::removeCustomCommandBuffer(const CommandBufferPtr &cb)
        {
            custom_.erase(std::remove(custom_.begin(), custom_.end(), cb), custom_.end());
        }

        //==================================================================
        // Debug lines
        //==================================================================
        void RenderSystem::clearDebugLines() { debug_vertices_.clear(); }

        void RenderSystem::drawLine(const Math::Vec3 &a, const Math::Vec3 &b, const Math::Color &color)
        {
            debug_vertices_.push_back(DebugVertex{{a.x, a.y, a.z}, {color.r, color.g, color.b, color.a}});
            debug_vertices_.push_back(DebugVertex{{b.x, b.y, b.z}, {color.r, color.g, color.b, color.a}});
        }

        void RenderSystem::drawBox(const Math::AABB &box, const Math::Color &color)
        {
            if (!box.valid) return;
            Math::Vec3 mn = box.minPoint, mx = box.maxPoint;
            Math::Vec3 c[8] = {
                {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
                {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z},
            };
            static const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                             {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (auto &e : edges) drawLine(c[e[0]], c[e[1]], color);
        }

        void RenderSystem::drawSphere(const Math::Vec3 &center, float radius, const Math::Color &color, int segments)
        {
            segments = std::max(6, segments);
            for (int axis = 0; axis < 3; ++axis)
            {
                for (int i = 0; i < segments; ++i)
                {
                    float a0 = Math::TWO_PI * (float)i / (float)segments;
                    float a1 = Math::TWO_PI * (float)(i + 1) / (float)segments;
                    Math::Vec3 p0, p1;
                    if (axis == 0)
                    {
                        p0 = center + Math::Vec3(0, std::cos(a0), std::sin(a0)) * radius;
                        p1 = center + Math::Vec3(0, std::cos(a1), std::sin(a1)) * radius;
                    }
                    else if (axis == 1)
                    {
                        p0 = center + Math::Vec3(std::cos(a0), 0, std::sin(a0)) * radius;
                        p1 = center + Math::Vec3(std::cos(a1), 0, std::sin(a1)) * radius;
                    }
                    else
                    {
                        p0 = center + Math::Vec3(std::cos(a0), std::sin(a0), 0) * radius;
                        p1 = center + Math::Vec3(std::cos(a1), std::sin(a1), 0) * radius;
                    }
                    drawLine(p0, p1, color);
                }
            }
        }

    } // namespace Render
} // namespace Genesis
