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

#include "external/imgui/imgui_impl_vulkan.h"

#include <SDL.h>
#include <SDL_vulkan.h>
#include <algorithm>

namespace Genesis
{
#define checkResult(result) \
    GENESISASSERT_MSG(result == VK_SUCCESS, "Vulkan assert error code: %u", result)

    VkDebugUtilsMessengerCreateInfoEXT createDebugMessenger()
    {
        static PFN_vkDebugUtilsMessengerCallbackEXT debugCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData) -> VkBool32
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
            availableExtensions.resize(extensionCount);
            vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());
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

    GpuDevice *GpuDevice::instance()
    {
        static GpuDevice s_gpu_device;
        return &s_gpu_device;
    }

    uint32_t findQueueFamily(VkPhysicalDevice device, VkQueueFlagBits queueType)
    {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; i++)
        {
            if (queueFamilies[i].queueFlags & queueType)
            {
                return i;
            }
        }
        return UINT32_MAX;
    }

    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
    {
        for (const auto& availableFormat : availableFormats)
        {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return availableFormat;
            }
        }
        return availableFormats[0];
    }

    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
    {
        for (const auto& availablePresentMode : availablePresentModes)
        {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return availablePresentMode;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height)
    {
        if (capabilities.currentExtent.width != UINT32_MAX)
        {
            return capabilities.currentExtent;
        }
        else
        {
            VkExtent2D actualExtent = {width, height};
            actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
            actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));
            return actualExtent;
        }
    }

    void
    GpuDevice::createSwapchain()
    {
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_physical_device_, vk_surface_, &capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(vk_physical_device_, vk_surface_, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> availableFormats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(vk_physical_device_, vk_surface_, &formatCount, availableFormats.data());

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(vk_physical_device_, vk_surface_, &presentModeCount, nullptr);
        std::vector<VkPresentModeKHR> availablePresentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(vk_physical_device_, vk_surface_, &presentModeCount, availablePresentModes.data());

        VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(availableFormats);
        VkPresentModeKHR presentMode = choosePresentMode(availablePresentModes);
        VkExtent2D extent = chooseSwapExtent(capabilities, window_width_, window_height_);

        uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
        {
            imageCount = capabilities.maxImageCount;
        }

        swapchain_image_format_ = surfaceFormat.format;
        swapchain_extent_ = extent;

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = vk_surface_;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        VkResult result = vkCreateSwapchainKHR(vk_device_, &createInfo, nullptr, &vk_swapchain_);
        checkResult(result);

        // get swapchain images
        vkGetSwapchainImagesKHR(vk_device_, vk_swapchain_, &imageCount, nullptr);
        swapchain_images_.resize(imageCount);
        vkGetSwapchainImagesKHR(vk_device_, vk_swapchain_, &imageCount, swapchain_images_.data());

        // create image views
        swapchain_image_views_.resize(imageCount);
        for (size_t i = 0; i < swapchain_images_.size(); i++)
        {
            VkImageViewCreateInfo imageViewCreateInfo{};
            imageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imageViewCreateInfo.image = swapchain_images_[i];
            imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imageViewCreateInfo.format = swapchain_image_format_;
            imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
            imageViewCreateInfo.subresourceRange.levelCount = 1;
            imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
            imageViewCreateInfo.subresourceRange.layerCount = 1;

            result = vkCreateImageView(vk_device_, &imageViewCreateInfo, nullptr, &swapchain_image_views_[i]);
            checkResult(result);
        }
    }

    void GpuDevice::createRenderPass()
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapchain_image_format_;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassCreateInfo{};
        renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCreateInfo.attachmentCount = 1;
        renderPassCreateInfo.pAttachments = &colorAttachment;
        renderPassCreateInfo.subpassCount = 1;
        renderPassCreateInfo.pSubpasses = &subpass;
        renderPassCreateInfo.dependencyCount = 1;
        renderPassCreateInfo.pDependencies = &dependency;

        VkResult result = vkCreateRenderPass(vk_device_, &renderPassCreateInfo, nullptr, &vk_render_pass_);
        checkResult(result);
    }

    void GpuDevice::createFramebuffers()
    {
        swapchain_framebuffers_.resize(swapchain_image_views_.size());

        for (size_t i = 0; i < swapchain_image_views_.size(); i++)
        {
            VkImageView attachments[] = {swapchain_image_views_[i]};

            VkFramebufferCreateInfo framebufferCreateInfo{};
            framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferCreateInfo.renderPass = vk_render_pass_;
            framebufferCreateInfo.attachmentCount = 1;
            framebufferCreateInfo.pAttachments = attachments;
            framebufferCreateInfo.width = swapchain_extent_.width;
            framebufferCreateInfo.height = swapchain_extent_.height;
            framebufferCreateInfo.layers = 1;

            VkResult result = vkCreateFramebuffer(vk_device_, &framebufferCreateInfo, nullptr, &swapchain_framebuffers_[i]);
            checkResult(result);
        }
    }

    void GpuDevice::createCommandPool()
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = graphics_queue_family_;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        VkResult result = vkCreateCommandPool(vk_device_, &poolInfo, nullptr, &vk_command_pool_);
        checkResult(result);

        // create command buffers
        command_buffers_.resize(swapchain_framebuffers_.size());

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = vk_command_pool_;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = (uint32_t)command_buffers_.size();

        result = vkAllocateCommandBuffers(vk_device_, &allocInfo, command_buffers_.data());
        checkResult(result);
    }

    void GpuDevice::createSyncObjects()
    {
        image_available_semaphores_.resize(MAX_FRAMES_IN_FLIGHT);
        render_finished_semaphores_.resize(MAX_FRAMES_IN_FLIGHT);
        in_flight_fences_.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            vkCreateSemaphore(vk_device_, &semaphoreInfo, nullptr, &image_available_semaphores_[i]);
            vkCreateSemaphore(vk_device_, &semaphoreInfo, nullptr, &render_finished_semaphores_[i]);
            vkCreateFence(vk_device_, &fenceInfo, nullptr, &in_flight_fences_[i]);
        }
    }

    void GpuDevice::initImgui()
    {
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = vk_instance_;
        init_info.PhysicalDevice = vk_physical_device_;
        init_info.Device = vk_device_;
        init_info.QueueFamily = graphics_queue_family_;
        init_info.Queue = vk_graphics_queue_;
        init_info.DescriptorPool = vk_descriptor_pool_;
        init_info.MinImageCount = (uint32_t)swapchain_images_.size();
        init_info.ImageCount = (uint32_t)swapchain_images_.size();
        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        ImGui_ImplVulkan_Init(&init_info, vk_render_pass_);

        // upload fonts
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = vk_command_pool_;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(vk_device_, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        ImGui_ImplVulkan_CreateFontsTexture(commandBuffer);

        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(vk_graphics_queue_, 1, &submitInfo, VK_NULL_HANDLE);
        vkDeviceWaitIdle(vk_device_);

        vkFreeCommandBuffers(vk_device_, vk_command_pool_, 1, &commandBuffer);

        ImGui_ImplVulkan_DestroyFontUploadObjects();
    }

    VkCommandBuffer GpuDevice::beginSingleTimeCommands()
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = vk_command_pool_;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(vk_device_, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        return commandBuffer;
    }

    void GpuDevice::endSingleTimeCommands(VkCommandBuffer commandBuffer)
    {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(vk_graphics_queue_, 1, &submitInfo, VK_NULL_HANDLE);
        vkDeviceWaitIdle(vk_device_);

        vkFreeCommandBuffers(vk_device_, vk_command_pool_, 1, &commandBuffer);
    }

    void GpuDevice::createDescriptorPool()
    {
        VkDescriptorPoolSize pool_sizes[] =
        {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };
        const uint32_t pool_size_count = sizeof(pool_sizes) / sizeof(pool_sizes[0]);

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000 * pool_size_count;
        pool_info.poolSizeCount = pool_size_count;
        pool_info.pPoolSizes = pool_sizes;

        VkResult result = vkCreateDescriptorPool(vk_device_, &pool_info, nullptr, &vk_descriptor_pool_);
        checkResult(result);
    }

    void
    GpuDevice::init(const DeviceCreateInfo &config)
    {
        GenesisPrint("init GpuDevice\n");
        allocator_ = config.m_allocator;
        window_ = config.m_window;
        window_width_ = config.m_window_size.first;
        window_height_ = config.m_window_size.second;

        // init vulkan instance
        VkResult result;

        // get required extensions from SDL
        unsigned int extensionCount = 0;
        if (!SDL_Vulkan_GetInstanceExtensions((SDL_Window*)window_, &extensionCount, nullptr))
        {
            GenesisPrint("SDL_Vulkan_GetInstanceExtensions failed\n");
            return;
        }
        std::vector<const char*> extensions(extensionCount);
        if (!SDL_Vulkan_GetInstanceExtensions((SDL_Window*)window_, &extensionCount, extensions.data()))
        {
            GenesisPrint("SDL_Vulkan_GetInstanceExtensions failed\n");
            return;
        }

        // add portability extension for macOS
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

        instance_extensions_ = extensions;

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

        // create surface
        if (!SDL_Vulkan_CreateSurface((SDL_Window*)window_, vk_instance_, &vk_surface_))
        {
            GenesisPrint("SDL_Vulkan_CreateSurface failed\n");
            return;
        }

        // pick physical device
        uint32_t physicalDeviceCount = 0;
        vkEnumeratePhysicalDevices(vk_instance_, &physicalDeviceCount, nullptr);
        if (physicalDeviceCount == 0)
        {
            GenesisPrint("Failed to find GPUs with Vulkan support!\n");
            return;
        }
        std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
        vkEnumeratePhysicalDevices(vk_instance_, &physicalDeviceCount, physicalDevices.data());
        vk_physical_device_ = physicalDevices[0];

        // find queue families
        graphics_queue_family_ = findQueueFamily(vk_physical_device_, VK_QUEUE_GRAPHICS_BIT);
        if (graphics_queue_family_ == UINT32_MAX)
        {
            GenesisPrint("Failed to find graphics queue family!\n");
            return;
        }

        // create logical device
        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = graphics_queue_family_;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            "VK_KHR_portability_subset"
        };

        VkDeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

        result = vkCreateDevice(vk_physical_device_, &deviceCreateInfo, nullptr, &vk_device_);
        checkResult(result);

        // get queues
        vkGetDeviceQueue(vk_device_, graphics_queue_family_, 0, &vk_graphics_queue_);

        // create swapchain
        createSwapchain();

        // create render pass for ImGui
        createRenderPass();

        // create framebuffers
        createFramebuffers();

        // create command pool and command buffers
        createCommandPool();

        // create descriptor pool
        createDescriptorPool();

        // create sync objects
        createSyncObjects();
    }

    void GpuDevice::beginFrame()
    {
        vkWaitForFences(vk_device_, 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);

        VkResult result = vkAcquireNextImageKHR(vk_device_, vk_swapchain_, UINT64_MAX, image_available_semaphores_[current_frame_], VK_NULL_HANDLE, &image_index_);
        if (result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            // recreate swapchain
            return;
        }
        else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            GenesisPrint("Failed to acquire swapchain image!\n");
            return;
        }

        vkResetFences(vk_device_, 1, &in_flight_fences_[current_frame_]);

        vkResetCommandBuffer(command_buffers_[image_index_], 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        result = vkBeginCommandBuffer(command_buffers_[image_index_], &beginInfo);
        checkResult(result);

        // begin render pass
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = vk_render_pass_;
        renderPassInfo.framebuffer = swapchain_framebuffers_[image_index_];
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = swapchain_extent_;

        VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(command_buffers_[image_index_], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    }

    void GpuDevice::endFrame()
    {
        vkCmdEndRenderPass(command_buffers_[image_index_]);

        VkResult result = vkEndCommandBuffer(command_buffers_[image_index_]);
        checkResult(result);

        // submit command buffer
        VkSemaphore waitSemaphores[] = {image_available_semaphores_[current_frame_]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        VkSemaphore signalSemaphores[] = {render_finished_semaphores_[current_frame_]};

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &command_buffers_[image_index_];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        result = vkQueueSubmit(vk_graphics_queue_, 1, &submitInfo, in_flight_fences_[current_frame_]);
        checkResult(result);

        // present image
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &vk_swapchain_;
        presentInfo.pImageIndices = &image_index_;

        result = vkQueuePresentKHR(vk_graphics_queue_, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            // recreate swapchain
        }
        else if (result != VK_SUCCESS)
        {
            GenesisPrint("Failed to present swapchain image!\n");
            return;
        }

        current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void GpuDevice::shutdown()
    {
        vkDeviceWaitIdle(vk_device_);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            vkDestroySemaphore(vk_device_, render_finished_semaphores_[i], nullptr);
            vkDestroySemaphore(vk_device_, image_available_semaphores_[i], nullptr);
            vkDestroyFence(vk_device_, in_flight_fences_[i], nullptr);
        }

        vkDestroyDescriptorPool(vk_device_, vk_descriptor_pool_, nullptr);

        vkDestroyCommandPool(vk_device_, vk_command_pool_, nullptr);

        for (auto framebuffer : swapchain_framebuffers_)
        {
            vkDestroyFramebuffer(vk_device_, framebuffer, nullptr);
        }

        vkDestroyRenderPass(vk_device_, vk_render_pass_, nullptr);

        for (auto imageView : swapchain_image_views_)
        {
            vkDestroyImageView(vk_device_, imageView, nullptr);
        }

        vkDestroySwapchainKHR(vk_device_, vk_swapchain_, nullptr);

        vkDestroyDevice(vk_device_, nullptr);

#if defined(VULKAN_DEBUG_REPORT)
        auto vkDestroyDebugUtilsMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(vk_instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (vkDestroyDebugUtilsMessenger)
        {
            vkDestroyDebugUtilsMessenger(vk_instance_, vk_debug_utils_messenger_, vk_allocation_callbacks_);
        }
#endif

        vkDestroySurfaceKHR(vk_instance_, vk_surface_, nullptr);
        vkDestroyInstance(vk_instance_, nullptr);
    }
}