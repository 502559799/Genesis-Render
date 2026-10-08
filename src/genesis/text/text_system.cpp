/*
 * Genesis Render - SDF text system implementation.
 */
#include "text/text_system.hpp"
#include "foundation_layer/genesis_log.hpp"
#include "foundation_layer/math/math.hpp"
#include "resource/resource_types.hpp"
#include "renderer/render_internal.hpp"

#include <cmath>
#include <cstring>
#include <vector>

// stb_truetype is vendored inside Dear ImGui (imstb_truetype.h).  We compile
// our own static copy (STBTT_STATIC) so the font-rasterization code stays
// internal to this translation unit and does not collide with the global
// symbols imgui_draw.cpp also emits.
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
            // 1-D Felzenszwalb-Huttenlocher distance transform: for each q
            // finds the closest position j where fin[j]==0 (a feature),
            // returning sqrt of the squared distance.
            void edt1d(int n, const float *fin, float *fout)
            {
                std::vector<int> v(n);
                std::vector<float> z(n + 1);
                int k = 0;
                v[0] = 0;
                z[0] = -1e30f;
                z[1] = 1e30f;
                for (int q = 1; q < n; ++q)
                {
                    float s;
                    for (;;)
                    {
                        s = ((fin[q] + (float)q * q) - (fin[v[k]] + (float)v[k] * v[k])) /
                            (2.0f * (float)q - 2.0f * (float)v[k]);
                        if (k > 0 && s <= z[k]) --k;
                        else break;
                    }
                    ++k;
                    v[k] = q;
                    z[k] = s;
                    z[k + 1] = 1e30f;
                }
                k = 0;
                for (int q = 0; q < n; ++q)
                {
                    while (z[k + 1] < (float)q) ++k;
                    float val = fin[v[k]] + (float)(q - v[k]) * (q - v[k]);
                    fout[q] = std::sqrt(std::max(0.0f, val));
                }
            }

            // Euclidean distance transform over a binary grid (feature = 0).
            void distanceTransform(int W, int H, const std::vector<float> &feat, std::vector<float> &dist)
            {
                std::vector<float> col(W * H), row(W * H);
                for (int x = 0; x < W; ++x)
                {
                    std::vector<float> f(H);
                    for (int y = 0; y < H; ++y) f[y] = feat[y * W + x];
                    edt1d(H, f.data(), f.data());
                    for (int y = 0; y < H; ++y) col[y * W + x] = f[y];
                }
                for (int y = 0; y < H; ++y)
                {
                    edt1d(W, &col[y * W], &row[y * W]);
                }
                dist.swap(row);
            }

            constexpr uint8_t kAsciiFirst = 32;
            constexpr uint8_t kAsciiCount = 95; // 32..126
        }

        //==================================================================
        // Lifecycle
        //==================================================================
        bool TextSystem::initialize(RHI::RHIDevice *device, const std::string &ttf_path, uint32_t atlas_size)
        {
            device_ = device;
            atlas_size_ = atlas_size;

            std::vector<uint8_t> font_data;
            if (!ResourceIO::readFile(ttf_path, font_data) || font_data.empty())
            {
                GEN_LOG_ERROR("[Text] cannot read font '%s'", ttf_path.c_str());
                return false;
            }
            buildGlyphAtlas(font_data);
            if (!createPipeline()) return false;

            // Dynamic vertex buffer for the quads (grown as needed).
            RHI::BufferDesc vb_desc;
            vb_desc.name = "text-vertices";
            vb_desc.size = 1 << 16;
            vb_desc.usage = (uint32_t)RHI::BufferUsageFlagBits::Vertex | (uint32_t)RHI::BufferUsageFlagBits::TransferDst;
            vb_desc.memory_usage = RHI::MemoryUsage::CpuToGpu;
            vertex_buffer_ = device->createBuffer(vb_desc);
            vertex_buffer_3d_ = device->createBuffer(vb_desc);
            if (!vertex_buffer_ || !vertex_buffer_3d_) return false;

            // Index buffer: quads are 4 verts -> 6 indices.
            std::vector<uint16_t> indices;
            // 8k quads worth; grows if a single call needs more (we rebuild).
            indices.resize(8192 * 6);
            for (int i = 0; i < 8192; ++i)
            {
                uint32_t b = (uint32_t)i * 4;
                indices[i * 6 + 0] = (uint16_t)(b + 0);
                indices[i * 6 + 1] = (uint16_t)(b + 1);
                indices[i * 6 + 2] = (uint16_t)(b + 2);
                indices[i * 6 + 3] = (uint16_t)(b + 0);
                indices[i * 6 + 4] = (uint16_t)(b + 2);
                indices[i * 6 + 5] = (uint16_t)(b + 3);
            }
            RHI::BufferDesc ib_desc;
            ib_desc.name = "text-indices";
            ib_desc.size = indices.size() * sizeof(uint16_t);
            ib_desc.usage = (uint32_t)RHI::BufferUsageFlagBits::Index;
            ib_desc.memory_usage = RHI::MemoryUsage::GpuOnly;
            ib_desc.initial_data = indices.data();
            ib_desc.initial_data_size = indices.size() * sizeof(uint16_t);
            index_buffer_ = device->createBuffer(ib_desc);

            ready_ = vertex_buffer_ && vertex_buffer_3d_ && index_buffer_ && pipeline_3d_;
            return ready_;
        }

        void TextSystem::shutdown()
        {
            if (!device_) return;
            if (vertex_buffer_) { device_->destroyBuffer(vertex_buffer_); vertex_buffer_ = nullptr; }
            if (vertex_buffer_3d_) { device_->destroyBuffer(vertex_buffer_3d_); vertex_buffer_3d_ = nullptr; }
            if (index_buffer_) { device_->destroyBuffer(index_buffer_); index_buffer_ = nullptr; }
            if (ubo_) { device_->destroyBuffer(ubo_); ubo_ = nullptr; }
            if (set_) { device_->destroyDescriptorSet(set_); set_ = nullptr; }
            for (auto &kv : pipeline2d_) { if (kv.second) device_->destroyPipeline(kv.second); }
            pipeline2d_.clear();
            if (pipeline_3d_) { device_->destroyPipeline(pipeline_3d_); pipeline_3d_ = nullptr; }
            if (layout_) { device_->destroyResourceLayout(layout_); layout_ = nullptr; }
            if (sampler_) { device_->destroySampler(sampler_); sampler_ = nullptr; }
            if (atlas_) { device_->destroyTexture(atlas_); atlas_ = nullptr; }
            device_ = nullptr;
            ready_ = false;
            glyphs_loaded_ = 0;
        }

        void TextSystem::clear() { queue_.clear(); }

        //==================================================================
        // Glyph atlas (coverage -> signed distance)
        //==================================================================
        void TextSystem::buildGlyphAtlas(const std::vector<uint8_t> &ttf)
        {
            const uint32_t W = atlas_size_;
            const uint32_t H = atlas_size_;
            const float font_pixels = 48.0f; // SDF detail size in the atlas

            stbtt_fontinfo font;
            if (!stbtt_InitFont(&font, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0)))
            {
                GEN_LOG_ERROR("[Text] stbtt_InitFont failed");
                return;
            }
            float scale = stbtt_ScaleForMappingEmToPixels(&font, font_pixels);

            float ascent = 0, descent = 0, line_gap = 0;
            stbtt_GetScaledFontVMetrics(ttf.data(), 0, font_pixels, &ascent, &descent, &line_gap);
            ascent_ = ascent;

            // Coverage bitmap (1/0), laid out in a simple row packing.
            std::vector<uint8_t> coverage(W * H, 0);
            uint32_t pen_x = 8, pen_y = 8, row_h = 0;
            std::vector<uint16_t> glyph_boxes; // x0,y0,x1,y1 per glyph (atlas px)

            // Bake the printable ASCII range (32..126); the rest of the
            // glyph table stays empty and is skipped at draw time.
            for (int ch = kAsciiFirst; ch < kAsciiFirst + kAsciiCount; ++ch)
            {
                Glyph &g = glyphs_[ch];
                g.advance = 0;
                int glyph = stbtt_FindGlyphIndex(&font, ch);
                if (glyph == 0)
                {
                    g.u0 = g.v0 = g.u1 = g.v1 = 0;
                    continue;
                }
                int advance = 0, lbearing = 0;
                stbtt_GetGlyphHMetrics(&font, glyph, &advance, &lbearing);
                g.advance = (float)advance * scale;

                int ix0, iy0, ix1, iy1;
                stbtt_GetGlyphBitmapBox(&font, glyph, scale, scale, &ix0, &iy0, &ix1, &iy1);
                int gw = ix1 - ix0;
                int gh = iy1 - iy0;
                if (gw <= 0 || gh <= 0)
                {
                    g.u0 = g.v0 = g.u1 = g.v1 = 0;
                    glyph_boxes.push_back(0); glyph_boxes.push_back(0); glyph_boxes.push_back(0); glyph_boxes.push_back(0);
                    continue;
                }
                const uint32_t margin = 6; // distance-transform margin
                uint32_t total_w = (uint32_t)gw + margin * 2;
                uint32_t total_h = (uint32_t)gh + margin * 2;
                if (pen_x + total_w >= W)
                {
                    pen_x = 8;
                    pen_y += row_h + 8;
                    row_h = 0;
                }
                if (pen_y + total_h >= H)
                {
                    GEN_LOG_WARN("[Text] atlas too small for glyph %d (skipping rest)", ch);
                    break;
                }
                uint32_t ox = pen_x + margin;
                uint32_t oy = pen_y + margin;

                std::vector<uint8_t> coverage_glyph((uint32_t)gw * (uint32_t)gh, 0);
                stbtt_MakeGlyphBitmap(&font, coverage_glyph.data(), gw, gh, gw, scale, scale, glyph);
                for (int y = 0; y < gh; ++y)
                    for (int x = 0; x < gw; ++x)
                        coverage[((uint32_t)oy + y) * W + (uint32_t)ox + x] = coverage_glyph[y * gw + x] > 128 ? 1 : 0;

                g.u0 = (float)ox / W;
                g.v0 = (float)oy / H;
                g.u1 = (float)(ox + gw) / W;
                g.v1 = (float)(oy + gh) / H;
                // ix0..iy1 are already in output pixels (stbtt scales the
                // bitmap box), so no second scaling here.
                g.bearing_x = (float)ix0;
                g.bearing_y = (float)iy0;
                g.glyph_w = (float)gw;
                g.glyph_h = (float)gh;

                pen_x += total_w + 4;
                row_h = std::max(row_h, total_h);
                if (ch >= kAsciiFirst && ch < kAsciiFirst + kAsciiCount) ++glyphs_loaded_;
                (void)glyph_boxes;
            }

            // ---- signed distance transform ------------------------------
            // feature = 0 at inside pixels for the "distance to inside" map
            // and at outside pixels for the "distance to outside" map.
            std::vector<float> f_in(W * H, 1e30f);
            std::vector<float> f_out(W * H, 1e30f);
            for (size_t i = 0; i < coverage.size(); ++i)
            {
                if (coverage[i]) f_in[i] = 0.0f;
                else f_out[i] = 0.0f;
            }
            std::vector<float> d_in, d_out;
            distanceTransform(W, H, f_in, d_in);
            distanceTransform(W, H, f_out, d_out);

            std::vector<uint8_t> sdf(W * H, 128);
            const float sdf_half = font_pixels * 0.30f; // pixel distance mapped to the 0..1 range
            for (size_t i = 0; i < coverage.size(); ++i)
            {
                float signed_dist = coverage[i] ? d_out[i] : -d_in[i];
                float v = 0.5f + signed_dist / sdf_half;
                v = std::max(0.0f, std::min(1.0f, v));
                sdf[i] = (uint8_t)(v * 255.0f + 0.5f);
            }

            // ---- upload atlas as R8 -------------------------------------
            RHI::TextureDesc tex;
            tex.type = RHI::TextureType::Tex2D;
            tex.format = RHI::PixelFormat::R8_UNORM;
            tex.width = W;
            tex.height = H;
            tex.mip_levels = 1;
            tex.usage = (uint32_t)RHI::TextureUsageFlagBits::Sampled | (uint32_t)RHI::TextureUsageFlagBits::TransferDst;
            tex.name = "text-atlas";
            atlas_ = device_->createTexture(tex);
            if (!atlas_) return;
            device_->writeTexture(atlas_, sdf.data(), sdf.size(), W, H, 0, 0, 1);

            RHI::SamplerDesc samp;
            samp.min_filter = RHI::FilterMode::Linear;
            samp.mag_filter = RHI::FilterMode::Linear;
            samp.address_u = RHI::AddressMode::ClampToEdge;
            samp.address_v = RHI::AddressMode::ClampToEdge;
            samp.max_lod = 0.0f;
            samp.min_lod = 0.0f;
            samp.name = "text-atlas-sampler";
            sampler_ = device_->createSampler(samp);
        }

        //==================================================================
        // Pipeline / layout
        //==================================================================
        namespace
        {
            RHI::VertexLayout glyphVertexLayout()
            {
                RHI::VertexLayout layout;
                layout.stride = sizeof(Text::GlyphVertex);
                layout.attributes = {
                    {0, RHI::VertexFormat::Float3, 0},
                    {1, RHI::VertexFormat::Float2, 12},
                    {2, RHI::VertexFormat::Float4, 20},
                    {3, RHI::VertexFormat::Float4, 36},
                };
                return layout;
            }
        }

        namespace
        {
            RHI::RHIPipeline *createTextPipeline(RHI::RHIDevice *device, RHI::RHIResourceLayout *layout,
                                                 RHI::PixelFormat color_fmt, bool depth_test)
            {
                RHI::PipelineDesc d;
                d.name = "text-glyph";
                RHI::ShaderStageDesc vs, fs;
                if (!Render::detail::fillShaderStage("ui/text_glyph.vert", device->backend(), RHI::ShaderStageFlagBits::Vertex, vs) ||
                    !Render::detail::fillShaderStage("ui/text_glyph.frag", device->backend(), RHI::ShaderStageFlagBits::Fragment, fs))
                    return nullptr;
                d.shaders = {vs, fs};
                d.vertex_layout = glyphVertexLayout();
                d.topology = RHI::PrimitiveTopology::TriangleList;
                d.rasterizer.cull_mode = RHI::CullMode::None;
                d.depth_stencil.depth_test = depth_test;
                d.depth_stencil.depth_write = false;
                d.blend.attachments[0].blend_enable = true;
                d.blend.attachments[0].src_color = RHI::BlendFactor::SrcAlpha;
                d.blend.attachments[0].dst_color = RHI::BlendFactor::OneMinusSrcAlpha;
                d.blend.attachments[0].src_alpha = RHI::BlendFactor::One;
                d.blend.attachments[0].dst_alpha = RHI::BlendFactor::OneMinusSrcAlpha;
                d.resource_layout = layout->desc();
                d.color_formats[0] = color_fmt;
                d.color_format_count = 1;
                // Match the HDR scene target's depth/stencil format (the 3D
                // text draws into the HDR pass, whose depth texture is D24S8;
                // Metal requires the pipeline depth format to match).
                d.depth_format = depth_test ? RHI::PixelFormat::D24_UNORM_S8_UINT : RHI::PixelFormat::Undefined;
                return device->createPipeline(d);
            }
        }

        bool TextSystem::createPipeline()
        {
            RHI::ResourceLayoutDesc layout_desc;
            layout_desc.name = "text-layout";
            layout_desc.bindings = {
                {0, 0, RHI::ResourceType::UniformBuffer,
                 (uint32_t)RHI::ShaderStageFlagBits::Vertex | (uint32_t)RHI::ShaderStageFlagBits::Fragment,
                 1, "TextParams"},
                {0, 1, RHI::ResourceType::CombinedImageSampler,
                 (uint32_t)RHI::ShaderStageFlagBits::Fragment, 1, "u_glyph_atlas"},
            };
            layout_ = device_->createResourceLayout(layout_desc);
            if (!layout_) return false;

            set_ = device_->createDescriptorSet(layout_, 0);
            if (!set_) return false;

            RHI::DescriptorImageInfo img{atlas_, sampler_, RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1};
            RHI::WriteDescriptorSet writes[1];
            writes[0] = RHI::WriteDescriptorSet{1, 0, 1, RHI::ResourceType::CombinedImageSampler, nullptr, &img};
            set_->write(writes, 1);

            RHI::BufferDesc ubo_desc;
            ubo_desc.name = "text-ubo";
            ubo_desc.size = 16 * 5; // mat4 (64) + vec4 (16)
            ubo_desc.usage = (uint32_t)RHI::BufferUsageFlagBits::Uniform;
            ubo_desc.memory_usage = RHI::MemoryUsage::CpuToGpu;
            ubo_ = device_->createBuffer(ubo_desc);
            if (!ubo_) return false;

            // 3D text: HDR target (RGBA16F) + depth.
            pipeline_3d_ = createTextPipeline(device_, layout_, RHI::PixelFormat::RGBA16_SFLOAT, true);
            if (!pipeline_3d_) return false;
            return true;
        }

        RHI::RHIPipeline *TextSystem::pipeline2D(RHI::PixelFormat color_fmt)
        {
            auto it = pipeline2d_.find(color_fmt);
            if (it != pipeline2d_.end()) return it->second;
            RHI::RHIPipeline *p = createTextPipeline(device_, layout_, color_fmt, false);
            if (p) pipeline2d_[color_fmt] = p;
            return p;
        }

        //==================================================================
        // Queuing
        //==================================================================
        void TextSystem::drawText2D(float x, float y, float size_pixels, const std::string &text,
                                    const Math::Color &color, float screen_w, float screen_h)
        {
            TextRequest r;
            r.is_2d = true;
            r.x_px = x;
            r.y_px = y;
            r.size_px = size_pixels;
            r.screen_w = screen_w;
            r.screen_h = screen_h;
            r.text = text;
            r.color = color;
            queue_.push_back(std::move(r));
        }

        void TextSystem::drawText3D(const Math::Mat4 &world, const std::string &text,
                                    const Math::Color &color, float size_world)
        {
            TextRequest r;
            r.is_2d = false;
            r.world = world;
            r.text = text;
            r.color = color;
            r.size_world = size_world;
            queue_.push_back(std::move(r));
        }

        //==================================================================
        // Vertex building
        //==================================================================
        namespace
        {
            // SDF edge half-width in atlas-uv units, derived from the scale
            // so a ~1px screen AA window is kept; clamped to stay crisp.
            inline float computeSoftness(float scale)
            {
                float soft = 0.012f * scale + 0.015f;
                if (soft < 0.008f) soft = 0.008f;
                if (soft > 0.12f) soft = 0.12f;
                return soft;
            }

            inline void emitGlyph(std::vector<GlyphVertex> &out,
                                   float x0, float y0, float x1, float y1,
                                   float u0, float v0, float u1, float v1,
                                   const float color[4], float soft, float z = 0.0f)
            {
                float px[4][3] = {{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}};
                float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
                for (int i = 0; i < 4; ++i)
                {
                    GlyphVertex &g = out.emplace_back();
                    g.position[0] = px[i][0];
                    g.position[1] = px[i][1];
                    g.position[2] = px[i][2];
                    g.uv[0] = uv[i][0];
                    g.uv[1] = uv[i][1];
                    g.color[0] = color[0];
                    g.color[1] = color[1];
                    g.color[2] = color[2];
                    g.color[3] = color[3];
                    g.params[0] = 0;
                    g.params[1] = soft;
                    g.params[2] = 0;
                    g.params[3] = 0;
                }
            }
        }

        void TextSystem::buildVertices(std::vector<GlyphVertex> &verts, bool is_2d)
        {
            for (const TextRequest &r : queue_)
            {
                if (r.is_2d != is_2d) continue;
                // Scale = output units per atlas pixel (atlas was rasterised at font_pixels = ascent_).
                float scale = is_2d ? (r.size_px / ascent_) : (r.size_world / ascent_);
                float soft = computeSoftness(scale);
                const float color[4] = {r.color.r, r.color.g, r.color.b, r.color.a};

                float pen = 0.0f; // advance cursor, in output units
                for (char ch : r.text)
                {
                    if (ch < 32 || ch > 126) continue;
                    const Glyph &g = glyphs_[(int)ch];
                    if (g.glyph_w <= 0.0f || g.glyph_h <= 0.0f)
                    {
                        pen += g.advance * scale;
                        continue;
                    }

                    // Quad in the glyph's local space, before the text origin.
                    float lx0 = (pen + g.bearing_x) * scale;
                    float lx1 = lx0 + g.glyph_w * scale;
                    // bearing_y is the top of the glyph box in output pixels
                    // above the baseline (positive up).  Box height below
                    // the top = bearing_y - (bearing_y - glyph_h).
                    float top_y = g.bearing_y * scale;
                    float bottom_y = (g.bearing_y - g.glyph_h) * scale;

                    if (is_2d)
                    {
                        // Screen space: origin is the TOP-LEFT, y grows DOWN.
                        // The baseline sits at origin.y + ascent*scale (the
                        // text's ascender height).  Top of the glyph box is
                        // above the baseline by top_y (up = smaller y).
                        float base_y = (float)r.y_px + ascent_ * scale;
                        float qy0 = base_y - top_y;        // TOP (smaller screen y)
                        float qy1 = base_y - bottom_y;      // BOTTOM (larger screen y)
                        float qx0 = (float)r.x_px + lx0;
                        float qx1 = (float)r.x_px + lx1;
                        emitGlyph(verts, qx0, qy0, qx1, qy1, g.u0, g.v0, g.u1, g.v1, color, soft);
                    }
                    else
                    {
                        // World space: origin = world translation, y grows UP.
                        // Column-major Mat4: translation is m[3]/m[7]/m[15].
                        float ox = r.world.m[3];
                        float oy = r.world.m[7];
                        float oz = r.world.m[15];
                        // Baseline at origin; box top +top_y, bottom +bottom_y.
                        float qy0 = oy + top_y;
                        float qy1 = oy + bottom_y;
                        float qx0 = ox + lx0;
                        float qx1 = ox + lx1;
                        // Billboard: the quad lies in a plane parallel to the
                        // screen at the origin depth; view*proj is applied by
                        // the vertex shader (u_view_proj).
                        emitGlyph(verts, qx0, qy0, qx1, qy1, g.u0, g.v0, g.u1, g.v1, color, soft, oz);
                    }

                    pen += g.advance * scale;
                }
            }
        }

        void TextSystem::flush2D(RHI::RHICommandBuffer *cmd, uint32_t width, uint32_t height,
                                 RHI::PixelFormat target_format)
        {
            if (queue_.empty()) return;
            std::vector<GlyphVertex> verts;
            buildVertices(verts, true);
            if (verts.empty()) return;

            // Screen pixels (y down from top) -> NDC clip space.
            for (auto &v : verts)
            {
                v.position[0] = (v.position[0] / (float)width) * 2.0f - 1.0f;
                v.position[1] = 1.0f - (v.position[1] / (float)height) * 2.0f;
                v.position[2] = 0.0f;
            }
            RHI::RHIPipeline *p = pipeline2D(target_format);
            if (!p) return;
            uploadAndDraw(cmd, verts, p, /*is2d=*/true);
        }

        void TextSystem::flush3D(RHI::RHICommandBuffer *cmd, const Math::Mat4 &view_proj)
        {
            if (queue_.empty() || !pipeline_3d_) return;
            std::vector<GlyphVertex> verts;
            buildVertices(verts, false);
            if (verts.empty()) return;

            // The shader transforms world->clip via u_view_proj (mode=0).
            TextParamsUBO ubo;
            ubo.view_proj = Render::GpuMat4::from(view_proj);
            ubo.mode = Render::GpuVec4(0.0f, 0.0f, 0.0f, 0.0f);
            device_->writeBuffer(ubo_, &ubo, sizeof(ubo), 0);
            uploadAndDraw(cmd, verts, pipeline_3d_, /*is2d=*/false);
        }

        void TextSystem::uploadAndDraw(RHI::RHICommandBuffer *cmd, const std::vector<GlyphVertex> &verts,
                                       RHI::RHIPipeline *pipeline, bool is2d)
        {
            if (!pipeline || verts.empty()) return;

            // Grow the vertex buffer if needed.
            RHI::RHIBuffer *vb = is2d ? vertex_buffer_ : vertex_buffer_3d_;
            size_t need_bytes = verts.size() * sizeof(GlyphVertex);
            if (need_bytes > vb->size())
            {
                if (is2d) { device_->destroyBuffer(vb); }
                else { device_->destroyBuffer(vb); }
                RHI::BufferDesc d;
                d.name = is2d ? "text-vertices-2d" : "text-vertices-3d";
                d.size = std::max<uint64_t>(need_bytes, 1 << 16);
                d.usage = (uint32_t)RHI::BufferUsageFlagBits::Vertex | (uint32_t)RHI::BufferUsageFlagBits::TransferDst;
                d.memory_usage = RHI::MemoryUsage::CpuToGpu;
                RHI::RHIBuffer *fresh = device_->createBuffer(d);
                if (is2d) vertex_buffer_ = fresh;
                else vertex_buffer_3d_ = fresh;
                vb = fresh;
                if (!vb) return;
            }
            device_->writeBuffer(vb, verts.data(), (uint64_t)need_bytes, 0);

            // If the quad count exceeds the index buffer capacity, rebuild it.
            size_t quad_count = verts.size() / 4;
            if (quad_count * 6 > index_buffer_->size() / sizeof(uint16_t))
            {
                std::vector<uint16_t> idx(quad_count * 6);
                for (size_t i = 0; i < quad_count; ++i)
                {
                    uint32_t b = (uint32_t)i * 4;
                    idx[i * 6 + 0] = (uint16_t)(b + 0);
                    idx[i * 6 + 1] = (uint16_t)(b + 1);
                    idx[i * 6 + 2] = (uint16_t)(b + 2);
                    idx[i * 6 + 3] = (uint16_t)(b + 0);
                    idx[i * 6 + 4] = (uint16_t)(b + 2);
                    idx[i * 6 + 5] = (uint16_t)(b + 3);
                }
                RHI::BufferDesc d;
                d.name = "text-indices";
                d.size = idx.size() * sizeof(uint16_t);
                d.usage = (uint32_t)RHI::BufferUsageFlagBits::Index;
                d.memory_usage = RHI::MemoryUsage::GpuOnly;
                d.initial_data = idx.data();
                d.initial_data_size = idx.size() * sizeof(uint16_t);
                device_->destroyBuffer(index_buffer_);
                index_buffer_ = device_->createBuffer(d);
                if (!index_buffer_) return;
            }

            // Fill the 2D UBO (mode=2d) when in 2D; 3D UBO is already written.
            if (is2d)
            {
                TextParamsUBO ubo;
                ubo.view_proj = Render::GpuMat4::identity();
                ubo.mode = Render::GpuVec4(1.0f, 0.0f, 0.0f, 0.0f);
                device_->writeBuffer(ubo_, &ubo, sizeof(ubo), 0);
            }

            RHI::DescriptorBufferInfo buf{ubo_, 0, sizeof(TextParamsUBO)};
            RHI::DescriptorImageInfo img{atlas_, sampler_, RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1};
            RHI::WriteDescriptorSet writes[2];
            writes[0] = RHI::WriteDescriptorSet{0, 0, 1, RHI::ResourceType::UniformBuffer, &buf, nullptr};
            writes[1] = RHI::WriteDescriptorSet{1, 0, 1, RHI::ResourceType::CombinedImageSampler, nullptr, &img};
            set_->write(writes, 2);

            cmd->bindPipeline(pipeline);
            cmd->bindVertexBuffer(vb, 0);
            cmd->bindIndexBuffer(index_buffer_, RHI::IndexType::UInt16, 0);
            RHI::RHIDescriptorSet *sets[1] = {set_};
            cmd->bindDescriptorSets(pipeline, sets, 1, nullptr, 0);
            cmd->drawIndexed((uint32_t)(quad_count * 6), 0, 0, 1, 0);
        }

    } // namespace Text
} // namespace Genesis
