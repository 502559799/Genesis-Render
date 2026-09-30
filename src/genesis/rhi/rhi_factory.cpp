/*
 * Genesis Render - RHI factory
 *
 * Single place that knows about every concrete backend.  Renderer code only
 * ever calls RHIDevice::create().
 */
#include "rhi/rhi.hpp"
#include "foundation_layer/genesis_log.hpp"

#if GENESIS_RHI_OPENGL
#include "rhi/opengl/gl_backend.hpp"
#endif
#if GENESIS_RHI_VULKAN
#include "rhi/vulkan/vk_backend.hpp"
#endif
#if GENESIS_RHI_METAL
#include "rhi/metal/metal_backend.hpp"
#endif

namespace Genesis
{
    namespace RHI
    {
        RHIDevice *RHIDevice::create(const DeviceDesc &desc)
        {
#if GENESIS_RHI_METAL
            if (desc.backend == BackendType::Metal)
            {
                RHIDevice *device = createMetalDevice();
                if (!device->initialize(desc))
                {
                    GEN_LOG_ERROR("[RHI] Metal backend initialisation failed");
                    device->shutdown();
                    delete device;
                    return nullptr;
                }
                return device;
            }
#endif
#if GENESIS_RHI_VULKAN
            if (desc.backend == BackendType::Vulkan)
            {
                VkDeviceRHI *device = new VkDeviceRHI();
                if (!device->initialize(desc))
                {
                    GEN_LOG_ERROR("[RHI] Vulkan backend initialisation failed");
                    device->shutdown();
                    delete device;
                    return nullptr;
                }
                return device;
            }
#endif
#if GENESIS_RHI_OPENGL
            if (desc.backend == BackendType::OpenGL)
            {
                GLDevice *device = new GLDevice();
                if (!device->initialize(desc))
                {
                    GEN_LOG_ERROR("[RHI] OpenGL backend initialisation failed");
                    device->shutdown();
                    delete device;
                    return nullptr;
                }
                return device;
            }
#endif
            GEN_LOG_ERROR("[RHI] requested backend '%s' is not compiled in", backendTypeName(desc.backend));
            return nullptr;
        }

        // Compile-time visibility of what is available.
        bool backendAvailable(BackendType type)
        {
#if GENESIS_RHI_METAL
            if (type == BackendType::Metal) return true;
#endif
#if GENESIS_RHI_VULKAN
            if (type == BackendType::Vulkan) return true;
#endif
#if GENESIS_RHI_OPENGL
            if (type == BackendType::OpenGL) return true;
#endif
            (void)type;
            return false;
        }

    } // namespace RHI
} // namespace Genesis
