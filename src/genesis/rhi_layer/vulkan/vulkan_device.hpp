/*
 * @Descripttion: Vulkan 后端渲染设备
 *               由 demo1/graphics/gpu_device 重构而来, 扩展能力:
 *               资源池(handle)、渲染管线、描述符集、深度缓冲、
 *               离屏渲染目标、cubemap、mipmap、截图回读
 * @version: v1.0
 */
#ifndef __GENESIS_VULKAN_DEVICE_H__
#define __GENESIS_VULKAN_DEVICE_H__

#include "rhi_layer/rhi_device.hpp"
#include "rhi_layer/resource_pool.hpp"

#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

#include "external/vk_mem_alloc.h"

namespace Genesis
{
    namespace rhi
    {
        static constexpr int kMaxFramesInFlight = 2;

        struct VkBufferRes
        {
            VkBuffer m_buffer = VK_NULL_HANDLE;
            VmaAllocation m_allocation = VK_NULL_HANDLE;
            uint64_t m_size = 0;
            uint32_t m_usage = 0;
            bool m_host_visible = false;
            void *m_mapped = nullptr;
        };

        struct VkTextureRes
        {
            VkImage m_image = VK_NULL_HANDLE;
            VmaAllocation m_allocation = VK_NULL_HANDLE;
            VkImageView m_default_view = VK_NULL_HANDLE; // 整体采样视图
            TextureInfo m_info{};
            bool m_render_target = false;
        };

        struct VkSamplerRes
        {
            VkSampler m_sampler = VK_NULL_HANDLE;
        };

        struct VkShaderRes
        {
            VkShaderModule m_module = VK_NULL_HANDLE;
            ShaderStage m_stage = ShaderStage::Vertex;
        };

        struct VkSetLayoutRes
        {
            VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
            DescriptorSetLayoutCreateInfo m_info{};
        };

        struct VkPipelineRes
        {
            VkPipeline m_pipeline = VK_NULL_HANDLE;
            VkPipelineLayout m_layout = VK_NULL_HANDLE;
            RenderTargetLayout m_rt_layout{};
        };

        struct VkSetRes
        {
            VkDescriptorSet m_set = VK_NULL_HANDLE;
        };

        struct VkRenderTargetRes
        {
            VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
            VkRenderPass m_render_pass = VK_NULL_HANDLE; // clear 变体
            uint32_t m_width = 0;
            uint32_t m_height = 0;
            RenderTargetLayout m_layout{};
            std::vector<VkImageView> m_owned_views; // 面/mip 视图
        };

        struct RenderPassKey
        {
            RenderTargetLayout m_layout;
            bool m_clear = true;
            bool operator==(const RenderPassKey &o) const { return m_clear == o.m_clear && m_layout == o.m_layout; }
        };

        struct RenderPassKeyHash
        {
            size_t operator()(const RenderPassKey &k) const
            {
                size_t h = std::hash<uint32_t>()(k.m_layout.m_color_count) ^ (std::hash<uint32_t>()((uint32_t)k.m_layout.m_depth_format) << 8) ^ (std::hash<bool>()(k.m_clear) << 16);
                for (uint32_t i = 0; i < k.m_layout.m_color_count; ++i)
                    h ^= std::hash<uint32_t>()((uint32_t)k.m_layout.m_color_formats[i]) << (i + 1);
                return h;
            }
        };

        class VulkanDevice : public RHIDevice
        {
        public:
            VulkanDevice() = default;
            ~VulkanDevice() override = default;

            bool init(const DeviceCreateInfo &info) override;
            void shutdown() override;

            Backend getBackend() const override { return Backend::Vulkan; }
            uint32_t getWidth() const override { return m_width; }
            uint32_t getHeight() const override { return m_height; }
            RenderTargetLayout getSwapchainLayout() const override { return m_main_layout; }

            BufferHandle createBuffer(const BufferCreateInfo &info) override;
            void destroyBuffer(BufferHandle handle) override;
            void updateBuffer(BufferHandle handle, const void *data, uint64_t size, uint64_t offset) override;

            TextureHandle createTexture(const TextureCreateInfo &info) override;
            void destroyTexture(TextureHandle handle) override;
            void updateTexture(TextureHandle handle, const void *data, uint64_t size, uint32_t mipLevel, uint32_t layer) override;
            void generateMipmaps(TextureHandle handle) override;
            TextureInfo getTextureInfo(TextureHandle handle) const override;

            SamplerHandle createSampler(const SamplerCreateInfo &info) override;
            void destroySampler(SamplerHandle handle) override;

            ShaderHandle createShader(const ShaderCreateInfo &info) override;
            void destroyShader(ShaderHandle handle) override;

            PipelineHandle createPipeline(const PipelineCreateInfo &info) override;
            void destroyPipeline(PipelineHandle handle) override;

            DescriptorSetLayoutHandle createDescriptorSetLayout(const DescriptorSetLayoutCreateInfo &info) override;
            void destroyDescriptorSetLayout(DescriptorSetLayoutHandle handle) override;

            DescriptorSetHandle createDescriptorSet(const DescriptorSetCreateInfo &info) override;
            void destroyDescriptorSet(DescriptorSetHandle handle) override;

            RenderTargetHandle createRenderTarget(const RenderTargetCreateInfo &info) override;
            void destroyRenderTarget(RenderTargetHandle handle) override;

            void beginFrame() override;
            void endFrame() override;
            void beginCommands() override;
            void endCommands() override;

            void beginRenderPass(RenderTargetHandle target, const float clearColor[4], float clearDepth, bool clear) override;
            void endRenderPass() override;

            void bindPipeline(PipelineHandle pipeline) override;
            void bindDescriptorSet(uint32_t slot, DescriptorSetHandle set) override;
            void bindVertexBuffer(BufferHandle buffer, uint64_t offset) override;
            void bindIndexBuffer(BufferHandle buffer, uint64_t offset) override;
            void draw(uint32_t vertexCount, uint32_t firstVertex) override;
            void drawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) override;

            bool captureScreen(const char *pngPath) override;
            bool readbackTexture(TextureHandle handle, uint8_t *outRGBA, uint32_t rowPitch) override;
            void waitIdle() override;
            void requestScreenCapture(const char *pngPath) override;

            //------[后端逃逸舱口: 仅供 ImGui Vulkan 后端集成使用]------//
            VkDevice getVkDevice() const { return m_device; }
            VkInstance getVkInstance() const { return m_instance; }
            VkPhysicalDevice getVkPhysicalDevice() const { return m_physical_device; }
            VkQueue getVkQueue() const { return m_graphics_queue; }
            uint32_t getVkQueueFamily() const { return m_graphics_queue_family; }
            VkDescriptorPool getVkDescriptorPool() const { return m_descriptor_pool; }
            VkCommandBuffer getCurrentCommandBuffer() const { return m_recording_cmd; }
            VkRenderPass getMainRenderPass();

        private:
            bool createInstance();
            bool createSwapchain();
            void destroySwapchain();
            VkRenderPass getOrCreateRenderPass(const RenderTargetLayout &layout, bool clear);
            VkCommandBuffer beginOneTimeCommands();
            void endOneTimeCommands(VkCommandBuffer cmd);
            void transitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, uint32_t layers, bool isDepth);
            void uploadTextureData(TextureHandle handle, const void *data, uint64_t size, uint32_t mipLevel, uint32_t layer);
            void immediateTransitionToShaderRead(VkTextureRes &tex);

            static void *vmaAllocate(const VkBufferCreateInfo &bi, VkMemoryPropertyFlags props, VkBuffer *buf, VmaAllocation *alloc);

        private:
            void *m_window = nullptr;
            uint32_t m_width = 0;
            uint32_t m_height = 0;
            bool m_enable_debug = false;

            VkInstance m_instance = VK_NULL_HANDLE;
            VkSurfaceKHR m_surface = VK_NULL_HANDLE;
            VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
            VkDevice m_device = VK_NULL_HANDLE;
            VkQueue m_graphics_queue = VK_NULL_HANDLE;
            uint32_t m_graphics_queue_family = UINT32_MAX;

            VmaAllocator m_vma = VK_NULL_HANDLE;

            VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
            std::vector<VkImage> m_swapchain_images;
            std::vector<VkImageView> m_swapchain_views;
            std::vector<VkFramebuffer> m_swapchain_framebuffers;
            VkFormat m_swapchain_format = VK_FORMAT_B8G8R8A8_UNORM;
            Format m_swapchain_rhi_format = Format::BGRA8_UNorm;

            TextureHandle m_depth_texture; // 交换链配套深度
            RenderTargetLayout m_main_layout{};

            VkCommandPool m_command_pool = VK_NULL_HANDLE;
            VkCommandBuffer m_frame_cmds[kMaxFramesInFlight] = {};
            VkCommandBuffer m_recording_cmd = VK_NULL_HANDLE; // 当前录制中的 cmd (帧内或一次性)

            VkDescriptorPool m_descriptor_pool = VK_NULL_HANDLE;

            VkSemaphore m_image_available[kMaxFramesInFlight] = {};
            VkSemaphore m_render_finished[kMaxFramesInFlight] = {};
            VkFence m_in_flight[kMaxFramesInFlight] = {};

            uint32_t m_current_frame = 0;
            uint32_t m_image_index = 0;
            bool m_in_frame = false;
            bool m_in_pass = false;
            VkPipelineLayout m_current_pipeline_layout = VK_NULL_HANDLE;

            // 截图请求
            std::string m_capture_path;
            bool m_capture_pending = false;

            // 资源池
            ResourcePool<VkBufferRes> m_buffers;
            ResourcePool<VkTextureRes> m_textures;
            ResourcePool<VkSamplerRes> m_samplers;
            ResourcePool<VkShaderRes> m_shaders;
            ResourcePool<VkPipelineRes> m_pipelines;
            ResourcePool<VkSetLayoutRes> m_set_layouts;
            ResourcePool<VkSetRes> m_sets;
            ResourcePool<VkRenderTargetRes> m_render_targets;

            std::unordered_map<RenderPassKey, VkRenderPass, RenderPassKeyHash> m_render_pass_cache;
        };

    } // namespace rhi
} // namespace Genesis

#endif // __GENESIS_VULKAN_DEVICE_H__
