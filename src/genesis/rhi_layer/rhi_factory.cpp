/*
 * @Descripttion: RHI 设备工厂
 */
#include "rhi_layer/rhi_device.hpp"
#include "rhi_layer/vulkan/vulkan_device.hpp"
#include "rhi_layer/opengl/opengl_device.hpp"

namespace Genesis
{
    namespace rhi
    {
        RHIDevice *createRHIDevice(Backend backend)
        {
            if (backend == Backend::Vulkan)
                return new VulkanDevice();
            return new OpenGLDevice();
        }

        void destroyRHIDevice(RHIDevice *device)
        {
            delete device;
        }
    } // namespace rhi
} // namespace Genesis
