/*
 * Genesis Render - signed distance field font implementation.
 */
#include "text/font.hpp"
#include "foundation_layer/genesis_log.hpp"
#include "resource/resource_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

// stb_truetype is vendored inside Dear ImGui (imstb_truetype.h).  A private
// static copy keeps the rasteriser internal to this translation unit so it
// does not collide with the symbols imgui_draw.cpp emits.
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imgui/imstb_truetype.h"
#undef STB_TRUETYPE_IMPLEMENTATION
#undef STBTT_STATIC

namespace Genesis
{
    namespace Text
    {
        namespace
        {
            // 1-D squared Euclidean distance transform (Felzenszwalb &
            // Huttenlocher).  f holds 0 at feature pixels and "infinity"
            // elsewhere; d receives the squared distance to the nearest one.
            void edt1d(const float *f, float *d, int n, std::vector<int> &v, std::vector<float> &z)
            {
                int k = 0;
                v[0] = 0;
                z[0] = -1e20f;
                z[1] = 1e20f;
                for (int q = 1; q < n; ++q)
                {
                    float s = ((f[q] + (float)q * q) - (f[v[k]] + (float)v[k] * v[k])) / (2.0f * q - 2.0f * v[k]);
                    while (s <= z[k])
                    {
                        --k;
                        s = ((f[q] + (float)q * q) - (f[v[k]] + (float)v[k] * v[k])) / (2.0f * q - 2.0f * v[k]);
                    }
                    ++k;
                    v[k] = q;
                    z[k] = s;
                    z[k + 1] = 1e20f;
                }
                k = 0;
                for (int q = 0; q < n; ++q)
                {
                    while (z[k + 1] < (float)q) ++k;
                    float dq = (float)(q - v[k]);
                    d[q] = dq * dq + f[v[k]];
                }
            }

            // 2-D squared distance transform over a W x H grid.
            void edt2d(std::vector<float> &grid, int W, int H)
            {
                const int n = std::max(W, H);
                std::vector<float> f(n), d(n), z(n + 1);
                std::vector<int> v(n);
                for (int x = 0; x < W; ++x)
                {
                    for (int y = 0; y < H; ++y) f[y] = grid[(size_t)y * W + x];
                    edt1d(f.data(), d.data(), H, v, z);
                    for (int y = 0; y < H; ++y) grid[(size_t)y * W + x] = d[y];
                }
                for (int y = 0; y < H; ++y)
                {
                    for (int x = 0; x < W; ++x) f[x] = grid[(size_t)y * W + x];
                    edt1d(f.data(), d.data(), W, v, z);
                    for (int x = 0; x < W; ++x) grid[(size_t)y * W + x] = d[x];
                }
            }

            // Code points baked into the atlas: printable ASCII + Latin-1.
            std::vector<uint32_t> bakedCodepoints()
            {
                std::vector<uint32_t> cps;
                for (uint32_t c = 32; c <= 126; ++c) cps.push_back(c);
                for (uint32_t c = 160; c <= 255; ++c) cps.push_back(c);
                cps.push_back(0x2022); // bullet
                cps.push_back(0x2026); // ellipsis
                cps.push_back(0x00B0); // degree (already in Latin-1, harmless)
                cps.push_back(0xFFFD); // replacement
                return cps;
            }
        } // namespace

        std::vector<uint32_t> decodeUtf8(const std::string &text)
        {
            std::vector<uint32_t> out;
            out.reserve(text.size());
            const unsigned char *s = (const unsigned char *)text.data();
            size_t i = 0, n = text.size();
            while (i < n)
            {
                uint32_t c = s[i];
                int extra = 0;
                if (c < 0x80) extra = 0;
                else if ((c & 0xE0) == 0xC0) { c &= 0x1F; extra = 1; }
                else if ((c & 0xF0) == 0xE0) { c &= 0x0F; extra = 2; }
                else if ((c & 0xF8) == 0xF0) { c &= 0x07; extra = 3; }
                else { out.push_back(0xFFFD); ++i; continue; }
                bool ok = true;
                for (int k = 1; k <= extra; ++k)
                {
                    if (i + k >= n || (s[i + k] & 0xC0) != 0x80) { ok = false; break; }
                    c = (c << 6) | (s[i + k] & 0x3F);
                }
                if (!ok)
                {
                    out.push_back(0xFFFD);
                    ++i;
                    continue;
                }
                out.push_back(c);
                i += (size_t)extra + 1;
            }
            return out;
        }

        bool Font::load(RHI::RHIDevice *device, const std::string &ttf_path, uint32_t atlas_size, float glyph_pixels)
        {
            release();
            device_ = device;
            path_ = ttf_path;
            glyph_pixels_ = glyph_pixels;
            spread_px_ = std::max(4.0f, glyph_pixels * 0.14f);

            std::vector<uint8_t> ttf;
            if (!ResourceIO::readFile(ttf_path, ttf) || ttf.empty())
            {
                GEN_LOG_ERROR("[Font] cannot read '%s'", ttf_path.c_str());
                return false;
            }
            stbtt_fontinfo font;
            if (!stbtt_InitFont(&font, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0)))
            {
                GEN_LOG_ERROR("[Font] '%s' is not a valid TrueType font", ttf_path.c_str());
                return false;
            }
            const float scale = stbtt_ScaleForMappingEmToPixels(&font, glyph_pixels);
            int ascent = 0, descent = 0, line_gap = 0;
            stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
            ascent_ = ascent * scale;
            descent_ = descent * scale;
            line_gap_ = line_gap * scale;

            const int W = (int)atlas_size, H = (int)atlas_size;
            const int margin = (int)std::ceil(spread_px_) + 1;
            std::vector<uint8_t> atlas((size_t)W * H, 0);
            int pen_x = 1, pen_y = 1, row_h = 0;

            for (uint32_t cp : bakedCodepoints())
            {
                int index = stbtt_FindGlyphIndex(&font, (int)cp);
                if (index == 0 && cp != 0xFFFD && cp != 32) continue;
                Glyph g;
                int adv = 0, lsb = 0;
                stbtt_GetGlyphHMetrics(&font, index, &adv, &lsb);
                g.advance = adv * scale;

                int x0, y0, x1, y1;
                stbtt_GetGlyphBitmapBox(&font, index, scale, scale, &x0, &y0, &x1, &y1);
                const int gw = x1 - x0, gh = y1 - y0;
                if (gw > 0 && gh > 0)
                {
                    const int cw = gw + margin * 2, ch = gh + margin * 2;
                    if (pen_x + cw >= W)
                    {
                        pen_x = 1;
                        pen_y += row_h + 1;
                        row_h = 0;
                    }
                    if (pen_y + ch >= H)
                    {
                        GEN_LOG_WARN("[Font] atlas %ux%u full - remaining glyphs skipped", atlas_size, atlas_size);
                        break;
                    }
                    std::vector<uint8_t> cov((size_t)gw * gh, 0);
                    stbtt_MakeGlyphBitmap(&font, cov.data(), gw, gh, gw, scale, scale, index);

                    // Signed distance of every cell of the padded glyph box:
                    // distance to the nearest inside pixel minus distance to
                    // the nearest outside pixel, using the anti-aliased
                    // coverage to place the edge at sub-pixel precision.
                    std::vector<float> inside((size_t)cw * ch), outside((size_t)cw * ch);
                    for (int y = 0; y < ch; ++y)
                        for (int x = 0; x < cw; ++x)
                        {
                            int gx = x - margin, gy = y - margin;
                            float c = (gx >= 0 && gy >= 0 && gx < gw && gy < gh) ? cov[(size_t)gy * gw + gx] / 255.0f : 0.0f;
                            size_t i = (size_t)y * cw + x;
                            inside[i] = c >= 0.5f ? 0.0f : 1e20f;
                            outside[i] = c < 0.5f ? 0.0f : 1e20f;
                        }
                    edt2d(inside, cw, ch);
                    edt2d(outside, cw, ch);
                    for (int y = 0; y < ch; ++y)
                        for (int x = 0; x < cw; ++x)
                        {
                            size_t i = (size_t)y * cw + x;
                            // Distances are measured between pixel centres;
                            // the outline lies halfway between an inside and
                            // an outside pixel.
                            float dist = std::sqrt(outside[i]) - std::sqrt(inside[i]);
                            dist += dist > 0.0f ? -0.5f : 0.5f;
                            float v = 0.5f + dist / (2.0f * spread_px_);
                            atlas[(size_t)(pen_y + y) * W + (pen_x + x)] = (uint8_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f);
                        }

                    g.has_bitmap = true;
                    g.u0 = (float)pen_x / W;
                    g.v0 = (float)pen_y / H;
                    g.u1 = (float)(pen_x + cw) / W;
                    g.v1 = (float)(pen_y + ch) / H;
                    g.left = (float)(x0 - margin);
                    g.top = (float)(-y0 + margin); // stb y0 is negative above the baseline
                    g.width = (float)cw;
                    g.height = (float)ch;
                    pen_x += cw + 1;
                    row_h = std::max(row_h, ch);
                }
                glyphs_[cp] = g;
            }

            RHI::TextureDesc td;
            td.type = RHI::TextureType::Tex2D;
            td.format = RHI::PixelFormat::R8_UNORM;
            td.width = atlas_size;
            td.height = atlas_size;
            td.mip_levels = 1;
            td.usage = (uint32_t)RHI::TextureUsageFlagBits::Sampled | (uint32_t)RHI::TextureUsageFlagBits::TransferDst;
            td.name = "font.sdf_atlas";
            atlas_ = device_->createTexture(td);
            if (!atlas_) return false;
            device_->writeTexture(atlas_, atlas.data(), atlas.size(), atlas_size, atlas_size, 0, 0, 1);

            RHI::SamplerDesc sd;
            sd.min_filter = sd.mag_filter = RHI::FilterMode::Linear;
            sd.mipmap_mode = RHI::MipmapMode::Nearest;
            sd.address_u = sd.address_v = sd.address_w = RHI::AddressMode::ClampToEdge;
            sd.max_lod = 0.0f;
            sd.name = "font.sampler";
            sampler_ = device_->createSampler(sd);
            GEN_LOG_INFO("[Font] baked %zu SDF glyphs from %s (%ux%u atlas, %.0f px, spread %.1f px)", glyphs_.size(),
                         ResourceIO::pathFilename(ttf_path).c_str(), atlas_size, atlas_size, glyph_pixels, spread_px_);
            return sampler_ != nullptr;
        }

        void Font::release()
        {
            if (!device_) return;
            if (atlas_) device_->destroyTexture(atlas_);
            if (sampler_) device_->destroySampler(sampler_);
            atlas_ = nullptr;
            sampler_ = nullptr;
            glyphs_.clear();
            device_ = nullptr;
        }

        const Font::Glyph *Font::find(uint32_t cp) const
        {
            auto it = glyphs_.find(cp);
            if (it != glyphs_.end()) return &it->second;
            it = glyphs_.find(0xFFFD);
            if (it != glyphs_.end()) return &it->second;
            it = glyphs_.find('?');
            return it != glyphs_.end() ? &it->second : nullptr;
        }

        TextLayout Font::layout(const std::string &utf8, float size, TextAlign align, TextAnchor anchor, bool y_down,
                                float line_spacing) const
        {
            TextLayout out;
            if (glyphs_.empty() || size <= 0.0f) return out;
            const float s = size / glyph_pixels_;
            const float line_h = (ascent_ - descent_ + line_gap_) * line_spacing * s;
            const float ascent = ascent_ * s;

            // Split into lines first so each line can be aligned.
            std::vector<std::vector<uint32_t>> lines(1);
            for (uint32_t cp : decodeUtf8(utf8))
            {
                if (cp == '\n') lines.emplace_back();
                else if (cp != '\r') lines.back().push_back(cp);
            }
            std::vector<float> widths;
            float max_w = 0.0f;
            for (const auto &line : lines)
            {
                float w = 0.0f;
                for (uint32_t cp : line)
                    if (const Glyph *g = find(cp)) w += g->advance * s;
                widths.push_back(w);
                max_w = std::max(max_w, w);
            }
            const float block_h = ascent + line_h * (float)(lines.size() - 1) - descent_ * s;
            out.size = Math::Vec2(max_w, block_h);
            out.lines = (uint32_t)lines.size();

            // Baseline of the first line relative to the origin, measured
            // downwards (converted for y-up at emission).
            float first_baseline = ascent;
            if (anchor == TextAnchor::Baseline) first_baseline = 0.0f;
            else if (anchor == TextAnchor::Middle) first_baseline = ascent - block_h * 0.5f;

            for (size_t li = 0; li < lines.size(); ++li)
            {
                float pen = 0.0f;
                if (align == TextAlign::Center) pen = -widths[li] * 0.5f;
                else if (align == TextAlign::Right) pen = -widths[li];
                const float baseline = first_baseline + line_h * (float)li;
                for (uint32_t cp : lines[li])
                {
                    const Glyph *g = find(cp);
                    if (!g) continue;
                    if (g->has_bitmap)
                    {
                        GlyphQuad q;
                        const float x0 = pen + g->left * s;
                        const float x1 = x0 + g->width * s;
                        const float top_down = baseline - g->top * s; // top edge, y down
                        const float bottom_down = top_down + g->height * s;
                        q.x0 = x0;
                        q.x1 = x1;
                        q.u0 = g->u0;
                        q.u1 = g->u1;
                        q.v0 = g->v0;
                        q.v1 = g->v1;
                        if (y_down)
                        {
                            q.y0 = top_down;
                            q.y1 = bottom_down;
                        }
                        else
                        {
                            q.y0 = -top_down;
                            q.y1 = -bottom_down;
                        }
                        out.quads.push_back(q);
                    }
                    pen += g->advance * s;
                }
            }
            return out;
        }

        Math::Vec2 Font::measure(const std::string &utf8, float size) const
        {
            return layout(utf8, size, TextAlign::Left, TextAnchor::Top, true).size;
        }

    } // namespace Text
} // namespace Genesis
