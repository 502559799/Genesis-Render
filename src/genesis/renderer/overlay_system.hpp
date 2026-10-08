/*
 * Genesis Render - overlay / command buffer feature system.
 *
 * This is the engine's programmable rendering surface beyond the built-in
 * ECS-driven scene.  It supports three of the requested capabilities in one
 * place, all reachable from Lua:
 *
 *   1. Free command list: the user (a script) records arbitrary draw commands
 *      in one place (the "organise" step) and they are submitted during the
 *      next frame's render passes (the "execute" step).  This naturally
 *      separates logic from rendering and lets draws be ordered freely,
 *      rather than being tied to the fixed ECS Renderer + Camera flow.
 *
 *   2. Programmable pipeline: two fullscreen custom effects (invert / scanline)
 *      can be layered after tone mapping, demonstrating a user-controlled
 *      post-processing pass.  The overlay quad pipeline is also its own
 *      user-created pipeline.
 *
 *   3. SDF text in both 2D (screen space) and 3D (world space).
 *
 * The system is backend-agnostic (only uses the RHI).
 */
#ifndef __GENESIS_OVERLAY_SYSTEM_HPP__
#define __GENESIS_OVERLAY_SYSTEM_HPP__

#include "foundation_layer/math/math.hpp"
#include "rhi/rhi.hpp"
#include "text/text_system.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Genesis
{
    namespace Render
    {
        enum class PostEffect : uint32_t
        {
            None = 0,
            Invert = 1,
            Scanline = 2,
        };

        // A 2D screen-space draw command (coordinates in pixels, origin
        // top-left, y down).
        struct Overlay2DCommand
        {
            enum Type { Fill, Line } type = Fill;
            float x0 = 0, y0 = 0, x1 = 0, y1 = 0; // fill: rect; line: endpoints
            float r = 1, g = 1, b = 1, a = 1;
            float rot = 0;
        };

        // A 3D world-space draw command (quad in the XY plane at depth z,
        // world units).
        struct Overlay3DCommand
        {
            enum Type { Quad } type = Quad;
            float x = 0, y = 0, z = 0; // anchor (center)
            float w = 0.1f, h = 0.1f;
            float r = 1, g = 1, b = 1, a = 1;
        };

        class OverlaySystem
        {
        public:
            OverlaySystem() = default;
            ~OverlaySystem() { shutdown(); }

            void initialize(RHI::RHIDevice *device, const std::string &ttf_path);
            void shutdown();
            bool ready() const { return ready_; }

            // ---- command organisation (called from logic/scripts) -----
            void clearCommands();
            void fill2D(float x, float y, float w, float h, float r, float g, float b, float a, float rot = 0.0f);
            void line2D(float x0, float y0, float x1, float y1, float r, float g, float b, float a);
            void quad3D(float x, float y, float z, float w, float h, float r, float g, float b, float a);

            void postEffect(PostEffect e) { post_effect_ = e; }
            PostEffect postEffect() const { return post_effect_; }

            Text::TextSystem &text() { return text_; }

            // One quad vertex (position vec3 + uv vec2 + color vec4).
            struct QuadVertex
            {
                float position[3];
                float uv[2];
                float color[4];
            };
            static_assert(sizeof(QuadVertex) == 36, "QuadVertex: 12+8+16");
            static constexpr uint32_t kMaxQuads = 4096;

            // ---- command submission (called by RenderSystem) ------------
            void render3D(RHI::RHICommandBuffer *cmd, const Math::Mat4 &view_proj,
                          uint32_t scene_w, uint32_t scene_h);
            // `target_format` is the colour format of the present/capture
            // render pass the 2D overlay + text are drawn into.
            void render2D(RHI::RHICommandBuffer *cmd, uint32_t width, uint32_t height, RHI::PixelFormat target_format);

            // Programmable post effect: draws the fullscreen effect pass
            // (tonemap + user effect) into the present target when an effect
            // is active.  Returns false when no effect is set (caller should
            // use its own tonemap pipeline instead).
            bool presentEffect(RHI::RHICommandBuffer *cmd, RHI::PixelFormat fmt,
                               RHI::RHITexture *scene_color, RHI::RHISampler *sampler,
                               RHI::RHIBuffer *fullscreen_quad_vb,
                               float exposure, float gamma, int tonemap, float saturation,
                               bool flip_uv, float time);

        private:
            void createResources();
            RHI::RHIPipeline *quadPipeline2D(RHI::PixelFormat fmt);
            RHI::RHIPipeline *effectPipeline(RHI::PixelFormat fmt);

            RHI::RHIDevice *device_ = nullptr;
            Text::TextSystem text_;
            bool ready_ = false;

            // Overlay quad pipeline resources (per present format)
            RHI::RHIResourceLayout *quad_layout_ = nullptr;
            RHI::RHIDescriptorSet *quad_set_ = nullptr;
            RHI::RHIBuffer *quad_ubo_ = nullptr;
            RHI::RHIBuffer *quad_vb_ = nullptr;
            RHI::RHIBuffer *quad_vb_3d_ = nullptr;
            RHI::RHITexture *white_tex_ = nullptr;
            RHI::RHISampler *quad_sampler_ = nullptr;
            std::unordered_map<RHI::PixelFormat, RHI::RHIPipeline *> quad_pipelines_;
            RHI::RHIPipeline *quad_pipeline_3d_ = nullptr; // HDR + depth

            // Custom post effect pipelines (fullscreen, per present format)
            std::unordered_map<RHI::PixelFormat, RHI::RHIPipeline *> effect_pipelines_;
            RHI::RHIResourceLayout *effect_layout_ = nullptr;
            RHI::RHIDescriptorSet *effect_set_ = nullptr;
            RHI::RHIBuffer *effect_ubo_ = nullptr;

            // Recorded command list
            std::vector<Overlay2DCommand> cmds2d_;
            std::vector<Overlay3DCommand> cmds3d_;
            PostEffect post_effect_ = PostEffect::None;
        };

    } // namespace Render
} // namespace Genesis

#endif /* __GENESIS_OVERLAY_SYSTEM_HPP__ */
