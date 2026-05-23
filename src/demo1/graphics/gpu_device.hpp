/*
 * @Author: Yan Dongbao
 * @Date: 2024-11-08 20:52:19
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-11-13 21:14:04
 * @version:
 */
#ifndef __VULKAN_RENDERER_GPU_DEVICE_H__
#define __VULKAN_RENDERER_GPU_DEVICE_H__

#include "foundation_layer/array.hpp"
#include "foundation_layer/service.hpp"
#include "graphics/gpu_resources.hpp"

#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

namespace Genesis
{
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    struct DeviceCreateInfo
    {
        Allocator *m_allocator = nullptr;
        void *m_window = nullptr;
        std::pair<uint16_t, uint16_t> m_window_size = {1, 1};

        bool m_enable_debug = false;

        DeviceCreateInfo &setWindow(void *window, uint32_t width, uint32_t height)
        {
            m_window = window;
            m_window_size.first = width;
            m_window_size.second = height;
            return *this;
        }

        DeviceCreateInfo &setAllocator(Allocator *allocator)
        {
            m_allocator = allocator;
            return *this;
        }
    };

    class GpuDevice : public Service
    {
    public:
        GENESIS_DECLARE_SERVICE(GpuDevice);

        void init(const DeviceCreateInfo &config);
        void shutdown();

        void beginFrame();
        void endFrame();

        VkRenderPass getRenderPass() const { return vk_render_pass_; }
        VkCommandBuffer getCurrentCommandBuffer() const { return command_buffers_[image_index_]; }
        uint32_t getCurrentFrame() const { return current_frame_; }
        VkDevice getDevice() const { return vk_device_; }
        VkPhysicalDevice getPhysicalDevice() const { return vk_physical_device_; }

        /*resources create & destroy*/
        // Buffer
        BufferHandle createBuffer(const BufferCreateInfo &desc);
        void destroyBuffer(BufferHandle handle);
        // Texture
        TextureHandle createTexture(const TextureCreateInfo &desc);
        void destroyTexture(TextureHandle handle);
        // Sampler
        SamplerHandle createSampler(const SamplerCreateInfo &desc);
        void destroySampler(SamplerHandle handle);
        // shader
        ShaderStateHandle createShaderState(const ShaderStateCreateInfo &desc);
        void destroyShaderState(ShaderStateHandle handle);

        void initImgui();

    private:
        VkResult createVulkanInstance(const void *pNext = nullptr, VkInstanceCreateFlags flags = 0);
        VkResult checkInstanceExtensionSupport(const std::vector<const char *> &extensions);

        void createSwapchain();
        void createRenderPass();
        void createFramebuffers();
        void createCommandPool();
        void createDescriptorPool();
        void createSyncObjects();

        VkCommandBuffer beginSingleTimeCommands();
        void endSingleTimeCommands(VkCommandBuffer commandBuffer);

    private:
        Allocator *allocator_ = nullptr;
        void *window_ = nullptr;
        uint32_t window_width_ = 0;
        uint32_t window_height_ = 0;
        VkAllocationCallbacks *vk_allocation_callbacks_ = nullptr;

        VkInstance vk_instance_ = VK_NULL_HANDLE;
        VkSurfaceKHR vk_surface_ = VK_NULL_HANDLE;
        VkPhysicalDevice vk_physical_device_ = VK_NULL_HANDLE;
        VkDevice vk_device_ = VK_NULL_HANDLE;
        VkQueue vk_graphics_queue_ = VK_NULL_HANDLE;

        VkSwapchainKHR vk_swapchain_ = VK_NULL_HANDLE;
        std::vector<VkImage> swapchain_images_;
        std::vector<VkImageView> swapchain_image_views_;
        VkFormat swapchain_image_format_;
        VkExtent2D swapchain_extent_;

        VkRenderPass vk_render_pass_ = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> swapchain_framebuffers_;

        VkCommandPool vk_command_pool_ = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> command_buffers_;

        VkDescriptorPool vk_descriptor_pool_ = VK_NULL_HANDLE;

        std::vector<VkSemaphore> image_available_semaphores_;
        std::vector<VkSemaphore> render_finished_semaphores_;
        std::vector<VkFence> in_flight_fences_;

        uint32_t current_frame_ = 0;
        uint32_t image_index_ = 0;

        uint32_t graphics_queue_family_ = UINT32_MAX;

        std::vector<const char *> instance_layers_;
        std::vector<const char *> instance_extensions_;
        std::vector<const char *> device_extensions_;

        VkDebugUtilsMessengerEXT vk_debug_utils_messenger_; // 检查初始化流程中所有其他步骤
    };
}

#endif /* __VULKAN_RENDERER_GPU_DEVICE_H__ */
