/*
 * @Descripttion: OpenGL (core profile) 后端渲染设备实现
 */
#include "rhi_layer/opengl/opengl_device.hpp"
#include "rhi_layer/opengl/gl_loader.hpp"

#include "foundation_layer/log.hpp"
#include "foundation_layer/assert.hpp"

#include <SDL.h>
#include <cstring>

#include "external/stb_image_write.h"

namespace Genesis
{
    namespace rhi
    {
        //-----------------------------------------------------------
        // 类型转换
        //-----------------------------------------------------------
        static void toGLTextureFormat(Format format, GLint &internal, GLenum &upload_format, GLenum &upload_type)
        {
            switch (format)
            {
            case Format::R8_UNorm: internal = GL_R8; upload_format = GL_RED; upload_type = GL_UNSIGNED_BYTE; break;
            case Format::RG8_UNorm: internal = GL_RG8; upload_format = GL_RG; upload_type = GL_UNSIGNED_BYTE; break;
            case Format::RGBA8_UNorm: internal = GL_RGBA8; upload_format = GL_RGBA; upload_type = GL_UNSIGNED_BYTE; break;
            case Format::BGRA8_UNorm: internal = GL_RGBA8; upload_format = GL_BGRA; upload_type = GL_UNSIGNED_BYTE; break;
            case Format::RGBA8_sRGB: internal = GL_SRGB8_ALPHA8; upload_format = GL_RGBA; upload_type = GL_UNSIGNED_BYTE; break;
            case Format::BGRA8_sRGB: internal = GL_SRGB8_ALPHA8; upload_format = GL_BGRA; upload_type = GL_UNSIGNED_BYTE; break;
            case Format::RG16_Float: internal = GL_RG16F; upload_format = GL_RG; upload_type = GL_HALF_FLOAT; break;
            case Format::RGBA16_Float: internal = GL_RGBA16F; upload_format = GL_RGBA; upload_type = GL_HALF_FLOAT; break;
            case Format::RGBA32_Float: internal = GL_RGBA32F; upload_format = GL_RGBA; upload_type = GL_FLOAT; break;
            case Format::D32_Float: internal = GL_DEPTH_COMPONENT32F; upload_format = GL_DEPTH_COMPONENT; upload_type = GL_FLOAT; break;
            case Format::D24_S8: internal = GL_DEPTH24_STENCIL8; upload_format = GL_DEPTH_COMPONENT; upload_type = GL_FLOAT; break;
            default: internal = GL_RGBA8; upload_format = GL_RGBA; upload_type = GL_UNSIGNED_BYTE; break;
            }
        }

        static GLenum toGLCompareOp(CompareOp op)
        {
            switch (op)
            {
            case CompareOp::Never: return GL_NEVER;
            case CompareOp::Less: return GL_LESS;
            case CompareOp::LessOrEqual: return GL_LEQUAL;
            case CompareOp::Greater: return GL_GREATER;
            case CompareOp::GreaterOrEqual: return GL_GEQUAL;
            case CompareOp::Equal: return GL_EQUAL;
            case CompareOp::Always: return GL_ALWAYS;
            default: return GL_LEQUAL;
            }
        }

        static GLint toGLFilter(Filter filter)
        {
            return filter == Filter::Linear ? GL_LINEAR : GL_NEAREST;
        }

        static GLint toGLAddressMode(AddressMode mode)
        {
            switch (mode)
            {
            case AddressMode::Repeat: return GL_REPEAT;
            case AddressMode::MirroredRepeat: return GL_MIRRORED_REPEAT;
            case AddressMode::ClampToEdge: return GL_CLAMP_TO_EDGE;
            default: return GL_REPEAT;
            }
        }

        static uint32_t glTextureTarget(TextureType type)
        {
            return type == TextureType::TextureCube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        }

        //-----------------------------------------------------------
        // 初始化
        //-----------------------------------------------------------
        bool OpenGLDevice::init(const DeviceCreateInfo &info)
        {
            GenesisPrint("[RHI:OpenGL] init device (%ux%u)\n", info.m_width, info.m_height);
            m_window = info.m_window;
            m_width = info.m_width;
            m_height = info.m_height;

            m_gl_context = SDL_GL_CreateContext((SDL_Window *)m_window);
            if (!m_gl_context)
            {
                GenesisPrint("[RHI:OpenGL] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
                return false;
            }
            SDL_GL_MakeCurrent((SDL_Window *)m_window, m_gl_context);
            SDL_GL_SetSwapInterval(1);

            if (!glLoadAll())
                return false;

            GenesisPrint("[RHI:OpenGL] vendor: %s\n", (const char *)glGetString(GL_VENDOR));
            GenesisPrint("[RHI:OpenGL] renderer: %s\n", (const char *)glGetString(GL_RENDERER));
            GenesisPrint("[RHI:OpenGL] version: %s\n", (const char *)glGetString(GL_VERSION));

            return true;
        }

        void OpenGLDevice::shutdown()
        {
            for (auto &pair : m_vao_cache)
                glDeleteVertexArrays(1, &pair.second);
            m_vao_cache.clear();

            for (auto &res : m_buffers.m_items)
                if (res.m_id)
                    glDeleteBuffers(1, &res.m_id);
            for (auto &res : m_textures.m_items)
                if (res.m_id)
                    glDeleteTextures(1, &res.m_id);
            for (auto &res : m_shaders.m_items)
                if (res.m_id)
                    glDeleteShader(res.m_id);
            for (auto &res : m_pipelines.m_items)
                if (res.m_program)
                    glDeleteProgram(res.m_program);
            for (auto &res : m_render_targets.m_items)
                if (res.m_fbo)
                    glDeleteFramebuffers(1, &res.m_fbo);

            if (m_gl_context)
            {
                SDL_GL_DeleteContext(m_gl_context);
                m_gl_context = nullptr;
            }
            GenesisPrint("[RHI:OpenGL] shutdown complete\n");
        }

        //-----------------------------------------------------------
        // Buffer
        //-----------------------------------------------------------
        BufferHandle OpenGLDevice::createBuffer(const BufferCreateInfo &info)
        {
            uint32_t idx = m_buffers.allocate();
            GLBufferRes &res = m_buffers.get(idx);
            res = {};
            glGenBuffers(1, &res.m_id);
            res.m_size = info.m_size;
            res.m_usage = info.m_usage;

            GLenum target = GL_ARRAY_BUFFER;
            if (info.m_usage & BufferUsage::Index)
                target = GL_ELEMENT_ARRAY_BUFFER;
            else if (info.m_usage & BufferUsage::Uniform)
                target = GL_UNIFORM_BUFFER;

            glBindBuffer(target, res.m_id);
            glBufferData(target, (GLsizeiptr)info.m_size, info.m_data, (info.m_usage & BufferUsage::Uniform) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
            glBindBuffer(target, 0);

            return {idx};
        }

        void OpenGLDevice::destroyBuffer(BufferHandle handle)
        {
            GLBufferRes &res = m_buffers.get(handle.m_handle);
            if (res.m_id)
                glDeleteBuffers(1, &res.m_id);
            res = {};
            m_buffers.release(handle.m_handle);
        }

        void OpenGLDevice::updateBuffer(BufferHandle handle, const void *data, uint64_t size, uint64_t offset)
        {
            GLBufferRes &res = m_buffers.get(handle.m_handle);
            GLenum target = (res.m_usage & BufferUsage::Uniform) ? GL_UNIFORM_BUFFER : GL_ARRAY_BUFFER;
            glBindBuffer(target, res.m_id);
            glBufferSubData(target, (GLintptr)offset, (GLsizeiptr)size, data);
            glBindBuffer(target, 0);
        }

        //-----------------------------------------------------------
        // Texture
        //-----------------------------------------------------------
        TextureHandle OpenGLDevice::createTexture(const TextureCreateInfo &info)
        {
            uint32_t idx = m_textures.allocate();
            GLTextureRes &res = m_textures.get(idx);
            res = {};
            res.m_info.m_type = info.m_type;
            res.m_info.m_format = info.m_format;
            res.m_info.m_width = info.m_width;
            res.m_info.m_height = info.m_height;
            res.m_info.m_depth = info.m_depth;
            res.m_info.m_mip_levels = info.m_mip_levels;
            res.m_info.m_array_layers = info.m_array_layers;
            res.m_render_target = info.m_render_target;

            GLenum target = glTextureTarget(info.m_type);
            glGenTextures(1, &res.m_id);
            glBindTexture(target, res.m_id);

            GLint internal;
            GLenum upload_format, upload_type;
            toGLTextureFormat(info.m_format, internal, upload_format, upload_type);

            bool is_cube = info.m_type == TextureType::TextureCube;
            uint32_t face_count = is_cube ? 6 : 1;

            for (uint32_t face = 0; face < face_count; ++face)
            {
                for (uint32_t mip = 0; mip < info.m_mip_levels; ++mip)
                {
                    uint32_t w = info.m_width >> mip;
                    uint32_t h = info.m_height >> mip;
                    w = w ? w : 1;
                    h = h ? h : 1;
                    GLenum face_target = is_cube ? (GL_TEXTURE_CUBE_MAP_POSITIVE_X + face) : GL_TEXTURE_2D;
                    glTexImage2D(face_target, mip, internal, w, h, 0, upload_format, upload_type, nullptr);
                }
            }

            // 默认采样参数 (会被 sampler 覆盖, 这里给一个安全默认)
            glTexParameteri(target, GL_TEXTURE_MIN_FILTER, info.m_mip_levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_REPEAT);
            // GL mipmap 完整性: mip 链不到 1x1 时必须限制 MAX_LEVEL (mesa 严格要求)
            glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, (GLint)(info.m_mip_levels - 1));

            glBindTexture(target, 0);

            if (info.m_data)
            {
                // 初始数据: 每层仅 mip 0 (mip 链由 generateMipmaps 生成)
                const uint8_t *ptr = (const uint8_t *)info.m_data;
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glBindTexture(target, res.m_id);
                for (uint32_t face = 0; face < face_count; ++face)
                {
                    GLenum face_target = is_cube ? (GL_TEXTURE_CUBE_MAP_POSITIVE_X + face) : GL_TEXTURE_2D;
                    glTexSubImage2D(face_target, 0, 0, 0, info.m_width, info.m_height, upload_format, upload_type, ptr);
                    ptr += (size_t)info.m_width * info.m_height * formatSize(info.m_format);
                }
                glBindTexture(target, 0);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            }

            return {idx};
        }

        void OpenGLDevice::destroyTexture(TextureHandle handle)
        {
            GLTextureRes &res = m_textures.get(handle.m_handle);
            if (res.m_id)
                glDeleteTextures(1, &res.m_id);
            res = {};
            m_textures.release(handle.m_handle);
        }

        void OpenGLDevice::updateTexture(TextureHandle handle, const void *data, uint64_t size, uint32_t mipLevel, uint32_t layer)
        {
            (void)size;
            GLTextureRes &res = m_textures.get(handle.m_handle);
            GLenum target = glTextureTarget(res.m_info.m_type);
            bool is_cube = res.m_info.m_type == TextureType::TextureCube;

            GLint internal;
            GLenum upload_format, upload_type;
            toGLTextureFormat(res.m_info.m_format, internal, upload_format, upload_type);

            uint32_t w = res.m_info.m_width >> mipLevel;
            uint32_t h = res.m_info.m_height >> mipLevel;
            w = w ? w : 1;
            h = h ? h : 1;

            GLenum face_target = is_cube ? (GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer) : GL_TEXTURE_2D;
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glBindTexture(target, res.m_id);
            glTexSubImage2D(face_target, mipLevel, 0, 0, w, h, upload_format, upload_type, data);
            glBindTexture(target, 0);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        }

        void OpenGLDevice::generateMipmaps(TextureHandle handle)
        {
            GLTextureRes &res = m_textures.get(handle.m_handle);
            GLenum target = glTextureTarget(res.m_info.m_type);
            glBindTexture(target, res.m_id);
            glGenerateMipmap(target);
            glBindTexture(target, 0);
        }

        TextureInfo OpenGLDevice::getTextureInfo(TextureHandle handle) const
        {
            return m_textures.get(handle.m_handle).m_info;
        }

        //-----------------------------------------------------------
        // Sampler
        //-----------------------------------------------------------
        SamplerHandle OpenGLDevice::createSampler(const SamplerCreateInfo &info)
        {
            uint32_t idx = m_samplers.allocate();
            m_samplers.get(idx).m_info = info;
            return {idx};
        }

        void OpenGLDevice::destroySampler(SamplerHandle handle)
        {
            m_samplers.get(handle.m_handle).m_info = {};
            m_samplers.release(handle.m_handle);
        }

        void OpenGLDevice::applySamplerToTexture(uint32_t texture_target, const SamplerCreateInfo &info)
        {
            GLint min_filter;
            if (info.m_mipmap_filter == Filter::Linear && info.m_min_filter == Filter::Linear)
                min_filter = GL_LINEAR_MIPMAP_LINEAR;
            else if (info.m_min_filter == Filter::Linear)
                min_filter = GL_LINEAR;
            else
                min_filter = GL_NEAREST;
            glTexParameteri(texture_target, GL_TEXTURE_MIN_FILTER, min_filter);
            glTexParameteri(texture_target, GL_TEXTURE_MAG_FILTER, toGLFilter(info.m_mag_filter));
            glTexParameteri(texture_target, GL_TEXTURE_WRAP_S, toGLAddressMode(info.m_address_u));
            glTexParameteri(texture_target, GL_TEXTURE_WRAP_T, toGLAddressMode(info.m_address_v));
            glTexParameteri(texture_target, GL_TEXTURE_WRAP_R, toGLAddressMode(info.m_address_w));
            glTexParameterf(texture_target, GL_TEXTURE_MAX_LOD, info.m_max_lod);
        }

        //-----------------------------------------------------------
        // Shader
        //-----------------------------------------------------------
        ShaderHandle OpenGLDevice::createShader(const ShaderCreateInfo &info)
        {
            GENESISASSERT_MSG(info.m_glsl != nullptr, "OpenGL shader requires GLSL source");

            GLenum stage = info.m_stage == ShaderStage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
            GLuint shader = glCreateShader(stage);

            // 组装源码: version + 宏 + 主体 (主体已由构建期展开 #include)
            std::string source =
#ifdef __APPLE__
                "#version 410 core\n"
#else
                "#version 420 core\n"
#endif
                "#define GENESIS_OPENGL 1\n";
            source += info.m_glsl;

            const GLchar *src_ptr = source.c_str();
            glShaderSource(shader, 1, &src_ptr, nullptr);
            glCompileShader(shader);

            GLint compiled = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
            if (!compiled)
            {
                char log[4096];
                glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
                GenesisPrint("[RHI:OpenGL] shader compile error (%s):\n%s\n", info.m_name ? info.m_name : "unnamed", log);
                glDeleteShader(shader);
                return {};
            }

            uint32_t idx = m_shaders.allocate();
            GLShaderRes &res = m_shaders.get(idx);
            res.m_id = shader;
            res.m_stage = info.m_stage;
            return {idx};
        }

        void OpenGLDevice::destroyShader(ShaderHandle handle)
        {
            GLShaderRes &res = m_shaders.get(handle.m_handle);
            if (res.m_id)
                glDeleteShader(res.m_id);
            res = {};
            m_shaders.release(handle.m_handle);
        }

        //-----------------------------------------------------------
        // Pipeline
        //-----------------------------------------------------------
        PipelineHandle OpenGLDevice::createPipeline(const PipelineCreateInfo &info)
        {
            GLShaderRes &vs = m_shaders.get(info.m_vs.m_handle);
            GLShaderRes &fs = m_shaders.get(info.m_fs.m_handle);

            GLuint program = glCreateProgram();
            glAttachShader(program, vs.m_id);
            glAttachShader(program, fs.m_id);
            glLinkProgram(program);

            GLint linked = GL_FALSE;
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
            if (!linked)
            {
                char log[4096];
                glGetProgramInfoLog(program, sizeof(log), nullptr, log);
                GenesisPrint("[RHI:OpenGL] program link error (%s):\n%s\n", info.m_name ? info.m_name : "unnamed", log);
                glDeleteProgram(program);
                return {};
            }

            uint32_t idx = m_pipelines.allocate();
            GLPipelineRes &res = m_pipelines.get(idx);
            res = {};
            res.m_program = program;
            res.m_info = info;

            // 解析全部 uniform 绑定 (按名字)
            for (uint32_t slot = 0; slot < info.m_set_layout_count; ++slot)
            {
                GLSetLayoutRes &layout_res = m_set_layouts.get(info.m_set_layouts[slot].m_handle);
                for (uint32_t b = 0; b < layout_res.m_info.m_binding_count; ++b)
                {
                    const DescriptorBinding &binding = layout_res.m_info.m_bindings[b];
                    GLUniformBinding ub{};
                    ub.m_slot = slot;
                    ub.m_binding = binding.m_binding;
                    ub.m_type = binding.m_type;
                    if (binding.m_type == DescriptorType::UniformBuffer)
                    {
                        GLuint block_index = glGetUniformBlockIndex(program, binding.m_name);
                        ub.m_block_index = (int32_t)block_index;
                    }
                    else
                    {
                        ub.m_sampler_location = glGetUniformLocation(program, binding.m_name);
                    }
                    res.m_uniform_bindings.push_back(ub);
                }
            }

            return {idx};
        }

        void OpenGLDevice::destroyPipeline(PipelineHandle handle)
        {
            GLPipelineRes &res = m_pipelines.get(handle.m_handle);
            if (res.m_program)
                glDeleteProgram(res.m_program);
            res = {};
            m_pipelines.release(handle.m_handle);
        }

        //-----------------------------------------------------------
        // Descriptor
        //-----------------------------------------------------------
        DescriptorSetLayoutHandle OpenGLDevice::createDescriptorSetLayout(const DescriptorSetLayoutCreateInfo &info)
        {
            uint32_t idx = m_set_layouts.allocate();
            m_set_layouts.get(idx).m_info = info;
            return {idx};
        }

        void OpenGLDevice::destroyDescriptorSetLayout(DescriptorSetLayoutHandle handle)
        {
            m_set_layouts.get(handle.m_handle).m_info = {};
            m_set_layouts.release(handle.m_handle);
        }

        DescriptorSetHandle OpenGLDevice::createDescriptorSet(const DescriptorSetCreateInfo &info)
        {
            uint32_t idx = m_sets.allocate();
            GLSetRes &res = m_sets.get(idx);
            res = {};
            res.m_layout = info.m_layout;

            GLSetLayoutRes &layout_res = m_set_layouts.get(info.m_layout.m_handle);

            for (uint32_t i = 0; i < info.m_write_count; ++i)
            {
                const DescriptorWrite &w = info.m_writes[i];
                GLSetWriteRes wr{};
                wr.m_binding = w.m_binding;
                wr.m_buffer = w.m_buffer;
                wr.m_buffer_offset = w.m_buffer_offset;
                wr.m_buffer_size = w.m_buffer_size;
                wr.m_texture = w.m_texture;
                wr.m_sampler = w.m_sampler;
                // 查 layout 中类型
                for (uint32_t j = 0; j < layout_res.m_info.m_binding_count; ++j)
                {
                    if (layout_res.m_info.m_bindings[j].m_binding == w.m_binding)
                    {
                        wr.m_type = layout_res.m_info.m_bindings[j].m_type;
                        break;
                    }
                }
                res.m_writes.push_back(wr);
            }
            return {idx};
        }

        void OpenGLDevice::destroyDescriptorSet(DescriptorSetHandle handle)
        {
            m_sets.get(handle.m_handle) = {};
            m_sets.release(handle.m_handle);
        }

        //-----------------------------------------------------------
        // RenderTarget
        //-----------------------------------------------------------
        RenderTargetHandle OpenGLDevice::createRenderTarget(const RenderTargetCreateInfo &info)
        {
            uint32_t idx = m_render_targets.allocate();
            GLRenderTargetRes &res = m_render_targets.get(idx);
            res = {};

            glGenFramebuffers(1, &res.m_fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, res.m_fbo);

            RenderTargetLayout layout{};
            layout.m_color_count = info.m_color_count;

            uint32_t width = 0, height = 0;
            for (uint32_t i = 0; i < info.m_color_count; ++i)
            {
                GLTextureRes &tex = m_textures.get(info.m_colors[i].m_texture.m_handle);
                layout.m_color_formats[i] = tex.m_info.m_format;
                uint32_t mip = info.m_colors[i].m_mip_level;
                uint32_t w = tex.m_info.m_width >> mip;
                uint32_t h = tex.m_info.m_height >> mip;
                width = w ? w : 1;
                height = h ? h : 1;

                if (tex.m_info.m_type == TextureType::TextureCube)
                {
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_CUBE_MAP_POSITIVE_X + info.m_colors[i].m_layer, tex.m_id, mip);
                }
                else
                {
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, tex.m_id, mip);
                }
            }

            if (info.m_color_count > 0)
            {
                GLenum draw_buffers[kMaxColorAttachments];
                for (uint32_t i = 0; i < info.m_color_count; ++i)
                    draw_buffers[i] = GL_COLOR_ATTACHMENT0 + i;
                glDrawBuffers(info.m_color_count, draw_buffers);
            }

            if (info.m_has_depth)
            {
                GLTextureRes &tex = m_textures.get(info.m_depth.m_texture.m_handle);
                layout.m_depth_format = tex.m_info.m_format;
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tex.m_id, 0);
            }
            else
            {
                layout.m_depth_format = Format::Undefined;
            }

            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE)
            {
                GenesisPrint("[RHI:OpenGL] framebuffer incomplete: 0x%x (err=0x%x, colors=%u, mip=%u, layer=%u, w=%u h=%u)\n",
                             status, glGetError(), info.m_color_count, info.m_colors[0].m_mip_level, info.m_colors[0].m_layer, width, height);
            }
            GENESISASSERT_MSG(status == GL_FRAMEBUFFER_COMPLETE, "framebuffer incomplete: 0x%x", status);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            res.m_width = width;
            res.m_height = height;
            res.m_layout = layout;
            return {idx};
        }

        void OpenGLDevice::destroyRenderTarget(RenderTargetHandle handle)
        {
            GLRenderTargetRes &res = m_render_targets.get(handle.m_handle);
            if (res.m_fbo)
                glDeleteFramebuffers(1, &res.m_fbo);
            res = {};
            m_render_targets.release(handle.m_handle);
        }

        //-----------------------------------------------------------
        // Frame
        //-----------------------------------------------------------
        void OpenGLDevice::beginFrame()
        {
            // OpenGL 无 acquire 语义
        }

        void OpenGLDevice::doCapture()
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            std::vector<uint8_t> pixels((size_t)m_width * m_height * 4);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            glPixelStorei(GL_PACK_ALIGNMENT, 4);

            // GL 原点在左下, 垂直翻转
            std::vector<uint8_t> flipped(pixels.size());
            uint32_t row = (uint32_t)m_width * 4;
            for (uint32_t y = 0; y < m_height; ++y)
                memcpy(flipped.data() + y * row, pixels.data() + (m_height - 1 - y) * row, row);

            stbi_write_png(m_capture_path.c_str(), m_width, m_height, 4, flipped.data(), m_width * 4);
            GenesisPrint("[RHI:OpenGL] screenshot saved: %s\n", m_capture_path.c_str());
            m_capture_pending = false;
        }

        void OpenGLDevice::endFrame()
        {
            GENESISASSERT_MSG(!m_in_pass, "render pass still open at endFrame");
            if (m_capture_pending)
                doCapture();
            SDL_GL_SwapWindow((SDL_Window *)m_window);
        }

        void OpenGLDevice::beginCommands()
        {
            // OpenGL 命令立即执行, 无需显式命令缓冲
        }

        void OpenGLDevice::endCommands()
        {
            glFinish();
        }

        //-----------------------------------------------------------
        // Render pass
        //-----------------------------------------------------------
        void OpenGLDevice::beginRenderPass(RenderTargetHandle target, const float clearColor[4], float clearDepth, bool clear)
        {
            GENESISASSERT_MSG(!m_in_pass, "nested render pass");

            uint32_t width = m_width, height = m_height;
            RenderTargetLayout layout{};
            layout.m_color_count = 1;
            layout.m_color_formats[0] = Format::BGRA8_UNorm;
            layout.m_depth_format = Format::D32_Float;

            if (target.isSwapchain())
            {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
            }
            else
            {
                GLRenderTargetRes &rt = m_render_targets.get(target.m_handle);
                glBindFramebuffer(GL_FRAMEBUFFER, rt.m_fbo);
                width = rt.m_width;
                height = rt.m_height;
                layout = rt.m_layout;
            }

            glViewport(0, 0, width, height);
            glScissor(0, 0, width, height);

            if (clear)
            {
                GLbitfield bits = 0;
                if (layout.m_color_count > 0)
                {
                    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
                    bits |= GL_COLOR_BUFFER_BIT;
                }
                if (isDepthFormat(layout.m_depth_format))
                {
                    glDepthMask(GL_TRUE);
                    glClearDepth(clearDepth);
                    bits |= GL_DEPTH_BUFFER_BIT;
                }
                glClear(bits);
            }

            m_in_pass = true;
        }

        void OpenGLDevice::endRenderPass()
        {
            m_in_pass = false;
        }

        //-----------------------------------------------------------
        // 绑定与绘制
        //-----------------------------------------------------------
        uint32_t OpenGLDevice::getOrCreateVAO(uint32_t pipeline_idx)
        {
            GLPipelineRes &pipeline = m_pipelines.get(pipeline_idx);

            uint32_t vbo_idx = m_current_vbo.isValid() ? m_current_vbo.m_handle : 0xfffff;
            uint32_t ibo_idx = m_current_ibo.isValid() ? m_current_ibo.m_handle : 0xfffff;
            uint64_t key = ((uint64_t)pipeline_idx << 40) | ((uint64_t)vbo_idx << 20) | ibo_idx;

            auto it = m_vao_cache.find(key);
            if (it != m_vao_cache.end())
                return it->second;

            GLuint vao = 0;
            glGenVertexArrays(1, &vao);
            glBindVertexArray(vao);

            if (m_current_vbo.isValid() && pipeline.m_info.m_vertex_binding.m_stride > 0)
            {
                glBindBuffer(GL_ARRAY_BUFFER, m_buffers.get(m_current_vbo.m_handle).m_id);
                const VertexBinding &binding = pipeline.m_info.m_vertex_binding;
                for (uint32_t i = 0; i < binding.m_attribute_count; ++i)
                {
                    const VertexAttribute &attr = binding.m_attributes[i];
                    GLint components = 3;
                    switch (attr.m_format)
                    {
                    case VertexFormat::Float: components = 1; break;
                    case VertexFormat::Float2: components = 2; break;
                    case VertexFormat::Float3: components = 3; break;
                    case VertexFormat::Float4: components = 4; break;
                    }
                    glEnableVertexAttribArray(attr.m_location);
                    glVertexAttribPointer(attr.m_location, components, GL_FLOAT, GL_FALSE, binding.m_stride, (const void *)(uintptr_t)attr.m_offset);
                }
            }

            if (m_current_ibo.isValid())
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_buffers.get(m_current_ibo.m_handle).m_id);

            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            m_vao_cache[key] = vao;
            return vao;
        }

        void OpenGLDevice::bindPipeline(PipelineHandle pipeline)
        {
            m_current_pipeline = pipeline.m_handle;
            GLPipelineRes &res = m_pipelines.get(pipeline.m_handle);
            glUseProgram(res.m_program);

            const PipelineCreateInfo &info = res.m_info;

            if (info.m_cull_mode == CullMode::None)
            {
                glDisable(GL_CULL_FACE);
            }
            else
            {
                glEnable(GL_CULL_FACE);
                glCullFace(info.m_cull_mode == CullMode::Back ? GL_BACK : GL_FRONT);
            }
            glFrontFace(info.m_front_face == FrontFace::CounterClockwise ? GL_CCW : GL_CW);

            if (info.m_depth_test)
            {
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(toGLCompareOp(info.m_depth_compare));
            }
            else
            {
                glDisable(GL_DEPTH_TEST);
            }
            glDepthMask(info.m_depth_write ? GL_TRUE : GL_FALSE);

            if (info.m_blend_enable)
            {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }
            else
            {
                glDisable(GL_BLEND);
            }
        }

        void OpenGLDevice::bindDescriptorSet(uint32_t slot, DescriptorSetHandle set)
        {
            GLSetRes &res = m_sets.get(set.m_handle);
            GLPipelineRes &pipeline = m_pipelines.get(m_current_pipeline);

            for (const GLSetWriteRes &write : res.m_writes)
            {
                // 找到 pipeline 解析出的 uniform 位置
                const GLUniformBinding *ub = nullptr;
                for (auto &b : pipeline.m_uniform_bindings)
                {
                    if (b.m_slot == slot && b.m_binding == write.m_binding)
                    {
                        ub = &b;
                        break;
                    }
                }
                if (!ub)
                    continue;

                if (write.m_type == DescriptorType::UniformBuffer)
                {
                    if (ub->m_block_index < 0)
                        continue;
                    GLuint binding_point = slot * 8 + write.m_binding;
                    glUniformBlockBinding(pipeline.m_program, (GLuint)ub->m_block_index, binding_point);
                    glBindBufferBase(GL_UNIFORM_BUFFER, binding_point, m_buffers.get(write.m_buffer.m_handle).m_id);
                }
                else
                {
                    if (ub->m_sampler_location < 0 || !write.m_texture.isValid())
                        continue;
                    GLTextureRes &tex = m_textures.get(write.m_texture.m_handle);
                    GLenum target = glTextureTarget(tex.m_info.m_type);
                    GLuint unit = slot * 8 + write.m_binding;
                    glActiveTexture(GL_TEXTURE0 + unit);
                    glBindTexture(target, tex.m_id);
                    if (write.m_sampler.isValid())
                    {
                        applySamplerToTexture(target, m_samplers.get(write.m_sampler.m_handle).m_info);
                        // MAX_LOD 不得超过实际 mip 链, 否则采样不完整
                        float max_lod = (float)(tex.m_info.m_mip_levels - 1);
                        SamplerCreateInfo &si = m_samplers.get(write.m_sampler.m_handle).m_info;
                        glTexParameterf(target, GL_TEXTURE_MAX_LOD, si.m_max_lod < max_lod ? si.m_max_lod : max_lod);
                    }
                    glUniform1i(ub->m_sampler_location, (GLint)unit);
                }
            }
        }

        void OpenGLDevice::bindVertexBuffer(BufferHandle buffer, uint64_t offset)
        {
            (void)offset; // offset 由 VAO 属性内的偏移覆盖, demo 不使用非零偏移
            m_current_vbo = buffer;
        }

        void OpenGLDevice::bindIndexBuffer(BufferHandle buffer, uint64_t offset)
        {
            (void)offset;
            m_current_ibo = buffer;
        }

        void OpenGLDevice::draw(uint32_t vertexCount, uint32_t firstVertex)
        {
            uint32_t vao = getOrCreateVAO(m_current_pipeline);
            glBindVertexArray(vao);
            GLenum mode = m_pipelines.get(m_current_pipeline).m_info.m_topology == PrimitiveTopology::TriangleList ? GL_TRIANGLES : GL_LINES;
            glDrawArrays(mode, (GLint)firstVertex, (GLsizei)vertexCount);
            glBindVertexArray(0);
        }

        void OpenGLDevice::drawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset)
        {
            uint32_t vao = getOrCreateVAO(m_current_pipeline);
            glBindVertexArray(vao);
            GLenum mode = m_pipelines.get(m_current_pipeline).m_info.m_topology == PrimitiveTopology::TriangleList ? GL_TRIANGLES : GL_LINES;
            glDrawElementsBaseVertex(mode, (GLsizei)indexCount, GL_UNSIGNED_INT, (const void *)(uintptr_t)(firstIndex * 4), vertexOffset);
            glBindVertexArray(0);
        }

        //-----------------------------------------------------------
        // 回读
        //-----------------------------------------------------------
        bool OpenGLDevice::readbackTexture(TextureHandle handle, uint8_t *outRGBA, uint32_t rowPitch)
        {
            GLTextureRes &res = m_textures.get(handle.m_handle);
            if (res.m_info.m_type == TextureType::TextureCube)
                return false;

            GLuint fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, res.m_id, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glDeleteFramebuffers(1, &fbo);
                return false;
            }

            std::vector<uint8_t> pixels((size_t)res.m_info.m_width * res.m_info.m_height * 4);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, res.m_info.m_width, res.m_info.m_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            glPixelStorei(GL_PACK_ALIGNMENT, 4);

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);

            // 垂直翻转
            for (uint32_t y = 0; y < res.m_info.m_height; ++y)
            {
                memcpy(outRGBA + y * rowPitch,
                       pixels.data() + (res.m_info.m_height - 1 - y) * res.m_info.m_width * 4,
                       res.m_info.m_width * 4);
            }
            return true;
        }

        void OpenGLDevice::requestScreenCapture(const char *pngPath)
        {
            m_capture_path = pngPath;
            m_capture_pending = true;
        }

        bool OpenGLDevice::captureScreen(const char *pngPath)
        {
            requestScreenCapture(pngPath);
            return true;
        }

        void OpenGLDevice::waitIdle()
        {
            glFinish();
        }

    } // namespace rhi
} // namespace Genesis
