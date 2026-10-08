/*
 * Genesis Render - render system implementation (scene sync + frame execution)
 */
#include "renderer/render_system.hpp"
#include "renderer/render_internal.hpp"
#include "foundation_layer/genesis_log.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstring>

namespace Genesis
{
    namespace Render
    {
        //==================================================================
        // Model / entity registration
        //==================================================================
        uint32_t RenderSystem::registerModel(const ModelData &model, const std::string &name_hint)
        {
            if (!scene_) return 0xFFFFFFFFu;
            std::vector<uint32_t> indices = scene_->uploadModel(model, name_hint);
            return indices.empty() ? 0xFFFFFFFFu : indices.front();
        }

        void RenderSystem::bindEntityMeshes(const EntityMeshBinding &binding)
        {
            if (!binding.entity.valid()) return;
            bindings_[binding.entity.id] = binding;
        }

        //==================================================================
        // Scene synchronisation
        //==================================================================
        void RenderSystem::syncFromWorld(ECS::World &world, float delta_time)
        {
            (void)delta_time;
            if (!scene_) return;

            // ---- camera -------------------------------------------------
            bool camera_found = false;
            world.each<ECS::CameraComponent>([&](ECS::Entity e, ECS::CameraComponent &cam)
            {
                if (camera_found || !cam.active) return;
                ECS::TransformComponent *t = world.getComponent<ECS::TransformComponent>(e);
                if (!t) return;
                camera_found = true;
                camera_.updateFromTransform(t->world_matrix);
                camera_.fov_degrees = cam.fov_degrees;
                camera_.near_plane = cam.near_plane;
                camera_.far_plane = cam.far_plane;
                camera_.orthographic = cam.orthographic;
                camera_.ortho_size = cam.ortho_size;
                if (cam.aspect_override > 0.0f) camera_.aspect = cam.aspect_override;
                else if (settings_.hdr_height > 0)
                    camera_.aspect = (float)settings_.hdr_width / (float)settings_.hdr_height;
            });
            if (!camera_found && settings_.hdr_height > 0)
                camera_.aspect = (float)settings_.hdr_width / (float)settings_.hdr_height;

            // ---- environment (IBL requests) -----------------------------
            world.each<ECS::EnvironmentComponent>([&](ECS::Entity e, ECS::EnvironmentComponent &env)
            {
                (void)e;
                settings_.env_intensity = env.env_intensity;
                settings_.env_rotation_degrees = env.env_rotation_degrees;
                settings_.show_skybox = env.show_skybox;
                settings_.use_ibl = env.use_ibl;
                if (env.ibl_dirty)
                {
                    IblBakeSettings bake;
                    bake.cubemap_size = env.cubemap_size;
                    bake.irradiance_size = env.irradiance_size;
                    bake.prefiltered_size = env.prefiltered_size;
                    bake.brdf_lut_size = env.brdf_lut_size;
                    bool ok = env.hdr_equirect_path.empty() ? bakeProceduralIBL(bake)
                                                            : bakeIBL(env.hdr_equirect_path, bake);
                    env.ibl_dirty = !ok;
                    if (ok)
                    {
                        settings_.sky_mode = env.hdr_equirect_path.empty() ? SkyMode::Cubemap : SkyMode::Cubemap;
                    }
                }
            });
        }

        void RenderSystem::gatherRenderItems(ECS::World &world)
        {
            opaque_items_.clear();
            transparent_items_.clear();
            stats_.culled_items = 0;

            Math::Frustum frustum;
            bool have_frustum = false;
            if (settings_.frustum_culling)
            {
                frustum = Math::Frustum::fromViewProjection(camera_.view_projection);
                have_frustum = true;
                GEN_LOG_DEBUG("[Render] camera pos (%0.2f %0.2f %0.2f) fwd (%0.2f %0.2f %0.2f) aspect %0.3f fov %0.1f near %0.2f far %0.2f",
                              camera_.position.x, camera_.position.y, camera_.position.z,
                              camera_.forward.x, camera_.forward.y, camera_.forward.z,
                              camera_.aspect, camera_.fov_degrees, camera_.near_plane, camera_.far_plane);
            }

            world.each<ECS::MeshComponent>([&](ECS::Entity e, ECS::MeshComponent &mesh_comp)
            {
                if (!mesh_comp.visible || !mesh_comp.model) return;
                ECS::TransformComponent *t = world.getComponent<ECS::TransformComponent>(e);
                Math::Mat4 model = t ? t->world_matrix : Math::Mat4::identity();

                auto binding_it = bindings_.find(e.id);
                std::vector<uint32_t> gpu_meshes;
                if (binding_it != bindings_.end()) gpu_meshes = binding_it->second.mesh_indices;

                const ModelData &mdl = *mesh_comp.model;

                // A model may contain many meshes placed by its node hierarchy
                // (glTF scenes, the procedural material grid, FBX multi-model
                // files).  flattenNodes() collapses that hierarchy into one
                // entry per placed mesh, each with its own transform, so a
                // single entity renders a whole multi-part model correctly.
                std::vector<ModelData::PlacedMesh> placed = mdl.flattenNodes();
                if (!mesh_comp.mesh_indices.empty())
                {
                    std::vector<ModelData::PlacedMesh> filtered;
                    for (const auto &pm : placed)
                        if (std::find(mesh_comp.mesh_indices.begin(), mesh_comp.mesh_indices.end(),
                                      pm.mesh_index) != mesh_comp.mesh_indices.end())
                            filtered.push_back(pm);
                    placed.swap(filtered);
                }

                for (const ModelData::PlacedMesh &pm : placed)
                {
                    if (pm.mesh_index < 0 || (size_t)pm.mesh_index >= gpu_meshes.size()) continue;

                    GpuMesh *gpu = scene_->mesh(gpu_meshes[pm.mesh_index]);
                    if (!gpu) continue;

                    int32_t material_index = gpu->material_index;
                    size_t slot = (size_t)pm.mesh_index;
                    if (slot < mesh_comp.material_overrides.size() && mesh_comp.material_overrides[slot] >= 0)
                        material_index = mesh_comp.material_overrides[slot];
                    GpuMaterial *material = scene_->material(material_index);
                    if (!material) material = scene_->defaultMaterial();
                    if (!material) continue;

                    // entity world transform * node placement inside the model
                    Math::Mat4 item_model = model * pm.transform;

                    RenderItem item;
                    item.mesh = gpu;
                    item.material = material;
                    item.model = item_model;
                    item.normal_matrix = item_model.normalMatrix();
                    item.world_bounds = gpu->bounds.transformed(item_model);
                    item.pick_id = mesh_comp.pick_id;
                    item.entity = e;
                    item.depth = Math::length(camera_.position - item.world_bounds.center());

                    if (have_frustum && !frustum.intersectsAABB(item.world_bounds))
                    {
                        GEN_LOG_DEBUG("[Render] culled '%s' bounds [%0.2f %0.2f %0.2f]-[%0.2f %0.2f %0.2f] valid=%d",
                                      gpu->name.c_str(),
                                      item.world_bounds.minPoint.x, item.world_bounds.minPoint.y, item.world_bounds.minPoint.z,
                                      item.world_bounds.maxPoint.x, item.world_bounds.maxPoint.y, item.world_bounds.maxPoint.z,
                                      (int)item.world_bounds.valid);
                        ++stats_.culled_items;
                        continue;
                    }
                    GEN_LOG_DEBUG("[Render] draw '%s' depth %0.2f bounds valid=%d center (%0.2f %0.2f %0.2f)",
                                  gpu->name.c_str(), item.depth, (int)item.world_bounds.valid,
                                  item.world_bounds.center().x, item.world_bounds.center().y, item.world_bounds.center().z);
                    if (material->transparent) transparent_items_.push_back(item);
                    else opaque_items_.push_back(item);
                }
            });

            std::sort(opaque_items_.begin(), opaque_items_.end(),
                      [](const RenderItem &a, const RenderItem &b) { return a.depth < b.depth; });
            std::sort(transparent_items_.begin(), transparent_items_.end(),
                      [](const RenderItem &a, const RenderItem &b) { return a.depth > b.depth; });

            stats_.opaque_items = (uint32_t)opaque_items_.size();
            stats_.transparent_items = (uint32_t)transparent_items_.size();
            stats_.meshes = (uint32_t)scene_->meshCount();
            stats_.materials = (uint32_t)scene_->materialCount();
        }

        //==================================================================
        // UBO updates
        //==================================================================
        void RenderSystem::syncMaterialSettings()
        {
            // The shading model (Cook-Torrance vs Blinn-Phong) is a renderer
            // setting but lives in the per-material UBO, so it has to be pushed
            // into every material when it changes.
            float mode = (float)settings_.shading_mode;
            if (mode == last_shading_mode_) return;
            size_t updated = scene_->updateShadingMode(device_, mode);
            last_shading_mode_ = mode;
            if (updated)
                GEN_LOG_DEBUG("[Render] shading model -> %s (%zu materials updated)",
                              settings_.shading_mode == ShadingMode::BlinnPhong ? "Blinn-Phong" : "Cook-Torrance",
                              updated);
        }

        void RenderSystem::updatePerFrameUBO(float delta_time)
        {
            PerFrameUBO ubo;
            // The scene pass always renders into the offscreen HDR target, so
            // the camera matrices are built for an offscreen target.
            camera_.target_is_swapchain = false;
            camera_.backend = device_->backend();
            camera_.update();

            ubo.view = GpuMat4::from(camera_.view);
            ubo.projection = GpuMat4::from(camera_.projection);
            ubo.view_projection = GpuMat4::from(camera_.view_projection);
            ubo.inv_view_projection = GpuMat4::from(camera_.inv_view_projection);
            ubo.inv_view = GpuMat4::from(camera_.inv_view);
            ubo.camera_position = GpuVec4(camera_.position.x, camera_.position.y, camera_.position.z,
                                          Math::toRadians(camera_.fov_degrees));

            double time = (double)stats_.frame_index * (double)delta_time;
            ubo.frame_params = GpuVec4((float)time, delta_time, (float)stats_.frame_index, settings_.exposure);
            ubo.viewport_params = GpuVec4((float)hdr_target_.width, (float)hdr_target_.height,
                                          1.0f / (float)std::max(1u, hdr_target_.width),
                                          1.0f / (float)std::max(1u, hdr_target_.height));
            const bool zero_to_one = device_->backend() == RHI::BackendType::Vulkan;
            ubo.backend_params = GpuVec4(0.0f, zero_to_one ? 1.0f : 0.0f, zero_to_one ? 1.0f : 0.0f, 0.0f);

            if (std::getenv("GENESIS_DBG_DRAW"))
            {
                GEN_LOG_INFO("[CPUDBG] view_projection row0=(%.4f %.4f %.4f %.4f) row1=(%.4f %.4f %.4f %.4f) cam=(%.2f %.2f %.2f) ubo=%p",
                             ubo.view_projection.m[0], ubo.view_projection.m[1], ubo.view_projection.m[2], ubo.view_projection.m[3],
                             ubo.view_projection.m[4], ubo.view_projection.m[5], ubo.view_projection.m[6], ubo.view_projection.m[7],
                             camera_.position.x, camera_.position.y, camera_.position.z, (void *)per_frame_ubo_);
            }
            device_->writeBuffer(per_frame_ubo_, &ubo, sizeof(ubo), 0);
        }

        void RenderSystem::updateLightsUBO(ECS::World &world)
        {
            LightsUBO ubo;
            std::memset(&ubo, 0, sizeof(ubo));
            ubo.ambient_color = GpuVec4(settings_.ambient_color.r, settings_.ambient_color.g,
                                        settings_.ambient_color.b, settings_.ambient_intensity);

            uint32_t count = 0;
            world.each<ECS::LightComponent>([&](ECS::Entity e, ECS::LightComponent &light)
            {
                if (!light.active || count >= kMaxLights) return;
                ECS::TransformComponent *t = world.getComponent<ECS::TransformComponent>(e);
                Math::Mat4 world_matrix = t ? t->world_matrix : Math::Mat4::identity();

                GpuLight &g = ubo.lights[count];
                Math::Vec3 position = world_matrix.translation();
                Math::Vec3 direction = -world_matrix.zAxis();
                if (Math::lengthSquared(direction) < 1e-8f) direction = Math::Vec3(0, -1, 0);
                direction = Math::normalize(direction);

                g.position_range = GpuVec4(position.x, position.y, position.z, light.range);
                g.direction_cone = GpuVec4(direction.x, direction.y, direction.z, 0.0f);
                g.color_intensity = GpuVec4(light.color.r, light.color.g, light.color.b, light.intensity);
                g.type_params = GpuVec4((float)light.type,
                                        std::cos(Math::toRadians(light.spot_inner_degrees)),
                                        std::cos(Math::toRadians(light.spot_outer_degrees)),
                                        1.0f);
                ++count;
            });
            ubo.light_count_params = GpuVec4((float)count, 0.0f, 0.0f, 0.0f);
            stats_.active_lights = count;
            if (std::getenv("GENESIS_DBG_DRAW"))
            {
                for (uint32_t i = 0; i < count; ++i)
                {
                    const GpuLight &g = ubo.lights[i];
                    GEN_LOG_INFO("[CPUDBG] light[%u] type=%.0f pos=(%.2f %.2f %.2f) dir=(%.3f %.3f %.3f) color=(%.2f %.2f %.2f) intensity=%.2f enabled=%.0f",
                                 i, g.type_params.x, g.position_range.x, g.position_range.y, g.position_range.z,
                                 g.direction_cone.x, g.direction_cone.y, g.direction_cone.z,
                                 g.color_intensity.x, g.color_intensity.y, g.color_intensity.z,
                                 g.color_intensity.w, g.type_params.w);
                }
                GEN_LOG_INFO("[CPUDBG] lights ubo=%p count=%u ambient=(%.2f %.2f %.2f)x%.2f",
                             (void *)lights_ubo_, count, ubo.ambient_color.x, ubo.ambient_color.y,
                             ubo.ambient_color.z, ubo.ambient_color.w);
            }
            device_->writeBuffer(lights_ubo_, &ubo, sizeof(ubo), 0);
        }

        void RenderSystem::updateEnvironmentUBO()
        {
            EnvironmentUBO ubo;
            float rotation = Math::toRadians(settings_.env_rotation_degrees);
            ubo.env_params = GpuVec4(settings_.env_intensity, std::cos(rotation), std::sin(rotation),
                                     settings_.show_skybox ? 1.0f : 0.0f);
            // u_ibl_params.z doubles as the sky/IBL source selector used by
            // both scene/skybox.frag and scene/pbr_mesh.frag:
            //   0 = procedural gradient, 1 = equirect texture, 2 = baked cube.
            // Whenever a bake exists we must advertise mode 2, otherwise the
            // mesh shader would keep using the procedural fallback.
            float source_mode = ibl_ready_ ? 2.0f : (float)settings_.sky_mode;
            ubo.ibl_params = GpuVec4((settings_.use_ibl && ibl_ready_) ? 1.0f : 0.0f,
                                     (float)prefilter_mip_levels_,
                                     source_mode,
                                     0.0f);
            ubo.sky_tint = GpuVec4::from(settings_.sky_tint);
            ubo.ground_tint = GpuVec4::from(settings_.ground_tint);
            device_->writeBuffer(environment_ubo_, &ubo, sizeof(ubo), 0);
        }

        //==================================================================
        // Per-draw ring
        //==================================================================
        uint64_t RenderSystem::allocatePerDrawSlot()
        {
            uint64_t offset = per_draw_offset_;
            per_draw_offset_ += per_draw_stride_;
            const uint64_t frame_span = per_draw_stride_ * (kPerDrawSlots / 2);
            if (per_draw_offset_ >= per_draw_frame_base_ + frame_span)
            {
                // Ring exhausted: keep reusing the last slot rather than
                // corrupting the other frame's data.
                per_draw_offset_ -= per_draw_stride_;
                offset = per_draw_offset_;
            }
            return offset;
        }

        void *RenderSystem::perDrawSlotPointer(uint64_t offset)
        {
            static thread_local std::vector<uint8_t> scratch;
            (void)offset;
            (void)scratch;
            return nullptr; // unused: per-draw data is uploaded in one batch
        }

        //==================================================================
        // Present pipeline (per colour format)
        //==================================================================
        RHI::RHIPipeline *RenderSystem::presentPipeline(RHI::PixelFormat color_format)
        {
            auto it = present_pipelines_.find((uint32_t)color_format);
            if (it != present_pipelines_.end()) return it->second;

            RHI::PipelineDesc d;
            d.name = "post.tonemap";
            RHI::ShaderStageDesc vs, fs;
            if (!detail::fillShaderStage("common/fullscreen.vert", device_->backend(), RHI::ShaderStageFlagBits::Vertex, vs) ||
                !detail::fillShaderStage("post/tonemap.frag", device_->backend(), RHI::ShaderStageFlagBits::Fragment, fs))
                return nullptr;
            d.shaders = {vs, fs};
            d.vertex_layout = detail::quadVertexLayout();
            d.rasterizer.cull_mode = RHI::CullMode::None;
            d.depth_stencil.depth_test = false;
            d.depth_stencil.depth_write = false;
            d.blend.attachments[0].blend_enable = false;
            d.resource_layout = post_layout_->desc();
            d.color_formats[0] = color_format;
            d.color_format_count = 1;
            d.depth_format = RHI::PixelFormat::Undefined;

            RHI::RHIPipeline *pipeline = device_->createPipeline(d);
            if (pipeline) present_pipelines_[(uint32_t)color_format] = pipeline;
            else GEN_LOG_ERROR("[Render] failed to create the tone mapping pipeline for format %u", (unsigned)color_format);
            return pipeline;
        }

        //==================================================================
        // Frame execution
        //==================================================================
        void RenderSystem::update(ECS::World &world, float delta_time)
        {
            if (!initialized_) return;
            syncFromWorld(world, delta_time);
        }

        void RenderSystem::renderFrame(ECS::World &world, float delta_time, bool present_to_swapchain)
        {
            if (!initialized_) return;
            auto frame_start = std::chrono::steady_clock::now();

            // Per-frame counters (frame_index and the timing history survive).
            uint64_t keep_frame_index = stats_.frame_index;
            stats_ = RenderStats();
            stats_.frame_index = keep_frame_index;

            gatherRenderItems(world);
            syncMaterialSettings();
            updatePerFrameUBO(delta_time);
            updateLightsUBO(world);
            updateEnvironmentUBO();

            RHI::RHICommandBuffer *cmd = device_->currentCommandBuffer();
            if (!cmd)
            {
                GEN_LOG_WARN("[Render] no current command buffer (did the device begin a frame?)");
                return;
            }

            // ---- HDR scene pass ---------------------------------------
            renderScenePass(cmd, hdr_target_.pass, hdr_target_.framebuffer,
                            hdr_target_.width, hdr_target_.height,
                            Math::Color(0.0f, 0.0f, 0.0f, 1.0f));

            auto scene_end = std::chrono::steady_clock::now();
            stats_.scene_ms = std::chrono::duration<double, std::milli>(scene_end - frame_start).count();

            // ---- present / tone mapping pass --------------------------
            // See the orientation note in render_scene.cpp: offscreen images are
            // "row 0 = bottom" on both backends, so
            //   * presenting to the OpenGL default framebuffer needs no flip
            //     (its row 0 is the bottom of the window),
            //   * presenting to the Vulkan swapchain needs a flip (its row 0 is
            //     the top of the window),
            //   * capturing to a texture always flips, because the public
            //     contract of renderAndCapture() is "row 0 = top".
            const bool is_vulkan = device_->backend() == RHI::BackendType::Vulkan;
            if (present_to_swapchain && device_->swapchainRenderPass() && device_->currentSwapchainFramebuffer())
            {
                RHI::RHIFramebuffer *fb = device_->currentSwapchainFramebuffer();
                settings_.flip_post_uv = is_vulkan;
                renderPresentPass(cmd, device_->swapchainRenderPass(), fb,
                                  device_->swapchainWidth(), device_->swapchainHeight(),
                                  settings_.flip_post_uv);
            }
            else if (capture_target_.valid)
            {
                settings_.flip_post_uv = true;
                renderPresentPass(cmd, capture_target_.pass, capture_target_.framebuffer,
                                  capture_target_.width, capture_target_.height, true);
            }

            auto present_end = std::chrono::steady_clock::now();
            stats_.present_ms = std::chrono::duration<double, std::milli>(present_end - scene_end).count();
            ++stats_.frame_index;

            // Clear the free command list for the next frame: scripts record
            // commands during world.update (before this render), so they are
            // consumed here and must not persist.
            if (overlay_enabled_) overlay_.clearCommands();
        }

        void RenderSystem::renderScenePass(RHI::RHICommandBuffer *cmd, RHI::RHIRenderPass *pass,
                                           RHI::RHIFramebuffer *fb, uint32_t width, uint32_t height,
                                           const Math::Color &clear_color)
        {
            if (!pass || !fb) return;

            RHI::RenderPassBeginInfo begin;
            begin.clear_count = 2;
            begin.clears[0].color[0] = clear_color.r;
            begin.clears[0].color[1] = clear_color.g;
            begin.clears[0].color[2] = clear_color.b;
            begin.clears[0].color[3] = clear_color.a;
            begin.clears[1].depth = 1.0f;
            begin.clears[1].stencil = 0;
            cmd->beginRenderPass(pass, fb, begin);

            RHI::Viewport vp{0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
            cmd->setViewport(vp);
            cmd->setScissor(RHI::Rect2D{0, 0, width, height});

            // ---- build the per-draw ring contents ----------------------
            const size_t total_items = opaque_items_.size() + transparent_items_.size();
            per_draw_frame_base_ = (stats_.frame_index % 2) * per_draw_stride_ * (kPerDrawSlots / 2);
            per_draw_offset_ = per_draw_frame_base_;
            std::vector<uint8_t> per_draw_cpu(total_items * per_draw_stride_, 0);
            std::vector<uint64_t> item_offsets;
            item_offsets.reserve(total_items);

            auto recordItem = [&](const RenderItem &item)
            {
                uint64_t offset = allocatePerDrawSlot();
                PerDrawUBO ubo;
                ubo.model = GpuMat4::from(item.model);
                ubo.normal_matrix = GpuMat4::from(item.normal_matrix);
                ubo.object_params = GpuVec4((float)item.pick_id, settings_.wireframe ? 1.0f : 0.0f, 0.0f, 0.0f);
                std::memcpy(per_draw_cpu.data() + (offset - per_draw_frame_base_), &ubo, sizeof(ubo));
                item_offsets.push_back(offset);
                if (std::getenv("GENESIS_DBG_DRAW"))
                    GEN_LOG_INFO("[CPUDBG] per-draw slot=%llu nm[0]=%.3f nm[5]=%.3f nm[10]=%.3f model[0]=%.3f stride=%llu base=%llu",
                                 (unsigned long long)offset, ubo.normal_matrix.m[0], ubo.normal_matrix.m[5],
                                 ubo.normal_matrix.m[10], ubo.model.m[0],
                                 (unsigned long long)per_draw_stride_, (unsigned long long)per_draw_frame_base_);
            };
            for (const auto &item : opaque_items_) recordItem(item);
            for (const auto &item : transparent_items_) recordItem(item);
            if (!per_draw_cpu.empty())
                device_->writeBuffer(per_draw_ring_, per_draw_cpu.data(), per_draw_cpu.size(), per_draw_frame_base_);

            // ---- skybox -------------------------------------------------
            if (settings_.show_skybox && skybox_pipeline_ && skybox_set_)
            {
                cmd->bindPipeline(skybox_pipeline_);
                cmd->bindVertexBuffer(quad_vb_, 0);
                RHI::RHIDescriptorSet *sets[2] = {scene_set_, skybox_set_};
                cmd->bindDescriptorSets(skybox_pipeline_, sets, 2, nullptr, 0);
                cmd->draw(6, 0, 1, 0);
                ++stats_.draw_calls;
            }

            // ---- opaque + alpha masked ---------------------------------
            size_t offset_index = 0;
            auto drawList = [&](const std::vector<RenderItem> &items)
            {
                RHI::RHIPipeline *current = nullptr;
                for (const RenderItem &item : items)
                {
                    RHI::RHIPipeline *pipeline = pbr_opaque_;
                    if (item.material->transparent) pipeline = pbr_blend_;
                    else if (item.material->double_sided) pipeline = pbr_opaque_two_sided_;
                    else if (item.material->alpha_mask) pipeline = pbr_alpha_mask_ ? pbr_alpha_mask_ : pbr_opaque_;
                    if (!pipeline) pipeline = pbr_opaque_;

                    if (pipeline != current)
                    {
                        cmd->bindPipeline(pipeline);
                        current = pipeline;
                    }
                    cmd->bindVertexBuffer(item.mesh->vertex_buffer, 0);
                    cmd->bindIndexBuffer(item.mesh->index_buffer, item.mesh->index_type, 0);

                    RHI::RHIDescriptorSet *sets[3] = {scene_set_, item.material->descriptor_set, per_draw_set_};
                    uint32_t dynamic_offset = (uint32_t)item_offsets[offset_index++];
                    cmd->bindDescriptorSets(pipeline, sets, 3, &dynamic_offset, 1);
                    cmd->drawIndexed(item.mesh->index_count, 0, 0, 1, 0);

                    ++stats_.draw_calls;
                    stats_.triangles += item.mesh->index_count / 3;
                }
            };
            drawList(opaque_items_);
            drawList(transparent_items_);

            // ---- debug lines -------------------------------------------
            renderDebugLines(cmd);

            // ---- free 3D command list (CommandBuffer draw commands) ----
            // In the HDR pass, depth tested, so it composes with the scene.
            if (overlay_enabled_)
                overlay_.render3D(cmd, camera_.view_projection, hdr_target_.width, hdr_target_.height);

            cmd->endRenderPass();
        }

        void RenderSystem::renderPresentPass(RHI::RHICommandBuffer *cmd, RHI::RHIRenderPass *pass,
                                             RHI::RHIFramebuffer *fb, uint32_t width, uint32_t height, bool flip_uv)
        {
            if (!pass || !fb) return;
            RHI::PixelFormat fmt = (fb == device_->currentSwapchainFramebuffer())
                                      ? device_->swapchainFormat()
                                      : capture_target_.color_format;

            RHI::RenderPassBeginInfo begin;
            begin.clear_count = 1;
            begin.clears[0].color[0] = begin.clears[0].color[1] = begin.clears[0].color[2] = 0.0f;
            begin.clears[0].color[3] = 1.0f;
            cmd->beginRenderPass(pass, fb, begin);
            cmd->setViewport(RHI::Viewport{0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f});
            cmd->setScissor(RHI::Rect2D{0, 0, width, height});

            // Programmable post effect (CommandBuffer user pipeline): when the
            // overlay system has an effect active it draws the tonemap + user
            // effect itself, demonstrating a user-controlled render pipeline.
            RHI::RHISampler *scene_sampler = scene_->textures().white().sampler;

            bool used_effect = false;
            if (overlay_enabled_)
            {
                used_effect = overlay_.presentEffect(cmd, fmt, hdr_target_.color, scene_sampler,
                                                    quad_vb_, settings_.exposure, settings_.gamma,
                                                    (int)settings_.tonemap_mode, settings_.saturation,
                                                    flip_uv, (float)stats_.frame_index * 0.016f);
            }

            if (!used_effect)
            {
                RHI::RHIPipeline *pipeline = presentPipeline(fmt);
                if (!pipeline) { cmd->endRenderPass(); return; }

                PostParamsUBO post;
                post.a = GpuVec4(settings_.exposure, settings_.gamma, (float)settings_.tonemap_mode, flip_uv ? 1.0f : 0.0f);
                post.b = GpuVec4(settings_.vignette, settings_.saturation, (float)stats_.frame_index * 0.016f, 0.0f);
                device_->writeBuffer(post_ubo_, &post, sizeof(post), 0);

                RHI::DescriptorBufferInfo buf{post_ubo_, 0, sizeof(PostParamsUBO)};
                RHI::DescriptorImageInfo img{hdr_target_.color, scene_sampler,
                                             RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1};
                RHI::WriteDescriptorSet writes[2];
                writes[0] = RHI::WriteDescriptorSet{0, 0, 1, RHI::ResourceType::UniformBuffer, &buf, nullptr};
                writes[1] = RHI::WriteDescriptorSet{1, 0, 1, RHI::ResourceType::CombinedImageSampler, nullptr, &img};
                post_set_->write(writes, 2);

                cmd->bindPipeline(pipeline);
                cmd->bindVertexBuffer(quad_vb_, 0);
                RHI::RHIDescriptorSet *sets[1] = {post_set_};
                cmd->bindDescriptorSets(pipeline, sets, 1, nullptr, 0);
                cmd->draw(6, 0, 1, 0);
            }
            ++stats_.draw_calls;

            // ---- 2D free command list + SDF text (screen space) ----
            if (overlay_enabled_)
                overlay_.render2D(cmd, width, height, fmt);

            // UI / overlay hook: still inside the present pass.
            if (post_present_callback_) post_present_callback_(cmd, width, height, post_present_user_);

            cmd->endRenderPass();
        }

        bool RenderSystem::renderAndCapture(ECS::World &world, float delta_time,
                                            std::vector<uint8_t> &rgba_out, uint32_t &width, uint32_t &height)
        {
            if (!initialized_ || !capture_target_.valid)
            {
                GEN_LOG_ERROR("[Render] renderAndCapture requires an initialised render system");
                return false;
            }
            uint64_t keep_frame_index = stats_.frame_index;
            stats_ = RenderStats();
            stats_.frame_index = keep_frame_index;

            syncFromWorld(world, delta_time);
            gatherRenderItems(world);
            syncMaterialSettings();
            updatePerFrameUBO(delta_time);
            updateLightsUBO(world);
            updateEnvironmentUBO();

            RHI::RHICommandBuffer *cmd = device_->createCommandBuffer(RHI::CommandBufferUsage::OneTimeSubmit);
            if (!cmd) return false;
            cmd->begin(RHI::CommandBufferUsage::OneTimeSubmit);
            renderScenePass(cmd, hdr_target_.pass, hdr_target_.framebuffer, hdr_target_.width, hdr_target_.height,
                            Math::Color(0, 0, 0, 1));
            // Captured images are delivered with row 0 == top of the scene.
            renderPresentPass(cmd, capture_target_.pass, capture_target_.framebuffer,
                              capture_target_.width, capture_target_.height, true);
            cmd->end();
            device_->submitAndWait(cmd);
            device_->destroyCommandBuffer(cmd);
            ++stats_.frame_index;

            width = capture_target_.width;
            height = capture_target_.height;
            rgba_out.assign((size_t)width * height * 4, 0);

            RHI::RHICommandBuffer *read_cmd = device_->createCommandBuffer(RHI::CommandBufferUsage::OneTimeSubmit);
            if (!read_cmd) return false;
            bool ok = read_cmd->readImagePixels(capture_target_.color, 0, 0, rgba_out.data(), (uint32_t)rgba_out.size());
            device_->destroyCommandBuffer(read_cmd);
            if (!ok) GEN_LOG_ERROR("[Render] readback of the capture target failed");
            return ok;
        }

        //==================================================================
        // Debug lines
        //==================================================================
        void RenderSystem::clearDebugLines()
        {
            debug_vertices_.clear();
        }

        void RenderSystem::drawLine(const Math::Vec3 &a, const Math::Vec3 &b, const Math::Color &color)
        {
            DebugVertex v0{{a.x, a.y, a.z}, {color.r, color.g, color.b, color.a}};
            DebugVertex v1{{b.x, b.y, b.z}, {color.r, color.g, color.b, color.a}};
            debug_vertices_.push_back(v0);
            debug_vertices_.push_back(v1);
        }

        void RenderSystem::drawBox(const Math::AABB &box, const Math::Color &color)
        {
            if (!box.valid) return;
            Math::Vec3 mn = box.minPoint, mx = box.maxPoint;
            Math::Vec3 c[8] = {
                {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
                {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z},
            };
            static const int edges[12][2] = {
                {0, 1}, {1, 2}, {2, 3}, {3, 0},
                {4, 5}, {5, 6}, {6, 7}, {7, 4},
                {0, 4}, {1, 5}, {2, 6}, {3, 7}};
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

        void RenderSystem::renderDebugLines(RHI::RHICommandBuffer *cmd)
        {
            if (debug_vertices_.empty() || !line_pipeline_) return;

            uint64_t bytes = (uint64_t)debug_vertices_.size() * sizeof(DebugVertex);
            if (!debug_vb_ || debug_vb_capacity_ < bytes)
            {
                if (debug_vb_) device_->destroyBuffer(debug_vb_);
                RHI::BufferDesc d;
                d.name = "debug-lines";
                d.size = std::max<uint64_t>(bytes, 64 * 1024);
                d.usage = (uint32_t)RHI::BufferUsageFlagBits::Vertex | (uint32_t)RHI::BufferUsageFlagBits::TransferDst;
                d.memory_usage = RHI::MemoryUsage::CpuToGpu;
                debug_vb_ = device_->createBuffer(d);
                debug_vb_capacity_ = d.size;
                if (!debug_vb_) return;
            }
            device_->writeBuffer(debug_vb_, debug_vertices_.data(), bytes, 0);

            // Push constants: mat4 mvp + vec4 colour (colour is per vertex here,
            // so the constant colour is white).
            struct LinePush
            {
                float mvp[16];
                float color[4];
            } push;
            std::memcpy(push.mvp, camera_.view_projection.m, sizeof(push.mvp));
            push.color[0] = push.color[1] = push.color[2] = push.color[3] = 1.0f;

            cmd->bindPipeline(line_pipeline_);
            cmd->bindVertexBuffer(debug_vb_, 0);
            cmd->bindDescriptorSets(line_pipeline_, nullptr, 0, nullptr, 0);
            cmd->setPushConstants(&push, sizeof(push));
            cmd->draw((uint32_t)debug_vertices_.size(), 0, 1, 0);
            ++stats_.draw_calls;
        }

    } // namespace Render
} // namespace Genesis
