/*
 * Genesis Render - frame execution: scene sync, the built-in forward pipeline
 * and the CommandBuffer executor.
 *
 * The FrameExecutor is the single piece of code that turns rendering intent
 * into RHI calls.  The built-in forward pipeline is itself expressed with the
 * executor's primitives (bind target, draw items, blit), and user
 * CommandBuffers run through exactly the same code, so a custom pipeline has
 * the same capabilities as the built-in one:
 *
 *   targets  : lazily opened render passes; switching target closes the
 *              current pass, a later draw re-opens it with LoadOp::Load, a
 *              ClearRenderTarget re-opens it with LoadOp::Clear
 *   views    : any camera / view-projection, pushed as a PerFrame slice of
 *              the uniform ring (dynamic offset)
 *   geometry : RenderScene meshes with any Material, whole entities or the
 *              culled scene, skybox, debug lines
 *   post     : fullscreen blits with the post/effects.frag effect set
 *   batch    : 2D / 3D quads, lines, images and SDF text, merged into as
 *              few draws as the texture / space / depth state allows
 */
#include "renderer/render_system.hpp"
#include "renderer/render_internal.hpp"
#include "foundation_layer/genesis_log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace Genesis
{
    namespace Render
    {
        namespace
        {
            // Pixel (top-left origin, y down) -> clip matrix for a viewport
            // rectangle given in the same pixel space.
            Math::Mat4 pixelToClip(float vx, float vy, float vw, float vh, bool row0_top)
            {
                Math::Mat4 m = Math::Mat4::identity();
                const float sy = row0_top ? 2.0f / vh : -2.0f / vh;
                m.m[0] = 2.0f / vw;
                m.m[5] = sy;
                m.m[10] = 1.0f;
                m.m[12] = -1.0f - vx * (2.0f / vw);
                m.m[13] = (row0_top ? -1.0f : 1.0f) - vy * sy;
                return m;
            }

            void fillPerFrame(PerFrameUBO &ubo, const CameraState &cam, uint32_t w, uint32_t h, double time, float dt,
                              uint64_t frame, float exposure)
            {
                ubo.view = GpuMat4::from(cam.view);
                ubo.projection = GpuMat4::from(cam.projection);
                ubo.view_projection = GpuMat4::from(cam.view_projection);
                ubo.inv_view_projection = GpuMat4::from(cam.inv_view_projection);
                ubo.inv_view = GpuMat4::from(cam.inv_view);
                ubo.camera_position = GpuVec4(cam.position.x, cam.position.y, cam.position.z, Math::toRadians(cam.fov_degrees));
                ubo.frame_params = GpuVec4((float)time, dt, (float)frame, exposure);
                ubo.viewport_params = GpuVec4((float)w, (float)h, 1.0f / (float)std::max(1u, w), 1.0f / (float)std::max(1u, h));
                ubo.backend_params = GpuVec4(0.0f, cam.depth_zero_to_one ? 1.0f : 0.0f, 0.0f, 0.0f);
            }
        } // namespace

        //==================================================================
        // Executor
        //==================================================================
        class FrameExecutor
        {
        public:
            FrameExecutor(RenderSystem &rs, RHI::RHICommandBuffer *cmd, ECS::World *world, float dt, const TargetRef &present,
                          uint64_t serial)
                : rs_(rs), cmd_(cmd), world_(world), dt_(dt), present_(present), serial_(serial)
            {
            }

            //--------------------------------------------------------------
            // Targets
            //--------------------------------------------------------------
            TargetRef resolve(const RenderTargetId &id) const
            {
                switch (id.kind)
                {
                case RenderTargetId::Kind::Camera: return rs_.hdr_target_ ? rs_.hdr_target_->target() : TargetRef();
                case RenderTargetId::Kind::Backbuffer: return present_;
                case RenderTargetId::Kind::Texture: return id.texture && id.texture->valid() ? id.texture->target() : TargetRef();
                }
                return TargetRef();
            }

            void setTarget(const TargetRef &t)
            {
                if (has_target_ && t == target_) return;
                flushBatch();
                endPass();
                target_ = t;
                has_target_ = t.valid();
                custom_viewport_ = false;
                // swapchain contents are undefined until the first pass of the frame
                pending_clear_ = (t == present_ && !present_rendered_);
                if (following_main_) setMainView(); // keep the aspect right
            }

            void clearTarget(bool color, const Math::Color &c, bool depth, float d)
            {
                if (!has_target_ || (!color && !depth)) return;
                flushBatch();
                endPass();
                pending_clear_ = true;
                clear_color_ = color ? c : Math::Color(0, 0, 0, 1);
                clear_depth_ = d;
                ensurePass();
            }

            void ensurePass()
            {
                if (pass_open_ || !has_target_) return;
                RHI::RenderPassBeginInfo bi;
                bi.clear_count = 2;
                bi.clears[0].color[0] = clear_color_.r;
                bi.clears[0].color[1] = clear_color_.g;
                bi.clears[0].color[2] = clear_color_.b;
                bi.clears[0].color[3] = clear_color_.a;
                bi.clears[1].depth = clear_depth_;
                bi.clears[1].stencil = 0;
                cmd_->beginRenderPass(pending_clear_ ? target_.pass_clear : target_.pass_load, target_.framebuffer, bi);
                pass_open_ = true;
                pending_clear_ = false;
                if (target_ == present_) present_rendered_ = true;
                ++rs_.stats_.passes;
                bound_pipeline_ = nullptr;
                applyViewport();
            }

            void endPass()
            {
                if (!pass_open_) return;
                flushBatch();
                cmd_->endRenderPass();
                pass_open_ = false;
                bound_pipeline_ = nullptr;
            }

            // Screen space viewport rectangle (top-left origin).
            void viewportRect(float &x, float &y, float &w, float &h) const
            {
                if (custom_viewport_)
                {
                    x = vp_[0]; y = vp_[1]; w = vp_[2]; h = vp_[3];
                }
                else
                {
                    x = 0.0f; y = 0.0f; w = (float)target_.width; h = (float)target_.height;
                }
            }

            void applyViewport()
            {
                if (!pass_open_) return;
                float x, y, w, h;
                viewportRect(x, y, w, h);
                // Row space: y counts rows of the attachment.
                const float row_y = target_.row0_top ? y : (float)target_.height - y - h;
                cmd_->setViewport(RHI::Viewport{x, row_y, w, h, 0.0f, 1.0f});
                const int32_t sx = std::max(0, (int32_t)std::floor(x));
                const int32_t sy = std::max(0, (int32_t)std::floor(row_y));
                cmd_->setScissor(RHI::Rect2D{sx, sy, (uint32_t)std::max(1.0f, w), (uint32_t)std::max(1.0f, h)});
            }

            void setViewport(float x, float y, float w, float h)
            {
                flushBatch();
                custom_viewport_ = true;
                vp_[0] = x; vp_[1] = y; vp_[2] = std::max(1.0f, w); vp_[3] = std::max(1.0f, h);
                applyViewport();
                if (following_main_) setMainView();
            }

            void resetViewport()
            {
                flushBatch();
                custom_viewport_ = false;
                applyViewport();
                if (following_main_) setMainView();
            }

            //--------------------------------------------------------------
            // Views
            //--------------------------------------------------------------
            float targetAspect() const
            {
                float x, y, w, h;
                viewportRect(x, y, w, h);
                return h > 0.0f ? w / h : 1.0f;
            }

            uint32_t pushView(const CameraState &c)
            {
                PerFrameUBO ubo;
                fillPerFrame(ubo, c, target_.width, target_.height, rs_.time_, dt_, rs_.stats_.frame_index, rs_.settings_.exposure);
                return rs_.frames_.pushUniform(ubo);
            }

            void setView(const CameraState &c, bool main)
            {
                flushBatch();
                view_ = c;
                view_offset_ = pushView(view_);
                following_main_ = main;
                items_valid_ = false;
            }

            void setMainView()
            {
                CameraState c = rs_.camera_;
                c.depth_zero_to_one = rs_.device_->info().clip_depth_zero_to_one;
                if (!c.orthographic || c.aspect <= 0.0f) c.aspect = targetAspect();
                if (main_aspect_override_ > 0.0f) c.aspect = main_aspect_override_;
                c.update();
                setView(c, true);
            }

            void setLookAt(const Math::Vec3 &eye, const Math::Vec3 &target, float fov, float near_plane, float far_plane)
            {
                CameraState c;
                c.depth_zero_to_one = rs_.device_->info().clip_depth_zero_to_one;
                c.lookAt(eye, target);
                c.fov_degrees = fov;
                c.near_plane = near_plane;
                c.far_plane = far_plane;
                c.aspect = targetAspect();
                c.update();
                setView(c, false);
            }

            void setViewProjection(const Math::Mat4 &view, const Math::Mat4 &proj)
            {
                CameraState c;
                c.depth_zero_to_one = rs_.device_->info().clip_depth_zero_to_one;
                c.view = view;
                c.projection = proj;
                c.view_projection = proj * view;
                c.inv_view_projection = c.view_projection.inversed();
                c.inv_view = view.inversed();
                c.position = c.inv_view.translation();
                c.forward = Math::normalize(-c.inv_view.zAxis());
                c.up = Math::normalize(c.inv_view.yAxis());
                c.right = Math::normalize(c.inv_view.xAxis());
                setView(c, false);
            }

            void setCameraEntity(ECS::Entity e)
            {
                if (!world_) return;
                ECS::CameraComponent *cam = world_->getComponent<ECS::CameraComponent>(e);
                ECS::TransformComponent *t = world_->getComponent<ECS::TransformComponent>(e);
                if (!cam || !t) return;
                CameraState c;
                c.depth_zero_to_one = rs_.device_->info().clip_depth_zero_to_one;
                c.updateFromTransform(t->world_matrix);
                c.fov_degrees = cam->fov_degrees;
                c.near_plane = cam->near_plane;
                c.far_plane = cam->far_plane;
                c.orthographic = cam->orthographic;
                c.ortho_size = cam->ortho_size;
                c.aspect = cam->aspect_override > 0.0f ? cam->aspect_override : targetAspect();
                c.update();
                setView(c, false);
            }

            //--------------------------------------------------------------
            // Frame shared uniforms
            //--------------------------------------------------------------
            void uploadSharedUniforms()
            {
                LightsUBO lights;
                std::memset(&lights, 0, sizeof(lights));
                const RenderSettings &s = rs_.settings_;
                lights.ambient_color = GpuVec4(s.ambient_color.r, s.ambient_color.g, s.ambient_color.b, s.ambient_intensity);
                uint32_t count = 0;
                if (world_)
                {
                    world_->each<ECS::LightComponent>([&](ECS::Entity e, ECS::LightComponent &light)
                    {
                        if (!light.active || count >= kMaxLights) return;
                        ECS::TransformComponent *t = world_->getComponent<ECS::TransformComponent>(e);
                        Math::Mat4 wm = t ? t->world_matrix : Math::Mat4::identity();
                        GpuLight &g = lights.lights[count];
                        Math::Vec3 position = wm.translation();
                        Math::Vec3 direction = -wm.zAxis();
                        if (Math::lengthSquared(direction) < 1e-8f) direction = Math::Vec3(0, -1, 0);
                        direction = Math::normalize(direction);
                        g.position_range = GpuVec4(position.x, position.y, position.z, light.range);
                        g.direction_cone = GpuVec4(direction.x, direction.y, direction.z, 0.0f);
                        g.color_intensity = GpuVec4(light.color.r, light.color.g, light.color.b, light.intensity);
                        g.type_params = GpuVec4((float)light.type, std::cos(Math::toRadians(light.spot_inner_degrees)),
                                                std::cos(Math::toRadians(light.spot_outer_degrees)), 1.0f);
                        ++count;
                    });
                }
                lights.light_count_params = GpuVec4((float)count, 0.0f, 0.0f, 0.0f);
                rs_.stats_.active_lights = count;
                lights_offset_ = rs_.frames_.pushUniform(lights);

                EnvironmentUBO env;
                const float rotation = Math::toRadians(s.env_rotation_degrees);
                env.env_params = GpuVec4(s.env_intensity, std::cos(rotation), std::sin(rotation), s.show_skybox ? 1.0f : 0.0f);
                // ibl_params.z selects the sky / IBL source for skybox.frag
                // and pbr_mesh.frag: 0 procedural, 1 equirect, 2 baked cube.
                const float source_mode = rs_.ibl_ready_ ? 2.0f : (float)s.sky_mode;
                env.ibl_params = GpuVec4((s.use_ibl && rs_.ibl_ready_) ? 1.0f : 0.0f,
                                         (float)rs_.prefilter_mip_levels_, source_mode, 0.0f);
                env.sky_tint = GpuVec4::from(s.sky_tint);
                env.ground_tint = GpuVec4::from(s.ground_tint);
                env_offset_ = rs_.frames_.pushUniform(env);
            }

            //--------------------------------------------------------------
            // Scene items
            //--------------------------------------------------------------
            void gather(const CameraState &cam, std::vector<RenderItem> &opaque, std::vector<RenderItem> &transparent,
                        bool count_stats)
            {
                opaque.clear();
                transparent.clear();
                if (!world_ || !rs_.scene_) return;
                const bool cull = rs_.settings_.frustum_culling;
                Math::Frustum frustum = Math::Frustum::fromViewProjection(cam.view_projection);
                uint32_t culled = 0;
                world_->each<ECS::MeshComponent>([&](ECS::Entity e, ECS::MeshComponent &mc)
                {
                    if (!mc.visible || !mc.model) return;
                    auto bit = rs_.bindings_.find(e.id);
                    if (bit == rs_.bindings_.end() || bit->second.entity.generation != e.generation) return;
                    appendEntityItems(e, mc, bit->second, cam, cull ? &frustum : nullptr, opaque, transparent, culled);
                });
                std::sort(opaque.begin(), opaque.end(), [](const RenderItem &a, const RenderItem &b) { return a.depth < b.depth; });
                std::sort(transparent.begin(), transparent.end(), [](const RenderItem &a, const RenderItem &b) { return a.depth > b.depth; });
                if (count_stats)
                {
                    rs_.stats_.culled_items = culled;
                    rs_.stats_.opaque_items = (uint32_t)opaque.size();
                    rs_.stats_.transparent_items = (uint32_t)transparent.size();
                }
            }

            void appendEntityItems(ECS::Entity e, const ECS::MeshComponent &mc, const RenderSystem::EntityMeshBinding &binding,
                                   const CameraState &cam, const Math::Frustum *frustum, std::vector<RenderItem> &opaque,
                                   std::vector<RenderItem> &transparent, uint32_t &culled)
            {
                ECS::TransformComponent *t = world_->getComponent<ECS::TransformComponent>(e);
                const Math::Mat4 model = t ? t->world_matrix : Math::Mat4::identity();
                std::vector<ModelData::PlacedMesh> placed = mc.model->flattenNodes();
                for (const ModelData::PlacedMesh &pm : placed)
                {
                    if (!mc.mesh_indices.empty() &&
                        std::find(mc.mesh_indices.begin(), mc.mesh_indices.end(), pm.mesh_index) == mc.mesh_indices.end())
                        continue;
                    if (pm.mesh_index < 0 || (size_t)pm.mesh_index >= binding.mesh_indices.size()) continue;
                    GpuMesh *gpu = rs_.scene_->mesh(binding.mesh_indices[pm.mesh_index]);
                    if (!gpu) continue;
                    int32_t mi = gpu->material_index;
                    const size_t slot = (size_t)pm.mesh_index;
                    if (slot < mc.material_overrides.size() && mc.material_overrides[slot] >= 0) mi = mc.material_overrides[slot];
                    else if (slot < binding.material_overrides.size() && binding.material_overrides[slot] >= 0)
                        mi = binding.material_overrides[slot];
                    Material *material = rs_.scene_->material(mi);
                    if (!material) material = rs_.scene_->defaultMaterial();

                    RenderItem item;
                    item.mesh = gpu;
                    item.material = material;
                    item.model = model * pm.transform;
                    item.normal_matrix = item.model.normalMatrix();
                    item.world_bounds = gpu->bounds.transformed(item.model);
                    item.pick_id = mc.pick_id;
                    item.entity = e;
                    item.depth = Math::length(cam.position - item.world_bounds.center());
                    if (frustum && item.world_bounds.valid && !frustum->intersectsAABB(item.world_bounds))
                    {
                        ++culled;
                        continue;
                    }
                    if (material->isTransparent()) transparent.push_back(item);
                    else opaque.push_back(item);
                }
            }

            //--------------------------------------------------------------
            // Mesh drawing
            //--------------------------------------------------------------
            RHI::RHIPipeline *meshPipeline(const Material &m)
            {
                PipelineKey key;
                key.vertex_shader = "scene/pbr_mesh.vert";
                key.fragment_shader = "scene/pbr_mesh.frag";
                key.layout = &rs_.mesh_layout_->desc();
                key.vertex_layout = VertexLayoutKind::Mesh;
                key.cull = m.rhiCull();
                key.polygon = rs_.settings_.wireframe ? RHI::PolygonMode::Line : RHI::PolygonMode::Fill;
                key.depth_test = m.depth_test;
                key.depth_write = m.depth_write && !m.isTransparent();
                key.blend = m.blend == MaterialBlend::Transparent ? BlendPreset::Alpha
                            : m.blend == MaterialBlend::Additive  ? BlendPreset::Additive
                                                                  : BlendPreset::Opaque;
                key.color_format = target_.color_format;
                key.depth_format = target_.depth_format;
                return rs_.pipelines_.get(key, "pbr");
            }

            uint32_t materialOffset(Material &m)
            {
                if (m.ubo_frame != serial_)
                {
                    m.ubo_offset = rs_.frames_.pushUniform(m.buildUBO(rs_.settings_.shading_mode));
                    m.ubo_frame = serial_;
                }
                return m.ubo_offset;
            }

            // Reading the texture that is being rendered to is undefined on every backend.
            bool samplesTarget(const Material &m) const
            {
                if (!target_.color) return false;
                for (const Material::Slot &s : m.slots)
                    if (s.enabled && s.render_texture && s.render_texture->color() == target_.color) return true;
                return false;
            }

            void drawMeshItem(GpuMesh *mesh, Material *material, const Math::Mat4 &model, const Math::Mat4 &normal_matrix,
                              uint32_t pick_id)
            {
                if (!mesh || !material || !has_target_) return;
                if (samplesTarget(*material))
                {
                    ++rs_.stats_.feedback_skips;
                    return;
                }
                flushBatch();
                ensurePass();
                if (!rs_.scene_->prepareMaterial(*material)) return;
                RHI::RHIPipeline *pipeline = meshPipeline(*material);
                if (!pipeline) return;

                PerDrawUBO pd;
                pd.model = GpuMat4::from(model);
                pd.normal_matrix = GpuMat4::from(normal_matrix);
                pd.object_params = GpuVec4((float)pick_id, rs_.settings_.wireframe ? 1.0f : 0.0f, 0.0f, 0.0f);
                const uint32_t draw_offset = rs_.frames_.pushUniform(pd);
                const uint32_t mat_offset = materialOffset(*material);

                if (pipeline != bound_pipeline_)
                {
                    cmd_->bindPipeline(pipeline);
                    bound_pipeline_ = pipeline;
                }
                cmd_->bindVertexBuffer(mesh->vertex_buffer, 0);
                cmd_->bindIndexBuffer(mesh->index_buffer, mesh->index_type, 0);
                RHI::RHIDescriptorSet *sets[3] = {rs_.scene_set_, material->descriptor_set, rs_.per_draw_set_};
                const uint32_t offsets[5] = {view_offset_, lights_offset_, env_offset_, mat_offset, draw_offset};
                cmd_->bindDescriptorSets(pipeline, sets, 3, offsets, 5);
                cmd_->drawIndexed(mesh->index_count, 0, 0, 1, 0);
                ++rs_.stats_.draw_calls;
                rs_.stats_.triangles += mesh->index_count / 3;
            }

            void drawItems(const std::vector<RenderItem> &items)
            {
                for (const RenderItem &item : items)
                    drawMeshItem(item.mesh, item.material, item.model, item.normal_matrix, item.pick_id);
            }

            void ensureItems()
            {
                if (items_valid_) return;
                if (following_main_ && main_items_)
                {
                    // Re-use the main camera's culled lists.
                    view_opaque_ = rs_.opaque_items_;
                    view_transparent_ = rs_.transparent_items_;
                }
                else
                {
                    gather(view_, view_opaque_, view_transparent_, false);
                }
                items_valid_ = true;
            }

            void drawScene()
            {
                ensureItems();
                drawItems(view_opaque_);
                drawItems(view_transparent_);
            }

            void drawEntity(ECS::Entity e)
            {
                if (!world_ || !world_->isAlive(e)) return;
                ECS::MeshComponent *mc = world_->getComponent<ECS::MeshComponent>(e);
                const RenderSystem::EntityMeshBinding *b = rs_.binding(e);
                if (!mc || !mc->model || !b) return;
                std::vector<RenderItem> opaque, transparent;
                uint32_t culled = 0;
                appendEntityItems(e, *mc, *b, view_, nullptr, opaque, transparent, culled);
                drawItems(opaque);
                drawItems(transparent);
            }

            void drawSkybox()
            {
                if (!has_target_) return;
                flushBatch();
                ensurePass();
                PipelineKey key;
                key.vertex_shader = "common/fullscreen.vert";
                key.fragment_shader = "scene/skybox.frag";
                key.layout = &rs_.skybox_layout_->desc();
                key.vertex_layout = VertexLayoutKind::Fullscreen;
                key.cull = RHI::CullMode::None;
                key.depth_test = true;
                key.depth_write = false;
                key.color_format = target_.color_format;
                key.depth_format = target_.depth_format;
                RHI::RHIPipeline *p = rs_.pipelines_.get(key, "skybox");
                if (!p) return;
                cmd_->bindPipeline(p);
                bound_pipeline_ = p;
                cmd_->bindVertexBuffer(rs_.quad_vb_, 0);
                RHI::RHIDescriptorSet *sets[2] = {rs_.scene_set_, rs_.skybox_set_};
                const uint32_t offsets[3] = {view_offset_, lights_offset_, env_offset_};
                cmd_->bindDescriptorSets(p, sets, 2, offsets, 3);
                cmd_->draw(6, 0, 1, 0);
                ++rs_.stats_.draw_calls;
            }

            void drawDebugLines()
            {
                if (rs_.debug_vertices_.empty() || !has_target_) return;
                flushBatch();
                ensurePass();
                PipelineKey key;
                key.vertex_shader = "scene/debug_line.vert";
                key.fragment_shader = "scene/debug_line.frag";
                key.layout = &rs_.line_layout_->desc();
                key.vertex_layout = VertexLayoutKind::Line;
                key.topology = RHI::PrimitiveTopology::LineList;
                key.cull = RHI::CullMode::None;
                key.depth_test = true;
                key.depth_write = false;
                key.color_format = target_.color_format;
                key.depth_format = target_.depth_format;
                RHI::RHIPipeline *p = rs_.pipelines_.get(key, "debug-lines");
                if (!p) return;
                const uint64_t bytes = rs_.debug_vertices_.size() * sizeof(RenderSystem::DebugVertex);
                TransientAllocation vb = rs_.frames_.uploadVertices(rs_.debug_vertices_.data(), bytes);
                if (!vb.valid()) return;
                struct LinePush
                {
                    float mvp[16];
                    float color[4];
                } push;
                std::memcpy(push.mvp, view_.view_projection.m, sizeof(push.mvp));
                push.color[0] = push.color[1] = push.color[2] = push.color[3] = 1.0f;
                cmd_->bindPipeline(p);
                bound_pipeline_ = p;
                cmd_->bindVertexBuffer(vb.buffer, vb.offset);
                cmd_->bindDescriptorSets(p, nullptr, 0, nullptr, 0);
                cmd_->setPushConstants(&push, sizeof(push));
                cmd_->draw((uint32_t)rs_.debug_vertices_.size(), 0, 1, 0);
                ++rs_.stats_.draw_calls;
            }

            //--------------------------------------------------------------
            // Fullscreen blits
            //--------------------------------------------------------------
            RenderTexturePtr scratchFor(const TargetRef &like)
            {
                for (auto &s : scratch_)
                    if (s->width() == like.width && s->height() == like.height && s->colorFormat() == like.color_format) return s;
                RenderTexturePtr rt = rs_.createRenderTexture("blit.scratch", like.width, like.height,
                                                              RHI::pixelFormatIsFloat(like.color_format), false);
                if (rt) scratch_.push_back(rt);
                return rt;
            }

            void fullscreen(RHI::RHITexture *source, uint32_t src_w, uint32_t src_h, PostEffect effect,
                            const Math::Vec4 &params, const Math::Color &tint, bool tonemap, float vignette, float saturation,
                            bool flip)
            {
                if (!source || !has_target_) return;
                flushBatch();
                ensurePass();
                PipelineKey key;
                key.vertex_shader = "common/fullscreen.vert";
                key.fragment_shader = "post/effects.frag";
                key.layout = &rs_.post_layout_->desc();
                key.vertex_layout = VertexLayoutKind::Fullscreen;
                key.cull = RHI::CullMode::None;
                key.depth_test = false;
                key.depth_write = false;
                key.color_format = target_.color_format;
                key.depth_format = target_.depth_format;
                RHI::RHIPipeline *p = rs_.pipelines_.get(key, "post");
                if (!p) return;
                const RenderSettings &s = rs_.settings_;
                PostParamsUBO u;
                u.a = GpuVec4(s.exposure, s.gamma, (float)s.tonemap_mode, flip ? 1.0f : 0.0f);
                u.b = GpuVec4(vignette, saturation, (float)rs_.time_, (float)effect);
                u.c = GpuVec4(params.x, params.y, params.z, params.w);
                u.d = GpuVec4(1.0f / (float)std::max(1u, src_w), 1.0f / (float)std::max(1u, src_h), tonemap ? 1.0f : 0.0f, 0.0f);
                u.tint = GpuVec4::from(tint);
                const uint32_t offset = rs_.frames_.pushUniform(u);
                RHI::RHIDescriptorSet *set = rs_.textureSet(rs_.post_layout_, source, rs_.linear_clamp_, true);
                if (!set) return;
                cmd_->bindPipeline(p);
                bound_pipeline_ = p;
                cmd_->bindVertexBuffer(rs_.quad_vb_, 0);
                cmd_->bindDescriptorSets(p, &set, 1, &offset, 1);
                cmd_->draw(6, 0, 1, 0);
                ++rs_.stats_.draw_calls;
            }

            void blit(const TargetRef &src, const TargetRef &dst, PostEffect effect, const Math::Vec4 &params,
                      const Math::Color &tint, bool tonemap, float vignette = 0.0f, float saturation = 1.0f)
            {
                if (!src.valid() || !src.color)
                {
                    GEN_LOG_WARN("[CommandBuffer] blit: the source cannot be sampled (backbuffer or invalid target)");
                    return;
                }
                if (!dst.valid()) return;
                TargetRef source = src;
                if (src == dst)
                {
                    // Reading and writing one texture in a pass is undefined:
                    // copy through a scratch target first.
                    RenderTexturePtr tmp = scratchFor(src);
                    if (!tmp) return;
                    setTarget(tmp->target());
                    fullscreen(src.color, src.width, src.height, PostEffect::None, Math::Vec4(1, 1, 0, 0),
                               Math::Color::white(), false, 0.0f, 1.0f, false);
                    source = tmp->target();
                }
                setTarget(dst);
                // Rendered images are bottom-up; flip when the destination is
                // displayed top-down.
                const bool flip = dst.row0_top && !src.row0_top;
                const bool apply_tonemap = tonemap || (src.hdr && !dst.hdr);
                fullscreen(source.color, source.width, source.height, effect, params, tint, apply_tonemap, vignette,
                           saturation, flip);
            }

            //--------------------------------------------------------------
            // Batched primitives
            //--------------------------------------------------------------
            struct Segment
            {
                RHI::RHITexture *texture = nullptr;
                RHI::RHISampler *sampler = nullptr;
                bool is3d = false;
                bool depth_test = false;
                BlendPreset blend = BlendPreset::Alpha;
                uint32_t matrix_offset = 0;
                uint32_t first_index = 0;
                uint32_t index_count = 0;
            };

            void beginSegment(RHI::RHITexture *tex, RHI::RHISampler *samp, bool is3d)
            {
                if (!tex) { tex = rs_.white_; samp = rs_.linear_clamp_; }
                const bool depth = is3d && depth3d_;
                if (!segments_.empty())
                {
                    Segment &last = segments_.back();
                    if (last.texture == tex && last.sampler == samp && last.is3d == is3d && last.depth_test == depth &&
                        (!is3d || last_view_serial_ == view_offset_) && (is3d || last_2d_serial_ == viewport_serial()))
                        return;
                }
                Segment s;
                s.texture = tex;
                s.sampler = samp;
                s.is3d = is3d;
                s.depth_test = depth;
                s.first_index = (uint32_t)indices_.size();
                BatchParamsUBO u;
                if (is3d)
                {
                    u.view_proj = GpuMat4::from(view_.view_projection);
                    last_view_serial_ = view_offset_;
                }
                else
                {
                    float x, y, w, h;
                    viewportRect(x, y, w, h);
                    u.view_proj = GpuMat4::from(pixelToClip(x, y, w, h, target_.row0_top));
                    last_2d_serial_ = viewport_serial();
                }
                u.params = GpuVec4(0, 0, 0, 0);
                s.matrix_offset = rs_.frames_.pushUniform(u);
                segments_.push_back(s);
            }

            uint64_t viewport_serial() const
            {
                float x, y, w, h;
                viewportRect(x, y, w, h);
                uint64_t v = 0;
                std::memcpy(&v, &w, 4);
                uint32_t hb;
                std::memcpy(&hb, &h, 4);
                return (v << 32) ^ hb ^ ((uint64_t)(uint32_t)x << 16) ^ (uint64_t)(uint32_t)y ^ (uint64_t)target_.row0_top;
            }

            void pushQuad(const Math::Vec3 p[4], const float uv[4][2], const Math::Color &c, float kind, float outline,
                          const Math::Color &oc)
            {
                const uint32_t base = (uint32_t)vertices_.size();
                for (int i = 0; i < 4; ++i)
                {
                    BatchVertex v;
                    v.position[0] = p[i].x; v.position[1] = p[i].y; v.position[2] = p[i].z;
                    v.uv[0] = uv[i][0]; v.uv[1] = uv[i][1];
                    v.color[0] = c.r; v.color[1] = c.g; v.color[2] = c.b; v.color[3] = c.a;
                    v.params[0] = kind; v.params[1] = outline; v.params[2] = 0.0f; v.params[3] = 0.0f;
                    v.outline[0] = oc.r; v.outline[1] = oc.g; v.outline[2] = oc.b; v.outline[3] = oc.a;
                    vertices_.push_back(v);
                }
                const uint32_t idx[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
                indices_.insert(indices_.end(), idx, idx + 6);
                segments_.back().index_count += 6;
                ++rs_.stats_.batched_quads;
            }

            void rect2D(float x, float y, float w, float h, const Math::Color &c, RHI::RHITexture *tex = nullptr,
                        RHI::RHISampler *samp = nullptr, bool flip_v = false)
            {
                beginSegment(tex, samp, false);
                const Math::Vec3 p[4] = {{x, y, 0}, {x + w, y, 0}, {x + w, y + h, 0}, {x, y + h, 0}};
                const float v0 = flip_v ? 1.0f : 0.0f, v1 = flip_v ? 0.0f : 1.0f;
                const float uv[4][2] = {{0, v0}, {1, v0}, {1, v1}, {0, v1}};
                pushQuad(p, uv, c, 0.0f, 0.0f, Math::Color(0, 0, 0, 0));
            }

            void line2D(const Math::Vec2 &a, const Math::Vec2 &b, const Math::Color &c, float thickness)
            {
                Math::Vec2 d = b - a;
                float len = Math::length(d);
                if (len < 1e-5f) return;
                Math::Vec2 n = Math::Vec2(-d.y, d.x) * (0.5f * std::max(0.5f, thickness) / len);
                beginSegment(nullptr, nullptr, false);
                const Math::Vec3 p[4] = {{a.x + n.x, a.y + n.y, 0}, {b.x + n.x, b.y + n.y, 0},
                                         {b.x - n.x, b.y - n.y, 0}, {a.x - n.x, a.y - n.y, 0}};
                const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                pushQuad(p, uv, c, 0.0f, 0.0f, Math::Color(0, 0, 0, 0));
            }

            void text2D(const std::string &text, float x, float y, float size, const Math::Color &c, Text::TextAlign align,
                        float outline, const Math::Color &oc)
            {
                Text::Font *font = rs_.font();
                if (!font || text.empty()) return;
                Text::TextLayout layout = font->layout(text, size, align, Text::TextAnchor::Top, true);
                beginSegment(font->atlas(), font->sampler(), false);
                for (const Text::GlyphQuad &q : layout.quads)
                {
                    const Math::Vec3 p[4] = {{x + q.x0, y + q.y0, 0}, {x + q.x1, y + q.y0, 0},
                                             {x + q.x1, y + q.y1, 0}, {x + q.x0, y + q.y1, 0}};
                    const float uv[4][2] = {{q.u0, q.v0}, {q.u1, q.v0}, {q.u1, q.v1}, {q.u0, q.v1}};
                    pushQuad(p, uv, c, 1.0f, outline, oc);
                }
            }

            void quad3D(const Math::Mat4 &transform, const Math::Vec2 &size, const Math::Color &c, bool billboard)
            {
                beginSegment(nullptr, nullptr, true);
                const float hw = size.x * 0.5f, hh = size.y * 0.5f;
                Math::Vec3 p[4];
                if (billboard)
                {
                    const Math::Vec3 o = transform.translation();
                    const Math::Vec3 r = view_.right * hw, u = view_.up * hh;
                    p[0] = o - r + u; p[1] = o + r + u; p[2] = o + r - u; p[3] = o - r - u;
                }
                else
                {
                    p[0] = transform.transformPoint(Math::Vec3(-hw, hh, 0));
                    p[1] = transform.transformPoint(Math::Vec3(hw, hh, 0));
                    p[2] = transform.transformPoint(Math::Vec3(hw, -hh, 0));
                    p[3] = transform.transformPoint(Math::Vec3(-hw, -hh, 0));
                }
                const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                pushQuad(p, uv, c, 0.0f, 0.0f, Math::Color(0, 0, 0, 0));
            }

            void line3D(const Math::Vec3 &a, const Math::Vec3 &b, const Math::Color &c, float thickness)
            {
                Math::Vec3 d = b - a;
                if (Math::lengthSquared(d) < 1e-12f) return;
                Math::Vec3 to_eye = view_.position - (a + b) * 0.5f;
                Math::Vec3 side = Math::cross(d, to_eye);
                if (Math::lengthSquared(side) < 1e-12f) side = Math::cross(d, view_.up);
                side = Math::normalize(side) * (0.5f * thickness);
                beginSegment(nullptr, nullptr, true);
                const Math::Vec3 p[4] = {a + side, b + side, b - side, a - side};
                const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                pushQuad(p, uv, c, 0.0f, 0.0f, Math::Color(0, 0, 0, 0));
            }

            void text3D(const std::string &text, const Math::Mat4 &transform, float size, const Math::Color &c,
                        bool billboard, Text::TextAlign align, float outline, const Math::Color &oc)
            {
                Text::Font *font = rs_.font();
                if (!font || text.empty()) return;
                Text::TextLayout layout = font->layout(text, size, align, Text::TextAnchor::Middle, false);
                beginSegment(font->atlas(), font->sampler(), true);
                const Math::Vec3 o = transform.translation();
                for (const Text::GlyphQuad &q : layout.quads)
                {
                    Math::Vec3 p[4];
                    const float xs[4] = {q.x0, q.x1, q.x1, q.x0};
                    const float ys[4] = {q.y0, q.y0, q.y1, q.y1};
                    for (int i = 0; i < 4; ++i)
                    {
                        if (billboard) p[i] = o + view_.right * xs[i] + view_.up * ys[i];
                        else p[i] = transform.transformPoint(Math::Vec3(xs[i], ys[i], 0.0f));
                    }
                    const float uv[4][2] = {{q.u0, q.v0}, {q.u1, q.v0}, {q.u1, q.v1}, {q.u0, q.v1}};
                    pushQuad(p, uv, c, 1.0f, outline, oc);
                }
            }

            void flushBatch()
            {
                if (segments_.empty() || flushing_) return;
                flushing_ = true;
                if (!vertices_.empty() && has_target_)
                {
                    ensurePass();
                    TransientAllocation vb = rs_.frames_.uploadVertices(vertices_.data(), vertices_.size() * sizeof(BatchVertex));
                    TransientAllocation ib = rs_.frames_.uploadVertices(indices_.data(), indices_.size() * sizeof(uint32_t));
                    if (vb.valid() && ib.valid())
                    {
                        for (const Segment &s : segments_)
                        {
                            if (!s.index_count) continue;
                            PipelineKey key;
                            key.vertex_shader = "ui/batch.vert";
                            key.fragment_shader = "ui/batch.frag";
                            key.layout = &rs_.batch_layout_->desc();
                            key.vertex_layout = VertexLayoutKind::Batch;
                            key.cull = RHI::CullMode::None;
                            key.depth_test = s.depth_test;
                            key.depth_write = false;
                            key.blend = s.blend;
                            key.color_format = target_.color_format;
                            key.depth_format = target_.depth_format;
                            RHI::RHIPipeline *p = rs_.pipelines_.get(key, "batch");
                            RHI::RHIDescriptorSet *set = rs_.textureSet(rs_.batch_layout_, s.texture, s.sampler, true);
                            if (!p || !set) continue;
                            cmd_->bindPipeline(p);
                            bound_pipeline_ = p;
                            cmd_->bindVertexBuffer(vb.buffer, vb.offset);
                            cmd_->bindIndexBuffer(ib.buffer, RHI::IndexType::UInt32, ib.offset);
                            cmd_->bindDescriptorSets(p, &set, 1, &s.matrix_offset, 1);
                            cmd_->drawIndexed(s.index_count, s.first_index, 0, 1, 0);
                            ++rs_.stats_.draw_calls;
                        }
                    }
                }
                vertices_.clear();
                indices_.clear();
                segments_.clear();
                flushing_ = false;
            }

            //--------------------------------------------------------------
            // Text components
            //--------------------------------------------------------------
            void drawTextComponents(bool screen_space)
            {
                if (!world_ || !rs_.settings_.draw_text_components || !rs_.font()) return;
                world_->each<ECS::TextComponent>([&](ECS::Entity e, ECS::TextComponent &tc)
                {
                    if (!tc.visible || tc.text.empty() || tc.screen_space != screen_space) return;
                    ECS::TransformComponent *t = world_->getComponent<ECS::TransformComponent>(e);
                    Math::Mat4 m = t ? t->world_matrix : Math::Mat4::identity();
                    const Text::TextAlign align = (Text::TextAlign)std::min(2u, tc.align);
                    if (!screen_space)
                    {
                        Math::Mat4 placed = m * Math::Mat4::translate(tc.offset);
                        const bool keep = depth3d_;
                        depth3d_ = tc.depth_test;
                        text3D(tc.text, placed, tc.size, tc.color, tc.billboard, align, tc.outline, tc.outline_color);
                        depth3d_ = keep;
                        return;
                    }
                    Math::Vec4 clip = main_view_.view_projection * Math::Vec4(m.translation(), 1.0f);
                    if (clip.w <= 1e-4f) return;
                    const float nx = clip.x / clip.w, ny = clip.y / clip.w;
                    const float sx = (nx * 0.5f + 0.5f) * (float)target_.width + tc.offset.x;
                    const float sy = (1.0f - (ny * 0.5f + 0.5f)) * (float)target_.height + tc.offset.y;
                    Text::Font *font = rs_.font();
                    Math::Vec2 extent = font->measure(tc.text, tc.size);
                    text2D(tc.text, sx, sy - extent.y * 0.5f, tc.size, tc.color, align, tc.outline, tc.outline_color);
                });
            }

            //--------------------------------------------------------------
            // CommandBuffer execution
            //--------------------------------------------------------------
            void execute(const CommandBuffer &cb, const TargetRef &default_target)
            {
                setTarget(default_target);
                setMainView();
                custom_viewport_ = false;
                depth3d_ = true;
                ++rs_.stats_.command_buffers;
                for (const DrawCommand &c : cb.commands())
                {
                    ++rs_.stats_.commands;
                    run(c);
                }
                flushBatch();
            }

            void run(const DrawCommand &c)
            {
                using T = DrawCommand::Type;
                switch (c.type)
                {
                case T::SetRenderTarget:
                {
                    TargetRef t = resolve(c.target);
                    if (!t.valid())
                    {
                        GEN_LOG_WARN("[CommandBuffer] setRenderTarget: invalid target");
                        return;
                    }
                    setTarget(t);
                    break;
                }
                case T::ClearRenderTarget: clearTarget(c.b0, c.c0, c.b1, c.f0); break;
                case T::SetViewport: setViewport(c.v0.x, c.v0.y, c.v0.z, c.v0.w); break;
                case T::ResetViewport: resetViewport(); break;
                case T::SetViewProjection: setViewProjection(c.m0, c.m1); break;
                case T::SetCamera:
                    setLookAt(Math::Vec3(c.v0.x, c.v0.y, c.v0.z), Math::Vec3(c.v1.x, c.v1.y, c.v1.z), c.f0, c.f1, c.f2);
                    break;
                case T::SetCameraEntity: setCameraEntity(c.entity); break;
                case T::ResetCamera: setMainView(); break;
                case T::SetDepthTest3D:
                    flushBatch();
                    depth3d_ = c.b0;
                    break;
                case T::DrawMesh:
                {
                    GpuMesh *mesh = rs_.scene_->mesh(c.u0);
                    if (!mesh) return;
                    Material *m = c.material ? c.material.get() : rs_.scene_->material(mesh->material_index);
                    if (!m) m = rs_.scene_->defaultMaterial();
                    if (m->owner != rs_.scene_.get()) rs_.scene_->addMaterial(c.material);
                    drawMeshItem(mesh, m, c.m0, c.m0.normalMatrix(), 0);
                    break;
                }
                case T::DrawEntity: drawEntity(c.entity); break;
                case T::DrawScene: drawScene(); break;
                case T::DrawSkybox: drawSkybox(); break;
                case T::Blit:
                    blit(resolve(c.source), resolve(c.target), (PostEffect)c.u0, c.v1, c.c1, c.b0);
                    break;
                case T::DrawRect: rect2D(c.v0.x, c.v0.y, c.v0.z, c.v0.w, c.c0); break;
                case T::DrawLine2D: line2D(Math::Vec2(c.v0.x, c.v0.y), Math::Vec2(c.v0.z, c.v0.w), c.c0, c.f0); break;
                case T::DrawImage:
                {
                    TargetRef src = resolve(c.source);
                    if (!src.color) return;
                    RHI::RHISampler *samp = c.source.texture ? c.source.texture->sampler() : rs_.linear_clamp_;
                    rect2D(c.v0.x, c.v0.y, c.v0.z, c.v0.w, c.c0, src.color, samp, !src.row0_top);
                    break;
                }
                case T::DrawText:
                    text2D(c.text, c.v0.x, c.v0.y, c.f0, c.c0, (Text::TextAlign)c.u0, c.f1, c.c1);
                    break;
                case T::DrawLine3D:
                    line3D(Math::Vec3(c.v0.x, c.v0.y, c.v0.z), Math::Vec3(c.v1.x, c.v1.y, c.v1.z), c.c0, c.f0);
                    break;
                case T::DrawQuad3D: quad3D(c.m0, Math::Vec2(c.v0.x, c.v0.y), c.c0, c.b1); break;
                case T::DrawText3D: text3D(c.text, c.m0, c.f0, c.c0, c.b1, (Text::TextAlign)c.u0, c.f1, c.c1); break;
                }
            }

            //--------------------------------------------------------------
            // Frame programs
            //--------------------------------------------------------------
            void runEvent(CameraEvent e, const TargetRef &default_target)
            {
                for (const CommandBufferPtr &cb : rs_.injected_[(size_t)e])
                    if (cb && !cb->empty()) execute(*cb, default_target);
                const CommandBuffer &imm = rs_.immediate_[(size_t)e];
                if (!imm.empty()) execute(imm, default_target);
            }

            void forward()
            {
                const TargetRef camera = rs_.hdr_target_->target();
                runEvent(CameraEvent::BeforeScene, camera);

                // ---- scene pass --------------------------------------
                auto scene_start = std::chrono::steady_clock::now();
                setTarget(camera);
                setMainView();
                clearTarget(true, Math::Color(0.0f, 0.0f, 0.0f, 1.0f), true, 1.0f);
                if (rs_.settings_.show_skybox) drawSkybox();
                drawItems(rs_.opaque_items_);
                runEvent(CameraEvent::AfterOpaque, camera);
                setTarget(camera);
                setMainView();
                drawItems(rs_.transparent_items_);
                drawDebugLines();
                drawTextComponents(false);
                runEvent(CameraEvent::AfterTransparent, camera);
                for (const CommandBufferPtr &cb : rs_.once_)
                    if (cb) execute(*cb, camera);
                runEvent(CameraEvent::BeforePost, camera);
                flushBatch();
                endPass();
                auto scene_end = std::chrono::steady_clock::now();
                rs_.stats_.scene_ms = std::chrono::duration<double, std::milli>(scene_end - scene_start).count();

                // ---- present pass -------------------------------------
                const RenderSettings &s = rs_.settings_;
                setTarget(present_);
                clearTarget(true, Math::Color(0, 0, 0, 1), true, 1.0f);
                setMainView();
                blit(camera, present_, s.post_effect, s.post_params, s.post_tint, true, s.vignette, s.saturation);
                runEvent(CameraEvent::AfterPost, present_);
                setTarget(present_);
                setMainView();
                drawTextComponents(true);
                runEvent(CameraEvent::Overlay, present_);
                flushBatch();
                rs_.stats_.present_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - scene_end).count();
            }

            void custom()
            {
                // Only the events that do not depend on the built-in scene pass
                // exist here: BeforeScene (offscreen preparation) and Overlay.
                auto start = std::chrono::steady_clock::now();
                runEvent(CameraEvent::BeforeScene, rs_.hdr_target_->target());
                setTarget(present_);
                for (const CommandBufferPtr &cb : rs_.custom_)
                    if (cb) execute(*cb, present_);
                for (const CommandBufferPtr &cb : rs_.once_)
                    if (cb) execute(*cb, present_);
                setTarget(present_);
                setMainView();
                runEvent(CameraEvent::Overlay, present_);
                flushBatch();
                rs_.stats_.scene_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            }

            void finish(bool run_ui)
            {
                setTarget(present_);
                if (!present_rendered_) pending_clear_ = true;
                ensurePass();
                if (run_ui && rs_.post_present_callback_)
                {
                    rs_.post_present_callback_(cmd_, present_, rs_.post_present_user_);
                    bound_pipeline_ = nullptr;
                }
                endPass();
            }

            void run(bool run_ui)
            {
                main_view_ = rs_.camera_;
                main_view_.depth_zero_to_one = rs_.device_->info().clip_depth_zero_to_one;
                if (rs_.hdr_target_)
                {
                    const RenderTexture &hdr = *rs_.hdr_target_;
                    main_view_.aspect = hdr.height() ? (float)hdr.width() / (float)hdr.height() : 1.0f;
                }
                main_view_.update();
                rs_.camera_.view = main_view_.view;
                rs_.camera_.projection = main_view_.projection;
                rs_.camera_.view_projection = main_view_.view_projection;
                rs_.camera_.inv_view_projection = main_view_.inv_view_projection;
                rs_.camera_.inv_view = main_view_.inv_view;
                rs_.camera_.forward = main_view_.forward;
                rs_.camera_.up = main_view_.up;
                rs_.camera_.right = main_view_.right;
                rs_.camera_.aspect = main_view_.aspect;
                main_aspect_override_ = rs_.camera_aspect_override_;
                gather(main_view_, rs_.opaque_items_, rs_.transparent_items_, true);
                main_items_ = true;
                uploadSharedUniforms();

                if (rs_.settings_.pipeline_mode == PipelineMode::Custom) custom();
                else forward();
                finish(run_ui);
            }

        private:
            RenderSystem &rs_;
            RHI::RHICommandBuffer *cmd_;
            ECS::World *world_;
            float dt_;
            TargetRef present_;
            uint64_t serial_;

            TargetRef target_;
            bool has_target_ = false;
            bool pass_open_ = false;
            bool pending_clear_ = false;
            bool present_rendered_ = false;
            Math::Color clear_color_ = Math::Color(0, 0, 0, 1);
            float clear_depth_ = 1.0f;
            bool custom_viewport_ = false;
            float vp_[4] = {0, 0, 1, 1};
            RHI::RHIPipeline *bound_pipeline_ = nullptr;

            CameraState main_view_;
            CameraState view_;
            uint32_t view_offset_ = 0;
            bool following_main_ = true;
            float main_aspect_override_ = 0.0f;
            bool main_items_ = false;
            bool items_valid_ = false;
            std::vector<RenderItem> view_opaque_, view_transparent_;
            uint32_t lights_offset_ = 0;
            uint32_t env_offset_ = 0;
            bool depth3d_ = true;

            std::vector<BatchVertex> vertices_;
            std::vector<uint32_t> indices_;
            std::vector<Segment> segments_;
            uint64_t last_view_serial_ = ~0ull;
            uint64_t last_2d_serial_ = ~0ull;
            bool flushing_ = false;
            std::vector<RenderTexturePtr> &scratch_ = rs_.scratch_targets_;
        };

        //==================================================================
        // Scene synchronisation (RenderSystem::update)
        //==================================================================
        void RenderSystem::update(ECS::World &world, float delta_time)
        {
            if (!initialized_) return;
            syncFromWorld(world, delta_time);
        }

        void RenderSystem::syncFromWorld(ECS::World &world, float delta_time)
        {
            (void)delta_time;
            if (!scene_) return;
            world_ = &world;

            bool camera_found = false;
            camera_aspect_override_ = 0.0f;
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
                camera_aspect_override_ = cam.aspect_override;
            });

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
                    bool ok = env.hdr_equirect_path.empty() ? bakeProceduralIBL(bake) : bakeIBL(env.hdr_equirect_path, bake);
                    env.ibl_dirty = !ok;
                    if (ok) settings_.sky_mode = SkyMode::Cubemap;
                }
            });
        }

        //==================================================================
        // Frame entry points
        //==================================================================
        void RenderSystem::executeFrame(RHI::RHICommandBuffer *cmd, ECS::World &world, float delta_time, const TargetRef &present,
                                        bool run_ui)
        {
            world_ = &world;
            uint64_t keep_frame = stats_.frame_index;
            stats_ = RenderStats();
            stats_.frame_index = keep_frame;
            time_ += delta_time;
            ++exec_serial_;
            FrameExecutor exec(*this, cmd, &world, delta_time, present, exec_serial_);
            exec.run(run_ui);
            stats_.meshes = (uint32_t)scene_->meshCount();
            stats_.materials = (uint32_t)scene_->materialCount();
            stats_.uniform_bytes = frames_.uniformBytesUsed();
        }

        void RenderSystem::finishFrame()
        {
            for (auto &cb : immediate_) cb.clear();
            once_.clear();
            debug_vertices_.clear();
            ++stats_.frame_index;
        }

        TargetRef RenderSystem::swapchainTarget()
        {
            TargetRef t;
            RHI::RHIRenderPass *pass = device_->swapchainRenderPass();
            RHI::RHIFramebuffer *fb = device_->currentSwapchainFramebuffer();
            if (!pass || !fb) return t;
            if (!swapchain_load_pass_ || swapchain_load_format_ != device_->swapchainFormat())
            {
                RHI::RenderPassDesc d = pass->desc();
                d.name = "swapchain.load";
                d.attachments[0].load_op = RHI::LoadOp::Load;
                d.attachments[0].initial_layout = RHI::TextureLayout::PresentSrc;
                d.attachments[0].final_layout = RHI::TextureLayout::PresentSrc;
                swapchain_load_pass_ = device_->createRenderPass(d);
                swapchain_load_format_ = device_->swapchainFormat();
            }
            t.pass_clear = pass;
            t.pass_load = swapchain_load_pass_;
            t.framebuffer = fb;
            t.color_format = device_->swapchainFormat();
            t.depth_format = RHI::PixelFormat::Undefined;
            t.width = fb->width();
            t.height = fb->height();
            t.row0_top = device_->info().present_row0_top;
            t.hdr = false;
            t.identity = device_;
            return t;
        }

        void RenderSystem::renderFrame(ECS::World &world, float delta_time, bool present_to_swapchain)
        {
            if (!initialized_) return;
            RHI::RHICommandBuffer *cmd = device_->currentCommandBuffer();
            if (!cmd)
            {
                GEN_LOG_WARN("[Render] no current command buffer (did the device begin a frame?)");
                return;
            }
            frames_.beginFrame(device_->currentFrameIndex());
            TargetRef present = present_to_swapchain ? swapchainTarget() : TargetRef();
            if (!present.valid()) present = capture_target_->target();
            executeFrame(cmd, world, delta_time, present, true);
            finishFrame();
        }

        bool RenderSystem::renderAndCapture(ECS::World &world, float delta_time, std::vector<uint8_t> &rgba_out,
                                            uint32_t &width, uint32_t &height)
        {
            if (!initialized_ || !capture_target_ || !capture_target_->valid())
            {
                GEN_LOG_ERROR("[Render] renderAndCapture requires an initialised render system");
                return false;
            }
            // Offscreen frames run outside the swapchain loop: make sure no
            // in-flight frame still uses the current transient region.
            device_->waitIdle();
            frames_.beginFrame(frames_.slot());
            syncFromWorld(world, delta_time);

            RHI::RHICommandBuffer *cmd = device_->createCommandBuffer(RHI::CommandBufferUsage::OneTimeSubmit);
            if (!cmd) return false;
            cmd->begin(RHI::CommandBufferUsage::OneTimeSubmit);
            executeFrame(cmd, world, delta_time, capture_target_->target(), true);
            cmd->end();
            device_->submitAndWait(cmd);
            device_->destroyCommandBuffer(cmd);
            finishFrame();

            width = capture_target_->width();
            height = capture_target_->height();
            rgba_out.assign((size_t)width * height * 4, 0);
            RHI::RHICommandBuffer *read_cmd = device_->createCommandBuffer(RHI::CommandBufferUsage::OneTimeSubmit);
            if (!read_cmd) return false;
            bool ok = read_cmd->readImagePixels(capture_target_->color(), 0, 0, rgba_out.data(), (uint32_t)rgba_out.size());
            device_->destroyCommandBuffer(read_cmd);
            if (!ok) GEN_LOG_ERROR("[Render] readback of the capture target failed");
            return ok;
        }

    } // namespace Render
} // namespace Genesis
