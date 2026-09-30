/*
 * Genesis Render - Vulkan backend (declarations)
 *
 * Implements the RHI on Vulkan 1.1+.  Memory management goes through the
 * already-vendored VulkanMemoryAllocator (src/external/vk_mem_alloc.h).
 *
 * Frame model: VK_MAX_FRAMES_IN_FLIGHT command buffers, one per in-flight
 * frame, with the classic imageAvailable / renderFinished semaphore + inFlight
 * fence triple.  Descriptor sets come from a single large pool created at
 * device startup; the engine treats them as cheap, long-lived objects.
 */
#ifndef __GENESIS_VK_BACKEND_HPP__
#define __GENESIS_VK_BACKEND_HPP__

#include "rhi/rhi.hpp"

#include <vulkan/vulkan.h>

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace Genesis
{
    namespace RHI
    {
        static constexpr uint32_t VK_MAX_FRAMES_IN_FLIGHT = 2;

        class VkTextureRHI;
        class VkRenderPassRHI;
        class VkResourceLayoutRHI;
        class VkDeviceRHI;

        //==================================================================
        // Format helpers
        //==================================================================
        VkFormat vkPixelFormat(PixelFormat format);
        PixelFormat vkToPixelFormat(VkFormat format);
        VkImageAspectFlags vkAspectMask(PixelFormat format);
        VkImageLayout vkTextureLayout(TextureLayout layout);

        //==================================================================
        class VkBufferRHI : public RHIBuffer
        {
        public:
            VkBuffer buffer = VK_NULL_HANDLE;
            void *allocation = nullptr; // VmaAllocation
            BufferDesc desc_;
            void *mapped = nullptr;
            bool persistently_mapped = false;

            ~VkBufferRHI() override {}
            uint64_t size() const override { return desc_.size; }
            BufferUsageFlags usage() const override { return desc_.usage; }
            MemoryUsage memoryUsage() const override { return desc_.memory_usage; }
            const std::string &name() const override { return desc_.name; }
            void *map(uint64_t offset = 0, uint64_t size = ~0ull) override;
            void unmap() override;
            void *nativeHandle() const override { return reinterpret_cast<void *>(buffer); }
        };

        //==================================================================
        class VkTextureRHI : public RHITexture
        {
        public:
            VkImage image = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            void *allocation = nullptr; // VmaAllocation
            TextureDesc desc_;
            TextureLayout layout_ = TextureLayout::Undefined;

            ~VkTextureRHI() override {}
            const TextureDesc &desc() const override { return desc_; }
            uint32_t width() const override { return desc_.width; }
            uint32_t height() const override { return desc_.height; }
            uint32_t mipLevels() const override { return desc_.mip_levels; }
            uint32_t layerCount() const override { return desc_.layerCount(); }
            PixelFormat format() const override { return desc_.format; }
            const std::string &name() const override { return desc_.name; }
            TextureLayout currentLayout() const override { return layout_; }
            void setCurrentLayout(TextureLayout l) override { layout_ = l; }
            void *nativeHandle() const override { return reinterpret_cast<void *>(image); }
            void *nativeViewHandle() const override { return reinterpret_cast<void *>(view); }
        };

        //==================================================================
        class VkSamplerRHI : public RHISampler
        {
        public:
            VkSampler sampler = VK_NULL_HANDLE;
            SamplerDesc desc_;
            ~VkSamplerRHI() override {}
            const SamplerDesc &desc() const override { return desc_; }
            const std::string &name() const override { return desc_.name; }
            void *nativeHandle() const override { return reinterpret_cast<void *>(sampler); }
        };

        //==================================================================
        class VkShaderRHI : public RHIShader
        {
        public:
            VkShaderModule module = VK_NULL_HANDLE;
            ShaderStageFlagBits stage_flag = ShaderStageFlagBits::Vertex;
            std::string shader_name;
            ~VkShaderRHI() override {}
            ShaderStageFlagBits stage() const override { return stage_flag; }
            const std::string &name() const override { return shader_name; }
            void *nativeHandle() const override { return reinterpret_cast<void *>(module); }
        };

        //==================================================================
        class VkResourceLayoutRHI : public RHIResourceLayout
        {
        public:
            ResourceLayoutDesc desc_;
            std::vector<VkDescriptorSetLayout> set_layouts; // indexed by set
            VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
            std::vector<VkPushConstantRange> push_ranges;

            ~VkResourceLayoutRHI() override {}
            const ResourceLayoutDesc &desc() const override { return desc_; }
            const ResourceBinding *findBinding(uint32_t set, uint32_t binding) const override;
            void *nativeHandle() const override { return reinterpret_cast<void *>(pipeline_layout); }
        };

        //==================================================================
        class VkPipelineRHI : public RHIPipeline
        {
        public:
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkPipelineLayout layout = VK_NULL_HANDLE;
            std::vector<VkShaderModule> modules;
            VkRenderPass render_pass = VK_NULL_HANDLE;
            PipelineDesc desc_;
            bool compute_ = false;

            // Objects owned by this pipeline (created from desc_.resource_layout)
            std::vector<VkDescriptorSetLayout> owned_set_layouts;
            VkPipelineLayout owned_pipeline_layout = VK_NULL_HANDLE;

            ~VkPipelineRHI() override {}
            const std::string &name() const override { return desc_.name; }
            bool isCompute() const override { return compute_; }
            const ResourceLayoutDesc &resourceLayout() const override { return desc_.resource_layout; }
            void *nativeHandle() const override { return reinterpret_cast<void *>(pipeline); }
        };

        //==================================================================
        class VkRenderPassRHI : public RHIRenderPass
        {
        public:
            VkRenderPass render_pass = VK_NULL_HANDLE;
            RenderPassDesc desc_;
            uint64_t hash = 0;
            ~VkRenderPassRHI() override {}
            const RenderPassDesc &desc() const override { return desc_; }
            const std::string &name() const override { return desc_.name; }
            void *nativeHandle() const override { return reinterpret_cast<void *>(render_pass); }
        };

        //==================================================================
        class VkFramebufferRHI : public RHIFramebuffer
        {
        public:
            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            VkRenderPassRHI *pass = nullptr;
            uint32_t w = 1, h = 1;
            std::string fb_name;
            bool owns_pass = false;
            // One entry per render pass attachment (nullptr for swapchain
            // images); used to keep the tracked image layouts in sync with
            // the pass initial/final layouts.
            std::vector<VkTextureRHI *> attachment_textures;
            // Single-mip / single-layer views created for this framebuffer.
            std::vector<VkImageView> owned_views;
            ~VkFramebufferRHI() override {}
            uint32_t width() const override { return w; }
            uint32_t height() const override { return h; }
            RHIRenderPass *renderPass() const override { return pass; }
            const std::string &name() const override { return fb_name; }
            void *nativeHandle() const override { return reinterpret_cast<void *>(framebuffer); }
        };

        //==================================================================
        class VkDescriptorSetRHI : public RHIDescriptorSet
        {
        public:
            VkDescriptorSet set = VK_NULL_HANDLE;
            VkResourceLayoutRHI *layout_obj = nullptr;
            VkDeviceRHI *device = nullptr;
            ~VkDescriptorSetRHI() override {}
            RHIResourceLayout *layout() const override { return layout_obj; }
            void write(const WriteDescriptorSet *writes, uint32_t count) override;
            void *nativeHandle() const override { return reinterpret_cast<void *>(set); }
        };

        //==================================================================
        class VkCommandBufferRHI : public RHICommandBuffer
        {
        public:
            VkCommandBuffer cmd = VK_NULL_HANDLE;
            VkCommandPool pool = VK_NULL_HANDLE;
            VkDeviceRHI *device = nullptr;
            VkRenderPass current_pass = VK_NULL_HANDLE;
            VkRenderPassRHI *current_pass_obj = nullptr;
            VkFramebufferRHI *current_fb = nullptr;
            VkPipelineRHI *current_pipeline = nullptr;
            bool inside_pass = false;
            IndexType bound_index_type = IndexType::UInt16;
            VkPipelineBindPoint current_bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;

            VkCommandBufferRHI() {}
            ~VkCommandBufferRHI() override {}

            void begin(CommandBufferUsage usage) override;
            void end() override;
            void beginRenderPass(RHIRenderPass *pass, RHIFramebuffer *fb, const RenderPassBeginInfo &info) override;
            void endRenderPass() override;
            void bindPipeline(RHIPipeline *pipeline) override;
            void bindVertexBuffer(RHIBuffer *buffer, uint64_t offset) override;
            void bindIndexBuffer(RHIBuffer *buffer, IndexType type, uint64_t offset) override;
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
            void *nativeHandle() const override { return reinterpret_cast<void *>(cmd); }
        };

        //==================================================================
        class VkDeviceRHI : public RHIDevice
        {
        public:
            VkDeviceRHI();
            ~VkDeviceRHI() override;

            bool initialize(const DeviceDesc &desc) override;
            void shutdown() override;

            BackendType backend() const override { return BackendType::Vulkan; }
            const DeviceInfo &info() const override { return info_; }
            const DeviceDesc &desc() const override { return desc_; }

            bool createSwapchain(void *native_window, uint32_t width, uint32_t height) override;
            void destroySwapchain() override;
            void resizeSwapchain(uint32_t width, uint32_t height) override;
            uint32_t swapchainWidth() const override { return swapchain_extent_.width; }
            uint32_t swapchainHeight() const override { return swapchain_extent_.height; }
            PixelFormat swapchainFormat() const override { return swapchain_format_rhi_; }

            bool beginFrame() override;
            RHICommandBuffer *currentCommandBuffer() override { return frame_command_buffers_[current_frame_]; }
            RHIRenderPass *swapchainRenderPass() override { return swapchain_render_pass_; }
            RHIFramebuffer *currentSwapchainFramebuffer() override
            {
                return swapchain_framebuffers_.empty() ? nullptr : swapchain_framebuffers_[image_index_];
            }
            void endFrame() override;
            uint32_t currentFrameIndex() const override { return current_frame_; }
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

            void writeBuffer(RHIBuffer *buffer, const void *data, uint64_t size, uint64_t offset) override;
            void writeTexture(RHITexture *texture, const void *data, uint64_t size, uint32_t width, uint32_t height,
                              uint32_t mip, uint32_t base_layer, uint32_t layer_count) override;
            void submitImmediate(void (*record_fn)(RHICommandBuffer *, void *), void *user_data) override;
            void submitAndWait(RHICommandBuffer *cmd) override;
            uint64_t frameNumber() const override { return frame_number_; }

            // Native accessors needed by the ImGui Vulkan backend and by the
            // RHI-internal command recording helpers.
            VkInstance instance() const { return instance_; }
            VkPhysicalDevice physicalDevice() const { return physical_device_; }
            VkDevice device() const { return device_; }
            VkQueue graphicsQueue() const { return graphics_queue_; }
            uint32_t graphicsQueueFamily() const { return graphics_queue_family_; }
            VkCommandPool transientCommandPool() const { return transient_pool_; }
            VkDescriptorPool descriptorPool() const { return descriptor_pool_; }
            uint32_t imageIndex() const { return image_index_; }
            void *vmaAllocator() const { return vma_allocator_; }
            VkRenderPass vkSwapchainRenderPass() const { return swapchain_render_pass_ ? swapchain_render_pass_->render_pass : VK_NULL_HANDLE; }
            VkImageView swapchainImageView(uint32_t index) const { return swapchain_image_views_[index]; }
            uint32_t swapchainImageCount() const { return (uint32_t)swapchain_images_.size(); }
            void notifyFramebufferResize() { framebuffer_resized_ = true; }

            VkCommandBuffer beginSingleTimeCommands();
            void endSingleTimeCommands(VkCommandBuffer cmd);
            void transitionImage(VkCommandBuffer cmd, VkTextureRHI *tex, TextureLayout old_layout, TextureLayout new_layout,
                                 uint32_t base_mip, uint32_t mip_count, uint32_t base_layer, uint32_t layer_count);
            uint32_t findMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) const;

        private:
            bool createInstance();
            bool pickPhysicalDevice();
            bool createLogicalDevice();
            bool createVmaAllocator();
            bool createSurfaceAndSwapchain(void *native_window, uint32_t width, uint32_t height);
            bool createSwapchainObjects(uint32_t width, uint32_t height);
            void createSwapchainRenderPass();
            bool createDescriptorPool();
            bool createCommandPools();
            bool createSyncObjects();
            bool setupDebugMessenger();
            void destroySwapchainObjects();

            DeviceDesc desc_;
            DeviceInfo info_;
            void *window_ = nullptr;

            VkInstance instance_ = VK_NULL_HANDLE;
            VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
            VkSurfaceKHR surface_ = VK_NULL_HANDLE;
            VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
            VkDevice device_ = VK_NULL_HANDLE;
            VkPhysicalDeviceFeatures device_features_{}; // advertised device features
            VkQueue graphics_queue_ = VK_NULL_HANDLE;
            VkQueue present_queue_ = VK_NULL_HANDLE;
            uint32_t graphics_queue_family_ = UINT32_MAX;
            uint32_t present_queue_family_ = UINT32_MAX;
            void *vma_allocator_ = nullptr;

            VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
            std::vector<VkImage> swapchain_images_;
            std::vector<VkImageView> swapchain_image_views_;
            VkFormat swapchain_format_ = VK_FORMAT_B8G8R8A8_UNORM;
            PixelFormat swapchain_format_rhi_ = PixelFormat::BGRA8_UNORM;
            VkExtent2D swapchain_extent_ = {1, 1};
            VkPresentModeKHR present_mode_ = VK_PRESENT_MODE_FIFO_KHR;

            VkRenderPassRHI *swapchain_render_pass_ = nullptr;
            std::vector<VkFramebufferRHI *> swapchain_framebuffers_;

            VkCommandPool command_pool_ = VK_NULL_HANDLE;
            VkCommandPool transient_pool_ = VK_NULL_HANDLE;
            std::vector<VkCommandBufferRHI *> frame_command_buffers_;

            VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;

            std::vector<VkSemaphore> image_available_;
            std::vector<VkSemaphore> render_finished_;
            std::vector<VkFence> in_flight_fences_;

            uint32_t current_frame_ = 0;
            uint32_t image_index_ = 0;
            uint64_t frame_number_ = 0;
            bool swapchain_valid_ = false;
            bool initialized_ = false;
            bool framebuffer_resized_ = false;

            std::vector<VkBufferRHI *> buffers_;
            std::vector<VkTextureRHI *> textures_;
            std::vector<VkSamplerRHI *> samplers_;
            std::vector<VkShaderRHI *> shaders_;
            std::vector<VkResourceLayoutRHI *> layouts_;
            std::vector<VkPipelineRHI *> pipelines_;
            std::unordered_map<uint64_t, VkRenderPassRHI *> render_passes_;
            std::vector<VkFramebufferRHI *> framebuffers_;
            std::vector<VkDescriptorSetRHI *> descriptor_sets_;
            std::vector<VkCommandBufferRHI *> command_buffers_;
        };

    } // namespace RHI
} // namespace Genesis

#endif /* __GENESIS_VK_BACKEND_HPP__ */
