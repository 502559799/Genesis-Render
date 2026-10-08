/*
 * Genesis Render - overlay / command buffer feature system implementation.
 */
#include "renderer/overlay_system.hpp"
#include "foundation_layer/genesis_log.hpp"
#include "foundation_layer/math/math.hpp"
#include "renderer/render_internal.hpp"

#include <cmath>
#include <cstring>
#include <vector>

namespace Genesis
{
    namespace Render
    {
        namespace
        {
            struct QuadParamsUBO
            {
                GpuMat4 view_proj;
                GpuVec4 params; // x = is_2d, y/z/w = tint rgb
            };
            static_assert(sizeof(QuadParamsUBO) == 80, "QuadParamsUBO: 64 + 16");

            RHI::VertexLayout quadVertexLayout()
            {
                RHI::VertexLayout layout;
                layout.stride = sizeof(OverlaySystem::QuadVertex);
                layout.attributes = {
                    {0, RHI::VertexFormat::Float3, 0},
                    {1, RHI::VertexFormat::Float2, 12},
                    {2, RHI::VertexFormat::Float4, 20},
                };
                return layout;
            }

            RHI::RHIPipeline *createQuadPipeline(RHI::RHIDevice *device, RHI::RHIResourceLayout *layout,
                                                  RHI::PixelFormat color_fmt, bool depth)
            {
                RHI::PipelineDesc d;
                d.name = "overlay-quad";
                RHI::ShaderStageDesc vs, fs;
                if (!detail::fillShaderStage("ui/textured_quad.vert", device->backend(), RHI::ShaderStageFlagBits::Vertex, vs) ||
                    !detail::fillShaderStage("ui/textured_quad.frag", device->backend(), RHI::ShaderStageFlagBits::Fragment, fs))
                    return nullptr;
                d.shaders = {vs, fs};
                d.vertex_layout = quadVertexLayout();
                d.topology = RHI::PrimitiveTopology::TriangleList;
                d.rasterizer.cull_mode = RHI::CullMode::None;
                d.depth_stencil.depth_test = depth;
                d.depth_stencil.depth_write = false;
                d.blend.attachments[0].blend_enable = true;
                d.blend.attachments[0].src_color = RHI::BlendFactor::SrcAlpha;
                d.blend.attachments[0].dst_color = RHI::BlendFactor::OneMinusSrcAlpha;
                d.blend.attachments[0].src_alpha = RHI::BlendFactor::One;
                d.blend.attachments[0].dst_alpha = RHI::BlendFactor::OneMinusSrcAlpha;
                d.resource_layout = layout->desc();
                d.color_formats[0] = color_fmt;
                d.color_format_count = 1;
                // Match the HDR scene target's depth/stencil format (the
                // 3D overlay draws into the HDR pass, whose depth texture is
                // D24S8; Metal requires the pipeline depth format to match).
                d.depth_format = depth ? RHI::PixelFormat::D24_UNORM_S8_UINT : RHI::PixelFormat::Undefined;
                return device->createPipeline(d);
            }

            RHI::RHIPipeline *createEffectPipeline(RHI::RHIDevice *device, RHI::RHIResourceLayout *layout,
                                                   RHI::PixelFormat color_fmt)
            {
                RHI::PipelineDesc d;
                d.name = "overlay-effect";
                RHI::ShaderStageDesc vs, fs;
                if (!detail::fillShaderStage("common/fullscreen.vert", device->backend(), RHI::ShaderStageFlagBits::Vertex, vs) ||
                    !detail::fillShaderStage("post/effects.frag", device->backend(), RHI::ShaderStageFlagBits::Fragment, fs))
                    return nullptr;
                d.shaders = {vs, fs};
                d.vertex_layout = detail::quadVertexLayout();
                d.rasterizer.cull_mode = RHI::CullMode::None;
                d.depth_stencil.depth_test = false;
                d.depth_stencil.depth_write = false;
                d.blend.attachments[0].blend_enable = false;
                d.resource_layout = layout->desc();
                d.color_formats[0] = color_fmt;
                d.color_format_count = 1;
                d.depth_format = RHI::PixelFormat::Undefined;
                return device->createPipeline(d);
            }
        }

        //==================================================================
        // Lifecycle
        //==================================================================
        void OverlaySystem::initialize(RHI::RHIDevice *device, const std::string &ttf_path)
        {
            device_ = device;
            createResources();
            text_.initialize(device, ttf_path, 1024);
            ready_ = quad_layout_ && quad_set_ && quad_ubo_ && quad_vb_ && quad_vb_3d_ &&
                      quad_pipeline_3d_ && text_.ready();
        }

        void OverlaySystem::shutdown()
        {
            if (!device_) return;
            text_.shutdown();
            for (auto &kv : quad_pipelines_) device_->destroyPipeline(kv.second);
            quad_pipelines_.clear();
            if (quad_pipeline_3d_) { device_->destroyPipeline(quad_pipeline_3d_); quad_pipeline_3d_ = nullptr; }
            for (auto &kv : effect_pipelines_) device_->destroyPipeline(kv.second);
            effect_pipelines_.clear();
            if (quad_vb_) { device_->destroyBuffer(quad_vb_); quad_vb_ = nullptr; }
            if (quad_vb_3d_) { device_->destroyBuffer(quad_vb_3d_); quad_vb_3d_ = nullptr; }
            if (quad_ubo_) { device_->destroyBuffer(quad_ubo_); quad_ubo_ = nullptr; }
            if (effect_ubo_) { device_->destroyBuffer(effect_ubo_); effect_ubo_ = nullptr; }
            if (quad_set_) { device_->destroyDescriptorSet(quad_set_); quad_set_ = nullptr; }
            if (effect_set_) { device_->destroyDescriptorSet(effect_set_); effect_set_ = nullptr; }
            if (quad_layout_) { device_->destroyResourceLayout(quad_layout_); quad_layout_ = nullptr; }
            if (effect_layout_) { device_->destroyResourceLayout(effect_layout_); effect_layout_ = nullptr; }
            if (quad_sampler_) { device_->destroySampler(quad_sampler_); quad_sampler_ = nullptr; }
            if (white_tex_) { device_->destroyTexture(white_tex_); white_tex_ = nullptr; }
            device_ = nullptr;
            ready_ = false;
        }

        void OverlaySystem::createResources()
        {
            // ---- quad layout (same as textured_quad shader) -------------
            RHI::ResourceLayoutDesc lq;
            lq.name = "overlay-quad-layout";
            lq.bindings = {
                {0, 0, RHI::ResourceType::UniformBuffer,
                 (uint32_t)RHI::ShaderStageFlagBits::Vertex | (uint32_t)RHI::ShaderStageFlagBits::Fragment,
                 1, "QuadParams"},
                {0, 1, RHI::ResourceType::CombinedImageSampler,
                 (uint32_t)RHI::ShaderStageFlagBits::Fragment, 1, "u_quad_texture"},
            };
            quad_layout_ = device_->createResourceLayout(lq);
            quad_set_ = device_->createDescriptorSet(quad_layout_, 0);

            RHI::BufferDesc ub;
            ub.name = "overlay-quad-ubo";
            ub.size = sizeof(QuadParamsUBO);
            ub.usage = (uint32_t)RHI::BufferUsageFlagBits::Uniform;
            ub.memory_usage = RHI::MemoryUsage::CpuToGpu;
            quad_ubo_ = device_->createBuffer(ub);
            if (!quad_ubo_) return;

            // ---- white 1x1 texture + sampler ---------------------------
            uint32_t white_pixel = 0xFFFFFFFF;
            RHI::TextureDesc wt;
            wt.type = RHI::TextureType::Tex2D;
            wt.format = RHI::PixelFormat::RGBA8_UNORM;
            wt.width = 1; wt.height = 1; wt.mip_levels = 1;
            wt.usage = (uint32_t)RHI::TextureUsageFlagBits::Sampled | (uint32_t)RHI::TextureUsageFlagBits::TransferDst;
            wt.name = "overlay-white";
            white_tex_ = device_->createTexture(wt);
            device_->writeTexture(white_tex_, &white_pixel, 4, 1, 1, 0, 0, 1);

            RHI::SamplerDesc sp;
            sp.min_filter = RHI::FilterMode::Linear;
            sp.mag_filter = RHI::FilterMode::Linear;
            sp.address_u = RHI::AddressMode::ClampToEdge;
            sp.address_v = RHI::AddressMode::ClampToEdge;
            sp.name = "overlay-quad-sampler";
            quad_sampler_ = device_->createSampler(sp);

            // Bind the white texture + the QuadParams UBO into the set.
            RHI::DescriptorImageInfo img{white_tex_, quad_sampler_, RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1};
            RHI::DescriptorBufferInfo bufinfo{quad_ubo_, 0, sizeof(QuadParamsUBO)};
            RHI::WriteDescriptorSet w[2];
            w[0] = RHI::WriteDescriptorSet{0, 0, 1, RHI::ResourceType::UniformBuffer, &bufinfo, nullptr};
            w[1] = RHI::WriteDescriptorSet{1, 0, 1, RHI::ResourceType::CombinedImageSampler, nullptr, &img};
            quad_set_->write(w, 2);

            // ---- quad vertex buffers -----------------------------------
            RHI::BufferDesc vb;
            vb.name = "overlay-quad-vb";
            vb.size = sizeof(QuadVertex) * kMaxQuads * 6;
            vb.usage = (uint32_t)RHI::BufferUsageFlagBits::Vertex | (uint32_t)RHI::BufferUsageFlagBits::TransferDst;
            vb.memory_usage = RHI::MemoryUsage::CpuToGpu;
            quad_vb_ = device_->createBuffer(vb);
            quad_vb_3d_ = device_->createBuffer(vb);

            // ---- effect layout -----------------------------------------
            RHI::ResourceLayoutDesc le;
            le.name = "overlay-effect-layout";
            le.bindings = {
                {0, 0, RHI::ResourceType::UniformBuffer,
                 (uint32_t)RHI::ShaderStageFlagBits::Vertex | (uint32_t)RHI::ShaderStageFlagBits::Fragment, 1, "PostParams"},
                {0, 1, RHI::ResourceType::CombinedImageSampler,
                 (uint32_t)RHI::ShaderStageFlagBits::Fragment, 1, "u_scene_color"},
            };
            effect_layout_ = device_->createResourceLayout(le);
            effect_set_ = device_->createDescriptorSet(effect_layout_, 0);

            RHI::BufferDesc eb;
            eb.name = "overlay-effect-ubo";
            eb.size = sizeof(PostParamsUBO);
            eb.usage = (uint32_t)RHI::BufferUsageFlagBits::Uniform;
            eb.memory_usage = RHI::MemoryUsage::CpuToGpu;
            effect_ubo_ = device_->createBuffer(eb);

            // 3D quad pipeline (HDR target + depth)
            quad_pipeline_3d_ = createQuadPipeline(device_, quad_layout_, RHI::PixelFormat::RGBA16_SFLOAT, true);
        }

        RHI::RHIPipeline *OverlaySystem::quadPipeline2D(RHI::PixelFormat fmt)
        {
            auto it = quad_pipelines_.find(fmt);
            if (it != quad_pipelines_.end()) return it->second;
            RHI::RHIPipeline *p = createQuadPipeline(device_, quad_layout_, fmt, false);
            if (p) quad_pipelines_[fmt] = p;
            return p;
        }

        RHI::RHIPipeline *OverlaySystem::effectPipeline(RHI::PixelFormat fmt)
        {
            auto it = effect_pipelines_.find(fmt);
            if (it != effect_pipelines_.end()) return it->second;
            RHI::RHIPipeline *p = createEffectPipeline(device_, effect_layout_, fmt);
            if (p) effect_pipelines_[fmt] = p;
            return p;
        }

        // Programmable post effect: tonemap + user effect branch into the
        // present target.  Called by RenderSystem when an effect is active.
        bool OverlaySystem::presentEffect(RHI::RHICommandBuffer *cmd, RHI::PixelFormat fmt,
                                           RHI::RHITexture *scene_color, RHI::RHISampler *sampler,
                                           RHI::RHIBuffer *fullscreen_vb,
                                           float exposure, float gamma, int tonemap, float saturation,
                                           bool flip_uv, float time)
        {
            if (post_effect_ == PostEffect::None) return false;
            RHI::RHIPipeline *p = effectPipeline(fmt);
            if (!p || !scene_color) return false;

            PostParamsUBO post;
            post.a = GpuVec4(exposure, gamma, (float)tonemap, flip_uv ? 1.0f : 0.0f);
            post.b = GpuVec4(0.0f, saturation, time, (float)(uint32_t)post_effect_);
            device_->writeBuffer(effect_ubo_, &post, sizeof(post), 0);

            RHI::DescriptorBufferInfo buf{effect_ubo_, 0, sizeof(PostParamsUBO)};
            RHI::DescriptorImageInfo img{scene_color, sampler, RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1};
            RHI::WriteDescriptorSet writes[2];
            writes[0] = RHI::WriteDescriptorSet{0, 0, 1, RHI::ResourceType::UniformBuffer, &buf, nullptr};
            writes[1] = RHI::WriteDescriptorSet{1, 0, 1, RHI::ResourceType::CombinedImageSampler, nullptr, &img};
            effect_set_->write(writes, 2);

            cmd->bindPipeline(p);
            cmd->bindVertexBuffer(fullscreen_vb, 0);
            RHI::RHIDescriptorSet *sets[1] = {effect_set_};
            cmd->bindDescriptorSets(p, sets, 1, nullptr, 0);
            cmd->draw(6, 0, 1, 0);
            return true;
        }

        //==================================================================
        // Command organisation
        //==================================================================
        void OverlaySystem::clearCommands()
        {
            cmds2d_.clear();
            cmds3d_.clear();
            text_.clear();
        }

        void OverlaySystem::fill2D(float x, float y, float w, float h, float r, float g, float b, float a, float rot)
        {
            Overlay2DCommand c;
            c.type = Overlay2DCommand::Fill;
            c.x0 = x; c.y0 = y; c.x1 = w; c.y1 = h;
            c.r = r; c.g = g; c.b = b; c.a = a; c.rot = rot;
            cmds2d_.push_back(c);
        }

        void OverlaySystem::line2D(float x0, float y0, float x1, float y1, float r, float g, float b, float a)
        {
            Overlay2DCommand c;
            c.type = Overlay2DCommand::Line;
            c.x0 = x0; c.y0 = y0; c.x1 = x1; c.y1 = y1;
            c.r = r; c.g = g; c.b = b; c.a = a;
            cmds2d_.push_back(c);
        }

        void OverlaySystem::quad3D(float x, float y, float z, float w, float h, float r, float g, float b, float a)
        {
            Overlay3DCommand c;
            c.type = Overlay3DCommand::Quad;
            c.x = x; c.y = y; c.z = z; c.w = w; c.h = h;
            c.r = r; c.g = g; c.b = b; c.a = a;
            cmds3d_.push_back(c);
        }

        //==================================================================
        // Command submission
        //==================================================================
        namespace
        {
            void pushQuad(std::vector<OverlaySystem::QuadVertex> &out,
                          float x0, float y0, float z0, float x1, float y1, float z1,
                          bool is2d, float screen_w, float screen_h,
                          float cr, float cg, float cb, float ca)
            {
                // Two triangles (TRIANGLE_LIST, no index buffer): 6 vertices.
                int order[6] = {0, 1, 2, 0, 2, 3};
                float lx[4] = {0, 1, 1, 0};
                float ly[4] = {0, 0, 1, 1};
                (void)z1;
                for (int i = 0; i < 6; ++i)
                {
                    int k = order[i];
                    float lpx = x0 + (x1 - x0) * lx[k];
                    float lpy = y0 + (y1 - y0) * ly[k];
                    if (is2d)
                    {
                        float ndc_x = (lpx / screen_w) * 2.0f - 1.0f;
                        float ndc_y = 1.0f - (lpy / screen_h) * 2.0f;
                        out.emplace_back(OverlaySystem::QuadVertex{{ndc_x, ndc_y, 0.0f}, {lx[k], ly[k]}, {cr, cg, cb, ca}});
                    }
                    else
                    {
                        out.emplace_back(OverlaySystem::QuadVertex{{lpx, lpy, z0}, {lx[k], ly[k]}, {cr, cg, cb, ca}});
                    }
                }
            }
        }

        void OverlaySystem::render3D(RHI::RHICommandBuffer *cmd, const Math::Mat4 &view_proj,
                                     uint32_t scene_w, uint32_t scene_h)
        {
            if (!ready_ || cmds3d_.empty()) return;
            (void)scene_w; (void)scene_h;
            std::vector<QuadVertex> verts;
            for (const auto &c : cmds3d_)
            {
                pushQuad(verts, c.x - c.w * 0.5f, c.y - c.h * 0.5f, c.z,
                         c.x + c.w * 0.5f, c.y + c.h * 0.5f, c.z,
                         false, 0, 0, c.r, c.g, c.b, c.a);
            }
            if (verts.empty()) return;

            QuadParamsUBO ubo;
            ubo.view_proj = GpuMat4::from(view_proj);
            ubo.params = GpuVec4(0.0f, 1.0f, 1.0f, 1.0f); // is_2d = 0
            device_->writeBuffer(quad_ubo_, &ubo, sizeof(ubo), 0);

            device_->writeBuffer(quad_vb_3d_, verts.data(), verts.size() * sizeof(QuadVertex), 0);
            cmd->bindPipeline(quad_pipeline_3d_);
            cmd->bindVertexBuffer(quad_vb_3d_, 0);
            RHI::RHIDescriptorSet *sets[1] = {quad_set_};
            cmd->bindDescriptorSets(quad_pipeline_3d_, sets, 1, nullptr, 0);
            cmd->draw((uint32_t)verts.size(), 0, 1, 0);

            // ---- SDF text (3D, world space, depth tested) ----
            text_.flush3D(cmd, view_proj);
        }

        void OverlaySystem::render2D(RHI::RHICommandBuffer *cmd, uint32_t width, uint32_t height,
                                     RHI::PixelFormat target_format)
        {
            if (!ready_) return;
            RHI::RHIPipeline *p = nullptr;
            if (!cmds2d_.empty())
            {
                p = quadPipeline2D(target_format);

                std::vector<QuadVertex> verts;
                for (const auto &c : cmds2d_)
                {
                    if (c.type == Overlay2DCommand::Fill)
                        pushQuad(verts, c.x0, c.y0, 0, c.x0 + c.x1, c.y0 + c.y1, 0,
                                 true, (float)width, (float)height, c.r, c.g, c.b, c.a);
                    else
                    {
                        // thicken lines into a 2px quad for the quad pipeline
                        float th = 2.0f;
                        pushQuad(verts, c.x0, c.y0 - th * 0.5f, 0, c.x1, c.y1 + th * 0.5f, 0,
                                 true, (float)width, (float)height, c.r, c.g, c.b, c.a);
                    }
                }
                if (!verts.empty() && p)
                {
                    QuadParamsUBO ubo;
                    ubo.view_proj = GpuMat4::identity();
                    ubo.params = GpuVec4(1.0f, 1.0f, 1.0f, 1.0f);
                    device_->writeBuffer(quad_ubo_, &ubo, sizeof(ubo), 0);
                    device_->writeBuffer(quad_vb_, verts.data(), verts.size() * sizeof(QuadVertex), 0);
                    cmd->bindPipeline(p);
                    cmd->bindVertexBuffer(quad_vb_, 0);
                    RHI::RHIDescriptorSet *sets[1] = {quad_set_};
                    cmd->bindDescriptorSets(p, sets, 1, nullptr, 0);
                    cmd->draw((uint32_t)verts.size(), 0, 1, 0);
                }
            }

            // ---- SDF text (2D) ----
            text_.flush2D(cmd, width, height, target_format);
        }

    } // namespace Render
} // namespace Genesis
