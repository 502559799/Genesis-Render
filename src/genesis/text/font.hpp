/*
 * Genesis Render - signed distance field fonts.
 *
 * A Font bakes a TrueType face into a single-channel signed distance atlas
 * (stb_truetype coverage -> exact Euclidean distance transform on the CPU).
 * The distance is stored remapped to [0,1] with 0.5 on the outline, so one
 * atlas renders crisp text at any size, in screen space (2D) and in world
 * space (3D), with outlines produced by moving the threshold in the shader
 * (shaders/ui/batch.frag).
 *
 * Layout produces glyph quads in "font units": positions scaled so that the
 * em size equals `size`, with the pen starting at the origin.  Both y
 * directions are supported (screen space grows down, world space grows up).
 */
#ifndef __GENESIS_TEXT_FONT_HPP__
#define __GENESIS_TEXT_FONT_HPP__

#include "foundation_layer/math/math.hpp"
#include "rhi/rhi.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Genesis
{
    namespace Text
    {
        enum class TextAlign : uint32_t
        {
            Left = 0,
            Center = 1,
            Right = 2,
        };

        enum class TextAnchor : uint32_t
        {
            Top = 0,      // origin at the top of the first line (2D default)
            Middle = 1,   // origin at the vertical centre of the text block (3D default)
            Baseline = 2, // origin on the baseline of the first line
        };

        struct GlyphQuad
        {
            float x0, y0, x1, y1; // quad corners in layout space
            float u0, v0, u1, v1; // atlas uvs (v0 at y0)
        };

        struct TextLayout
        {
            std::vector<GlyphQuad> quads;
            Math::Vec2 size = Math::Vec2(0.0f); // block width / height
            uint32_t lines = 0;
        };

        class Font
        {
        public:
            Font() = default;
            ~Font() { release(); }
            Font(const Font &) = delete;
            Font &operator=(const Font &) = delete;

            bool load(RHI::RHIDevice *device, const std::string &ttf_path, uint32_t atlas_size = 1024,
                      float glyph_pixels = 56.0f);
            void release();
            bool ready() const { return atlas_ != nullptr; }

            RHI::RHITexture *atlas() const { return atlas_; }
            RHI::RHISampler *sampler() const { return sampler_; }
            const std::string &path() const { return path_; }
            uint32_t glyphCount() const { return (uint32_t)glyphs_.size(); }
            // Width of the distance ramp in SDF units per font unit (used to
            // convert outline widths given in em fractions).
            float sdfSpreadEm() const { return spread_px_ / glyph_pixels_; }

            // `y_down`: true for screen space (2D), false for world space.
            TextLayout layout(const std::string &utf8, float size, TextAlign align, TextAnchor anchor, bool y_down,
                              float line_spacing = 1.2f) const;
            Math::Vec2 measure(const std::string &utf8, float size) const;

        private:
            struct Glyph
            {
                float u0 = 0, v0 = 0, u1 = 0, v1 = 0;   // atlas uvs (v0 = top of the bitmap)
                float left = 0, top = 0;                // bitmap offset from the pen (px, y up)
                float width = 0, height = 0;            // bitmap size including the SDF margin (px)
                float advance = 0;                      // px
                bool has_bitmap = false;
            };
            const Glyph *find(uint32_t codepoint) const;

            RHI::RHIDevice *device_ = nullptr;
            RHI::RHITexture *atlas_ = nullptr;
            RHI::RHISampler *sampler_ = nullptr;
            std::unordered_map<uint32_t, Glyph> glyphs_;
            std::string path_;
            float glyph_pixels_ = 56.0f;
            float spread_px_ = 8.0f;
            float ascent_ = 0.0f, descent_ = 0.0f, line_gap_ = 0.0f;
        };

        // Decodes UTF-8 into code points (invalid bytes become U+FFFD).
        std::vector<uint32_t> decodeUtf8(const std::string &text);

    } // namespace Text
} // namespace Genesis

#endif /* __GENESIS_TEXT_FONT_HPP__ */
