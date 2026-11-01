/*
 * Genesis Render - OpenGL backend implementation (device + resources)
 */
#include "rhi/opengl/gl_backend.hpp"
#include "rhi/opengl/gl_convert.hpp"
#include "foundation_layer/genesis_log.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>

namespace Genesis
{
    namespace RHI
    {
        //==================================================================
        // Format mapping
        //==================================================================
        GLFormatMapping glPixelFormatMapping(PixelFormat format)
        {
            switch (format)
            {
            case PixelFormat::R8_UNORM: return {GL_R8, GL_RED, GL_UNSIGNED_BYTE};
            case PixelFormat::RG8_UNORM: return {GL_RG8, GL_RG, GL_UNSIGNED_BYTE};
            case PixelFormat::RGB8_UNORM: return {GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE};
            case PixelFormat::RGBA8_UNORM: return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
            case PixelFormat::RGBA8_SRGB: return {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE};
            case PixelFormat::BGRA8_UNORM: return {GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE};
            case PixelFormat::R16_SFLOAT: return {GL_R16F, GL_RED, GL_HALF_FLOAT};
            case PixelFormat::RG16_SFLOAT: return {GL_RG16F, GL_RG, GL_HALF_FLOAT};
            case PixelFormat::RGBA16_SFLOAT: return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
            case PixelFormat::R32_SFLOAT: return {GL_R32F, GL_RED, GL_FLOAT};
            case PixelFormat::RG32_SFLOAT: return {GL_RG32F, GL_RG, GL_FLOAT};
            case PixelFormat::RGB32_SFLOAT: return {GL_RGB32F, GL_RGB, GL_FLOAT};
            case PixelFormat::RGBA32_SFLOAT: return {GL_RGBA32F, GL_RGBA, GL_FLOAT};
            case PixelFormat::RGBA32_UINT: return {GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT};
            case PixelFormat::R32_UINT: return {GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT};
            case PixelFormat::D16_UNORM: return {GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT};
            case PixelFormat::D24_UNORM_S8_UINT: return {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8};
            case PixelFormat::D32_SFLOAT: return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT};
            case PixelFormat::D32_SFLOAT_S8_UINT: return {GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV};
            default: return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
            }
        }

        GLenum glTextureTarget(TextureType type)
        {
            switch (type)
            {
            case TextureType::Tex2D: return GL_TEXTURE_2D;
            case TextureType::TexCube: return GL_TEXTURE_CUBE_MAP;
            case TextureType::Tex3D: return GL_TEXTURE_3D;
            }
            return GL_TEXTURE_2D;
        }

        static uint64_t hashRenderPassDesc(const RenderPassDesc &d)
        {
            uint64_t h = 1469598103934665603ull;
            auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
            mix(d.attachment_count);
            for (uint32_t i = 0; i < d.attachment_count; ++i)
            {
                mix((uint64_t)d.attachments[i].format);
                mix((uint64_t)d.attachments[i].load_op);
                mix((uint64_t)d.attachments[i].store_op);
                mix((uint64_t)d.attachments[i].stencil_load_op);
                mix((uint64_t)d.attachments[i].stencil_store_op);
                mix(d.attachments[i].is_depth_stencil ? 1u : 0u);
            }
            return h;
        }

        //==================================================================
        // GLBuffer
        //==================================================================
        GLBuffer::GLBuffer(GLuint id, const BufferDesc &desc) : id_(id), desc_(desc)
        {
            target_ = glBufferTarget(desc_.usage);
        }

        GLBuffer::~GLBuffer()
        {
            if (id_) glDeleteBuffers(1, &id_);
        }

        void *GLBuffer::map(uint64_t offset, uint64_t size)
        {
            if (size == ~0ull) size = desc_.size - offset;
            glBindBuffer(GL_COPY_WRITE_BUFFER, id_);
            GLbitfield flags = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;
            if (desc_.memory_usage == MemoryUsage::CpuToGpu)
                flags = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT;
            else if (desc_.memory_usage == MemoryUsage::GpuToCpu)
                flags = GL_MAP_READ_BIT;
            void *p = glMapBufferRange(GL_COPY_WRITE_BUFFER, (GLintptr)offset, (GLsizeiptr)size, flags);
            mapped_ = (p != nullptr);
            return p;
        }

        void GLBuffer::unmap()
        {
            if (!mapped_) return;
            glBindBuffer(GL_COPY_WRITE_BUFFER, id_);
            glUnmapBuffer(GL_COPY_WRITE_BUFFER);
            mapped_ = false;
        }

        void GLBuffer::setCpuShadow(const void *data, uint64_t size, uint64_t offset)
        {
            if (offset + size > shadow_.size()) shadow_.resize((size_t)(offset + size));
            std::memcpy(shadow_.data() + offset, data, (size_t)size);
        }

        //==================================================================
        // GLTexture
        //==================================================================
        GLTexture::GLTexture(GLuint id, const TextureDesc &desc) : id_(id), desc_(desc)
        {
            target_ = glTextureTarget(desc_.type);
        }

        GLTexture::~GLTexture()
        {
            if (id_) glDeleteTextures(1, &id_);
        }

        //==================================================================
        // GLSampler
        //==================================================================
        GLSampler::GLSampler(GLuint id, const SamplerDesc &desc) : id_(id), desc_(desc) {}
        GLSampler::~GLSampler()
        {
            if (id_) glDeleteSamplers(1, &id_);
        }

        //==================================================================
        // GLShader
        //==================================================================
        GLShader::GLShader(GLuint id, ShaderStageFlagBits stage, const std::string &name)
            : id_(id), stage_(stage), name_(name) {}
        GLShader::~GLShader()
        {
            if (id_) glDeleteShader(id_);
        }

        //==================================================================
        // GLResourceLayout
        //==================================================================
        const ResourceBinding *GLResourceLayout::findBinding(uint32_t set, uint32_t binding) const
        {
            for (const auto &b : desc_.bindings)
                if (b.set == set && b.binding == binding) return &b;
            return nullptr;
        }

        //==================================================================
        // GLPipeline
        //==================================================================
        GLPipeline::GLPipeline(GLuint program, const PipelineDesc &desc) : program_(program), desc_(desc)
        {
            push_constant_ubos_.resize(kPushConstantSlots, 0);
        }

        GLPipeline::~GLPipeline()
        {
            for (auto &kv : vao_cache_)
                glDeleteVertexArrays(1, &kv.second.vao);
            vao_cache_.clear();
            if (!push_constant_ubos_.empty())
                glDeleteBuffers((GLsizei)push_constant_ubos_.size(), push_constant_ubos_.data());
            if (program_) glDeleteProgram(program_);
        }

        void GLPipeline::bindUniformBlocks()
        {
            glUseProgram(program_);
            for (const auto &b : desc_.resource_layout.bindings)
            {
                if (!resourceTypeIsBuffer(b.type)) continue;
                if (b.name.empty()) continue;
                GLuint idx = glGetUniformBlockIndex(program_, b.name.c_str());
                if (idx == GL_INVALID_INDEX)
                {
                    // Layouts are shared between pipelines, so a stage set may
                    // legitimately not reference every block.
                    GEN_LOG_DEBUG("[GL] pipeline '%s': uniform block '%s' (set=%u binding=%u) unused by the shader",
                                 desc_.name.c_str(), b.name.c_str(), b.set, b.binding);
                    continue;
                }
                glUniformBlockBinding(program_, idx, glUniformBlockIndex(b.set, b.binding));
            }
        }

        void GLPipeline::bindStorageBlocks()
        {
#if !defined(__APPLE__)
            glUseProgram(program_);
            for (const auto &b : desc_.resource_layout.bindings)
            {
                if (b.type != ResourceType::StorageBuffer && b.type != ResourceType::StorageBufferDynamic) continue;
                if (b.name.empty()) continue;
                GLuint index = glGetProgramResourceIndex(program_, GL_SHADER_STORAGE_BLOCK, b.name.c_str());
                if (index == GL_INVALID_INDEX)
                {
                    GEN_LOG_DEBUG("[GL] pipeline '%s': storage block '%s' unused by the shader",
                                  desc_.name.c_str(), b.name.c_str());
                    continue;
                }
                glShaderStorageBlockBinding(program_, index, glStorageBlockIndex(b.set, b.binding));
            }
#endif
        }

        void GLPipeline::bindSamplerUniforms()
        {
            glUseProgram(program_);
            sampler_uniforms_.clear();
            for (const auto &b : desc_.resource_layout.bindings)
            {
                if (!resourceTypeIsImage(b.type)) continue;
                if (b.type == ResourceType::Sampler) continue; // pure samplers share the unit of their image
                if (b.name.empty()) continue;
                GLint loc = glGetUniformLocation(program_, b.name.c_str());
                if (loc < 0)
                {
                    GEN_LOG_DEBUG("[GL] pipeline '%s': sampler uniform '%s' (set=%u binding=%u) unused by the shader",
                                 desc_.name.c_str(), b.name.c_str(), b.set, b.binding);
                    continue;
                }
                GLuint unit = glTextureUnit(b.set, b.binding);
                glUniform1i(loc, (GLint)unit);
                SamplerUniform su;
                su.location = loc;
                su.unit = unit;
                su.target = GL_TEXTURE_2D;
                su.name = b.name;
                sampler_uniforms_.push_back(su);
            }
        }

        void GLPipeline::setPushConstants(const void *data, uint32_t size)
        {
            if (size == 0) return;
            uint32_t slot = push_constant_slot_;
            push_constant_slot_ = (push_constant_slot_ + 1) % kPushConstantSlots;
            if (push_constant_ubos_[slot] == 0)
            {
                glGenBuffers(1, &push_constant_ubos_[slot]);
                glBindBuffer(GL_UNIFORM_BUFFER, push_constant_ubos_[slot]);
                glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)pushConstantSize(), nullptr, GL_DYNAMIC_DRAW);
                GLint block_index = glGetUniformBlockIndex(program_, "PushConstants");
                if (block_index != GL_INVALID_INDEX)
                    glUniformBlockBinding(program_, block_index, GL_PUSH_CONSTANT_BLOCK_BINDING);
            }
            glBindBuffer(GL_UNIFORM_BUFFER, push_constant_ubos_[slot]);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)size, data);
            glBindBufferRange(GL_UNIFORM_BUFFER, GL_PUSH_CONSTANT_BLOCK_BINDING,
                              push_constant_ubos_[slot], 0, (GLsizeiptr)pushConstantSize());
        }

        GLuint GLPipeline::findOrCreateVAO(GLBuffer *vb, uint64_t vb_offset, GLBuffer *ib, const VertexLayout &layout)
        {
            GLuint vb_id = vb ? vb->id() : 0;
            GLuint ib_id = ib ? ib->id() : 0;
            auto key = std::make_pair(vb_id, ib_id);
            auto pointAttributes = [&](uint64_t offset)
            {
                glBindBuffer(GL_ARRAY_BUFFER, vb_id);
                for (const auto &attr : layout.attributes)
                {
                    glEnableVertexAttribArray(attr.location);
                    glVertexAttribPointer(attr.location,
                                          glVertexComponentCount(attr.format),
                                          glVertexFormatGL(attr.format),
                                          glVertexNormalized(attr.format),
                                          (GLsizei)layout.stride,
                                          reinterpret_cast<const void *>(static_cast<uintptr_t>(attr.offset + offset)));
                    if (layout.per_instance) glVertexAttribDivisor(attr.location, 1);
                }
            };

            auto it = vao_cache_.find(key);
            if (it != vao_cache_.end())
            {
                // Transient vertex data is sub-allocated from a ring, so the
                // same buffer comes back at different offsets.
                if (vb && layout.valid() && it->second.vb_offset != vb_offset)
                {
                    glBindVertexArray(it->second.vao);
                    pointAttributes(vb_offset);
                    glBindVertexArray(0);
                    glBindBuffer(GL_ARRAY_BUFFER, 0);
                    it->second.vb_offset = vb_offset;
                }
                return it->second.vao;
            }

            GLuint vao = 0;
            glGenVertexArrays(1, &vao);
            glBindVertexArray(vao);
            if (vb && layout.valid()) pointAttributes(vb_offset);
            if (ib) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib_id);

            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            vao_cache_[key] = CachedVAO{vao, vb_offset};
            return vao;
        }

        //==================================================================
        // GLRenderPass / GLFramebuffer
        //==================================================================
        GLRenderPass::GLRenderPass(const RenderPassDesc &desc, uint64_t hash) : desc_(desc), hash_(hash) {}

        GLFramebuffer::GLFramebuffer(GLuint fbo, GLRenderPass *pass, uint32_t width, uint32_t height,
                                     const std::string &name, bool is_default)
            : fbo_(fbo), pass_(pass), width_(width), height_(height), name_(name), is_default_(is_default) {}

        GLFramebuffer::~GLFramebuffer()
        {
            if (fbo_ && !is_default_) glDeleteFramebuffers(1, &fbo_);
        }

        //==================================================================
        // GLDescriptorSet
        //==================================================================
        void GLDescriptorSet::write(const WriteDescriptorSet *writes, uint32_t count)
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                const WriteDescriptorSet &w = writes[i];
                for (uint32_t e = 0; e < w.count; ++e)
                {
                    Entry entry;
                    entry.binding = w.binding;
                    entry.type = w.type;
                    if (resourceTypeIsBuffer(w.type) && w.buffer_info)
                    {
                        const DescriptorBufferInfo &bi = w.buffer_info[e];
                        entry.buffer = static_cast<GLBuffer *>(bi.buffer);
                        entry.offset = bi.offset;
                        entry.range = bi.range ? bi.range : (entry.buffer ? entry.buffer->size() : 0);
                    }
                    else if (resourceTypeIsImage(w.type) && w.image_info)
                    {
                        const DescriptorImageInfo &ii = w.image_info[e];
                        entry.texture = static_cast<GLTexture *>(ii.texture);
                        entry.sampler = static_cast<GLSampler *>(ii.sampler);
                    }
                    // replace existing entry for the same binding if present
                    bool replaced = false;
                    for (auto &existing : entries_)
                    {
                        if (existing.binding == w.binding && existing.type == w.type)
                        {
                            existing = entry;
                            replaced = true;
                            break;
                        }
                    }
                    if (!replaced) entries_.push_back(entry);
                }
            }
        }

        //==================================================================
        // GLDevice
        //==================================================================
        GLDevice::GLDevice() {}

        GLDevice::~GLDevice()
        {
            shutdown();
        }

        bool GLDevice::initialize(const DeviceDesc &desc)
        {
            desc_ = desc;

            if (desc_.native_window)
            {
                window_ = desc_.native_window;
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
                SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
                SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
                SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
                SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
                SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
                SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
                SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

                gl_context_ = SDL_GL_CreateContext(reinterpret_cast<SDL_Window *>(window_));
                if (!gl_context_)
                {
                    GEN_LOG_ERROR("[GL] SDL_GL_CreateContext failed: %s", SDL_GetError());
                    return false;
                }
                SDL_GL_MakeCurrent(reinterpret_cast<SDL_Window *>(window_), gl_context_);
                SDL_GL_SetSwapInterval(desc_.vsync ? 1 : 0);
            }

            const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
            const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
            const char *vendor = reinterpret_cast<const char *>(glGetString(GL_VENDOR));
            if (!version || !renderer)
            {
                GEN_LOG_ERROR("[GL] no OpenGL context current");
                return false;
            }
            info_.backend = BackendType::OpenGL;
            info_.api_version = version;
            info_.device_name = std::string(renderer) + " (" + (vendor ? vendor : "") + ")";

            GLint max_tex = 0, max_ubo_bindings = 0, max_tex_units = 0, max_ubo_range = 0, min_align = 0;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
            glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &max_ubo_bindings);
            glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_tex_units);
            glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &max_ubo_range);
            glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &min_align);
            info_.max_texture_size = (uint32_t)max_tex;
            info_.max_uniform_buffer_bindings = (uint32_t)max_ubo_bindings;
            info_.max_texture_image_units = (uint32_t)max_tex_units;
            info_.max_uniform_buffer_range = (uint64_t)max_ubo_range;
            info_.min_uniform_buffer_offset_alignment = (uint64_t)(min_align > 0 ? min_align : 256);
            info_.max_push_constant_size = 128;
#if defined(__APPLE__)
            // Apple caps OpenGL at 4.1 core: no compute shaders (Metal covers it).
            info_.supports_compute = false;
#else
            {
                int major = 0, minor = 0;
                if (std::sscanf(version, "%d.%d", &major, &minor) != 2) { major = 0; minor = 0; }
                info_.supports_compute = major > 4 || (major == 4 && minor >= 3);
            }
#endif
            info_.supports_geometry_shader = true;
            info_.supports_tessellation = true;
            info_.supports_multisample = true;
            info_.supports_srgb_framebuffer = true;
            info_.clip_depth_zero_to_one = false;
            info_.present_row0_top = false;
            info_.depth_format = PixelFormat::D24_UNORM_S8_UINT;
            info_.frames_in_flight = 1;

            GEN_LOG_INFO("[GL] context: %s | %s", info_.api_version.c_str(), info_.device_name.c_str());

            glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glDepthFunc(GL_LEQUAL);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glFrontFace(GL_CCW);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

            initialized_ = true;

            if (window_)
            {
                int w = 0, h = 0;
                SDL_GL_GetDrawableSize(reinterpret_cast<SDL_Window *>(window_), &w, &h);
                if (!createSwapchain(window_, (uint32_t)std::max(1, w), (uint32_t)std::max(1, h))) return false;
            }
            return true;
        }

        void GLDevice::shutdown()
        {
            if (!initialized_) return;
            waitIdle();

            for (auto *p : command_buffers_) delete p;
            command_buffers_.clear();
            for (auto *p : descriptor_sets_) delete p;
            descriptor_sets_.clear();
            for (auto *p : framebuffers_) delete p;
            framebuffers_.clear();
            for (auto &kv : render_passes_) delete kv.second;
            render_passes_.clear();
            for (auto *p : pipelines_) delete p;
            pipelines_.clear();
            for (auto *p : layouts_) delete p;
            layouts_.clear();
            for (auto *p : shaders_) delete p;
            shaders_.clear();
            for (auto *p : samplers_) delete p;
            samplers_.clear();
            for (auto *p : textures_) delete p;
            textures_.clear();
            for (auto *p : buffers_) delete p;
            buffers_.clear();

            swapchain_render_pass_ = nullptr;
            swapchain_framebuffer_ = nullptr;
            frame_command_buffer_ = nullptr;

            if (scratch_fbo_) { glDeleteFramebuffers(1, &scratch_fbo_); scratch_fbo_ = 0; }

            if (gl_context_)
            {
                SDL_GL_DeleteContext(gl_context_);
                gl_context_ = nullptr;
            }
            initialized_ = false;
        }

        bool GLDevice::createSwapchain(void *native_window, uint32_t width, uint32_t height)
        {
            window_ = native_window ? native_window : window_;
            swapchain_width_ = width;
            swapchain_height_ = height;
            swapchain_format_ = desc_.srgb_backbuffer ? PixelFormat::RGBA8_SRGB : PixelFormat::RGBA8_UNORM;

            // The present pass is colour only on every backend (tone mapping,
            // 2D overlays and UI never depth test).
            RenderPassDesc rp;
            rp.name = "swapchain";
            rp.attachment_count = 1;
            rp.attachments[0].format = swapchain_format_;
            rp.attachments[0].load_op = LoadOp::Clear;
            rp.attachments[0].store_op = StoreOp::Store;
            rp.attachments[0].is_depth_stencil = false;
            rp.attachments[0].final_layout = TextureLayout::PresentSrc;

            swapchain_render_pass_ = static_cast<GLRenderPass *>(createRenderPass(rp));
            swapchain_framebuffer_ = new GLFramebuffer(0, swapchain_render_pass_, width, height, "swapchain-default", true);
            framebuffers_.push_back(swapchain_framebuffer_);

            if (!frame_command_buffer_)
            {
                frame_command_buffer_ = new GLCommandBuffer(this);
                command_buffers_.push_back(frame_command_buffer_);
            }
            return true;
        }

        void GLDevice::destroySwapchain()
        {
            if (swapchain_framebuffer_)
            {
                auto it = std::find(framebuffers_.begin(), framebuffers_.end(), swapchain_framebuffer_);
                if (it != framebuffers_.end()) framebuffers_.erase(it);
                delete swapchain_framebuffer_;
                swapchain_framebuffer_ = nullptr;
            }
            if (swapchain_render_pass_)
            {
                destroyRenderPass(swapchain_render_pass_);
                swapchain_render_pass_ = nullptr;
            }
        }

        void GLDevice::resizeSwapchain(uint32_t width, uint32_t height)
        {
            if (width == 0 || height == 0) return;
            swapchain_width_ = width;
            swapchain_height_ = height;
            if (swapchain_framebuffer_) swapchain_framebuffer_->resize(width, height);
        }

        bool GLDevice::beginFrame()
        {
            ++frame_number_;
            return true;
        }

        void GLDevice::endFrame()
        {
            if (window_ && !desc_.headless)
                SDL_GL_SwapWindow(reinterpret_cast<SDL_Window *>(window_));
        }

        void GLDevice::waitIdle()
        {
            if (!initialized_) return;
            glFinish();
        }

        //==================================================================
        // Resource creation
        //==================================================================
        RHIBuffer *GLDevice::createBuffer(const BufferDesc &desc)
        {
            // Data is always specified through GL_COPY_WRITE_BUFFER: binding
            // GL_ELEMENT_ARRAY_BUFFER here would silently rewrite the index
            // buffer of whatever vertex array object is currently bound.
            GLuint id = 0;
            glGenBuffers(1, &id);
            glBindBuffer(GL_COPY_WRITE_BUFFER, id);
            glBufferData(GL_COPY_WRITE_BUFFER, (GLsizeiptr)std::max<uint64_t>(desc.size, 4), desc.initial_data,
                         glUsageHint(desc.memory_usage));
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            GLBuffer *b = new GLBuffer(id, desc);
            buffers_.push_back(b);
            return b;
        }

        RHITexture *GLDevice::createTexture(const TextureDesc &desc)
        {
            GLuint id = 0;
            glGenTextures(1, &id);
            GLenum target = glTextureTarget(desc.type);
            GLFormatMapping fmt = glPixelFormatMapping(desc.format);
            bool is_depth = pixelFormatIsDepth(desc.format);

            glBindTexture(target, id);

            if (target == GL_TEXTURE_CUBE_MAP)
            {
                for (uint32_t layer = 0; layer < 6 * desc.array_layers; ++layer)
                {
                    GLenum face = GL_TEXTURE_CUBE_MAP_POSITIVE_X + (layer % 6);
                    for (uint32_t mip = 0; mip < desc.mip_levels; ++mip)
                    {
                        uint32_t w = std::max(1u, desc.width >> mip);
                        uint32_t h = std::max(1u, desc.height >> mip);
                        glTexImage2D(face, mip, fmt.internal_format, w, h, 0, fmt.format, fmt.type, nullptr);
                    }
                }
            }
            else if (target == GL_TEXTURE_3D)
            {
                for (uint32_t mip = 0; mip < desc.mip_levels; ++mip)
                {
                    uint32_t w = std::max(1u, desc.width >> mip);
                    uint32_t h = std::max(1u, desc.height >> mip);
                    uint32_t d = std::max(1u, desc.depth >> mip);
                    glTexImage3D(target, mip, fmt.internal_format, w, h, d, 0, fmt.format, fmt.type, nullptr);
                }
            }
            else
            {
                for (uint32_t layer = 0; layer < desc.array_layers; ++layer)
                {
                    for (uint32_t mip = 0; mip < desc.mip_levels; ++mip)
                    {
                        uint32_t w = std::max(1u, desc.width >> mip);
                        uint32_t h = std::max(1u, desc.height >> mip);
                        if (desc.array_layers > 1)
                            glTexImage3D(GL_TEXTURE_2D_ARRAY, mip, fmt.internal_format, w, h, desc.array_layers, 0,
                                         fmt.format, fmt.type, nullptr);
                        else
                            glTexImage2D(target, mip, fmt.internal_format, w, h, 0, fmt.format, fmt.type, nullptr);
                        break;
                    }
                    break;
                }
            }

            glTexParameteri(target, GL_TEXTURE_MIN_FILTER, desc.mip_levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            if (!is_depth)
            {
                glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
                glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, (GLint)(desc.mip_levels - 1));
            }
            glBindTexture(target, 0);

            GLenum err = glGetError();
            if (err != GL_NO_ERROR)
                GEN_LOG_ERROR("[GL] createTexture('%s') failed with GL error 0x%04x", desc.name.c_str(), err);

            GLTexture *tex = new GLTexture(id, desc);
            tex->setCurrentLayout((desc.usage & (uint32_t)TextureUsageFlagBits::RenderTarget)
                                      ? TextureLayout::ColorAttachmentOptimal
                                      : TextureLayout::ShaderReadOnlyOptimal);
            textures_.push_back(tex);
            return tex;
        }

        RHISampler *GLDevice::createSampler(const SamplerDesc &desc)
        {
            GLuint id = 0;
            glGenSamplers(1, &id);
            glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, desc.mipmap_mode == MipmapMode::Linear ? glMipFilter(desc.min_filter, desc.mipmap_mode) : glFilter(desc.min_filter));
            glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, glFilter(desc.mag_filter));
            glSamplerParameteri(id, GL_TEXTURE_WRAP_S, glAddress(desc.address_u));
            glSamplerParameteri(id, GL_TEXTURE_WRAP_T, glAddress(desc.address_v));
            glSamplerParameteri(id, GL_TEXTURE_WRAP_R, glAddress(desc.address_w));
            glSamplerParameterf(id, GL_TEXTURE_MIN_LOD, desc.min_lod);
            glSamplerParameterf(id, GL_TEXTURE_MAX_LOD, desc.max_lod);
            glSamplerParameterfv(id, GL_TEXTURE_BORDER_COLOR, desc.border_color);
            if (desc.compare_enable)
            {
                glSamplerParameteri(id, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
                glSamplerParameteri(id, GL_TEXTURE_COMPARE_FUNC, glCompareOp(desc.compare_op));
            }
            GLSampler *s = new GLSampler(id, desc);
            samplers_.push_back(s);
            return s;
        }

        // GL_COMPUTE_SHADER is a GL 4.3 symbol.  macOS exposes at most OpenGL
        // 4.1 core (no compute shaders), so the symbol does not exist there and
        // referencing it unconditionally breaks the mac build.  Resolve the
        // stage enum through this helper instead; compute requests fail with a
        // clear runtime error on 4.1 rather than a compile error everywhere.
        namespace
        {
            bool glStageEnum(ShaderStageFlagBits stage, GLenum &out)
            {
                switch (stage)
                {
                case ShaderStageFlagBits::Vertex: out = GL_VERTEX_SHADER; return true;
                case ShaderStageFlagBits::Fragment: out = GL_FRAGMENT_SHADER; return true;
                case ShaderStageFlagBits::Geometry: out = GL_GEOMETRY_SHADER; return true;
#ifdef GL_COMPUTE_SHADER
                case ShaderStageFlagBits::Compute: out = GL_COMPUTE_SHADER; return true;
#endif
                default: return false;
                }
            }
        } // namespace

        RHIShader *GLDevice::createShader(const ShaderStageDesc &desc)
        {
            if (desc.code_type != ShaderCodeType::GlslSource || !desc.code)
            {
                GEN_LOG_ERROR("[GL] shader '%s': OpenGL backend requires GLSL source", desc.name.c_str());
                return nullptr;
            }
            GLenum type = GL_VERTEX_SHADER;
            if (!glStageEnum(desc.stage, type))
            {
                GEN_LOG_ERROR("[GL] shader '%s': stage not supported by this OpenGL version "
                              "(compute needs GL 4.3+, this context caps at 4.1 core on macOS)",
                              desc.name.c_str());
                return nullptr;
            }

            GLuint id = glCreateShader(type);
            const char *src = reinterpret_cast<const char *>(desc.code);
            GLint len = (GLint)desc.code_size;
            glShaderSource(id, 1, &src, &len);
            glCompileShader(id);
            GLint ok = 0;
            glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                GLint log_len = 0;
                glGetShaderiv(id, GL_INFO_LOG_LENGTH, &log_len);
                std::string log((size_t)std::max(1, log_len), '\0');
                glGetShaderInfoLog(id, log_len, nullptr, &log[0]);
                GEN_LOG_ERROR("[GL] shader compile failed for '%s':\n%s", desc.name.c_str(), log.c_str());
                const char *lines = src;
                int line_no = 1;
                const char *p = lines;
                while (*p)
                {
                    const char *eol = std::strchr(p, '\n');
                    if (line_no <= 400)
                    {
                        // print the first lines to help locate the error
                    }
                    if (!eol) break;
                    p = eol + 1;
                    ++line_no;
                }
                glDeleteShader(id);
                return nullptr;
            }
            GLShader *shader = new GLShader(id, desc.stage, desc.name);
            shaders_.push_back(shader);
            return shader;
        }

        RHIResourceLayout *GLDevice::createResourceLayout(const ResourceLayoutDesc &desc)
        {
            GLResourceLayout *layout = new GLResourceLayout(desc);
            layouts_.push_back(layout);
            return layout;
        }

        RHIPipeline *GLDevice::createPipeline(const PipelineDesc &desc)
        {
            GLuint program = glCreateProgram();
            std::vector<GLuint> stages;
            for (const auto &sd : desc.shaders)
            {
                GLuint shader = 0;
                GLenum type = GL_VERTEX_SHADER;
                if (sd.code_type != ShaderCodeType::GlslSource || !sd.code)
                {
                    GEN_LOG_ERROR("[GL] pipeline '%s': shader stage '%s' is not GLSL source",
                                  desc.name.c_str(), sd.name.c_str());
                    for (GLuint s : stages) glDeleteShader(s);
                    glDeleteProgram(program);
                    return nullptr;
                }
                if (!glStageEnum(sd.stage, type))
                {
                    GEN_LOG_ERROR("[GL] pipeline '%s': stage '%s' not supported by this OpenGL "
                                  "version (compute needs GL 4.3+, macOS caps at 4.1 core)",
                                  desc.name.c_str(), sd.name.c_str());
                    for (GLuint s : stages) glDeleteShader(s);
                    glDeleteProgram(program);
                    return nullptr;
                }
                {
                    shader = glCreateShader(type);
                    const char *src = reinterpret_cast<const char *>(sd.code);
                    GLint len = (GLint)sd.code_size;
                    glShaderSource(shader, 1, &src, &len);
                    glCompileShader(shader);
                    GLint ok = 0;
                    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
                    if (!ok)
                    {
                        GLint log_len = 0;
                        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
                        std::string log((size_t)std::max(1, log_len), '\0');
                        glGetShaderInfoLog(shader, log_len, nullptr, &log[0]);
                        GEN_LOG_ERROR("[GL] pipeline '%s': stage compile failed:\n%s", desc.name.c_str(), log.c_str());
                        glDeleteShader(shader);
                        for (GLuint s : stages) glDeleteShader(s);
                        glDeleteProgram(program);
                        return nullptr;
                    }
                    stages.push_back(shader);
                    glAttachShader(program, shader);
                }
            }

            // Bind vertex attribute locations before linking so that the
            // location numbers in the RHI vertex layout always match.
            if (desc.vertex_layout.valid())
            {
                for (const auto &attr : desc.vertex_layout.attributes)
                {
                    // Names are not known here; shaders use explicit
                    // layout(location=N) in attributes, which GL honours.
                    (void)attr;
                }
            }

            glLinkProgram(program);
            GLint ok = 0;
            glGetProgramiv(program, GL_LINK_STATUS, &ok);
            if (!ok)
            {
                GLint log_len = 0;
                glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
                std::string log((size_t)std::max(1, log_len), '\0');
                glGetProgramInfoLog(program, log_len, nullptr, &log[0]);
                GEN_LOG_ERROR("[GL] pipeline '%s': link failed:\n%s", desc.name.c_str(), log.c_str());
                for (GLuint s : stages) glDeleteShader(s);
                glDeleteProgram(program);
                return nullptr;
            }
            for (GLuint s : stages)
            {
                glDetachShader(program, s);
                glDeleteShader(s);
            }

            GLPipeline *pipeline = new GLPipeline(program, desc);
            pipeline->bindUniformBlocks();
            pipeline->bindStorageBlocks();
            pipeline->bindSamplerUniforms();
            pipelines_.push_back(pipeline);
            return pipeline;
        }

        RHIRenderPass *GLDevice::createRenderPass(const RenderPassDesc &desc)
        {
            uint64_t hash = hashRenderPassDesc(desc);
            auto it = render_passes_.find(hash);
            if (it != render_passes_.end()) return it->second;
            GLRenderPass *pass = new GLRenderPass(desc, hash);
            render_passes_[hash] = pass;
            return pass;
        }

        RHIFramebuffer *GLDevice::createFramebuffer(const FramebufferDesc &desc)
        {
            RHIRenderPass *pass = createRenderPass(desc.render_pass);
            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);

            std::vector<GLenum> draw_buffers;
            uint32_t color_index = 0;
            for (size_t i = 0; i < desc.attachments.size(); ++i)
            {
                const FramebufferDesc::Attachment &a = desc.attachments[i];
                GLTexture *tex = static_cast<GLTexture *>(a.texture);
                if (!tex) continue;
                const AttachmentDesc &ad = desc.render_pass.attachments[(uint32_t)i];
                GLenum target = tex->target();
                if (ad.is_depth_stencil)
                {
                    if (tex->format() == PixelFormat::D24_UNORM_S8_UINT ||
                        tex->format() == PixelFormat::D32_SFLOAT_S8_UINT)
                    {
                        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, target, tex->id(), a.mip_level);
                    }
                    else
                    {
                        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, target, tex->id(), a.mip_level);
                    }
                }
                else
                {
                    GLenum attachment = GL_COLOR_ATTACHMENT0 + color_index;
                    if (target == GL_TEXTURE_CUBE_MAP)
                    {
                        glFramebufferTexture2D(GL_FRAMEBUFFER, attachment,
                                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + (a.array_layer % 6),
                                               tex->id(), a.mip_level);
                    }
                    else
                    {
                        glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, target, tex->id(), a.mip_level);
                    }
                    draw_buffers.push_back(attachment);
                    ++color_index;
                }
            }
            if (!draw_buffers.empty()) glDrawBuffers((GLsizei)draw_buffers.size(), draw_buffers.data());
            else glDrawBuffer(GL_NONE);
            if (color_index == 0) glReadBuffer(GL_NONE);

            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            if (status != GL_FRAMEBUFFER_COMPLETE)
            {
                GEN_LOG_ERROR("[GL] framebuffer '%s' incomplete: 0x%04x", desc.name.c_str(), status);
                glDeleteFramebuffers(1, &fbo);
                return nullptr;
            }

            GLFramebuffer *fb = new GLFramebuffer(fbo, static_cast<GLRenderPass *>(pass), desc.width, desc.height, desc.name, false);
            for (size_t i = 0; i < desc.attachments.size(); ++i)
                if (desc.attachments[i].texture && !desc.render_pass.attachments[(uint32_t)i].is_depth_stencil)
                    fb->addColorAttachment(static_cast<GLTexture *>(desc.attachments[i].texture)->id());
            framebuffers_.push_back(fb);
            return fb;
        }

        RHIDescriptorSet *GLDevice::createDescriptorSet(RHIResourceLayout *layout, uint32_t set_index)
        {
            GLDescriptorSet *set = new GLDescriptorSet(static_cast<GLResourceLayout *>(layout), set_index);
            descriptor_sets_.push_back(set);
            return set;
        }

        RHICommandBuffer *GLDevice::createCommandBuffer(CommandBufferUsage usage)
        {
            GLCommandBuffer *cb = new GLCommandBuffer(this);
            (void)usage;
            command_buffers_.push_back(cb);
            return cb;
        }

        //==================================================================
        // Destruction helpers
        //==================================================================
        template <typename T, typename Container>
        static void eraseAndDelete(Container &c, T *p)
        {
            // Only delete objects that are still registered: this turns an
            // accidental double destroy into a no-op instead of undefined
            // behaviour.
            auto it = std::find(c.begin(), c.end(), p);
            if (it == c.end()) return;
            c.erase(it);
            delete p;
        }

        void GLDevice::destroyBuffer(RHIBuffer *p) { if (p) eraseAndDelete(buffers_, static_cast<GLBuffer *>(p)); }
        void GLDevice::destroyTexture(RHITexture *p) { if (p) eraseAndDelete(textures_, static_cast<GLTexture *>(p)); }
        void GLDevice::destroySampler(RHISampler *p) { if (p) eraseAndDelete(samplers_, static_cast<GLSampler *>(p)); }
        void GLDevice::destroyShader(RHIShader *p) { if (p) eraseAndDelete(shaders_, static_cast<GLShader *>(p)); }
        void GLDevice::destroyResourceLayout(RHIResourceLayout *p) { if (p) eraseAndDelete(layouts_, static_cast<GLResourceLayout *>(p)); }
        void GLDevice::destroyPipeline(RHIPipeline *p) { if (p) eraseAndDelete(pipelines_, static_cast<GLPipeline *>(p)); }
        void GLDevice::destroyFramebuffer(RHIFramebuffer *p) { if (p) eraseAndDelete(framebuffers_, static_cast<GLFramebuffer *>(p)); }
        void GLDevice::destroyDescriptorSet(RHIDescriptorSet *p) { if (p) eraseAndDelete(descriptor_sets_, static_cast<GLDescriptorSet *>(p)); }
        void GLDevice::destroyCommandBuffer(RHICommandBuffer *p) { if (p) eraseAndDelete(command_buffers_, static_cast<GLCommandBuffer *>(p)); }

        void GLDevice::destroyRenderPass(RHIRenderPass *p)
        {
            if (!p) return;
            GLRenderPass *pass = static_cast<GLRenderPass *>(p);
            auto it = render_passes_.find(pass->hash());
            if (it != render_passes_.end()) render_passes_.erase(it);
            delete pass;
        }

        //==================================================================
        // Uploads
        //==================================================================
        void GLDevice::writeBuffer(RHIBuffer *buffer, const void *data, uint64_t size, uint64_t offset)
        {
            GLBuffer *b = static_cast<GLBuffer *>(buffer);
            if (!b || !data || !size) return;
            if (offset + size > b->size())
            {
                GEN_LOG_ERROR("[GL] writeBuffer('%s') out of range (%llu + %llu > %llu)", b->name().c_str(),
                              (unsigned long long)offset, (unsigned long long)size, (unsigned long long)b->size());
                return;
            }
            glBindBuffer(GL_COPY_WRITE_BUFFER, b->id());
            glBufferSubData(GL_COPY_WRITE_BUFFER, (GLintptr)offset, (GLsizeiptr)size, data);
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        }

        void GLDevice::writeTexture(RHITexture *texture, const void *data, uint64_t size,
                                    uint32_t width, uint32_t height,
                                    uint32_t mip, uint32_t base_layer, uint32_t layer_count)
        {
            GLTexture *t = static_cast<GLTexture *>(texture);
            if (!t) return;
            GLFormatMapping fmt = glPixelFormatMapping(t->format());
            GLenum target = t->target();
            glBindTexture(target, t->id());
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

            uint32_t bpp = pixelFormatBytesPerPixel(t->format());
            if (target == GL_TEXTURE_CUBE_MAP)
            {
                for (uint32_t layer = 0; layer < layer_count; ++layer)
                {
                    uint32_t face_index = base_layer + layer;
                    GLenum face = GL_TEXTURE_CUBE_MAP_POSITIVE_X + (face_index % 6);
                    const uint8_t *src = reinterpret_cast<const uint8_t *>(data) + (size_t)layer * width * height * bpp;
                    glTexSubImage2D(face, (GLint)mip, 0, 0, (GLsizei)width, (GLsizei)height, fmt.format, fmt.type, src);
                }
            }
            else
            {
                for (uint32_t layer = 0; layer < layer_count; ++layer)
                {
                    const uint8_t *src = reinterpret_cast<const uint8_t *>(data) + (size_t)layer * width * height * bpp;
                    glTexSubImage2D(target, (GLint)mip, 0, 0, (GLsizei)width, (GLsizei)height, fmt.format, fmt.type, src);
                }
            }
            (void)size;
            glBindTexture(target, 0);
        }

        void GLDevice::submitImmediate(void (*record_fn)(RHICommandBuffer *, void *), void *user_data)
        {
            GLCommandBuffer cb(this);
            cb.begin(CommandBufferUsage::OneTimeSubmit);
            record_fn(&cb, user_data);
            cb.end();
            glFlush();
        }

        void GLDevice::submitAndWait(RHICommandBuffer *cmd)
        {
            // The GL command buffer executes while recording, so there is
            // nothing left to submit; a flush is enough to make the results
            // visible to subsequent readbacks.
            (void)cmd;
            glFlush();
            glFinish();
        }

        GLuint GLDevice::scratchFBO()
        {
            if (!scratch_fbo_) glGenFramebuffers(1, &scratch_fbo_);
            return scratch_fbo_;
        }

    } // namespace RHI
} // namespace Genesis
