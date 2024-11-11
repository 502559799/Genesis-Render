/*
 * @Author: Yan Dongbao
 * @Date: 2024-11-12 15:09:04
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-11-13 21:19:10
 * @version:
 */
#include "graphics/gpu_device.hpp"
#include "foundation_layer/memory.hpp"

#define VMA_IMPLEMENTATION
#include "external/vk_mem_alloc.h"

#include <SDL.h>
#include <SDL_vulkan.h>

namespace Genesis
{
#define checkResult(result) \
    GENESISASSERT_MSG(result == VK_SUCCESS, "Vulkan assert error code: %u", result)

    VkDebugUtilsMessengerCreateInfoEXT createDebugMessenger()
    {
        static PFN_vkDebugUtilsMessengerCallbackEXT debugCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData)
            -> VkBool32
        {
            GenesisPrint("Message ID: %s %i\nMessage: %s\n\n", pCallbackData->messageIdNumber, pCallbackData->messageIdNumber, pCallbackData->pMessage);
            return VK_FALSE;
        };

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debugCallback};

        return debugCreateInfo;
    }

    VkResult GpuDevice::checkInstanceExtensionSupport(const std::vector<const char *> &extensions)
    {
        VkResult res = VK_SUCCESS;
        uint32_t extensionCount = 0;
        std::vector<VkExtensionProperties> availableExtensions;
        if (res = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr))
        {
            GenesisPrint("vkEnumerateInstanceExtensionProperties failed\n");
            return res;
        }
        if (extensionCount)
        {
            
        }

        return res;
    }

    VkResult GpuDevice::createVulkanInstance(const void *pNext, VkInstanceCreateFlags flags)
    {
        VkApplicationInfo applicationInfo = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = pNext,
            .pApplicationName = "GENESIS_VULKAN_APP",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "GENESIS_VULKAN_TUTORIAL",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = VK_API_VERSION_1_0};

        VkInstanceCreateInfo instanceCreateInfo = {
            .pNext = pNext,
            .flags = flags | VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR,
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &applicationInfo,
#if defined(VULKAN_DEBUG_REPORT)
            .enabledLayerCount = uint32_t(instance_layers_.size()),
            .ppEnabledLayerNames = instance_layers_.data(),
#else
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
#endif
            .enabledExtensionCount = uint32_t(instance_extensions_.size()),
            .ppEnabledExtensionNames = instance_extensions_.data(),
        };

#if defined(VULKAN_DEBUG_REPORT)
        const auto debug_create_info = createDebugMessenger();
#endif

        return vkCreateInstance(&instanceCreateInfo, nullptr, &vk_instance_);
    }

    void
    GpuDevice::init(const DeviceCreateInfo &config)
    {
        GenesisPrint("init GpuDevice\n");
        allocator_ = config.m_allocator;

        // init vulkan instance
        VkResult result;

        // create instance
        result = createVulkanInstance();
        checkResult(result);

#if defined(VULKAN_DEBUG_REPORT)


        PFN_vkCreateDebugUtilsMessengerEXT vKcreateDebugUtilsMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(vk_instance_, "vkCreateDebugUtilsMessengerEXT"));
        VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info = createDebugMessenger();
        if (vKcreateDebugUtilsMessenger)
        {
            result = vKcreateDebugUtilsMessenger(vk_instance_, &debug_messenger_create_info, vk_allocation_callbacks_, &vk_debug_utils_messenger_);
            if (result)
            {
                GenesisPrint("vkCreateDebugUtilsMessenger failed\n");
            }
        }
#endif
    }
}