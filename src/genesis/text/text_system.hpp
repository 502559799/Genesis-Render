/*
 * Genesis Render - signed-distance-field (SDF) text rendering.
 *
 * Renders text both in screen space (2D, pixel-anchored) and in world
 * space (3D, placed with the scene transform) from a single signed
 * distance glyph atlas.  A true SDF is baked from the font at load time
 * (alpha coverage -> 16-bit unsigned distance via a Felzenszwalb/Huttenlocher
 * distance transform) so edges stay crisp at any size and any camera
 * distance, and cheaply anti-aliased in the fragment shader.
 *
 * The system builds vertex data for the glyph quads and flushes them at
 * two points of the frame:
 *   - flush3D()  during the scene pass (depth tested, billboarded),
 *   - flush2D()  during the present pass (screen space, after tone mapping).
 *
 * Backend agnostic: it only uses the RHI, so it works on both OpenGL and
 * Vulkan.  Glyph coverage is produced on the CPU (stb_truetype, vendored
 * through Dear ImGui) and the signed distance is computed there as well.
 */
#ifndef __GENESIS_TEXT_SYSTEM_HPP__
#define __GENESIS_TEXT_SYSTEM_HPP__

#include "foundation_layer/math/math.hpp"
#include "renderer/render_ubo.hpp"
#include "rhi/rhi.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Genesis
{
    namespace Text
    {
        // One quad vertex.  All data is per-quad (expanded on the CPU) so no
        // instancing/push-constant gymnastics are needed.
        struct GlyphVertex
        {
            float position[3]; // 2D: clip NDC | 3D: world corner
            float uv[2];
            float color[4];
            float params[4];   // x = unused, y = sdf edge softness (AA), z/w unused
        };
        static_assert(sizeof(GlyphVertex) == 52, "GlyphVertex: 12+8+16+16");

        // GPU UBO (mirrors the GLSL TextParams block in text_glyph.vert).
        struct TextParamsUBO
        {
            ::Genesis::Render::GpuMat4 view_proj;
            ::Genesis::Render::GpuVec4 mode; // x = is_2d (1.0 => clip passthrough), y/z/w unused
        };
        static_assert(sizeof(TextParamsUBO) == 80, "TextParamsUBO: 64 + 16");

        struct TextSystem
        {
            TextSystem() = default;
            ~TextSystem() { shutdown(); }

            bool initialize(RHI::RHIDevice *device, const std::string &ttf_path, uint32_t atlas_size = 512);
            void shutdown();

            // ---- per frame: queue text, then flush ----------------------
            void clear(); // clears the queue (call at frame start)

            // Screen space, top-left anchor in pixels, size in pixels.
            void drawText2D(float x, float y, float size_pixels, const std::string &text,
                           const Math::Color &color, float screen_w, float screen_h);

            // World space, billboarded from the given transform (origin =
            // anchor, x axis = right, y axis = up), size in world units.
            void drawText3D(const Math::Mat4 &world, const std::string &text,
                            const Math::Color &color, float size_world = 0.15f);

            // ---- flush at the correct pass of the frame ----------------
            // `target_format` is the colour format of the render pass the text
            // is drawn into (present swapchain format, or the capture target).
            void flush2D(RHI::RHICommandBuffer *cmd, uint32_t width, uint32_t height, RHI::PixelFormat target_format);
            void flush3D(RHI::RHICommandBuffer *cmd, const Math::Mat4 &view_proj);

            bool ready() const { return ready_; }
            size_t glyphCount() const { return glyphs_loaded_; }

        private:
            RHI::RHIPipeline *pipeline2D(RHI::PixelFormat color_fmt);
            std::unordered_map<RHI::PixelFormat, RHI::RHIPipeline *> pipeline2d_;
            struct Glyph
            {
                float u0 = 0, v0 = 0, u1 = 0, v1 = 0; // atlas uv
                float advance = 0.0f;                 // horizontal advance in atlas px
                float bearing_x = 0.0f;               // pen->origin offset
                float bearing_y = 0.0f;               // baseline ascent offset
                float glyph_w = 0.0f, glyph_h = 0.0f; // glyph box in atlas px
            };

            struct TextRequest
            {
                std::string text;
                Math::Color color;
                bool is_2d = false;
                float x_px = 0, y_px = 0, size_px = 0, screen_w = 1, screen_h = 1;
                Math::Mat4 world = Math::Mat4::identity();
                float size_world = 0.15f;
            };

            void buildGlyphAtlas(const std::vector<uint8_t> &ttf);
            bool createPipeline();
            void buildVertices(std::vector<GlyphVertex> &verts, bool is_2d);
            void uploadAndDraw(RHI::RHICommandBuffer *cmd, const std::vector<GlyphVertex> &verts,
                               RHI::RHIPipeline *pipeline, bool is2d);

            RHI::RHIDevice *device_ = nullptr;
            RHI::RHIPipeline *pipeline_3d_ = nullptr;
            RHI::RHIResourceLayout *layout_ = nullptr;
            RHI::RHIDescriptorSet *set_ = nullptr;
            RHI::RHIBuffer *ubo_ = nullptr;
            RHI::RHIBuffer *vertex_buffer_ = nullptr;
            RHI::RHIBuffer *index_buffer_ = nullptr;
            RHI::RHIBuffer *vertex_buffer_3d_ = nullptr;
            RHI::RHITexture *atlas_ = nullptr;
            RHI::RHISampler *sampler_ = nullptr;

            Glyph glyphs_[128];
            float ascent_ = 0.0f;
            uint32_t atlas_size_ = 512;
            uint32_t glyphs_loaded_ = 0;
            bool ready_ = false;

            std::vector<TextRequest> queue_;
        };

    } // namespace Text
} // namespace Genesis

#endif /* __GENESIS_TEXT_SYSTEM_HPP__ */
