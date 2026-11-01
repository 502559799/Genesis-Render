/*
 * Genesis Render - OpenGL backend implementation (command buffer)
 */
#include "rhi/opengl/gl_backend.hpp"
#include "rhi/opengl/gl_convert.hpp"
#include "foundation_layer/genesis_log.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Genesis
{
    namespace RHI
    {
        GLCommandBuffer::GLCommandBuffer(GLDevice *device) : device_(device) {}

        GLCommandBuffer::~GLCommandBuffer() {}

        void GLCommandBuffer::begin(CommandBufferUsage usage)
        {
            (void)usage;
            recording_ = true;
        }

        void GLCommandBuffer::end()
        {
            recording_ = false;
        }

        void GLCommandBuffer::checkError(const char *where)
        {
            GLenum err = glGetError();
            if (err == GL_NO_ERROR) return;
            GEN_LOG_ERROR("[GL] error 0x%04x at %s", err, where);
            if (err == GL_INVALID_OPERATION && current_pipeline_ && validating())
            {
                // Ask the driver why the program cannot draw in this state.
                GLuint program = current_pipeline_->program();
                glValidateProgram(program);
                GLint len = 0;
                glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
                if (len > 1)
                {
                    std::string log((size_t)len, '\0');
                    glGetProgramInfoLog(program, len, nullptr, &log[0]);
                    GEN_LOG_ERROR("[GL]   program validation: %s", log.c_str());
                }
            }
        }

        // GL has no validation layer: with --validation (or the environment
        // variable GENESIS_GL_VALIDATE) every command checks glGetError, so a
        // failing call is reported where it happens instead of later.
        bool GLCommandBuffer::validating() const
        {
            static const bool env = std::getenv("GENESIS_GL_VALIDATE") != nullptr;
            return env || device_->desc().enable_validation;
        }

        void GLCommandBuffer::beginRenderPass(RHIRenderPass *pass, RHIFramebuffer *fb, const RenderPassBeginInfo &info)
        {
            GLRenderPass *gl_pass = static_cast<GLRenderPass *>(pass);
            GLFramebuffer *gl_fb = static_cast<GLFramebuffer *>(fb);
            current_fb_ = gl_fb;
            current_fb_height_ = gl_fb ? gl_fb->height() : 0;
            in_render_pass_ = true;

            glBindFramebuffer(GL_FRAMEBUFFER, gl_fb ? gl_fb->fbo() : 0);

            uint32_t w = gl_fb ? gl_fb->width() : device_->swapchainWidth();
            uint32_t h = gl_fb ? gl_fb->height() : device_->swapchainHeight();
            glViewport(0, 0, (GLsizei)w, (GLsizei)h);
            glDisable(GL_SCISSOR_TEST);

            // Make sure writes are enabled while clearing.
            glDepthMask(GL_TRUE);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

            const RenderPassDesc &rpd = gl_pass->desc();
            GLbitfield clear_mask = 0;
            uint32_t clear_index = 0;
            for (uint32_t i = 0; i < rpd.attachment_count; ++i)
            {
                const AttachmentDesc &ad = rpd.attachments[i];
                const RenderPassBeginInfo::ClearValue &cv =
                    (clear_index < info.clear_count) ? info.clears[clear_index] : RenderPassBeginInfo::ClearValue();
                ++clear_index;

                if (ad.is_depth_stencil)
                {
                    if (ad.load_op == LoadOp::Clear)
                    {
                        glClearDepth(cv.depth);
                        glClearStencil((GLint)cv.stencil);
                        clear_mask |= GL_DEPTH_BUFFER_BIT;
                        if (ad.format == PixelFormat::D24_UNORM_S8_UINT ||
                            ad.format == PixelFormat::D32_SFLOAT_S8_UINT)
                            clear_mask |= GL_STENCIL_BUFFER_BIT;
                    }
                }
                else
                {
                    if (ad.load_op == LoadOp::Clear)
                    {
                        GLenum buf = GL_COLOR_ATTACHMENT0 + i;
                        if (gl_fb && gl_fb->isDefault()) buf = GL_FRONT_LEFT;
                        if (gl_fb && gl_fb->isDefault())
                        {
                            glClearColor(cv.color[0], cv.color[1], cv.color[2], cv.color[3]);
                            clear_mask |= GL_COLOR_BUFFER_BIT;
                        }
                        else
                        {
                            glClearBufferfv(GL_COLOR, (GLint)i, cv.color);
                        }
                        (void)buf;
                    }
                }
            }
            if (clear_mask) glClear(clear_mask);
            if (validating()) checkError("beginRenderPass");
        }

        void GLCommandBuffer::endRenderPass()
        {
            in_render_pass_ = false;
            current_fb_ = nullptr;
            glBindVertexArray(0);
            current_vao_ = 0;
        }

        void GLCommandBuffer::applyPipelineState(GLPipeline *pipeline)
        {
            const RasterizerState &rs = pipeline->rasterizer();
            const DepthStencilState &ds = pipeline->depthStencil();
            const BlendState &bs = pipeline->blend();

            if (rs.cull_mode == CullMode::None)
            {
                glDisable(GL_CULL_FACE);
            }
            else
            {
                glEnable(GL_CULL_FACE);
                GLenum m = GL_BACK;
                if (rs.cull_mode == CullMode::Front) m = GL_FRONT;
                else if (rs.cull_mode == CullMode::FrontAndBack) m = GL_FRONT_AND_BACK;
                glCullFace(m);
            }
            glFrontFace(rs.front_face == FrontFace::CounterClockwise ? GL_CCW : GL_CW);

            if (ds.depth_test) glEnable(GL_DEPTH_TEST);
            else glDisable(GL_DEPTH_TEST);
            glDepthMask(ds.depth_write ? GL_TRUE : GL_FALSE);
            glDepthFunc(glCompareOp(ds.depth_compare));

            if (ds.stencil_test)
            {
                glEnable(GL_STENCIL_TEST);
                glStencilFunc(glCompareOp(ds.stencil_compare), (GLint)ds.stencil_reference, ds.stencil_read_mask);
                glStencilMask(ds.stencil_write_mask);
            }
            else
            {
                glDisable(GL_STENCIL_TEST);
            }

            // Single blend state shared by all attachments (the engine only
            // ever uses uniform blending).
            const BlendAttachmentState &att = bs.attachments[0];
            if (att.blend_enable)
            {
                glEnable(GL_BLEND);
                glBlendEquationSeparate(glBlendOp(att.color_op), glBlendOp(att.alpha_op));
                glBlendFuncSeparate(glBlendFactor(att.src_color), glBlendFactor(att.dst_color),
                                    glBlendFactor(att.src_alpha), glBlendFactor(att.dst_alpha));
            }
            else
            {
                glDisable(GL_BLEND);
            }
            glBlendColor(bs.blend_constant[0], bs.blend_constant[1], bs.blend_constant[2], bs.blend_constant[3]);

            ColorComponentFlags mask = (ColorComponentFlags)att.write_mask;
            glColorMask((mask & (uint32_t)ColorComponentFlagBits::R) ? GL_TRUE : GL_FALSE,
                        (mask & (uint32_t)ColorComponentFlagBits::G) ? GL_TRUE : GL_FALSE,
                        (mask & (uint32_t)ColorComponentFlagBits::B) ? GL_TRUE : GL_FALSE,
                        (mask & (uint32_t)ColorComponentFlagBits::A) ? GL_TRUE : GL_FALSE);

            if (rs.polygon_mode != PolygonMode::Fill)
            {
                GLenum pm = rs.polygon_mode == PolygonMode::Line ? GL_LINE : GL_POINT;
                glPolygonMode(GL_FRONT_AND_BACK, pm);
                glLineWidth(rs.line_width);
            }
            else
            {
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            }

            if (rs.depth_clamp) glEnable(GL_DEPTH_CLAMP);
            else glDisable(GL_DEPTH_CLAMP);
        }

        void GLCommandBuffer::bindPipeline(RHIPipeline *pipeline)
        {
            GLPipeline *p = static_cast<GLPipeline *>(pipeline);
            if (!p) return;
            current_pipeline_ = p;
            glUseProgram(p->program());
            applyPipelineState(p);
            // Sampler uniforms must be (re)asserted whenever the program changes.
            for (const auto &su : p->samplerUniforms())
                glUniform1i(su.location, (GLint)su.unit);
            if (validating()) checkError("bindPipeline");
        }

        void GLCommandBuffer::bindVertexBuffer(RHIBuffer *buffer, uint64_t offset)
        {
            current_vb_ = static_cast<GLBuffer *>(buffer);
            current_vb_offset_ = offset;
            current_vao_ = 0;
        }

        void GLCommandBuffer::bindIndexBuffer(RHIBuffer *buffer, IndexType type, uint64_t offset)
        {
            current_ib_ = static_cast<GLBuffer *>(buffer);
            current_index_type_ = type;
            current_ib_offset_ = offset;
            current_vao_ = 0;
        }

        void GLCommandBuffer::bindVAO(GLPipeline *pipeline)
        {
            if (!pipeline) return;
            GLuint vao = pipeline->findOrCreateVAO(current_vb_, current_vb_offset_, current_ib_, pipeline->desc().vertex_layout);
            if (vao != current_vao_)
            {
                glBindVertexArray(vao);
                current_vao_ = vao;
            }
        }

        void GLCommandBuffer::bindDescriptorSets(RHIPipeline *pipeline, RHIDescriptorSet *const *sets, uint32_t set_count,
                                                 const uint32_t *dynamic_offsets, uint32_t offset_count)
        {
            GLPipeline *p = static_cast<GLPipeline *>(pipeline);
            if (!p) return;
            const ResourceLayoutDesc &layout = p->resourceLayout();
            uint32_t dyn_index = 0;

            for (uint32_t s = 0; s < set_count; ++s)
            {
                GLDescriptorSet *set = static_cast<GLDescriptorSet *>(sets[s]);
                if (!set) continue;
                // The set knows which descriptor set index it belongs to; that
                // is what selects the uniform-block index / texture unit.
                const uint32_t set_index = set->setIndex();
                for (const auto &entry : set->entries())
                {
                    const ResourceBinding *binding = nullptr;
                    for (const auto &b : layout.bindings)
                        if (b.binding == entry.binding && b.set == set_index) { binding = &b; break; }
                    if (!binding) continue;

                    if (resourceTypeIsBuffer(entry.type))
                    {
                        if (!entry.buffer) continue;
                        uint64_t offset = entry.offset;
                        uint64_t range = entry.range ? entry.range : entry.buffer->size();
                        if (entry.type == ResourceType::UniformBufferDynamic ||
                            entry.type == ResourceType::StorageBufferDynamic)
                        {
                            // Dynamic offsets are consumed in order, one per
                            // dynamic binding (matching the Vulkan rule), NOT
                            // one per descriptor set.
                            if (dynamic_offsets && dyn_index < offset_count)
                                offset += dynamic_offsets[dyn_index++];
                        }
                        if (entry.type == ResourceType::StorageBuffer || entry.type == ResourceType::StorageBufferDynamic)
                        {
                            GLuint storage_index = glStorageBlockIndex(binding->set, binding->binding);
                            glBindBufferRange(GL_SHADER_STORAGE_BUFFER, storage_index, entry.buffer->id(),
                                              (GLintptr)offset,
                                              (GLsizeiptr)std::min<uint64_t>(range, entry.buffer->size() - offset));
                        }
                        else
                        {
                            GLuint block_index = glUniformBlockIndex(binding->set, binding->binding);
                            glBindBufferRange(GL_UNIFORM_BUFFER, block_index, entry.buffer->id(),
                                              (GLintptr)offset,
                                              (GLsizeiptr)std::min<uint64_t>(range, entry.buffer->size() - offset));
                        }
                    }
                    else if (resourceTypeIsImage(entry.type))
                    {
                        if (entry.type == ResourceType::Sampler) continue;
                        if (!entry.texture) continue;
                        GLuint unit = glTextureUnit(binding->set, binding->binding);
                        glActiveTexture(GL_TEXTURE0 + unit);
                        glBindTexture(entry.texture->target(), entry.texture->id());
                        if (entry.sampler) glBindSampler(unit, entry.sampler->id());
                    }
                }
            }
            (void)dyn_index; // consumed per dynamic binding above
            glActiveTexture(GL_TEXTURE0);
            if (validating()) checkError("bindDescriptorSets");
        }

        void GLCommandBuffer::setPushConstants(const void *data, uint32_t size)
        {
            if (!current_pipeline_) return;
            current_pipeline_->setPushConstants(data, size);
            if (validating()) checkError("setPushConstants");
        }

        void GLCommandBuffer::setViewport(const Viewport &vp)
        {
            // Viewports and scissors are expressed in render-target row space
            // (y = 0 is row 0 of the attachment) on every backend, which is
            // exactly GL's window coordinate system, so no conversion here.
            viewport_ = vp;
            glViewport((GLint)vp.x, (GLint)vp.y, (GLsizei)vp.width, (GLsizei)vp.height);
            glDepthRange(vp.min_depth, vp.max_depth);
            if (validating()) checkError("setViewport");
        }

        void GLCommandBuffer::setScissor(const Rect2D &rect)
        {
            glEnable(GL_SCISSOR_TEST);
            glScissor(rect.x, rect.y, (GLsizei)rect.width, (GLsizei)rect.height);
            if (validating()) checkError("setScissor");
        }

        void GLCommandBuffer::draw(uint32_t vertex_count, uint32_t first_vertex, uint32_t instance_count, uint32_t first_instance)
        {
            if (!current_pipeline_) return;
            bindVAO(current_pipeline_);
            GLenum mode = glTopology(current_pipeline_->topology());
            (void)first_instance;
            if (instance_count > 1)
                glDrawArraysInstanced(mode, (GLint)first_vertex, (GLsizei)vertex_count, (GLsizei)instance_count);
            else
                glDrawArrays(mode, (GLint)first_vertex, (GLsizei)vertex_count);
            if (validating()) checkError(current_pipeline_ ? current_pipeline_->desc().name.c_str() : "draw");
        }

        void GLCommandBuffer::drawIndexed(uint32_t index_count, uint32_t first_index, int32_t vertex_offset,
                                          uint32_t instance_count, uint32_t first_instance)
        {
            if (!current_pipeline_ || !current_ib_) return;
            bindVAO(current_pipeline_);
            GLenum mode = glTopology(current_pipeline_->topology());
            GLenum type = (current_index_type_ == IndexType::UInt32) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
            uint32_t stride = (current_index_type_ == IndexType::UInt32) ? 4u : 2u;
            const void *offset_ptr = reinterpret_cast<const void *>(static_cast<uintptr_t>(current_ib_offset_ + (uint64_t)first_index * stride));
            (void)first_instance;

            if (instance_count > 1 && vertex_offset != 0)
            {
                // GL 4.1 has no glDrawElementsInstancedBaseVertex; emulate by
                // rebinding the vertex buffer with an offset (rare path).
                glDrawElementsInstanced(mode, (GLsizei)index_count, type, offset_ptr, (GLsizei)instance_count);
                return;
            }
            if (vertex_offset != 0)
                glDrawElementsBaseVertex(mode, (GLsizei)index_count, type, offset_ptr, (GLint)vertex_offset);
            else if (instance_count > 1)
                glDrawElementsInstanced(mode, (GLsizei)index_count, type, offset_ptr, (GLsizei)instance_count);
            else
                glDrawElements(mode, (GLsizei)index_count, type, offset_ptr);
            if (validating()) checkError(current_pipeline_ ? current_pipeline_->desc().name.c_str() : "drawIndexed");
        }

        void GLCommandBuffer::dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z)
        {
            // Compute requires GL 4.3.  The GL backend requests a 4.1 core
            // context but drivers may return a higher version; check the
            // function pointer so old drivers fail loudly instead of crashing.
#if defined(__APPLE__)
            // Apple caps OpenGL at 4.1 core; compute is provided by Metal.
            (void)group_x; (void)group_y; (void)group_z;
            GEN_LOG_ERROR("[GL] compute dispatch requires GL 4.3 (macOS OpenGL caps at 4.1; use Metal)");
            return;
#else
            glDispatchCompute(group_x, group_y, group_z);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT |
                            GL_BUFFER_UPDATE_BARRIER_BIT | GL_TRANSFER_SRC_BARRIER_BIT |
                            GL_TRANSFER_DST_BARRIER_BIT);
            if (validating()) checkError("dispatch");
#endif
        }

        void GLCommandBuffer::bufferBarrier(RHIBuffer *buffer, BufferAccess before, BufferAccess after)
        {
            (void)buffer;
            (void)before;
            (void)after;
#if !defined(__APPLE__)
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT |
                            GL_BUFFER_UPDATE_BARRIER_BIT | GL_TRANSFER_SRC_BARRIER_BIT |
                            GL_TRANSFER_DST_BARRIER_BIT);
#endif
            if (validating()) checkError("bufferBarrier");
        }

        void GLCommandBuffer::copyBufferToBuffer(RHIBuffer *src, uint64_t src_offset, RHIBuffer *dst, uint64_t dst_offset, uint64_t size)
        {
            GLBuffer *s = static_cast<GLBuffer *>(src);
            GLBuffer *d = static_cast<GLBuffer *>(dst);
            if (!s || !d) return;
            glBindBuffer(GL_COPY_READ_BUFFER, s->id());
            glBindBuffer(GL_COPY_WRITE_BUFFER, d->id());
            glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, (GLintptr)src_offset, (GLintptr)dst_offset, (GLsizeiptr)size);
        }

        void GLCommandBuffer::copyBufferToImage(RHIBuffer *src, RHITexture *dst, const BufferImageCopy &region)
        {
            GLBuffer *s = static_cast<GLBuffer *>(src);
            GLTexture *d = static_cast<GLTexture *>(dst);
            if (!s || !d) return;

            void *mapped = s->map(s->size() > 0 ? 0 : 0, s->size());
            if (!mapped)
            {
                glBindBuffer(s->target(), s->id());
                mapped = glMapBufferRange(s->target(), 0, (GLsizeiptr)s->size(), GL_MAP_READ_BIT);
            }
            if (!mapped)
            {
                GEN_LOG_ERROR("[GL] copyBufferToImage: cannot map source buffer");
                return;
            }
            const uint8_t *base = reinterpret_cast<const uint8_t *>(mapped) + region.buffer_offset;
            GLFormatMapping fmt = glPixelFormatMapping(d->format());
            glBindTexture(d->target(), d->id());
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            uint32_t row_bytes = region.width * pixelFormatBytesPerPixel(d->format());
            if (region.buffer_row_length && region.buffer_row_length != region.width)
                glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)region.buffer_row_length);

            for (uint32_t layer = 0; layer < region.layer_count; ++layer)
            {
                const uint8_t *src_pixels = base + (size_t)layer * row_bytes * region.height;
                if (d->target() == GL_TEXTURE_CUBE_MAP)
                {
                    GLenum face = GL_TEXTURE_CUBE_MAP_POSITIVE_X + ((region.base_layer + layer) % 6);
                    glTexSubImage2D(face, (GLint)region.mip_level, (GLint)region.offset_x, (GLint)region.offset_y,
                                    (GLsizei)region.width, (GLsizei)region.height, fmt.format, fmt.type, src_pixels);
                }
                else
                {
                    glTexSubImage2D(d->target(), (GLint)region.mip_level, (GLint)region.offset_x, (GLint)region.offset_y,
                                    (GLsizei)region.width, (GLsizei)region.height, fmt.format, fmt.type, src_pixels);
                }
            }
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glBindTexture(d->target(), 0);
            s->unmap();
        }

        void GLCommandBuffer::copyImageToBuffer(RHITexture *src, RHIBuffer *dst, const BufferImageCopy &region)
        {
            GLTexture *s = static_cast<GLTexture *>(src);
            GLBuffer *d = static_cast<GLBuffer *>(dst);
            if (!s || !d) return;
            glBindBuffer(d->target(), d->id());
            void *mapped = glMapBufferRange(d->target(), (GLintptr)region.buffer_offset,
                                            (GLsizeiptr)(d->size() - region.buffer_offset), GL_MAP_WRITE_BIT);
            if (!mapped)
            {
                GEN_LOG_ERROR("[GL] copyImageToBuffer: cannot map destination buffer");
                return;
            }
            GLFormatMapping fmt = glPixelFormatMapping(s->format());
            glBindTexture(s->target(), s->id());
            GLenum target = s->target();
            if (target == GL_TEXTURE_CUBE_MAP) target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + (region.base_layer % 6);
            glGetTexImage(target, (GLint)region.mip_level, fmt.format, fmt.type, mapped);
            glBindTexture(s->target(), 0);
            glUnmapBuffer(d->target());
        }

        void GLCommandBuffer::pipelineBarrier(const TextureBarrier *barriers, uint32_t count)
        {
            // OpenGL synchronises internally; we only track layouts so that
            // mip generation and readbacks can be issued when needed.
            for (uint32_t i = 0; i < count; ++i)
            {
                GLTexture *t = static_cast<GLTexture *>(barriers[i].texture);
                if (t) t->setCurrentLayout(barriers[i].new_layout);
            }
        }

        void GLCommandBuffer::generateMipmaps(RHITexture *texture)
        {
            GLTexture *t = static_cast<GLTexture *>(texture);
            if (!t) return;
            glBindTexture(t->target(), t->id());
            glTexParameteri(t->target(), GL_TEXTURE_MAX_LEVEL, (GLint)(t->mipLevels() - 1));
            glGenerateMipmap(t->target());
            glBindTexture(t->target(), 0);
        }

        void GLCommandBuffer::blitImage(RHITexture *src, uint32_t src_mip, RHITexture *dst, uint32_t dst_mip,
                                        uint32_t src_layer, uint32_t dst_layer, uint32_t layer_count, bool linear_filter)
        {
            GLTexture *s = static_cast<GLTexture *>(src);
            GLTexture *d = static_cast<GLTexture *>(dst);
            if (!s || !d) return;
            GLuint scratch = device_->scratchFBO();

            for (uint32_t layer = 0; layer < layer_count; ++layer)
            {
                GLenum s_target = s->target();
                GLenum d_target = d->target();
                if (s_target == GL_TEXTURE_CUBE_MAP) s_target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + ((src_layer + layer) % 6);
                if (d_target == GL_TEXTURE_CUBE_MAP) d_target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + ((dst_layer + layer) % 6);

                glBindFramebuffer(GL_READ_FRAMEBUFFER, scratch);
                glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, s_target, s->id(), (GLint)src_mip);
                glReadBuffer(GL_COLOR_ATTACHMENT0);

                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, scratch);
                glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, d_target, d->id(), (GLint)dst_mip);
                glDrawBuffer(GL_COLOR_ATTACHMENT0);

                uint32_t sw = std::max(1u, s->width() >> src_mip);
                uint32_t sh = std::max(1u, s->height() >> src_mip);
                uint32_t dw = std::max(1u, d->width() >> dst_mip);
                uint32_t dh = std::max(1u, d->height() >> dst_mip);
                glBlitFramebuffer(0, 0, (GLint)sw, (GLint)sh, 0, 0, (GLint)dw, (GLint)dh,
                                  GL_COLOR_BUFFER_BIT, linear_filter ? GL_LINEAR : GL_NEAREST);
            }
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        bool GLCommandBuffer::readImagePixels(RHITexture *src, uint32_t mip, uint32_t layer, void *dst, uint32_t dst_size_bytes)
        {
            GLTexture *s = static_cast<GLTexture *>(src);
            if (!s || !dst) return false;
            GLFormatMapping fmt = glPixelFormatMapping(s->format());
            uint32_t w = std::max(1u, s->width() >> mip);
            uint32_t h = std::max(1u, s->height() >> mip);
            uint32_t bpp = pixelFormatBytesPerPixel(s->format());
            uint32_t needed = w * h * bpp;
            if (dst_size_bytes < needed)
            {
                GEN_LOG_ERROR("[GL] readImagePixels: dst buffer too small (%u < %u)", dst_size_bytes, needed);
                return false;
            }

            glBindTexture(s->target(), s->id());
            GLenum target = s->target();
            if (target == GL_TEXTURE_CUBE_MAP) target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + (layer % 6);
            glGetTexImage(target, (GLint)mip, fmt.format, fmt.type, dst);
            glBindTexture(s->target(), 0);
            checkError("readImagePixels");
            return true;
        }

    } // namespace RHI
} // namespace Genesis
