/*
 * Genesis Render - OpenGL backend (declarations)
 *
 * Implements the RHI on top of OpenGL 4.1 core profile.
 *
 * Notable semantic differences vs the Vulkan backend (documented, deliberate):
 *   - GLCommandBuffer executes immediately while recording.  `end()`/submit is
 *     therefore a no-op; ordering guarantees are identical for single-threaded
 *     render-thread usage, which is the only supported usage.
 *   - Texture layout barriers are tracked but produce no GL commands (the GL
 *     driver handles synchronisation), except for mip generation.
 *   - Descriptor sets are emulated: a GLDescriptorSet stores the written
 *     buffer/image references and applies them on bind via
 *     glBindBufferRange / glActiveTexture+glBindTexture+glBindSampler.
 *   - Push constants are emulated with a reserved uniform block (binding 0)
 *     backed by a small ring buffer, so shader code stays portable.
 */
#ifndef __GENESIS_GL_BACKEND_HPP__
#define __GENESIS_GL_BACKEND_HPP__

#include "rhi/rhi.hpp"
#include "rhi/opengl/gl_loader.hpp"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace Genesis
{
    namespace RHI
    {
        class GLDevice;

        //------------------------------------------------------------------
        struct GLFormatMapping
        {
            GLenum internal_format;
            GLenum format;
            GLenum type;
        };
        GLFormatMapping glPixelFormatMapping(PixelFormat format);
        GLenum glTextureTarget(TextureType type);

        //------------------------------------------------------------------
        class GLBuffer : public RHIBuffer
        {
        public:
            GLBuffer(GLuint id, const BufferDesc &desc);
            ~GLBuffer() override;

            uint64_t size() const override { return desc_.size; }
            BufferUsageFlags usage() const override { return desc_.usage; }
            MemoryUsage memoryUsage() const override { return desc_.memory_usage; }
            const std::string &name() const override { return desc_.name; }

            void *map(uint64_t offset = 0, uint64_t size = ~0ull) override;
            void unmap() override;
            void *nativeHandle() const override { return reinterpret_cast<void *>(static_cast<uintptr_t>(id_)); }

            GLuint id() const { return id_; }
            GLenum target() const { return target_; }
            bool cpuVisible() const { return desc_.memory_usage != MemoryUsage::GpuOnly; }
            void setCpuShadow(const void *data, uint64_t size, uint64_t offset);
            const std::vector<uint8_t> &shadow() const { return shadow_; }

        private:
            GLuint id_ = 0;
            GLenum target_ = GL_ARRAY_BUFFER;
            BufferDesc desc_;
            bool mapped_ = false;
            // GL 4.1 cannot map GPU-only buffers, and reading back uniform data
            // is sometimes needed for debugging; keep a CPU copy for
            // CPU-visible buffers.
            std::vector<uint8_t> shadow_;
        };

        //------------------------------------------------------------------
        class GLTexture : public RHITexture
        {
        public:
            GLTexture(GLuint id, const TextureDesc &desc);
            ~GLTexture() override;

            const TextureDesc &desc() const override { return desc_; }
            uint32_t width() const override { return desc_.width; }
            uint32_t height() const override { return desc_.height; }
            uint32_t mipLevels() const override { return desc_.mip_levels; }
            uint32_t layerCount() const override { return desc_.layerCount(); }
            PixelFormat format() const override { return desc_.format; }
            const std::string &name() const override { return desc_.name; }

            TextureLayout currentLayout() const override { return layout_; }
            void setCurrentLayout(TextureLayout layout) override { layout_ = layout; }

            void *nativeHandle() const override { return reinterpret_cast<void *>(static_cast<uintptr_t>(id_)); }
            void *nativeViewHandle() const override { return nativeHandle(); }

            GLuint id() const { return id_; }
            GLenum target() const { return target_; }

        private:
            GLuint id_ = 0;
            GLenum target_ = GL_TEXTURE_2D;
            TextureDesc desc_;
            TextureLayout layout_ = TextureLayout::Undefined;
        };

        //------------------------------------------------------------------
        class GLSampler : public RHISampler
        {
        public:
            GLSampler(GLuint id, const SamplerDesc &desc);
            ~GLSampler() override;
            const SamplerDesc &desc() const override { return desc_; }
            const std::string &name() const override { return desc_.name; }
            void *nativeHandle() const override { return reinterpret_cast<void *>(static_cast<uintptr_t>(id_)); }
            GLuint id() const { return id_; }

        private:
            GLuint id_ = 0;
            SamplerDesc desc_;
        };

        //------------------------------------------------------------------
        class GLShader : public RHIShader
        {
        public:
            GLShader(GLuint id, ShaderStageFlagBits stage, const std::string &name);
            ~GLShader() override;
            ShaderStageFlagBits stage() const override { return stage_; }
            const std::string &name() const override { return name_; }
            void *nativeHandle() const override { return reinterpret_cast<void *>(static_cast<uintptr_t>(id_)); }
            GLuint id() const { return id_; }

        private:
            GLuint id_ = 0;
            ShaderStageFlagBits stage_ = ShaderStageFlagBits::Vertex;
            std::string name_;
        };

        //------------------------------------------------------------------
        class GLResourceLayout : public RHIResourceLayout
        {
        public:
            explicit GLResourceLayout(const ResourceLayoutDesc &desc) : desc_(desc) {}
            ~GLResourceLayout() override {}
            const ResourceLayoutDesc &desc() const override { return desc_; }
            const ResourceBinding *findBinding(uint32_t set, uint32_t binding) const override;
            void *nativeHandle() const override { return nullptr; }

        private:
            ResourceLayoutDesc desc_;
        };

        //------------------------------------------------------------------
        class GLPipeline : public RHIPipeline
        {
        public:
            struct SamplerUniform
            {
                GLint location;
                GLuint unit;
                GLenum target;
                std::string name;
            };

            GLPipeline(GLuint program, const PipelineDesc &desc);
            ~GLPipeline() override;

            const std::string &name() const override { return desc_.name; }
            bool isCompute() const override { return desc_.is_compute; }
            const ResourceLayoutDesc &resourceLayout() const override { return desc_.resource_layout; }
            void *nativeHandle() const override { return reinterpret_cast<void *>(static_cast<uintptr_t>(program_)); }

            GLuint program() const { return program_; }
            const PipelineDesc &desc() const { return desc_; }

            void bindUniformBlocks();
            void bindSamplerUniforms();
            const std::vector<SamplerUniform> &samplerUniforms() const { return sampler_uniforms_; }

            // Push constant emulation -------------------------------------
            void setPushConstants(const void *data, uint32_t size);
            uint32_t pushConstantSize() const { return desc_.resource_layout.push_constant_size; }

            // VAO cache -----------------------------------------------------
            GLuint findOrCreateVAO(GLBuffer *vb, uint64_t vb_offset, GLBuffer *ib,
                                   const VertexLayout &layout);

            const DepthStencilState &depthStencil() const { return desc_.depth_stencil; }
            const BlendState &blend() const { return desc_.blend; }
            const RasterizerState &rasterizer() const { return desc_.rasterizer; }
            PrimitiveTopology topology() const { return desc_.topology; }

        private:
            GLuint program_ = 0;
            PipelineDesc desc_;
            std::vector<SamplerUniform> sampler_uniforms_;
            std::vector<GLuint> push_constant_ubos_;
            uint32_t push_constant_slot_ = 0;
            struct CachedVAO
            {
                GLuint vao = 0;
                uint64_t vb_offset = 0; // offset the attribute pointers currently use
            };
            std::map<std::pair<GLuint, GLuint>, CachedVAO> vao_cache_; // (vb,ib) -> vao
            static const uint32_t kPushConstantSlots = 3;
        };

        //------------------------------------------------------------------
        class GLRenderPass : public RHIRenderPass
        {
        public:
            GLRenderPass(const RenderPassDesc &desc, uint64_t hash);
            ~GLRenderPass() override {}
            const RenderPassDesc &desc() const override { return desc_; }
            const std::string &name() const override { return desc_.name; }
            void *nativeHandle() const override { return nullptr; }
            uint64_t hash() const { return hash_; }

        private:
            RenderPassDesc desc_;
            uint64_t hash_ = 0;
        };

        //------------------------------------------------------------------
        class GLFramebuffer : public RHIFramebuffer
        {
        public:
            GLFramebuffer(GLuint fbo, GLRenderPass *pass, uint32_t width, uint32_t height,
                          const std::string &name, bool is_default);
            ~GLFramebuffer() override;

            uint32_t width() const override { return width_; }
            uint32_t height() const override { return height_; }
            RHIRenderPass *renderPass() const override { return pass_; }
            const std::string &name() const override { return name_; }
            void *nativeHandle() const override { return reinterpret_cast<void *>(static_cast<uintptr_t>(fbo_)); }

            GLuint fbo() const { return fbo_; }
            bool isDefault() const { return is_default_; }
            const std::vector<GLuint> &colorAttachments() const { return color_attachments_; }
            void addColorAttachment(GLuint tex) { color_attachments_.push_back(tex); }
            void resize(uint32_t width, uint32_t height) { width_ = width; height_ = height; }

        private:
            GLuint fbo_ = 0;
            GLRenderPass *pass_ = nullptr;
            uint32_t width_ = 0, height_ = 0;
            std::string name_;
            bool is_default_ = false;
            std::vector<GLuint> color_attachments_;
        };

        //------------------------------------------------------------------
        class GLDescriptorSet : public RHIDescriptorSet
        {
        public:
            struct Entry
            {
                uint32_t binding = 0;
                ResourceType type = ResourceType::UniformBuffer;
                // buffer
                GLBuffer *buffer = nullptr;
                uint64_t offset = 0;
                uint64_t range = 0;
                // image
                GLTexture *texture = nullptr;
                GLSampler *sampler = nullptr;
            };

            GLDescriptorSet(GLResourceLayout *layout, uint32_t set_index)
                : layout_(layout), set_index_(set_index) {}
            ~GLDescriptorSet() override {}

            RHIResourceLayout *layout() const override { return layout_; }
            void write(const WriteDescriptorSet *writes, uint32_t count) override;
            void *nativeHandle() const override { return nullptr; }

            const std::vector<Entry> &entries() const { return entries_; }
            uint32_t setIndex() const { return set_index_; }

        private:
            GLResourceLayout *layout_ = nullptr;
            uint32_t set_index_ = 0;
            std::vector<Entry> entries_;
        };

        //------------------------------------------------------------------
        class GLCommandBuffer : public RHICommandBuffer
        {
        public:
            explicit GLCommandBuffer(GLDevice *device);
            ~GLCommandBuffer() override;

            void begin(CommandBufferUsage usage) override;
            void end() override;

            void beginRenderPass(RHIRenderPass *pass, RHIFramebuffer *fb, const RenderPassBeginInfo &info) override;
            void endRenderPass() override;

            void bindPipeline(RHIPipeline *pipeline) override;
            void bindVertexBuffer(RHIBuffer *buffer, uint64_t offset = 0) override;
            void bindIndexBuffer(RHIBuffer *buffer, IndexType type, uint64_t offset = 0) override;
            void bindDescriptorSets(RHIPipeline *pipeline, RHIDescriptorSet *const *sets, uint32_t set_count,
                                    const uint32_t *dynamic_offsets, uint32_t offset_count) override;
            void setPushConstants(const void *data, uint32_t size) override;
            void setViewport(const Viewport &vp) override;
            void setScissor(const Rect2D &rect) override;

            void draw(uint32_t vertex_count, uint32_t first_vertex, uint32_t instance_count, uint32_t first_instance) override;
            void drawIndexed(uint32_t index_count, uint32_t first_index, int32_t vertex_offset,
                             uint32_t instance_count, uint32_t first_instance) override;
            void dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z) override;

            void copyBufferToBuffer(RHIBuffer *src, uint64_t src_offset, RHIBuffer *dst, uint64_t dst_offset, uint64_t size) override;
            void copyBufferToImage(RHIBuffer *src, RHITexture *dst, const BufferImageCopy &region) override;
            void copyImageToBuffer(RHITexture *src, RHIBuffer *dst, const BufferImageCopy &region) override;
            void pipelineBarrier(const TextureBarrier *barriers, uint32_t count) override;
            void generateMipmaps(RHITexture *texture) override;
            void blitImage(RHITexture *src, uint32_t src_mip, RHITexture *dst, uint32_t dst_mip,
                           uint32_t src_layer, uint32_t dst_layer, uint32_t layer_count, bool linear_filter) override;
            bool readImagePixels(RHITexture *src, uint32_t mip, uint32_t layer, void *dst, uint32_t dst_size_bytes) override;

            void *nativeHandle() const override { return nullptr; }

        private:
            void applyPipelineState(GLPipeline *pipeline);
            void bindVAO(GLPipeline *pipeline);
            void checkError(const char *where);
            bool validating() const;

            GLDevice *device_ = nullptr;
            GLPipeline *current_pipeline_ = nullptr;
            GLBuffer *current_vb_ = nullptr;
            uint64_t current_vb_offset_ = 0;
            GLBuffer *current_ib_ = nullptr;
            uint64_t current_ib_offset_ = 0;
            IndexType current_index_type_ = IndexType::UInt16;
            GLuint current_vao_ = 0;
            bool in_render_pass_ = false;
            GLFramebuffer *current_fb_ = nullptr;
            uint32_t current_fb_height_ = 0;
            Viewport viewport_;
            bool recording_ = false;
        };

        //------------------------------------------------------------------
        class GLDevice : public RHIDevice
        {
        public:
            GLDevice();
            ~GLDevice() override;

            bool initialize(const DeviceDesc &desc) override;
            void shutdown() override;

            BackendType backend() const override { return BackendType::OpenGL; }
            const DeviceInfo &info() const override { return info_; }
            const DeviceDesc &desc() const override { return desc_; }

            bool createSwapchain(void *native_window, uint32_t width, uint32_t height) override;
            void destroySwapchain() override;
            void resizeSwapchain(uint32_t width, uint32_t height) override;
            uint32_t swapchainWidth() const override { return swapchain_width_; }
            uint32_t swapchainHeight() const override { return swapchain_height_; }
            PixelFormat swapchainFormat() const override { return swapchain_format_; }

            bool beginFrame() override;
            RHICommandBuffer *currentCommandBuffer() override { return frame_command_buffer_; }
            RHIRenderPass *swapchainRenderPass() override { return swapchain_render_pass_; }
            RHIFramebuffer *currentSwapchainFramebuffer() override { return swapchain_framebuffer_; }
            void endFrame() override;
            uint32_t currentFrameIndex() const override { return 0; }
            void waitIdle() override;

            RHIBuffer *createBuffer(const BufferDesc &desc) override;
            RHITexture *createTexture(const TextureDesc &desc) override;
            RHISampler *createSampler(const SamplerDesc &desc) override;
            RHIShader *createShader(const ShaderStageDesc &desc) override;
            RHIResourceLayout *createResourceLayout(const ResourceLayoutDesc &desc) override;
            RHIPipeline *createPipeline(const PipelineDesc &desc) override;
            RHIRenderPass *createRenderPass(const RenderPassDesc &desc) override;
            RHIFramebuffer *createFramebuffer(const FramebufferDesc &desc) override;
            RHIDescriptorSet *createDescriptorSet(RHIResourceLayout *layout, uint32_t set_index = 0) override;
            RHICommandBuffer *createCommandBuffer(CommandBufferUsage usage) override;

            void destroyBuffer(RHIBuffer *) override;
            void destroyTexture(RHITexture *) override;
            void destroySampler(RHISampler *) override;
            void destroyShader(RHIShader *) override;
            void destroyResourceLayout(RHIResourceLayout *) override;
            void destroyPipeline(RHIPipeline *) override;
            void destroyRenderPass(RHIRenderPass *) override;
            void destroyFramebuffer(RHIFramebuffer *) override;
            void destroyDescriptorSet(RHIDescriptorSet *) override;
            void destroyCommandBuffer(RHICommandBuffer *) override;

            void writeBuffer(RHIBuffer *buffer, const void *data, uint64_t size, uint64_t offset = 0) override;
            void writeTexture(RHITexture *texture, const void *data, uint64_t size,
                              uint32_t width, uint32_t height,
                              uint32_t mip = 0, uint32_t base_layer = 0, uint32_t layer_count = 1) override;
            void submitImmediate(void (*record_fn)(RHICommandBuffer *, void *), void *user_data) override;
            void submitAndWait(RHICommandBuffer *cmd) override;
            uint64_t frameNumber() const override { return frame_number_; }

            void *sdlWindow() const { return window_; }
            bool headless() const { return desc_.headless; }

            // Scratch FBO used for readbacks / blits outside a render pass.
            GLuint scratchFBO();

        private:
            DeviceDesc desc_;
            DeviceInfo info_;
            void *window_ = nullptr;
            void *gl_context_ = nullptr;
            uint32_t swapchain_width_ = 1;
            uint32_t swapchain_height_ = 1;
            PixelFormat swapchain_format_ = PixelFormat::RGBA8_UNORM;
            GLRenderPass *swapchain_render_pass_ = nullptr;
            GLFramebuffer *swapchain_framebuffer_ = nullptr;
            GLCommandBuffer *frame_command_buffer_ = nullptr;
            uint64_t frame_number_ = 0;
            bool initialized_ = false;
            GLuint scratch_fbo_ = 0;

            std::vector<GLBuffer *> buffers_;
            std::vector<GLTexture *> textures_;
            std::vector<GLSampler *> samplers_;
            std::vector<GLShader *> shaders_;
            std::vector<GLResourceLayout *> layouts_;
            std::vector<GLPipeline *> pipelines_;
            std::unordered_map<uint64_t, GLRenderPass *> render_passes_;
            std::vector<GLFramebuffer *> framebuffers_;
            std::vector<GLDescriptorSet *> descriptor_sets_;
            std::vector<GLCommandBuffer *> command_buffers_;
        };

    } // namespace RHI
} // namespace Genesis

#endif /* __GENESIS_GL_BACKEND_HPP__ */
