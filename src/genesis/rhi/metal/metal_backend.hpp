/*
 * Genesis Render - Metal backend (declarations)
 *
 * Implements the RHI on Apple Metal (macOS).  This is the third native
 * backend alongside OpenGL and Vulkan, sharing the exact same abstract
 * interface (src/genesis/rhi/rhi.hpp).
 *
 * Notable design points (mirrors the Vulkan backend):
 *   - One MTLCommandBuffer per in-flight frame, submitted to the
 *     CAMetalLayer of the SDL window's NSView.
 *   - Pipeline state is compiled once into MTLRenderPipelineState; the
 *     descriptor-set / uniform-set concepts are mapped to MTL vertex
 *     and fragment buffer arguments + texture arguments.
 *   - Shaders are compiled from embedded Metal Shading Language (MSL)
 *     strings at device creation (the Metal equivalent of the GLSL/SPIR-V
 *     shaders used by the other backends).
 *
 * The header is Objective-C free: every Metal object is carried as a
 * void* (id<...>), so the renderer and demo code never include Metal.
 */
#ifndef __GENESIS_METAL_BACKEND_HPP__
#define __GENESIS_METAL_BACKEND_HPP__

#include "rhi/rhi.hpp"

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace Genesis
{
    namespace RHI
    {
        static constexpr uint32_t METAL_MAX_FRAMES_IN_FLIGHT = 2;

        // Forward declarations (classes reference each other across sections).
        class MetalBufferRHI;
        class MetalTextureRHI;
        class MetalSamplerRHI;
        class MetalShaderRHI;
        class MetalResourceLayoutRHI;
        class MetalPipelineRHI;
        class MetalRenderPassRHI;
        class MetalFramebufferRHI;
        class MetalDescriptorSetRHI;
        class MetalCommandBufferRHI;
        class MetalDeviceRHI;

        //------------------------------------------------------------------
        struct MTLPixelFormatMap
        {
            unsigned int mtl_pixel_format; // MTLPixelFormat (cast when assigning)
            bool is_depth;
        };
        MTLPixelFormatMap mtlPixelFormatMap(PixelFormat format);

        //------------------------------------------------------------------
        class MetalBufferRHI : public RHIBuffer
        {
        public:
            void *mtl_buffer = nullptr; // id<MTLBuffer>
            uint64_t size_ = 0;
            BufferUsageFlags usage_ = 0;
            MemoryUsage memory_usage_ = MemoryUsage::GpuOnly;
            std::string name_;

            uint64_t size() const override { return size_; }
            BufferUsageFlags usage() const override { return usage_; }
            MemoryUsage memoryUsage() const override { return memory_usage_; }
            const std::string &name() const override { return name_; }

            void *map(uint64_t offset = 0, uint64_t size = ~0ull) override;
            void unmap() override;
            void *nativeHandle() const override { return mtl_buffer; }
        };

        //------------------------------------------------------------------
        class MetalTextureRHI : public RHITexture
        {
        public:
            void *mtl_texture = nullptr; // id<MTLTexture>
            void *mtl_view = nullptr;    // id<MTLTexture> (view, same as texture here)
            TextureDesc desc_;
            TextureLayout layout_ = TextureLayout::Undefined;

            const TextureDesc &desc() const override { return desc_; }
            uint32_t width() const override { return desc_.width; }
            uint32_t height() const override { return desc_.height; }
            uint32_t mipLevels() const override { return desc_.mip_levels; }
            uint32_t layerCount() const override { return desc_.layerCount(); }
            PixelFormat format() const override { return desc_.format; }
            const std::string &name() const override { return desc_.name; }

            TextureLayout currentLayout() const override { return layout_; }
            void setCurrentLayout(TextureLayout layout) override { layout_ = layout; }

            void *nativeHandle() const override { return mtl_texture; }
            void *nativeViewHandle() const override { return mtl_texture; }
        };

        //------------------------------------------------------------------
        class MetalSamplerRHI : public RHISampler
        {
        public:
            void *mtl_sampler = nullptr; // id<MTLSamplerState>
            SamplerDesc desc_;
            const SamplerDesc &desc() const override { return desc_; }
            const std::string &name() const override { return desc_.name; }
            void *nativeHandle() const override { return mtl_sampler; }
        };

        //------------------------------------------------------------------
        class MetalShaderRHI : public RHIShader
        {
        public:
            void *mtl_function = nullptr; // id<MTLFunction>
            ShaderStageFlagBits stage_ = ShaderStageFlagBits::Vertex;
            std::string name_;
            ShaderStageFlagBits stage() const override { return stage_; }
            const std::string &name() const override { return name_; }
            void *nativeHandle() const override { return mtl_function; }
        };

        //------------------------------------------------------------------
        class MetalResourceLayoutRHI : public RHIResourceLayout
        {
        public:
            ResourceLayoutDesc desc_;
            MetalResourceLayoutRHI() = default;
            explicit MetalResourceLayoutRHI(const ResourceLayoutDesc &desc) : desc_(desc) {}
            ~MetalResourceLayoutRHI() override {}
            const ResourceLayoutDesc &desc() const override { return desc_; }
            const ResourceBinding *findBinding(uint32_t set, uint32_t binding) const override;
            void *nativeHandle() const override { return nullptr; }
        };

        //------------------------------------------------------------------
        class MetalPipelineRHI : public RHIPipeline
        {
        public:
            void *pipeline_state = nullptr; // id<MTLRenderPipelineState>
            void *vertex_descriptor = nullptr; // MTLVertexDescriptor (owned)
            ResourceLayoutDesc resource_layout_;
            PipelineDesc desc_;

            const std::string &name() const override { return desc_.name; }
            bool isCompute() const override { return desc_.is_compute; }
            const ResourceLayoutDesc &resourceLayout() const override { return resource_layout_; }
            void *nativeHandle() const override { return pipeline_state; }

            // Push constants emulate a small uniform buffer at index 0 (binding 0).
            std::vector<char> push_constants;
        };

        //------------------------------------------------------------------
        class MetalRenderPassRHI : public RHIRenderPass
        {
        public:
            RenderPassDesc desc_;
            uint64_t hash_ = 0;
            MetalRenderPassRHI() = default;
            ~MetalRenderPassRHI() override {}
            const RenderPassDesc &desc() const override { return desc_; }
            const std::string &name() const override { return desc_.name; }
            void *nativeHandle() const override { return nullptr; }
        };

        //------------------------------------------------------------------
        class MetalFramebufferRHI : public RHIFramebuffer
        {
        public:
            void *render_pass_descriptor = nullptr; // MTLRenderPassDescriptor*
            MetalRenderPassRHI *pass_ = nullptr;
            uint32_t width_ = 0, height_ = 0;
            std::string name_;
            bool is_swapchain_ = false;

            uint32_t width() const override { return width_; }
            uint32_t height() const override { return height_; }
            RHIRenderPass *renderPass() const override { return pass_; }
            const std::string &name() const override { return name_; }
            void *nativeHandle() const override { return render_pass_descriptor; }
        };

        //------------------------------------------------------------------
        class MetalDescriptorSetRHI : public RHIDescriptorSet
        {
        public:
            struct BufferEntry
            {
                MetalBufferRHI *buffer = nullptr;
                uint64_t offset = 0;
                uint64_t range = 0;
                bool is_dynamic = false; // dynamic UBO: per-draw offset applied on bind
            };
            struct ImageEntry
            {
                MetalTextureRHI *texture = nullptr;
                MetalSamplerRHI *sampler = nullptr;
            };
            std::vector<BufferEntry> buffers;   // indexed by binding
            std::vector<ImageEntry> images;     // indexed by binding
            MetalResourceLayoutRHI *layout_ = nullptr;
            uint32_t set_index_ = 0;

            MetalDescriptorSetRHI() = default;
            ~MetalDescriptorSetRHI() override {}
            RHIResourceLayout *layout() const override { return layout_; }
            void write(const WriteDescriptorSet *writes, uint32_t count) override;
            void *nativeHandle() const override { return nullptr; }
        };

        //------------------------------------------------------------------
        class MetalCommandBufferRHI : public RHICommandBuffer
        {
        public:
            MetalDeviceRHI *device = nullptr;
            void *mtl_command_buffer = nullptr; // id<MTLCommandBuffer>
            void *mtl_encoder = nullptr;         // id<MTLRenderCommandEncoder>
            uint32_t frame_index_ = 0;
            bool recording_ = false;
            bool one_time_ = false;

            void begin(CommandBufferUsage usage) override;
            void end() override;

            void beginRenderPass(RHIRenderPass *pass, RHIFramebuffer *fb,
                                  const RenderPassBeginInfo &info) override;
            void endRenderPass() override;

            void bindPipeline(RHIPipeline *pipeline) override;
            void bindVertexBuffer(RHIBuffer *buffer, uint64_t offset = 0) override;
            void bindIndexBuffer(RHIBuffer *buffer, IndexType type, uint64_t offset = 0) override;
            void bindDescriptorSets(RHIPipeline *pipeline, RHIDescriptorSet *const *sets, uint32_t set_count,
                                    const uint32_t *dynamic_offsets, uint32_t offset_count) override;
            void setPushConstants(const void *data, uint32_t size) override;
            void setViewport(const Viewport &vp) override;
            void setScissor(const Rect2D &rect) override;

            void draw(uint32_t vertex_count, uint32_t first_vertex, uint32_t instance_count,
                      uint32_t first_instance) override;
            void drawIndexed(uint32_t index_count, uint32_t first_index, int32_t vertex_offset,
                             uint32_t instance_count, uint32_t first_instance) override;
            void dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z) override;

            void copyBufferToBuffer(RHIBuffer *src, uint64_t src_offset, RHIBuffer *dst,
                                     uint64_t dst_offset, uint64_t size) override;
            void copyBufferToImage(RHIBuffer *src, RHITexture *dst, const BufferImageCopy &region) override;
            void copyImageToBuffer(RHITexture *src, RHIBuffer *dst, const BufferImageCopy &region) override;
            void pipelineBarrier(const TextureBarrier *barriers, uint32_t count) override;
            void generateMipmaps(RHITexture *texture) override;
            void blitImage(RHITexture *src, uint32_t src_mip, RHITexture *dst, uint32_t dst_mip,
                           uint32_t src_layer, uint32_t dst_layer, uint32_t layer_count, bool linear_filter) override;
            bool readImagePixels(RHITexture *src, uint32_t mip, uint32_t layer,
                                 void *dst, uint32_t dst_size_bytes) override;

            void *nativeHandle() const override { return mtl_command_buffer; }

            // Per-encoder state.
            MetalPipelineRHI *current_pipeline = nullptr;
            MetalBufferRHI *current_vertex_buffer = nullptr;
            MetalBufferRHI *current_index_buffer = nullptr;
            uint32_t current_index_type = 0; // 0 u16, 1 u32
            MetalFramebufferRHI *current_framebuffer = nullptr;
            Viewport viewport_;
        };

        //------------------------------------------------------------------
        class MetalDeviceRHI : public RHIDevice
        {
        public:
            MetalDeviceRHI();
            ~MetalDeviceRHI() override;

            bool initialize(const DeviceDesc &desc) override;
            void shutdown() override;

            BackendType backend() const override { return BackendType::Metal; }
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
            uint32_t currentFrameIndex() const override { return frame_index_ % METAL_MAX_FRAMES_IN_FLIGHT; }
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

            // Current frame's CAMetalLayer drawable (id<CAMetalDrawable>).
            void *drawable_ = nullptr;
            void *last_command_buffer_ = nullptr; // id<MTLCommandBuffer> for waitIdle

            bool createSwapchainObjects(uint32_t width, uint32_t height);

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

            // Compiles an embedded MSL shader; returns a function handle (id<MTLFunction>).
            void *compileShader(const char *msl_source, const char *entry, ShaderStageFlagBits stage);

        private:
            DeviceDesc desc_;
            DeviceInfo info_;
            void *device_ = nullptr;            // id<MTLDevice>
            void *command_queue_ = nullptr;      // id<MTLCommandQueue>
            void *sw_layer_ = nullptr;          // CAMetalLayer*
            void *library_ = nullptr;           // id<MTLLibrary>
            void *window_ = nullptr;            // SDL_Window*

            uint32_t swapchain_width_ = 1;
            uint32_t swapchain_height_ = 1;
            PixelFormat swapchain_format_ = PixelFormat::BGRA8_UNORM;
            MetalRenderPassRHI *swapchain_render_pass_ = nullptr;
            MetalFramebufferRHI *swapchain_framebuffer_ = nullptr;
            MetalCommandBufferRHI *frame_command_buffer_ = nullptr;
            uint32_t frame_index_ = 0;
            uint64_t frame_number_ = 0;
            bool initialized_ = false;

            std::vector<MetalBufferRHI *> buffers_;
            std::vector<MetalTextureRHI *> textures_;
            std::vector<MetalSamplerRHI *> samplers_;
            std::vector<MetalShaderRHI *> shaders_;
            std::vector<MetalResourceLayoutRHI *> layouts_;
            std::vector<MetalPipelineRHI *> pipelines_;
            std::unordered_map<uint64_t, MetalRenderPassRHI *> render_passes_;
            std::vector<MetalFramebufferRHI *> framebuffers_;
            std::vector<MetalDescriptorSetRHI *> descriptor_sets_;
            std::vector<MetalCommandBufferRHI *> command_buffers_;

            // MSL source cache: built once from the embedded shader sources.
            std::unordered_map<std::string, void *> functions_;
        };

    } // namespace RHI
} // namespace Genesis

#endif /* __GENESIS_METAL_BACKEND_HPP__ */
