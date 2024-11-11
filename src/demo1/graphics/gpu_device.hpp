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

namespace Genesis
{
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

    private:
        VkResult createVulkanInstance(const void *pNext = nullptr, VkInstanceCreateFlags flags = 0);
        VkResult checkInstanceExtensionSupport(const std::vector<const char *> &extensions);

    private:
        Allocator *allocator_ = nullptr;
        VkAllocationCallbacks *vk_allocation_callbacks_ = nullptr;

        VkInstance vk_instance_;

        std::vector<const char *> instance_layers_;
        std::vector<const char *> instance_extensions_;
        std::vector<const char *> device_extensions_;

        VkDebugUtilsMessengerEXT vk_debug_utils_messenger_; // 检查初始化流程中所有其他步骤
    };
}

#endif /* __VULKAN_RENDERER_GPU_DEVICE_H__ */
