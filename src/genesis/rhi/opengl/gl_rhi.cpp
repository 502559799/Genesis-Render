/*
 * Genesis Render - Engine
 * gl_rhi.cpp : OpenGL 4.1 core backend (macOS compatible).
 */
#include "gl_rhi.hpp"
#include "../glsl_preprocess.hpp"
#include "foundation_layer/log.hpp"

#include <glad/glad.h>
#include <SDL2/SDL.h>

#include "external/imgui/imgui.h"
#include "external/imgui/imgui_impl_sdl.h"
#include "external/imgui/imgui_impl_opengl3.h"

#include <unordered_map>
#include <vector>
#include <cstring>
#include <regex>
#include <string>
#include <cstdio>
#include <algorithm>

namespace Genesis
{
namespace
{
    struct GLBuffer
    {
        GLuint id = 0;
        GLenum target = GL_ARRAY_BUFFER;
        size_t size = 0;
        bool alive = false;
    };
    struct GLTexture
    {
        GLuint id = 0;
        GLenum target = GL_TEXTURE_2D;
        GLenum internal = GL_RGBA8, fmt = GL_RGBA, type = GL_UNSIGNED_BYTE;
        uint32_t w = 1, h = 1;
        uint16_t mips = 1;
        bool cube = false;
        bool depth = false;
        bool alive = false;
    };
    struct GLSampler { GLuint id = 0; bool alive = false; };
    struct GLProgram { GLuint id = 0; bool alive = false; };
    struct GLPipeline
    {
        GLuint program = 0;
        GLuint vao = 0;
        VertexLayout layout;
        GLenum topology = GL_TRIANGLES;
        CullMode cull = CullMode::Back;
        bool depth_test = true, depth_write = true;
        CompareOp depth_compare = CompareOp::LessEqual;
        BlendMode blend = BlendMode::Off;
        bool alive = false;
    };
    struct GLSetBinding
    {
        bool has_buffer = false;
        BufferHandle buffer{};
        bool has_texture = false;
        TextureHandle texture{};
        SamplerHandle sampler{};
    };
    struct GLSet { GLSetBinding b[16]; bool alive = false; };

    GLenum toGLCompare(CompareOp op)
    {
        static const GLenum t[] = {GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL,
                                   GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
        return t[(int)op];
    }
    GLenum toGLTopology(PrimitiveTopology t)
    {
        switch (t)
        {
        case PrimitiveTopology::Lines: return GL_LINES;
        case PrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
        default: return GL_TRIANGLES;
        }
    }
    void toGLFormat(Format f, GLenum &internal_, GLenum &fmt, GLenum &type, int &channels)
    {
        switch (f)
        {
        case Format::R8Unorm: internal_ = GL_R8; fmt = GL_RED; type = GL_UNSIGNED_BYTE; channels = 1; break;
        case Format::R8G8B8A8Unorm:
        case Format::B8G8R8A8Unorm: internal_ = GL_RGBA8; fmt = GL_RGBA; type = GL_UNSIGNED_BYTE; channels = 4; break;
        case Format::R8G8B8A8Srgb:
        case Format::B8G8R8A8Srgb: internal_ = GL_SRGB8_ALPHA8; fmt = GL_RGBA; type = GL_UNSIGNED_BYTE; channels = 4; break;
        case Format::R16G16B16A16Float: internal_ = GL_RGBA16F; fmt = GL_RGBA; type = GL_HALF_FLOAT; channels = 4; break;
        case Format::R32Float: internal_ = GL_R32F; fmt = GL_RED; type = GL_FLOAT; channels = 1; break;
        case Format::R32G32Float: internal_ = GL_RG32F; fmt = GL_RG; type = GL_FLOAT; channels = 2; break;
        case Format::R32G32B32Float: internal_ = GL_RGB32F; fmt = GL_RGB; type = GL_FLOAT; channels = 3; break;
        case Format::R32G32B32A32Float: internal_ = GL_RGBA32F; fmt = GL_RGBA; type = GL_FLOAT; channels = 4; break;
        case Format::D32Float: internal_ = GL_DEPTH_COMPONENT32F; fmt = GL_DEPTH_COMPONENT; type = GL_FLOAT; channels = 1; break;
        case Format::D24UnormS8Uint: internal_ = GL_DEPTH24_STENCIL8; fmt = GL_DEPTH_STENCIL; type = GL_UNSIGNED_INT_24_8; channels = 1; break;
        default: internal_ = GL_RGBA8; fmt = GL_RGBA; type = GL_UNSIGNED_BYTE; channels = 4; break;
        }
    }

    class GLRhi final : public IRHIDevice
    {
    public:
        bool init(SDL_Window *window, uint32_t width, uint32_t height, bool enable_debug) override
        {
            (void)enable_debug;
            window_ = window;
            width_ = width;
            height_ = height;

            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
            SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
            glctx_ = SDL_GL_CreateContext(window_);
            if (!glctx_)
            {
                GenesisPrint("[GL] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
                return false;
            }
            SDL_GL_MakeCurrent(window_, glctx_);
            SDL_GL_SetSwapInterval(1);
            if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
            {
                GenesisPrint("[GL] gladLoadGL failed\n");
                return false;
            }
            GenesisPrint("[GL] %s | GL %s | GLSL %s\n", glGetString(GL_RENDERER),
                         glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
            if (getenv("GENESIS_GL_DEBUG"))
            {
                glEnable(GL_DEBUG_OUTPUT);
                glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_FALSE);
                glDebugMessageControl(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_ERROR, GL_DONT_CARE, 0, nullptr, GL_TRUE);
                glDebugMessageCallback(
                    [](GLenum, GLenum type, GLuint, GLenum sev, GLsizei, const GLchar *m, const void *) {
                        if (sev == GL_DEBUG_SEVERITY_HIGH || sev == GL_DEBUG_SEVERITY_MEDIUM)
                            GenesisPrint("[GLDBG] type=0x%x sev=0x%x: %s\n", type, sev, m);
                    }, nullptr);
            }

            caps_.backend = RenderBackend::OpenGL;
            caps_.clip_depth_zero_to_one = false;
            caps_.ndc_y_down = false;
            snprintf(caps_.device_name, sizeof(caps_.device_name), "%s",
                     (const char *)glGetString(GL_RENDERER));
            snprintf(caps_.api_version, sizeof(caps_.api_version), "OpenGL %s",
                     (const char *)glGetString(GL_VERSION));

            glGenFramebuffers(1, &offscreen_fbo_);
            glGenVertexArrays(1, &empty_vao_);

            SamplerDesc lin;
            linear_sampler_ = createSampler(lin);
            SamplerDesc near_;
            near_.min_filter = near_.mag_filter = near_.mip_filter = FilterMode::Nearest;
            nearest_sampler_ = createSampler(near_);
            SamplerDesc cmip;
            cmip.wrap_u = cmip.wrap_v = cmip.wrap_w = WrapMode::ClampEdge;
            cube_mip_sampler_ = createSampler(cmip);

            glGenBuffers(1, &push_ubo_);
            glBindBuffer(GL_UNIFORM_BUFFER, push_ubo_);
            glBufferData(GL_UNIFORM_BUFFER, 256, nullptr, GL_DYNAMIC_DRAW);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);

            createDefaultTargets();
            return true;
        }

        void shutdown() override
        {
            for (auto &p : pipelines_) if (p.alive) { glDeleteVertexArrays(1, &p.vao); p.alive = false; }
            for (auto &p : programs_) if (p.alive) { glDeleteProgram(p.id); p.alive = false; }
            for (auto &t : textures_) if (t.alive) { glDeleteTextures(1, &t.id); t.alive = false; }
            for (auto &s : samplers_) if (s.alive) { glDeleteSamplers(1, &s.id); s.alive = false; }
            for (auto &b : buffers_) if (b.alive) { glDeleteBuffers(1, &b.id); b.alive = false; }
            if (push_ubo_) glDeleteBuffers(1, &push_ubo_);
            destroyDefaultTargets();
            if (empty_vao_) glDeleteVertexArrays(1, &empty_vao_);
            if (offscreen_fbo_) glDeleteFramebuffers(1, &offscreen_fbo_);
            if (default_depth_rb_) glDeleteRenderbuffers(1, &default_depth_rb_);
            glFinish();
            SDL_GL_MakeCurrent(nullptr, nullptr);
            if (glctx_) { SDL_GL_DeleteContext(glctx_); glctx_ = nullptr; }
        }

        const DeviceCaps &caps() const override { return caps_; }
        Format swapchainFormat() const override { return Format::B8G8R8A8Unorm; }

        void onResize(uint32_t w, uint32_t h) override
        {
            if (w == 0 || h == 0 || (w == width_ && h == height_)) return;
            width_ = w; height_ = h;
            destroyDefaultTargets();
            createDefaultTargets();
        }

        // ------------------------------------------------------------- buffers
        BufferHandle createBuffer(const BufferDesc &desc, const void *data) override
        {
            GLBuffer b;
            b.size = desc.size;
            if (desc.usage & BufferUsage_Vertex) b.target = GL_ARRAY_BUFFER;
            else if (desc.usage & BufferUsage_Index) b.target = GL_ELEMENT_ARRAY_BUFFER;
            else b.target = GL_UNIFORM_BUFFER;
            GLenum usage = desc.host_visible ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
            glGenBuffers(1, &b.id);
            glBindBuffer(b.target, b.id);
            glBufferData(b.target, desc.size, data, usage);
            glBindBuffer(b.target, 0);
            b.alive = true;
            BufferHandle h; h.id = allocIndex(buffers_, b);
            return h;
        }
        void destroyBuffer(BufferHandle h) override { if (auto *b = lookup(h, buffers_)) { glDeleteBuffers(1, &b->id); b->alive = false; } }
        void writeBuffer(BufferHandle h, size_t offset, const void *data, size_t size) override
        {
            auto *b = lookup(h, buffers_);
            if (!b) return;
            glBindBuffer(b->target, b->id);
            glBufferSubData(b->target, offset, size, data);
            glBindBuffer(b->target, 0);
        }
        void *mapBuffer(BufferHandle h) override
        {
            auto *b = lookup(h, buffers_);
            if (!b) return nullptr;
            glBindBuffer(b->target, b->id);
            return glMapBuffer(b->target, GL_WRITE_ONLY);
        }
        void unmapBuffer(BufferHandle h) override
        {
            auto *b = lookup(h, buffers_);
            if (!b) return;
            glUnmapBuffer(b->target);
            glBindBuffer(b->target, 0);
        }

        // ------------------------------------------------------------ textures
        TextureHandle createTexture(const TextureDesc &desc, const void *data) override
        {
            GLTexture t;
            t.w = desc.width; t.h = desc.height; t.mips = desc.mip_levels;
            t.cube = desc.type == TextureType::TextureCube;
            t.target = t.cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
            int channels = 4;
            toGLFormat(desc.format, t.internal, t.fmt, t.type, channels);
            t.depth = (desc.usage & TextureUsage_DepthStencil) != 0;
            glGenTextures(1, &t.id);
            glBindTexture(t.target, t.id);
            glTexParameteri(t.target, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(t.target, GL_TEXTURE_MAX_LEVEL, desc.mip_levels - 1);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            uint32_t faces = t.cube ? 6u : 1u;
            for (uint32_t f = 0; f < faces; ++f)
            {
                GLenum face_target = t.cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + f : GL_TEXTURE_2D;
                uint32_t w = t.w, h = t.h;
                for (uint32_t m = 0; m < desc.mip_levels; ++m)
                {
                    const void *px = nullptr;
                    if (data && m == 0)
                        px = t.cube ? static_cast<const void *const *>(data)[f] : data;
                    glTexImage2D(face_target, m, t.internal, w, h, 0, t.fmt, t.type, px);
                    w = std::max(1u, w >> 1);
                    h = std::max(1u, h >> 1);
                }
            }
            glTexParameteri(t.target, GL_TEXTURE_MIN_FILTER, desc.mip_levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glTexParameteri(t.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(t.target, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(t.target, GL_TEXTURE_WRAP_T, GL_REPEAT);
            if (t.cube)
            {
                glTexParameteri(t.target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
            }
            glBindTexture(t.target, 0);
            t.alive = true;
            TextureHandle h; h.id = allocIndex(textures_, t);
            return h;
        }
        void uploadTexture2D(TextureHandle h, uint32_t level, uint32_t, uint32_t,
                            uint32_t w, uint32_t ht, const void *data) override
        {
            auto *t = lookup(h, textures_);
            if (!t) return;
            glBindTexture(GL_TEXTURE_2D, t->id);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, w, ht, t->fmt, t->type, data);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        void uploadCubeFace(TextureHandle h, uint32_t face, uint32_t level,
                            uint32_t w, uint32_t ht, const void *data) override
        {
            auto *t = lookup(h, textures_);
            if (!t) return;
            glBindTexture(GL_TEXTURE_CUBE_MAP, t->id);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, 0, 0, w, ht,
                            t->fmt, t->type, data);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        }
        void generateMipmaps(TextureHandle h) override
        {
            auto *t = lookup(h, textures_);
            if (!t || t->mips <= 1) return;
            glBindTexture(t->target, t->id);
            glGenerateMipmap(t->target);
            glBindTexture(t->target, 0);
        }
        void destroyTexture(TextureHandle h) override
        {
            if (auto *t = lookup(h, textures_)) { glDeleteTextures(1, &t->id); t->alive = false; }
        }

        // ------------------------------------------------------------ samplers
        SamplerHandle createSampler(const SamplerDesc &desc) override
        {
            GLSampler s;
            glGenSamplers(1, &s.id);
            auto filter = [](FilterMode f) { return f == FilterMode::Nearest ? GL_NEAREST : GL_LINEAR; };
            glSamplerParameteri(s.id, GL_TEXTURE_MIN_FILTER,
                                desc.mip_filter == FilterMode::Linear
                                    ? (desc.min_filter == FilterMode::Nearest ? GL_NEAREST_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_LINEAR)
                                    : filter(desc.min_filter));
            glSamplerParameteri(s.id, GL_TEXTURE_MAG_FILTER, filter(desc.mag_filter));
            auto wrap = [](WrapMode w) {
                return w == WrapMode::ClampEdge ? GL_CLAMP_TO_EDGE
                                               : w == WrapMode::MirroredRepeat ? GL_MIRRORED_REPEAT : GL_REPEAT;
            };
            glSamplerParameteri(s.id, GL_TEXTURE_WRAP_S, wrap(desc.wrap_u));
            glSamplerParameteri(s.id, GL_TEXTURE_WRAP_T, wrap(desc.wrap_v));
            glSamplerParameteri(s.id, GL_TEXTURE_WRAP_R, wrap(desc.wrap_w));
#ifdef GL_TEXTURE_MAX_ANISOTROPY
            if (GLAD_GL_ARB_texture_filter_anisotropic)
                glSamplerParameterf(s.id, GL_TEXTURE_MAX_ANISOTROPY,
                                    std::max(1.0f, desc.max_anisotropy));
#endif
            s.alive = true;
            SamplerHandle h; h.id = allocIndex(samplers_, s);
            return h;
        }
        void destroySampler(SamplerHandle h) override
        {
            if (auto *s = lookup(h, samplers_)) { glDeleteSamplers(1, &s->id); s->alive = false; }
        }

        // ------------------------------------------------------------ shaders
        ShaderHandle createShaderProgram(const ShaderProgramDesc &desc) override
        {
            GLProgram p;
            p.id = glCreateProgram();
            std::string merged;
            for (uint32_t i = 0; i < desc.stage_count; ++i)
            {
                const auto &st = desc.stages[i];
                if (!st.glsl) continue;
                std::string glsl = preprocessGLSLForOpenGL(st.glsl);
                merged += glsl;
                merged += "\n";
                GLenum type = st.stage == ShaderStage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
                GLuint sh = glCreateShader(type);
                const char *src = glsl.c_str();                    GenesisPrint("---- %s stage %d:\n%s\n", desc.name, (int)st.stage, src);
                glShaderSource(sh, 1, &src, nullptr);
                glCompileShader(sh);
                GLint ok = 0;
                glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
                if (!ok)
                {
                    char log[4096];
                    glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
                    GenesisPrint("[GL] shader compile failed (%s stage %d):\n%s\n",
                                 desc.name ? desc.name : "?", (int)st.stage, log);
                }
                glAttachShader(p.id, sh);
                glDeleteShader(sh);
            }
            glLinkProgram(p.id);
            GLint linked = 0;
            glGetProgramiv(p.id, GL_LINK_STATUS, &linked);
            if (!linked)
            {
                char log[4096];
                glGetProgramInfoLog(p.id, sizeof(log), nullptr, log);
                GenesisPrint("[GL] program link failed (%s): %s\n", desc.name ? desc.name : "?", log);
            }
            program_glsl_[p.id] = merged;
            applyBindingMarkers(p.id);
            p.alive = true;
            ShaderHandle h; h.id = allocIndex(programs_, p);
            return h;
        }
        void destroyShaderProgram(ShaderHandle h) override
        {
            if (auto *p = lookup(h, programs_))
            {
                program_glsl_.erase(p->id);
                glDeleteProgram(p->id);
                p->alive = false;
            }
        }

        // ------------------------------------------------------------ pipelines
        PipelineHandle createPipeline(const PipelineDesc &desc) override
        {
            GLPipeline p;
            if (auto *s = lookup(desc.shader, programs_)) p.program = s->id;
            p.layout = desc.vertex_layout;
            p.topology = toGLTopology(desc.topology);
            p.cull = desc.cull;
            p.depth_test = desc.depth_test;
            p.depth_write = desc.depth_write;
            p.depth_compare = desc.depth_compare;
            p.blend = desc.blend;
            glGenVertexArrays(1, &p.vao);
            p.alive = true;
            PipelineHandle h; h.id = allocIndex(pipelines_, p);
            return h;
        }
        void destroyPipeline(PipelineHandle h) override
        {
            if (auto *p = lookup(h, pipelines_)) { glDeleteVertexArrays(1, &p->vao); p->alive = false; }
        }

        // ------------------------------------------------------------ sets
        DescriptorSetHandle createDescriptorSet(const DescriptorSetDesc &desc) override
        {
            GLSet s;
            DescriptorSetHandle h; h.id = allocIndex(sets_, s);
            updateDescriptorSet(h, desc);
            return h;
        }
        void updateDescriptorSet(DescriptorSetHandle h, const DescriptorSetDesc &desc) override
        {
            auto *s = lookup(h, sets_);
            if (!s) return;
            for (uint32_t i = 0; i < desc.binding_count; ++i)
            {
                const auto &src = desc.bindings[i];
                auto &dst = s->b[src.binding];
                if (src.buffer.valid()) { dst.has_buffer = true; dst.buffer = src.buffer; }
                if (src.texture.valid())
                {
                    dst.has_texture = true;
                    dst.texture = src.texture;
                    dst.sampler = src.sampler;
                }
            }
        }
        void destroyDescriptorSet(DescriptorSetHandle h) override
        {
            if (auto *s = lookup(h, sets_)) s->alive = false;
        }

        // ------------------------------------------------------------ frame
        bool beginFrame() override { return width_ > 0 && height_ > 0; }
        uint32_t currentFrameIndex() const override { return 0; }

        void beginPass(uint32_t colorCount, ColorAttachment *colors, const DepthAttachment *depth) override
        {
            glBindFramebuffer(GL_FRAMEBUFFER, offscreen_fbo_);
            for (uint32_t i = 0; i < 4; ++i)
            {
                if (i < colorCount && colors[i].texture.valid())
                {
                    auto *t = lookup(colors[i].texture, textures_);
                    GLenum target = t->cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + colors[i].layer : GL_TEXTURE_2D;
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, target,
                                           t->id, colors[i].mip);
                }
                else
                {
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, 0, 0);
                }
            }
            if (depth && depth->texture.valid())
            {
                auto *t = lookup(depth->texture, textures_);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, t->target, t->id, 0);
            }
            else
            {
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
            }
            GLenum drawbuf[4] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
                                 GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3};
            glDrawBuffers(colorCount, drawbuf);

            GLuint fbo_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (fbo_status != GL_FRAMEBUFFER_COMPLETE)
                GenesisPrint("[GL] WARNING offscreen FBO incomplete: 0x%x\n", fbo_status);

            if (colorCount > 0)
            {
                auto *t0 = lookup(colors[0].texture, textures_);
                uint32_t mip = colors[0].mip;
                uint32_t w = std::max(1u, t0->w >> mip), h = std::max(1u, t0->h >> mip);
                glViewport(0, 0, w, h);
                glDisable(GL_SCISSOR_TEST); glScissor(0,0,w,h);
            }
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glClearColor(colors[0].clear_color[0], colors[0].clear_color[1],
                         colors[0].clear_color[2], colors[0].clear_color[3]);
            glClearDepth(1.0);
            glClear(GL_COLOR_BUFFER_BIT |
                    (depth && depth->texture.valid() && depth->clear ? GL_DEPTH_BUFFER_BIT : 0));
        }

        void endPass() override { glBindFramebuffer(GL_FRAMEBUFFER, 0); }

        void beginSwapchainPass(const float clear[4], float clear_depth) override
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            GLenum db = GL_BACK;
            glDrawBuffers(1, &db);
            glViewport(0, 0, width_, height_);
            glDisable(GL_SCISSOR_TEST); glScissor(0,0,width_,height_);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glClearColor(clear[0], clear[1], clear[2], clear[3]);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            (void)clear_depth;
        }
        void endSwapchainPass() override {}

        void endFrame() override { SDL_GL_SwapWindow(window_); }

        // ------------------------------------------------------------ commands
        void bindPipeline(PipelineHandle h) override
        {
            auto *p = lookup(h, pipelines_);
            if (!p) return;
            cur_pipeline_ = p;
            glUseProgram(p->program);
            glBindVertexArray(p->vao);
            if (p->depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
            glDepthMask(p->depth_write ? GL_TRUE : GL_FALSE);
            glDepthFunc(toGLCompare(p->depth_compare));
            // Projection negates NDC Y on both backends -> mirrored winding.
            glFrontFace(GL_CW);
            if (p->cull == CullMode::None) glDisable(GL_CULL_FACE);
            else
            {
                glEnable(GL_CULL_FACE);
                glCullFace(p->cull == CullMode::Front ? GL_FRONT : GL_BACK);
            }
            if (p->blend == BlendMode::Off) glDisable(GL_BLEND);
            else
            {
                glEnable(GL_BLEND);
                glBlendFunc(p->blend == BlendMode::Additive ? GL_ONE : GL_SRC_ALPHA,
                            p->blend == BlendMode::Additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
            }
            glBindBufferBase(GL_UNIFORM_BUFFER, kBindingPushEmu, push_ubo_);
            bound_sets_[0] = bound_sets_[1] = nullptr;
        }
        void bindDescriptorSet(uint32_t setIndex, DescriptorSetHandle h) override
        {
            if (setIndex > 1 || !h.valid()) return;
            auto *s = lookup(h, sets_);
            bound_sets_[setIndex] = s;
            applySet(s);
        }
        void pushConstants(const void *data, uint32_t size) override
        {
            glBindBuffer(GL_UNIFORM_BUFFER, push_ubo_);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, size, data);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }
        void bindVertexBuffer(BufferHandle h, size_t offset, uint32_t binding) override
        {
            (void)binding;
            auto *buf = lookup(h, buffers_);
            auto *p = cur_pipeline_;
            if (!buf || !p) return;
            glBindVertexArray(p->vao);
            glBindBuffer(GL_ARRAY_BUFFER, buf->id);
            for (uint32_t i = 0; i < p->layout.attribute_count; ++i)
            {
                const auto &a = p->layout.attributes[i];
                GLint comps = 4; GLenum type = GL_FLOAT; GLboolean norm = GL_FALSE;
                vertexFormat(a.format, comps, type, norm);
                glEnableVertexAttribArray(a.location);
                glVertexAttribPointer(a.location, comps, type, norm, p->layout.stride,
                                      reinterpret_cast<void *>(a.offset + offset));
            }
        }
        void bindIndexBuffer(BufferHandle h, IndexType it, size_t offset) override
        {
            auto *buf = lookup(h, buffers_);
            if (!buf) return;
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buf->id);
            index_type_ = it == IndexType::Uint32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
            index_offset_ = offset;
        }
        void setViewport(uint32_t w, uint32_t h) override { glViewport(0, 0, w, h); }
        void draw(uint32_t vc, uint32_t ic, uint32_t first) override
        {
            glDrawArraysInstanced(cur_pipeline_->topology, first, vc, ic);
        }
        void drawIndexed(uint32_t ic, uint32_t instances, uint32_t first, int32_t vbase) override
        {
            const void *ptr = reinterpret_cast<const void *>(
                index_offset_ + first * (index_type_ == GL_UNSIGNED_INT ? 4u : 2u));
            glDrawElementsInstancedBaseVertex(cur_pipeline_->topology, ic, index_type_, ptr,
                                              instances, vbase);
        }
        void drawFullscreen() override { glDrawArrays(GL_TRIANGLES, 0, 3); }

        void readTexturePixels(TextureHandle h, uint32_t w, uint32_t ht, void *out) override
        {
            auto *t = lookup(h, textures_);
            if (!t) return;
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            GLenum read_target = t->cube ? GL_TEXTURE_CUBE_MAP_POSITIVE_X : t->target;
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, read_target, t->id, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0);
            glViewport(0, 0, w, ht);
            glDisable(GL_SCISSOR_TEST);
            glScissor(0, 0, w, ht);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            uint8_t *dst = static_cast<uint8_t *>(out);
            glReadPixels(0, 0, w, ht, GL_RGBA, GL_UNSIGNED_BYTE, dst);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
        }

        TextureHandle sceneColor() const override { return scene_color_; }
        TextureHandle sceneDepth() const override { return scene_depth_; }
        SamplerHandle linearSampler() const override { return linear_sampler_; }
        SamplerHandle nearestSampler() const override { return nearest_sampler_; }
        SamplerHandle cubeMipSampler() const override { return cube_mip_sampler_; }

        // ------------------------------------------------------------- imgui
        void imguiBackendInit() override
        {
            ImGui_ImplSDL2_InitForOpenGL(window_, glctx_);
            ImGui_ImplOpenGL3_Init("#version 410 core");
            imgui_ready_ = true;
        }
        void imguiBackendBeginFrame() override
        {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL2_NewFrame();
        }
        void imguiBackendRenderDrawData(::ImDrawData *d) override
        {
            if (imgui_ready_) ImGui_ImplOpenGL3_RenderDrawData(d);
        }
        void imguiBackendShutdown() override
        {
            if (!imgui_ready_) return;
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplSDL2_Shutdown();
            imgui_ready_ = false;
        }
        void *imguiTextureID(TextureHandle t, SamplerHandle) override
        {
            auto *tex = lookup(t, textures_);
            return tex ? reinterpret_cast<void *>(static_cast<uintptr_t>(tex->id)) : nullptr;
        }

    private:
        template <typename T, size_t N>
        uint32_t allocIndex(std::array<T, N> &arr, const T &val)
        {
            for (uint32_t i = 0; i < (uint32_t)arr.size(); ++i)
            {
                if (!arr[i].alive) { arr[i] = val; arr[i].alive = true; return i + 1; }
            }
            GenesisPrint("[GL] resource pool exhausted (%zu)\n", N);
            return 0xffffffffu;
        }
        template <typename T, size_t N, typename Tag>
        T *lookup(const TRHIHandle<Tag> &h, std::array<T, N> &arr)
        {
            if (h.id == 0xffffffffu || h.id == 0 || h.id > (uint32_t)N) return nullptr;
            T &v = arr[h.id - 1];
            return v.alive ? &v : nullptr;
        }

        void applySet(GLSet *s)
        {
            if (!s) return;
            for (uint32_t i = 0; i < 16; ++i)
            {
                auto &b = s->b[i];
                if (b.has_buffer)
                    if (auto *buf = lookup(b.buffer, buffers_))
                        glBindBufferBase(GL_UNIFORM_BUFFER, i, buf->id);
                if (b.has_texture)
                {
                    auto *tex = lookup(b.texture, textures_);
                    if (!tex) continue;
                    glActiveTexture(GL_TEXTURE0 + i);
                    glBindTexture(tex->target, tex->id);
                    if (b.sampler.valid())
                    {
                        if (auto *sm = lookup(b.sampler, samplers_)) glBindSampler(i, sm->id);
                    }
                    else glBindSampler(i, 0);
                }
            }
        }

        static void vertexFormat(Format f, GLint &comps, GLenum &type, GLboolean &norm)
        {
            switch (f)
            {
            case Format::R32G32B32A32Float: comps = 4; type = GL_FLOAT; norm = GL_FALSE; break;
            case Format::R32G32B32Float: comps = 3; type = GL_FLOAT; norm = GL_FALSE; break;
            case Format::R32Float: comps = 1; type = GL_FLOAT; norm = GL_FALSE; break;
            case Format::R32G32Float: comps = 2; type = GL_FLOAT; norm = GL_FALSE; break;
            case Format::R8G8B8A8Unorm: comps = 4; type = GL_UNSIGNED_BYTE; norm = GL_TRUE; break;
            default: comps = 4; type = GL_FLOAT; norm = GL_FALSE; break;
            }
        }

        void applyBindingMarkers(GLuint program)
        {
            const std::string &src = program_glsl_[program];
            GLint block_count = 0;
            glGetProgramiv(program, GL_ACTIVE_UNIFORM_BLOCKS, &block_count);
            char name[128];
            for (GLint i = 0; i < block_count; ++i)
            {
                glGetActiveUniformBlockName(program, i, sizeof(name), nullptr, name);
                int binding = findMarker(src, name, true);
                if (binding >= 0) glUniformBlockBinding(program, i, binding);
            }
            glUseProgram(program);
            GLint uniform_count = 0;
            glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &uniform_count);
            GLsizei len; GLint sz; GLenum type;
            for (GLint i = 0; i < uniform_count; ++i)
            {
                glGetActiveUniform(program, i, sizeof(name), &len, &sz, &type, name);
                if (type == GL_SAMPLER_2D || type == GL_SAMPLER_CUBE)
                {
                    std::string nm = name;
                    if (auto pos = nm.find('['); pos != std::string::npos) nm = nm.substr(0, pos);
                    int binding = findMarker(src, nm, false);
                    if (binding >= 0)
                    {
                        GLint loc = glGetUniformLocation(program, nm.c_str());
                        if (loc >= 0) glUniform1i(loc, binding);
                    }
                }
            }
        }

        static int findMarker(const std::string &src, const std::string &name, bool block)
        {
            std::string re_text = R"(/\*@binding=(\d+)\*/[^;{}]*)" +
                                  std::string(block ? R"(\s*uniform\s+)" : R"(\s*uniform\s+\w+\s+)") +
                                  name;
            std::regex re(re_text);
            std::smatch m;
            if (std::regex_search(src, m, re)) return std::stoi(m[1].str());
            return -1;
        }

        void createDefaultTargets()
        {
            TextureDesc cd;
            cd.format = Format::R8G8B8A8Unorm;
            cd.width = width_; cd.height = height_;
            cd.usage = TextureUsage_RenderTarget | TextureUsage_Sampled;
            cd.name = "scene_color";
            scene_color_ = createTexture(cd, nullptr);

            TextureDesc dd;
            dd.format = Format::D32Float;
            dd.width = width_; dd.height = height_;
            dd.usage = TextureUsage_DepthStencil;
            dd.name = "scene_depth";
            scene_depth_ = createTexture(dd, nullptr);

            glGenRenderbuffers(1, &default_depth_rb_);
            glBindRenderbuffer(GL_RENDERBUFFER, default_depth_rb_);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width_, height_);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
        }
        void destroyDefaultTargets()
        {
            if (scene_color_.valid()) destroyTexture(scene_color_);
            if (scene_depth_.valid()) destroyTexture(scene_depth_);
        }

    private:
        static constexpr size_t kMaxBuffers = 1024;
        static constexpr size_t kMaxTextures = 1024;
        static constexpr size_t kMaxSamplers = 64;
        static constexpr size_t kMaxPrograms = 128;
        static constexpr size_t kMaxPipelines = 128;
        static constexpr size_t kMaxSets = 512;
        std::array<GLBuffer, kMaxBuffers> buffers_{};
        std::array<GLTexture, kMaxTextures> textures_{};
        std::array<GLSampler, kMaxSamplers> samplers_{};
        std::array<GLProgram, kMaxPrograms> programs_{};
        std::array<GLPipeline, kMaxPipelines> pipelines_{};
        std::array<GLSet, kMaxSets> sets_{};

        std::unordered_map<GLuint, std::string> program_glsl_;

        SDL_Window *window_ = nullptr;
        SDL_GLContext glctx_ = nullptr;
        DeviceCaps caps_;
        uint32_t width_ = 1, height_ = 1;

        GLuint offscreen_fbo_ = 0, empty_vao_ = 0, push_ubo_ = 0, default_depth_rb_ = 0;
        TextureHandle scene_color_, scene_depth_;
        SamplerHandle linear_sampler_, nearest_sampler_, cube_mip_sampler_;

        GLPipeline *cur_pipeline_ = nullptr;
        GLSet *bound_sets_[2] = {nullptr, nullptr};
        GLenum index_type_ = GL_UNSIGNED_INT;
        size_t index_offset_ = 0;
        bool imgui_ready_ = false;
    };
} // namespace

IRHIDevice *createGLRhi() { return new GLRhi(); }
} // namespace Genesis
