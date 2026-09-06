/*
 * Genesis Render - RHI (Render Hardware Interface) abstract API
 *
 * Design goals
 * ------------
 * 1. One renderer implementation on top of two very different native APIs
 *    (OpenGL 4.1+ core and Vulkan 1.1+).
 * 2. Explicit resources + explicit frames (like Vulkan), because that is the
 *    harder API to emulate; the GL backend absorbs the impedance mismatch
 *    (immediate execution, no image layouts, uniform blocks instead of
 *    descriptor sets).
 * 3. No native handle leaks into renderer code.  Native handles are only
 *    reachable through `nativeHandle()` for the rare integration case
 *    (e.g. handing the Vulkan command buffer to the ImGui backend).
 *
 * Threading model
 * ---------------
 * The RHI is single-threaded on the render thread.  Command buffers are
 * recorded and submitted from that thread only.
 */
#ifndef __GENESIS_RHI_HPP__
#define __GENESIS_RHI_HPP__

#include "rhi_desc.hpp"
#include <memory>
#include <string>
#include <vector>

namespace Genesis
{
    namespace RHI
    {
        class RHIBuffer;
        class RHITexture;
        class RHISampler;
        class RHIShader;
        class RHIResourceLayout;
        class RHIPipeline;
        class RHIRenderPass;
        class RHIFramebuffer;
        class RHIDescriptorSet;
        class RHICommandBuffer;
        class RHIDevice;

        //------------------------------------------------------------------
        // Resources
        //------------------------------------------------------------------
        class RHIBuffer
        {
        public:
            virtual ~RHIBuffer() {}
            virtual uint64_t size() const = 0;
            virtual BufferUsageFlags usage() const = 0;
            virtual MemoryUsage memoryUsage() const = 0;
            virtual const std::string &name() const = 0;

            // Returns a CPU-visible pointer, or nullptr when the buffer is
            // GPU-only (use RHIDevice::writeBuffer for those).
            virtual void *map(uint64_t offset = 0, uint64_t size = ~0ull) = 0;
            virtual void unmap() = 0;

            virtual void *nativeHandle() const = 0;
        };

        class RHITexture
        {
        public:
            virtual ~RHITexture() {}
            virtual const TextureDesc &desc() const = 0;
            virtual uint32_t width() const = 0;
            virtual uint32_t height() const = 0;
            virtual uint32_t mipLevels() const = 0;
            virtual uint32_t layerCount() const = 0;
            virtual PixelFormat format() const = 0;
            virtual const std::string &name() const = 0;

            // Current layout tracked by the backend (Vulkan needs it, GL keeps
            // it for mip-generation bookkeeping).
            virtual TextureLayout currentLayout() const = 0;
            virtual void setCurrentLayout(TextureLayout layout) = 0;

            virtual void *nativeHandle() const = 0;      // VkImage / GL texture id
            virtual void *nativeViewHandle() const = 0;   // VkImageView / GL texture id
        };

        class RHISampler
        {
        public:
            virtual ~RHISampler() {}
            virtual const SamplerDesc &desc() const = 0;
            virtual const std::string &name() const = 0;
            virtual void *nativeHandle() const = 0;
        };

        class RHIShader
        {
        public:
            virtual ~RHIShader() {}
            virtual ShaderStageFlagBits stage() const = 0;
            virtual const std::string &name() const = 0;
            virtual void *nativeHandle() const = 0;
        };

        class RHIResourceLayout
        {
        public:
            virtual ~RHIResourceLayout() {}
            virtual const ResourceLayoutDesc &desc() const = 0;
            virtual const ResourceBinding *findBinding(uint32_t set, uint32_t binding) const = 0;
            virtual void *nativeHandle() const = 0;
        };

        class RHIPipeline
        {
        public:
            virtual ~RHIPipeline() {}
            virtual const std::string &name() const = 0;
            virtual bool isCompute() const = 0;
            virtual const ResourceLayoutDesc &resourceLayout() const = 0;
            virtual void *nativeHandle() const = 0;
        };

        class RHIRenderPass
        {
        public:
            virtual ~RHIRenderPass() {}
            virtual const RenderPassDesc &desc() const = 0;
            virtual const std::string &name() const = 0;
            virtual void *nativeHandle() const = 0;
        };

        class RHIFramebuffer
        {
        public:
            virtual ~RHIFramebuffer() {}
            virtual uint32_t width() const = 0;
            virtual uint32_t height() const = 0;
            virtual RHIRenderPass *renderPass() const = 0;
            virtual const std::string &name() const = 0;
            virtual void *nativeHandle() const = 0;
        };

        class RHIDescriptorSet
        {
        public:
            virtual ~RHIDescriptorSet() {}
            virtual RHIResourceLayout *layout() const = 0;
            virtual void write(const WriteDescriptorSet *writes, uint32_t count) = 0;
            virtual void *nativeHandle() const = 0;
        };

        //------------------------------------------------------------------
        // Command buffer
        //------------------------------------------------------------------
        enum class CommandBufferUsage : uint32_t
        {
            OneTimeSubmit = 0,
            RenderFrame = 1,
        };

        class RHICommandBuffer
        {
        public:
            virtual ~RHICommandBuffer() {}

            virtual void begin(CommandBufferUsage usage) = 0;
            virtual void end() = 0;

            virtual void beginRenderPass(RHIRenderPass *pass, RHIFramebuffer *fb,
                                         const RenderPassBeginInfo &info) = 0;
            virtual void endRenderPass() = 0;

            virtual void bindPipeline(RHIPipeline *pipeline) = 0;
            virtual void bindVertexBuffer(RHIBuffer *buffer, uint64_t offset = 0) = 0;
            virtual void bindIndexBuffer(RHIBuffer *buffer, IndexType type, uint64_t offset = 0) = 0;

            // Sets are indexed by `set` in the pipeline's resource layout.
            virtual void bindDescriptorSets(RHIPipeline *pipeline,
                                            RHIDescriptorSet *const *sets, uint32_t set_count,
                                            const uint32_t *dynamic_offsets, uint32_t offset_count) = 0;

            virtual void setPushConstants(const void *data, uint32_t size) = 0;

            virtual void setViewport(const Viewport &vp) = 0;
            virtual void setScissor(const Rect2D &rect) = 0;

            virtual void draw(uint32_t vertex_count, uint32_t first_vertex = 0,
                              uint32_t instance_count = 1, uint32_t first_instance = 0) = 0;
            virtual void drawIndexed(uint32_t index_count, uint32_t first_index = 0,
                                     int32_t vertex_offset = 0,
                                     uint32_t instance_count = 1, uint32_t first_instance = 0) = 0;

            // Compute
            virtual void dispatch(uint32_t group_x, uint32_t group_y = 1, uint32_t group_z = 1) = 0;

            // Transfers / barriers
            virtual void copyBufferToBuffer(RHIBuffer *src, uint64_t src_offset,
                                            RHIBuffer *dst, uint64_t dst_offset, uint64_t size) = 0;
            virtual void copyBufferToImage(RHIBuffer *src, RHITexture *dst,
                                           const BufferImageCopy &region) = 0;
            virtual void copyImageToBuffer(RHITexture *src, RHIBuffer *dst,
                                           const BufferImageCopy &region) = 0;
            virtual void pipelineBarrier(const TextureBarrier *barriers, uint32_t count) = 0;
            virtual void generateMipmaps(RHITexture *texture) = 0;

            // Blit / resolve used by the IBL prefilter chain and readbacks.
            virtual void blitImage(RHITexture *src, uint32_t src_mip, RHITexture *dst, uint32_t dst_mip,
                                   uint32_t src_layer, uint32_t dst_layer, uint32_t layer_count,
                                   bool linear_filter) = 0;

            // Reads back a region of a render target into CPU memory.
            // Synchronises implicitly.  Only legal outside a render pass.
            virtual bool readImagePixels(RHITexture *src, uint32_t mip, uint32_t layer,
                                         void *dst, uint32_t dst_size_bytes) = 0;

            virtual void *nativeHandle() const = 0;
        };

        //------------------------------------------------------------------
        // Device
        //------------------------------------------------------------------
        class RHIDevice
        {
        public:
            virtual ~RHIDevice() {}

            // Factory: creates the requested backend.  Returns nullptr on failure.
            static RHIDevice *create(const DeviceDesc &desc);

            virtual bool initialize(const DeviceDesc &desc) = 0;
            virtual void shutdown() = 0;

            virtual BackendType backend() const = 0;
            virtual const DeviceInfo &info() const = 0;
            virtual const DeviceDesc &desc() const = 0;

            // Window / swapchain.  `native_window` is an SDL_Window*.
            virtual bool createSwapchain(void *native_window, uint32_t width, uint32_t height) = 0;
            virtual void destroySwapchain() = 0;
            virtual void resizeSwapchain(uint32_t width, uint32_t height) = 0;
            virtual uint32_t swapchainWidth() const = 0;
            virtual uint32_t swapchainHeight() const = 0;
            virtual PixelFormat swapchainFormat() const = 0;

            // Frame loop
            virtual bool beginFrame() = 0;                    // acquire + reset frame cmd buffer
            virtual RHICommandBuffer *currentCommandBuffer() = 0;
            virtual RHIRenderPass *swapchainRenderPass() = 0;
            virtual RHIFramebuffer *currentSwapchainFramebuffer() = 0;
            virtual void endFrame() = 0;                      // submit + present
            virtual uint32_t currentFrameIndex() const = 0;   // frame-in-flight index
            virtual void waitIdle() = 0;

            // Object creation
            virtual RHIBuffer *createBuffer(const BufferDesc &desc) = 0;
            virtual RHITexture *createTexture(const TextureDesc &desc) = 0;
            virtual RHISampler *createSampler(const SamplerDesc &desc) = 0;
            virtual RHIShader *createShader(const ShaderStageDesc &desc) = 0;
            virtual RHIResourceLayout *createResourceLayout(const ResourceLayoutDesc &desc) = 0;
            virtual RHIPipeline *createPipeline(const PipelineDesc &desc) = 0;
            virtual RHIRenderPass *createRenderPass(const RenderPassDesc &desc) = 0;
            virtual RHIFramebuffer *createFramebuffer(const FramebufferDesc &desc) = 0;
            // `set_index` selects which set of the layout this set is allocated from.
            virtual RHIDescriptorSet *createDescriptorSet(RHIResourceLayout *layout, uint32_t set_index = 0) = 0;
            virtual RHICommandBuffer *createCommandBuffer(CommandBufferUsage usage) = 0;

            virtual void destroyBuffer(RHIBuffer *) = 0;
            virtual void destroyTexture(RHITexture *) = 0;
            virtual void destroySampler(RHISampler *) = 0;
            virtual void destroyShader(RHIShader *) = 0;
            virtual void destroyResourceLayout(RHIResourceLayout *) = 0;
            virtual void destroyPipeline(RHIPipeline *) = 0;
            virtual void destroyRenderPass(RHIRenderPass *) = 0;
            virtual void destroyFramebuffer(RHIFramebuffer *) = 0;
            virtual void destroyDescriptorSet(RHIDescriptorSet *) = 0;
            virtual void destroyCommandBuffer(RHICommandBuffer *) = 0;

            // Convenience upload helpers (they record into a one-time command
            // buffer internally and synchronise).
            virtual void writeBuffer(RHIBuffer *buffer, const void *data, uint64_t size, uint64_t offset = 0) = 0;
            virtual void writeTexture(RHITexture *texture, const void *data, uint64_t size,
                                      uint32_t width, uint32_t height,
                                      uint32_t mip = 0, uint32_t base_layer = 0, uint32_t layer_count = 1) = 0;

            // Executes a one-shot command buffer and waits for it.
            virtual void submitImmediate(void (*record_fn)(RHICommandBuffer *, void *), void *user_data) = 0;

            // Submits an already-recorded command buffer and blocks until the
            // GPU is done.  Used by offscreen rendering / readback paths
            // (headless verification, screenshots, IBL baking).
            virtual void submitAndWait(RHICommandBuffer *cmd) = 0;

            // Frame statistics exposed to the UI.
            virtual uint64_t frameNumber() const = 0;
        };

        //------------------------------------------------------------------
        // Helpers shared by both backends
        //------------------------------------------------------------------
        // Returns true when the requested backend was compiled into the engine.
        bool backendAvailable(BackendType type);

        // Aligned size helper for dynamic uniform buffers.
        inline uint64_t alignUp(uint64_t value, uint64_t alignment)
        {
            if (alignment == 0) return value;
            return (value + alignment - 1) & ~(alignment - 1);
        }

    } // namespace RHI
} // namespace Genesis

#endif /* __GENESIS_RHI_HPP__ */
