/*
 * @Descripttion: Vulkan 后端渲染设备实现
 */
#include "rhi_layer/vulkan/vulkan_device.hpp"

#include "foundation_layer/log.hpp"
#include "foundation_layer/assert.hpp"

#define VMA_IMPLEMENTATION
#include "external/vk_mem_alloc.h"

#include <SDL.h>
#include <SDL_vulkan.h>
#include <cstring>

#include "external/stb_image_write.h"

namespace Genesis
{
    namespace rhi
    {
#define VK_CHECK(result) GENESISASSERT_MSG((result) == VK_SUCCESS, "Vulkan error code: %d", (int)(result))

        //-----------------------------------------------------------
        // 类型转换
        //-----------------------------------------------------------
        static VkFormat toVkFormat(Format format)
        {
            switch (format)
            {
            case Format::R8_UNorm: return VK_FORMAT_R8_UNORM;
            case Format::RG8_UNorm: return VK_FORMAT_R8G8_UNORM;
            case Format::RGBA8_UNorm: return VK_FORMAT_R8G8B8A8_UNORM;
            case Format::BGRA8_UNorm: return VK_FORMAT_B8G8R8A8_UNORM;
            case Format::RGBA8_sRGB: return VK_FORMAT_R8G8B8A8_SRGB;
            case Format::BGRA8_sRGB: return VK_FORMAT_B8G8R8A8_SRGB;
            case Format::RG16_Float: return VK_FORMAT_R16G16_SFLOAT;
            case Format::RGBA16_Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
            case Format::RGBA32_Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
            case Format::D32_Float: return VK_FORMAT_D32_SFLOAT;
            case Format::D24_S8: return VK_FORMAT_D24_UNORM_S8_UINT;
            default: return VK_FORMAT_UNDEFINED;
            }
        }

        static Format fromVkFormat(VkFormat format)
        {
            switch (format)
            {
            case VK_FORMAT_B8G8R8A8_UNORM: return Format::BGRA8_UNorm;
            case VK_FORMAT_R8G8B8A8_UNORM: return Format::RGBA8_UNorm;
            case VK_FORMAT_B8G8R8A8_SRGB: return Format::BGRA8_sRGB;
            case VK_FORMAT_R8G8B8A8_SRGB: return Format::RGBA8_sRGB;
            default: return Format::BGRA8_UNorm;
            }
        }

        static VkFilter toVkFilter(Filter filter)
        {
            return filter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        }

        static VkSamplerAddressMode toVkAddressMode(AddressMode mode)
        {
            switch (mode)
            {
            case AddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case AddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case AddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            }
        }

        static VkCompareOp toVkCompareOp(CompareOp op)
        {
            switch (op)
            {
            case CompareOp::Never: return VK_COMPARE_OP_NEVER;
            case CompareOp::Less: return VK_COMPARE_OP_LESS;
            case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
            case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
            case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
            case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
            default: return VK_COMPARE_OP_LESS_OR_EQUAL;
            }
        }

        static VkCullModeFlags toVkCullMode(CullMode mode)
        {
            switch (mode)
            {
            case CullMode::None: return VK_CULL_MODE_NONE;
            case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
            case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
            default: return VK_CULL_MODE_BACK_BIT;
            }
        }

        static uint32_t vertexFormatSize(VertexFormat format)
        {
            switch (format)
            {
            case VertexFormat::Float: return 4;
            case VertexFormat::Float2: return 8;
            case VertexFormat::Float3: return 12;
            case VertexFormat::Float4: return 16;
            default: return 12;
            }
        }

        static VkFormat toVkVertexFormat(VertexFormat format)
        {
            switch (format)
            {
            case VertexFormat::Float: return VK_FORMAT_R32_SFLOAT;
            case VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
            case VertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
            case VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
            default: return VK_FORMAT_R32G32B32_SFLOAT;
            }
        }

        //-----------------------------------------------------------
        // 初始化
        //-----------------------------------------------------------
        bool VulkanDevice::createInstance()
        {
            uint32_t extension_count = 0;
            if (!SDL_Vulkan_GetInstanceExtensions((SDL_Window *)m_window, &extension_count, nullptr))
            {
                GenesisPrint("SDL_Vulkan_GetInstanceExtensions failed\n");
                return false;
            }
            std::vector<const char *> extensions(extension_count);
            SDL_Vulkan_GetInstanceExtensions((SDL_Window *)m_window, &extension_count, extensions.data());

#ifdef __APPLE__
            extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

            VkApplicationInfo app_info{};
            app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            app_info.pApplicationName = "GENESIS_RHI";
            app_info.apiVersion = VK_API_VERSION_1_0;

            VkInstanceCreateInfo create_info{};
            create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            create_info.pApplicationInfo = &app_info;
            create_info.enabledExtensionCount = (uint32_t)extensions.size();
            create_info.ppEnabledExtensionNames = extensions.data();
#ifdef __APPLE__
            create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

            if (m_enable_debug)
            {
                static const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
                uint32_t layer_count = 0;
                vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
                std::vector<VkLayerProperties> available(layer_count);
                vkEnumerateInstanceLayerProperties(&layer_count, available.data());
                for (auto &lp : available)
                {
                    if (strcmp(lp.layerName, layers[0]) == 0)
                    {
                        create_info.enabledLayerCount = 1;
                        create_info.ppEnabledLayerNames = layers;
                        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                        create_info.enabledExtensionCount = (uint32_t)extensions.size();
                        break;
                    }
                }
            }

            VkResult result = vkCreateInstance(&create_info, nullptr, &m_instance);
            VK_CHECK(result);
            return result == VK_SUCCESS;
        }

        bool VulkanDevice::init(const DeviceCreateInfo &info)
        {
            GenesisPrint("[RHI:Vulkan] init device (%ux%u)\n", info.m_width, info.m_height);
            m_window = info.m_window;
            m_width = info.m_width;
            m_height = info.m_height;
            m_enable_debug = info.m_enable_debug;

            if (!createInstance())
                return false;

            if (!SDL_Vulkan_CreateSurface((SDL_Window *)m_window, m_instance, &m_surface))
            {
                GenesisPrint("[RHI:Vulkan] SDL_Vulkan_CreateSurface failed\n");
                return false;
            }

            // 物理设备: 选择支持 graphics+present 且队列族完备的第一块
            uint32_t device_count = 0;
            vkEnumeratePhysicalDevices(m_instance, &device_count, nullptr);
            if (device_count == 0)
            {
                GenesisPrint("[RHI:Vulkan] no vulkan device found\n");
                return false;
            }
            std::vector<VkPhysicalDevice> devices(device_count);
            vkEnumeratePhysicalDevices(m_instance, &device_count, devices.data());

            m_physical_device = VK_NULL_HANDLE;
            for (auto &dev : devices)
            {
                uint32_t family_count = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(dev, &family_count, nullptr);
                std::vector<VkQueueFamilyProperties> families(family_count);
                vkGetPhysicalDeviceQueueFamilyProperties(dev, &family_count, families.data());

                for (uint32_t i = 0; i < family_count; ++i)
                {
                    VkBool32 present_support = VK_FALSE;
                    vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, m_surface, &present_support);
                    if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present_support)
                    {
                        m_physical_device = dev;
                        m_graphics_queue_family = i;
                        break;
                    }
                }
                if (m_physical_device != VK_NULL_HANDLE)
                    break;
            }
            if (m_physical_device == VK_NULL_HANDLE)
            {
                GenesisPrint("[RHI:Vulkan] no suitable gpu (graphics+present) found\n");
                return false;
            }

            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(m_physical_device, &props);
            GenesisPrint("[RHI:Vulkan] physical device: %s\n", props.deviceName);

            // 逻辑设备
            float queue_priority = 1.0f;
            VkDeviceQueueCreateInfo queue_info{};
            queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_info.queueFamilyIndex = m_graphics_queue_family;
            queue_info.queueCount = 1;
            queue_info.pQueuePriorities = &queue_priority;

            std::vector<const char *> device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
#ifdef __APPLE__
            device_extensions.push_back("VK_KHR_portability_subset");
#endif

            VkDeviceCreateInfo device_info{};
            device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            device_info.pQueueCreateInfos = &queue_info;
            device_info.queueCreateInfoCount = 1;
            device_info.enabledExtensionCount = (uint32_t)device_extensions.size();
            device_info.ppEnabledExtensionNames = device_extensions.data();

            VK_CHECK(vkCreateDevice(m_physical_device, &device_info, nullptr, &m_device));
            vkGetDeviceQueue(m_device, m_graphics_queue_family, 0, &m_graphics_queue);

            // VMA
            VmaAllocatorCreateInfo vma_info{};
            vma_info.instance = m_instance;
            vma_info.physicalDevice = m_physical_device;
            vma_info.device = m_device;
            VK_CHECK(vmaCreateAllocator(&vma_info, &m_vma));

            // 命令池与帧命令缓冲 (必须在任何纹理创建之前: 布局转换依赖一次性命令)
            {
                VkCommandPoolCreateInfo pool_info{};
                pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                pool_info.queueFamilyIndex = m_graphics_queue_family;
                pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                VK_CHECK(vkCreateCommandPool(m_device, &pool_info, nullptr, &m_command_pool));

                VkCommandBufferAllocateInfo alloc_info{};
                alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                alloc_info.commandPool = m_command_pool;
                alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                alloc_info.commandBufferCount = kMaxFramesInFlight;
                VK_CHECK(vkAllocateCommandBuffers(m_device, &alloc_info, m_frame_cmds));
            }

            if (!createSwapchain())
                return false;

            // 交换链配套深度纹理
            TextureCreateInfo depth_info{};
            depth_info.m_format = Format::D32_Float;
            depth_info.m_width = m_width;
            depth_info.m_height = m_height;
            depth_info.m_render_target = true;
            depth_info.m_name = "swapchain_depth";
            m_depth_texture = createTexture(depth_info);

            m_main_layout.m_color_count = 1;
            m_main_layout.m_color_formats[0] = m_swapchain_rhi_format;
            m_main_layout.m_depth_format = Format::D32_Float;

            // 交换链帧缓冲 (使用 clear 变体 render pass 创建, load 变体兼容)
            VkRenderPass main_pass = getOrCreateRenderPass(m_main_layout, true);
            VkTextureRes &depth_res = m_textures.get(m_depth_texture.m_handle);
            m_swapchain_framebuffers.resize(m_swapchain_views.size());
            for (size_t i = 0; i < m_swapchain_views.size(); ++i)
            {
                VkImageView attachments[] = {m_swapchain_views[i], depth_res.m_default_view};
                VkFramebufferCreateInfo fb_info{};
                fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                fb_info.renderPass = main_pass;
                fb_info.attachmentCount = 2;
                fb_info.pAttachments = attachments;
                fb_info.width = m_width;
                fb_info.height = m_height;
                fb_info.layers = 1;
                VK_CHECK(vkCreateFramebuffer(m_device, &fb_info, nullptr, &m_swapchain_framebuffers[i]));
            }

            // 描述符池
            VkDescriptorPoolSize pool_sizes[] = {
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024},
                {VK_DESCRIPTOR_TYPE_SAMPLER, 256},
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 256},
            };
            VkDescriptorPoolCreateInfo desc_pool_info{};
            desc_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            desc_pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            desc_pool_info.maxSets = 1024;
            desc_pool_info.poolSizeCount = sizeof(pool_sizes) / sizeof(pool_sizes[0]);
            desc_pool_info.pPoolSizes = pool_sizes;
            VK_CHECK(vkCreateDescriptorPool(m_device, &desc_pool_info, nullptr, &m_descriptor_pool));

            // 同步对象
            VkSemaphoreCreateInfo sem_info{};
            sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            VkFenceCreateInfo fence_info{};
            fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            for (int i = 0; i < kMaxFramesInFlight; ++i)
            {
                vkCreateSemaphore(m_device, &sem_info, nullptr, &m_image_available[i]);
                vkCreateSemaphore(m_device, &sem_info, nullptr, &m_render_finished[i]);
                vkCreateFence(m_device, &fence_info, nullptr, &m_in_flight[i]);
            }

            return true;
        }

        bool VulkanDevice::createSwapchain()
        {
            VkSurfaceCapabilitiesKHR capabilities;
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical_device, m_surface, &capabilities);

            uint32_t format_count = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &format_count, nullptr);
            std::vector<VkSurfaceFormatKHR> formats(format_count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &format_count, formats.data());

            VkSurfaceFormatKHR surface_format = formats[0];
            for (auto &f : formats)
            {
                if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                {
                    surface_format = f;
                    break;
                }
            }

            VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

            VkExtent2D extent = capabilities.currentExtent;
            if (extent.width == UINT32_MAX)
            {
                extent.width = m_width;
                extent.height = m_height;
            }
            m_width = extent.width;
            m_height = extent.height;

            uint32_t image_count = capabilities.minImageCount + 1;
            if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
                image_count = capabilities.maxImageCount;

            VkSwapchainCreateInfoKHR create_info{};
            create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            create_info.surface = m_surface;
            create_info.minImageCount = image_count;
            create_info.imageFormat = surface_format.format;
            create_info.imageColorSpace = surface_format.colorSpace;
            create_info.imageExtent = extent;
            create_info.imageArrayLayers = 1;
            // TRANSFER_SRC 用于截图回读
            create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            create_info.preTransform = capabilities.currentTransform;
            create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            create_info.presentMode = present_mode;
            create_info.clipped = VK_TRUE;

            VK_CHECK(vkCreateSwapchainKHR(m_device, &create_info, nullptr, &m_swapchain));

            m_swapchain_format = surface_format.format;
            m_swapchain_rhi_format = fromVkFormat(surface_format.format);

            vkGetSwapchainImagesKHR(m_device, m_swapchain, &image_count, nullptr);
            m_swapchain_images.resize(image_count);
            vkGetSwapchainImagesKHR(m_device, m_swapchain, &image_count, m_swapchain_images.data());

            m_swapchain_views.resize(image_count);
            for (size_t i = 0; i < image_count; ++i)
            {
                VkImageViewCreateInfo view_info{};
                view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                view_info.image = m_swapchain_images[i];
                view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
                view_info.format = m_swapchain_format;
                view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                VK_CHECK(vkCreateImageView(m_device, &view_info, nullptr, &m_swapchain_views[i]));
            }
            return true;
        }

        void VulkanDevice::destroySwapchain()
        {
            for (auto fb : m_swapchain_framebuffers)
                vkDestroyFramebuffer(m_device, fb, nullptr);
            m_swapchain_framebuffers.clear();
            for (auto view : m_swapchain_views)
                vkDestroyImageView(m_device, view, nullptr);
            m_swapchain_views.clear();
            if (m_swapchain != VK_NULL_HANDLE)
            {
                vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
                m_swapchain = VK_NULL_HANDLE;
            }
        }

        void VulkanDevice::shutdown()
        {
            if (m_device == VK_NULL_HANDLE)
                return;
            vkDeviceWaitIdle(m_device);

            destroySwapchain();

            for (auto &pair : m_render_pass_cache)
                vkDestroyRenderPass(m_device, pair.second, nullptr);
            m_render_pass_cache.clear();

            // 销毁所有池内资源
            for (size_t i = 0; i < m_buffers.m_items.size(); ++i)
            {
                auto &res = m_buffers.m_items[i];
                if (res.m_buffer != VK_NULL_HANDLE)
                    vmaDestroyBuffer(m_vma, res.m_buffer, res.m_allocation);
            }
            for (size_t i = 0; i < m_textures.m_items.size(); ++i)
            {
                auto &res = m_textures.m_items[i];
                if (res.m_default_view != VK_NULL_HANDLE)
                    vkDestroyImageView(m_device, res.m_default_view, nullptr);
                if (res.m_image != VK_NULL_HANDLE)
                    vmaDestroyImage(m_vma, res.m_image, res.m_allocation);
            }
            for (size_t i = 0; i < m_samplers.m_items.size(); ++i)
            {
                if (m_samplers.m_items[i].m_sampler != VK_NULL_HANDLE)
                    vkDestroySampler(m_device, m_samplers.m_items[i].m_sampler, nullptr);
            }
            for (size_t i = 0; i < m_shaders.m_items.size(); ++i)
            {
                if (m_shaders.m_items[i].m_module != VK_NULL_HANDLE)
                    vkDestroyShaderModule(m_device, m_shaders.m_items[i].m_module, nullptr);
            }
            for (size_t i = 0; i < m_pipelines.m_items.size(); ++i)
            {
                auto &res = m_pipelines.m_items[i];
                if (res.m_pipeline != VK_NULL_HANDLE)
                    vkDestroyPipeline(m_device, res.m_pipeline, nullptr);
                if (res.m_layout != VK_NULL_HANDLE)
                    vkDestroyPipelineLayout(m_device, res.m_layout, nullptr);
            }
            for (size_t i = 0; i < m_set_layouts.m_items.size(); ++i)
            {
                if (m_set_layouts.m_items[i].m_layout != VK_NULL_HANDLE)
                    vkDestroyDescriptorSetLayout(m_device, m_set_layouts.m_items[i].m_layout, nullptr);
            }
            for (size_t i = 0; i < m_render_targets.m_items.size(); ++i)
            {
                auto &res = m_render_targets.m_items[i];
                if (res.m_framebuffer != VK_NULL_HANDLE)
                    vkDestroyFramebuffer(m_device, res.m_framebuffer, nullptr);
                for (auto view : res.m_owned_views)
                    vkDestroyImageView(m_device, view, nullptr);
            }

            for (int i = 0; i < kMaxFramesInFlight; ++i)
            {
                vkDestroySemaphore(m_device, m_image_available[i], nullptr);
                vkDestroySemaphore(m_device, m_render_finished[i], nullptr);
                vkDestroyFence(m_device, m_in_flight[i], nullptr);
            }

            vkDestroyDescriptorPool(m_device, m_descriptor_pool, nullptr);
            vkDestroyCommandPool(m_device, m_command_pool, nullptr);

            vmaDestroyAllocator(m_vma);

            vkDestroyDevice(m_device, nullptr);
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
            vkDestroyInstance(m_instance, nullptr);

            m_device = VK_NULL_HANDLE;
            GenesisPrint("[RHI:Vulkan] shutdown complete\n");
        }

        //-----------------------------------------------------------
        // Render pass 缓存
        //-----------------------------------------------------------
        VkRenderPass VulkanDevice::getOrCreateRenderPass(const RenderTargetLayout &layout, bool clear)
        {
            RenderPassKey key{layout, clear};
            auto it = m_render_pass_cache.find(key);
            if (it != m_render_pass_cache.end())
                return it->second;

            VkAttachmentDescription attachments[kMaxColorAttachments + 1];
            VkAttachmentReference color_refs[kMaxColorAttachments];
            uint32_t attachment_count = 0;

            for (uint32_t i = 0; i < layout.m_color_count; ++i)
            {
                VkAttachmentDescription &att = attachments[attachment_count];
                att = {};
                att.format = toVkFormat(layout.m_color_formats[i]);
                att.samples = VK_SAMPLE_COUNT_1_BIT;
                att.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
                att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                // 交换链目标 -> PRESENT; 离屏目标 -> SHADER_READ (供后续采样)
                bool is_swapchain = (layout.m_color_formats[i] == m_swapchain_rhi_format) && (layout.m_depth_format == m_main_layout.m_depth_format) && layout.m_color_count == 1;
                if (is_swapchain)
                {
                    att.initialLayout = clear ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                }
                else
                {
                    // 离屏纹理常驻 SHADER_READ_ONLY (创建时已转换)
                    att.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }

                color_refs[i] = {attachment_count, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
                ++attachment_count;
            }

            VkAttachmentReference depth_ref{};
            bool has_depth = isDepthFormat(layout.m_depth_format);
            if (has_depth)
            {
                VkAttachmentDescription &att = attachments[attachment_count];
                att = {};
                att.format = toVkFormat(layout.m_depth_format);
                att.samples = VK_SAMPLE_COUNT_1_BIT;
                att.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
                att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                att.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                att.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depth_ref = {attachment_count, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
                ++attachment_count;
            }

            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = layout.m_color_count;
            subpass.pColorAttachments = color_refs;
            subpass.pDepthStencilAttachment = has_depth ? &depth_ref : nullptr;

            VkSubpassDependency dependency{};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            VkRenderPassCreateInfo pass_info{};
            pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            pass_info.attachmentCount = attachment_count;
            pass_info.pAttachments = attachments;
            pass_info.subpassCount = 1;
            pass_info.pSubpasses = &subpass;
            pass_info.dependencyCount = 1;
            pass_info.pDependencies = &dependency;

            VkRenderPass render_pass = VK_NULL_HANDLE;
            VK_CHECK(vkCreateRenderPass(m_device, &pass_info, nullptr, &render_pass));
            m_render_pass_cache[key] = render_pass;
            return render_pass;
        }

        VkRenderPass VulkanDevice::getMainRenderPass()
        {
            return getOrCreateRenderPass(m_main_layout, true);
        }

        //-----------------------------------------------------------
        // 一次性命令
        //-----------------------------------------------------------
        VkCommandBuffer VulkanDevice::beginOneTimeCommands()
        {
            VkCommandBufferAllocateInfo alloc_info{};
            alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            alloc_info.commandPool = m_command_pool;
            alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc_info.commandBufferCount = 1;

            VkCommandBuffer cmd;
            vkAllocateCommandBuffers(m_device, &alloc_info, &cmd);

            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &begin_info);
            return cmd;
        }

        void VulkanDevice::endOneTimeCommands(VkCommandBuffer cmd)
        {
            vkEndCommandBuffer(cmd);

            VkSubmitInfo submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &cmd;
            vkQueueSubmit(m_graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
            vkQueueWaitIdle(m_graphics_queue);

            vkFreeCommandBuffers(m_device, m_command_pool, 1, &cmd);
        }

        void VulkanDevice::beginCommands()
        {
            GENESISASSERT_MSG(!m_in_frame, "beginCommands inside frame");
            m_recording_cmd = beginOneTimeCommands();
        }

        void VulkanDevice::endCommands()
        {
            endOneTimeCommands(m_recording_cmd);
            m_recording_cmd = VK_NULL_HANDLE;
        }

        //-----------------------------------------------------------
        // Buffer
        //-----------------------------------------------------------
        BufferHandle VulkanDevice::createBuffer(const BufferCreateInfo &info)
        {
            VkBufferCreateInfo buffer_info{};
            buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buffer_info.size = info.m_size;
            buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            if (info.m_usage & BufferUsage::Vertex)
                buffer_info.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            if (info.m_usage & BufferUsage::Index)
                buffer_info.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            if (info.m_usage & BufferUsage::Uniform)
                buffer_info.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

            bool host_visible = (info.m_usage & BufferUsage::Uniform) != 0;

            VmaAllocationCreateInfo alloc_info{};
            if (host_visible)
            {
                alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
                alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
                buffer_info.usage &= ~VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            }
            else
            {
                alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
            }

            uint32_t idx = m_buffers.allocate();
            VkBufferRes &res = m_buffers.get(idx);
            res = {};

            VmaAllocationInfo alloc_result{};
            VK_CHECK(vmaCreateBuffer(m_vma, &buffer_info, &alloc_info, &res.m_buffer, &res.m_allocation, &alloc_result));
            res.m_size = info.m_size;
            res.m_usage = info.m_usage;
            res.m_host_visible = host_visible;
            res.m_mapped = alloc_result.pMappedData;

            if (info.m_data != nullptr)
                updateBuffer({idx}, info.m_data, info.m_size, 0);

            return {idx};
        }

        void VulkanDevice::destroyBuffer(BufferHandle handle)
        {
            VkBufferRes &res = m_buffers.get(handle.m_handle);
            if (res.m_buffer != VK_NULL_HANDLE)
            {
                vmaDestroyBuffer(m_vma, res.m_buffer, res.m_allocation);
                res = {};
            }
            m_buffers.release(handle.m_handle);
        }

        void VulkanDevice::updateBuffer(BufferHandle handle, const void *data, uint64_t size, uint64_t offset)
        {
            VkBufferRes &res = m_buffers.get(handle.m_handle);

            if (res.m_host_visible && res.m_mapped)
            {
                memcpy((uint8_t *)res.m_mapped + offset, data, size);
                return;
            }

            // device local: staging 中转
            VkBufferCreateInfo staging_info{};
            staging_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            staging_info.size = size;
            staging_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

            VmaAllocationCreateInfo staging_alloc{};
            staging_alloc.usage = VMA_MEMORY_USAGE_CPU_ONLY;
            staging_alloc.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

            VkBuffer staging_buffer;
            VmaAllocation staging_allocation;
            VmaAllocationInfo staging_alloc_info{};
            VK_CHECK(vmaCreateBuffer(m_vma, &staging_info, &staging_alloc, &staging_buffer, &staging_allocation, &staging_alloc_info));
            memcpy(staging_alloc_info.pMappedData, data, size);

            VkCommandBuffer cmd = beginOneTimeCommands();
            VkBufferCopy copy{0, offset, size};
            vkCmdCopyBuffer(cmd, staging_buffer, res.m_buffer, 1, &copy);
            endOneTimeCommands(cmd);

            vmaDestroyBuffer(m_vma, staging_buffer, staging_allocation);
        }

        //-----------------------------------------------------------
        // Texture
        //-----------------------------------------------------------
        void VulkanDevice::transitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, uint32_t layers, bool isDepth)
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = mipLevels;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = layers;

            VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

            if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
            {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            }
            else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }
            else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }
            else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            }
            else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
            {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            }
            else if (oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
            {
                barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                src_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            }
            else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
            {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
                src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            }
            else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
            {
                barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            }
            else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
            {
                barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            }
            else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }
            else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
            {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            }
            else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
            {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            }
            else
            {
                GENESISASSERT_MSG(false, "unsupported layout transition %d -> %d", (int)oldLayout, (int)newLayout);
            }

            vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        void VulkanDevice::immediateTransitionToShaderRead(VkTextureRes &tex)
        {
            VkCommandBuffer cmd = beginOneTimeCommands();
            transitionImageLayout(cmd, tex.m_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, tex.m_info.m_mip_levels, tex.m_info.m_array_layers, false);
            endOneTimeCommands(cmd);
        }

        TextureHandle VulkanDevice::createTexture(const TextureCreateInfo &info)
        {
            uint32_t idx = m_textures.allocate();
            VkTextureRes &res = m_textures.get(idx);
            res = {};
            res.m_info.m_type = info.m_type;
            res.m_info.m_format = info.m_format;
            res.m_info.m_width = info.m_width;
            res.m_info.m_height = info.m_height;
            res.m_info.m_depth = info.m_depth;
            res.m_info.m_mip_levels = info.m_mip_levels;
            res.m_info.m_array_layers = info.m_array_layers;
            res.m_render_target = info.m_render_target;

            bool is_depth = isDepthFormat(info.m_format);
            bool is_cube = info.m_type == TextureType::TextureCube;

            VkImageCreateInfo image_info{};
            image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            image_info.imageType = VK_IMAGE_TYPE_2D;
            image_info.format = toVkFormat(info.m_format);
            image_info.extent = {info.m_width, info.m_height, 1};
            image_info.mipLevels = info.m_mip_levels;
            image_info.arrayLayers = info.m_array_layers;
            image_info.samples = VK_SAMPLE_COUNT_1_BIT;
            image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            if (is_cube)
                image_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

            if (is_depth)
            {
                image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            }
            else if (info.m_render_target)
            {
                image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            }
            else
            {
                image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            }

            VmaAllocationCreateInfo alloc_info{};
            alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
            VK_CHECK(vmaCreateImage(m_vma, &image_info, &alloc_info, &res.m_image, &res.m_allocation, nullptr));

            // 默认采样视图
            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = res.m_image;
            view_info.viewType = is_cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = toVkFormat(info.m_format);
            view_info.subresourceRange.aspectMask = is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = info.m_mip_levels;
            view_info.subresourceRange.baseArrayLayer = 0;
            view_info.subresourceRange.layerCount = info.m_array_layers;
            VK_CHECK(vkCreateImageView(m_device, &view_info, nullptr, &res.m_default_view));

            // 布局初始化
            {
                VkCommandBuffer cmd = beginOneTimeCommands();
                if (is_depth)
                    transitionImageLayout(cmd, res.m_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, info.m_mip_levels, info.m_array_layers, true);
                else
                    transitionImageLayout(cmd, res.m_image, VK_IMAGE_LAYOUT_UNDEFINED, info.m_data ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, info.m_mip_levels, info.m_array_layers, false);
                endOneTimeCommands(cmd);
            }

            if (info.m_data)
            {
                // 上传全部 layer/mip (紧密排列)
                uploadTextureData({idx}, info.m_data, 0, UINT32_MAX, 0);
            }

            return {idx};
        }

        void VulkanDevice::uploadTextureData(TextureHandle handle, const void *data, uint64_t size, uint32_t mipLevel, uint32_t layer)
        {
            VkTextureRes &res = m_textures.get(handle.m_handle);
            TextureInfo &ti = res.m_info;

            // 计算整个资源大小 (所有 layer + mip 紧密排列)
            auto mipSize = [&](uint32_t mip) -> uint64_t {
                uint32_t w = ti.m_width >> mip;
                uint32_t h = ti.m_height >> mip;
                if (w == 0)
                    w = 1;
                if (h == 0)
                    h = 1;
                return (uint64_t)w * h * formatSize(ti.m_format);
            };

            uint64_t total_size = 0;
            std::vector<VkBufferImageCopy> regions;
            if (mipLevel == UINT32_MAX)
            {
                // 初始数据: 每层仅 mip 0 (mip 链由 generateMipmaps 生成)
                for (uint32_t l = 0; l < ti.m_array_layers; ++l)
                {
                    VkBufferImageCopy region{};
                    region.bufferOffset = total_size;
                    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, l, 1};
                    region.imageExtent = {ti.m_width, ti.m_height, 1};
                    regions.push_back(region);
                    total_size += mipSize(0);
                }
            }
            else
            {
                VkBufferImageCopy region{};
                region.bufferOffset = 0;
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mipLevel, layer, 1};
                region.imageExtent = {ti.m_width >> mipLevel ? ti.m_width >> mipLevel : 1, ti.m_height >> mipLevel ? ti.m_height >> mipLevel : 1, 1};
                regions.push_back(region);
                total_size = size ? size : mipSize(mipLevel);
            }

            VkBufferCreateInfo staging_info{};
            staging_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            staging_info.size = total_size;
            staging_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

            VmaAllocationCreateInfo staging_alloc{};
            staging_alloc.usage = VMA_MEMORY_USAGE_CPU_ONLY;
            staging_alloc.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

            VkBuffer staging_buffer;
            VmaAllocation staging_allocation;
            VmaAllocationInfo staging_alloc_info{};
            VK_CHECK(vmaCreateBuffer(m_vma, &staging_info, &staging_alloc, &staging_buffer, &staging_allocation, &staging_alloc_info));
            memcpy(staging_alloc_info.pMappedData, data, total_size);

            VkCommandBuffer cmd = beginOneTimeCommands();
            if (mipLevel != UINT32_MAX)
            {
                // 单层上传需要当前处于 TRANSFER_DST
                transitionImageLayout(cmd, res.m_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, ti.m_mip_levels, ti.m_array_layers, false);
            }
            vkCmdCopyBufferToImage(cmd, staging_buffer, res.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)regions.size(), regions.data());
            transitionImageLayout(cmd, res.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ti.m_mip_levels, ti.m_array_layers, false);
            endOneTimeCommands(cmd);

            vmaDestroyBuffer(m_vma, staging_buffer, staging_allocation);
        }

        void VulkanDevice::updateTexture(TextureHandle handle, const void *data, uint64_t size, uint32_t mipLevel, uint32_t layer)
        {
            uploadTextureData(handle, data, size, mipLevel, layer);
        }

        void VulkanDevice::generateMipmaps(TextureHandle handle)
        {
            VkTextureRes &res = m_textures.get(handle.m_handle);
            TextureInfo &ti = res.m_info;
            if (ti.m_mip_levels <= 1)
                return;

            VkCommandBuffer cmd = beginOneTimeCommands();
            // 全部子资源: SHADER_READ -> TRANSFER_DST
            transitionImageLayout(cmd, res.m_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, ti.m_mip_levels, ti.m_array_layers, false);

            for (uint32_t layer = 0; layer < ti.m_array_layers; ++layer)
            {
                for (uint32_t mip = 1; mip < ti.m_mip_levels; ++mip)
                {
                    // 源 mip (mip-1): DST -> SRC
                    VkImageMemoryBarrier barrier{};
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    barrier.image = res.m_image;
                    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 1, layer, 1};
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

                    VkImageBlit blit{};
                    uint32_t src_w = ti.m_width >> (mip - 1), src_h = ti.m_height >> (mip - 1);
                    uint32_t dst_w = ti.m_width >> mip, dst_h = ti.m_height >> mip;
                    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, layer, 1};
                    blit.srcOffsets[1] = {(int32_t)(src_w ? src_w : 1), (int32_t)(src_h ? src_h : 1), 1};
                    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1};
                    blit.dstOffsets[1] = {(int32_t)(dst_w ? dst_w : 1), (int32_t)(dst_h ? dst_h : 1), 1};
                    vkCmdBlitImage(cmd, res.m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, res.m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
                }
            }

            // 收尾: mip 0..n-2 处于 SRC, mip n-1 处于 DST, 分两条 barrier 统一回 SHADER_READ
            VkImageMemoryBarrier barriers[2]{};
            barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barriers[0].image = res.m_image;
            barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, ti.m_mip_levels - 1, 0, ti.m_array_layers};
            barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barriers[1].image = res.m_image;
            barriers[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, ti.m_mip_levels - 1, 1, 0, ti.m_array_layers};
            barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);
            endOneTimeCommands(cmd);
        }

        TextureInfo VulkanDevice::getTextureInfo(TextureHandle handle) const
        {
            return m_textures.get(handle.m_handle).m_info;
        }

        void VulkanDevice::destroyTexture(TextureHandle handle)
        {
            VkTextureRes &res = m_textures.get(handle.m_handle);
            if (res.m_default_view != VK_NULL_HANDLE)
                vkDestroyImageView(m_device, res.m_default_view, nullptr);
            if (res.m_image != VK_NULL_HANDLE)
                vmaDestroyImage(m_vma, res.m_image, res.m_allocation);
            res = {};
            m_textures.release(handle.m_handle);
        }

        //-----------------------------------------------------------
        // Sampler
        //-----------------------------------------------------------
        SamplerHandle VulkanDevice::createSampler(const SamplerCreateInfo &info)
        {
            VkSamplerCreateInfo sampler_info{};
            sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sampler_info.minFilter = toVkFilter(info.m_min_filter);
            sampler_info.magFilter = toVkFilter(info.m_mag_filter);
            sampler_info.mipmapMode = info.m_mipmap_filter == Filter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler_info.addressModeU = toVkAddressMode(info.m_address_u);
            sampler_info.addressModeV = toVkAddressMode(info.m_address_v);
            sampler_info.addressModeW = toVkAddressMode(info.m_address_w);
            sampler_info.minLod = 0.0f;
            sampler_info.maxLod = info.m_max_lod;
            sampler_info.anisotropyEnable = VK_FALSE;

            uint32_t idx = m_samplers.allocate();
            VK_CHECK(vkCreateSampler(m_device, &sampler_info, nullptr, &m_samplers.get(idx).m_sampler));
            return {idx};
        }

        void VulkanDevice::destroySampler(SamplerHandle handle)
        {
            VkSamplerRes &res = m_samplers.get(handle.m_handle);
            if (res.m_sampler != VK_NULL_HANDLE)
                vkDestroySampler(m_device, res.m_sampler, nullptr);
            res = {};
            m_samplers.release(handle.m_handle);
        }

        //-----------------------------------------------------------
        // Shader
        //-----------------------------------------------------------
        ShaderHandle VulkanDevice::createShader(const ShaderCreateInfo &info)
        {
            GENESISASSERT_MSG(info.m_spirv != nullptr && info.m_spirv_size > 0, "Vulkan shader requires SPIR-V");
            VkShaderModuleCreateInfo module_info{};
            module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            module_info.codeSize = info.m_spirv_size;
            module_info.pCode = info.m_spirv;

            uint32_t idx = m_shaders.allocate();
            VkShaderRes &res = m_shaders.get(idx);
            res.m_stage = info.m_stage;
            VK_CHECK(vkCreateShaderModule(m_device, &module_info, nullptr, &res.m_module));
            return {idx};
        }

        void VulkanDevice::destroyShader(ShaderHandle handle)
        {
            VkShaderRes &res = m_shaders.get(handle.m_handle);
            if (res.m_module != VK_NULL_HANDLE)
                vkDestroyShaderModule(m_device, res.m_module, nullptr);
            res = {};
            m_shaders.release(handle.m_handle);
        }

        //-----------------------------------------------------------
        // Pipeline
        //-----------------------------------------------------------
        PipelineHandle VulkanDevice::createPipeline(const PipelineCreateInfo &info)
        {
            VkShaderRes &vs = m_shaders.get(info.m_vs.m_handle);
            VkShaderRes &fs = m_shaders.get(info.m_fs.m_handle);

            VkPipelineShaderStageCreateInfo stages[2]{};
            stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vs.m_module;
            stages[0].pName = "main";
            stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fs.m_module;
            stages[1].pName = "main";

            // 顶点输入
            VkVertexInputBindingDescription binding_desc{};
            binding_desc.binding = 0;
            binding_desc.stride = info.m_vertex_binding.m_stride;
            binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            VkVertexInputAttributeDescription attr_descs[kMaxVertexAttributes];
            for (uint32_t i = 0; i < info.m_vertex_binding.m_attribute_count; ++i)
            {
                const VertexAttribute &attr = info.m_vertex_binding.m_attributes[i];
                attr_descs[i] = {attr.m_location, 0, toVkVertexFormat(attr.m_format), attr.m_offset};
            }

            VkPipelineVertexInputStateCreateInfo vertex_input{};
            vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertex_input.vertexBindingDescriptionCount = info.m_vertex_binding.m_stride ? 1 : 0;
            vertex_input.pVertexBindingDescriptions = &binding_desc;
            vertex_input.vertexAttributeDescriptionCount = info.m_vertex_binding.m_attribute_count;
            vertex_input.pVertexAttributeDescriptions = attr_descs;

            VkPipelineInputAssemblyStateCreateInfo input_assembly{};
            input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            input_assembly.topology = info.m_topology == PrimitiveTopology::TriangleList ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST : VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

            VkPipelineViewportStateCreateInfo viewport_state{};
            viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport_state.viewportCount = 1;
            viewport_state.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.cullMode = toVkCullMode(info.m_cull_mode);
            // 缠绕方向: Vulkan 帧缓冲 y 向下 + 投影 Y 翻转 = 双重抵消,
            // 与 OpenGL 保持一致的 frontFace 映射 (实测验证, 勿随意改动)
            rasterizer.frontFace = info.m_front_face == FrontFace::CounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
            rasterizer.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo depth_stencil{};
            depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth_stencil.depthTestEnable = info.m_depth_test ? VK_TRUE : VK_FALSE;
            depth_stencil.depthWriteEnable = info.m_depth_write ? VK_TRUE : VK_FALSE;
            depth_stencil.depthCompareOp = toVkCompareOp(info.m_depth_compare);

            VkPipelineColorBlendAttachmentState blend_attachment{};
            blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            blend_attachment.blendEnable = info.m_blend_enable ? VK_TRUE : VK_FALSE;
            blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
            blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

            VkPipelineColorBlendStateCreateInfo blending{};
            blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blending.attachmentCount = info.m_render_target_layout.m_color_count;
            VkPipelineColorBlendAttachmentState blend_attachments[kMaxColorAttachments];
            for (uint32_t i = 0; i < blending.attachmentCount; ++i)
                blend_attachments[i] = blend_attachment;
            blending.pAttachments = blend_attachments;

            VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic_state{};
            dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic_state.dynamicStateCount = 2;
            dynamic_state.pDynamicStates = dynamic_states;

            // pipeline layout
            VkDescriptorSetLayout set_layouts[kMaxDescriptorSetSlots];
            for (uint32_t i = 0; i < info.m_set_layout_count; ++i)
                set_layouts[i] = m_set_layouts.get(info.m_set_layouts[i].m_handle).m_layout;

            VkPipelineLayoutCreateInfo layout_info{};
            layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layout_info.setLayoutCount = info.m_set_layout_count;
            layout_info.pSetLayouts = set_layouts;

            uint32_t idx = m_pipelines.allocate();
            VkPipelineRes &res = m_pipelines.get(idx);
            res = {};
            res.m_rt_layout = info.m_render_target_layout;

            VK_CHECK(vkCreatePipelineLayout(m_device, &layout_info, nullptr, &res.m_layout));

            VkRenderPass render_pass = getOrCreateRenderPass(info.m_render_target_layout, true);

            VkGraphicsPipelineCreateInfo pipeline_info{};
            pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipeline_info.stageCount = 2;
            pipeline_info.pStages = stages;
            pipeline_info.pVertexInputState = &vertex_input;
            pipeline_info.pInputAssemblyState = &input_assembly;
            pipeline_info.pViewportState = &viewport_state;
            pipeline_info.pRasterizationState = &rasterizer;
            pipeline_info.pMultisampleState = &multisampling;
            pipeline_info.pDepthStencilState = &depth_stencil;
            pipeline_info.pColorBlendState = &blending;
            pipeline_info.pDynamicState = &dynamic_state;
            pipeline_info.layout = res.m_layout;
            pipeline_info.renderPass = render_pass;
            pipeline_info.subpass = 0;

            VK_CHECK(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &res.m_pipeline));
            return {idx};
        }

        void VulkanDevice::destroyPipeline(PipelineHandle handle)
        {
            VkPipelineRes &res = m_pipelines.get(handle.m_handle);
            if (res.m_pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(m_device, res.m_pipeline, nullptr);
            if (res.m_layout != VK_NULL_HANDLE)
                vkDestroyPipelineLayout(m_device, res.m_layout, nullptr);
            res = {};
            m_pipelines.release(handle.m_handle);
        }

        //-----------------------------------------------------------
        // Descriptor
        //-----------------------------------------------------------
        DescriptorSetLayoutHandle VulkanDevice::createDescriptorSetLayout(const DescriptorSetLayoutCreateInfo &info)
        {
            VkDescriptorSetLayoutBinding bindings[kMaxDescriptorBindings];
            for (uint32_t i = 0; i < info.m_binding_count; ++i)
            {
                const DescriptorBinding &b = info.m_bindings[i];
                VkDescriptorSetLayoutBinding &vb = bindings[i];
                vb = {};
                vb.binding = b.m_binding;
                vb.descriptorType = b.m_type == DescriptorType::UniformBuffer ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                vb.descriptorCount = 1;
                vb.stageFlags = 0;
                if (b.m_stages & ShaderStageFlags::Vertex)
                    vb.stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
                if (b.m_stages & ShaderStageFlags::Fragment)
                    vb.stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
            }

            VkDescriptorSetLayoutCreateInfo layout_info{};
            layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            layout_info.bindingCount = info.m_binding_count;
            layout_info.pBindings = bindings;

            uint32_t idx = m_set_layouts.allocate();
            VkSetLayoutRes &res = m_set_layouts.get(idx);
            res.m_info = info;
            VK_CHECK(vkCreateDescriptorSetLayout(m_device, &layout_info, nullptr, &res.m_layout));
            return {idx};
        }

        void VulkanDevice::destroyDescriptorSetLayout(DescriptorSetLayoutHandle handle)
        {
            VkSetLayoutRes &res = m_set_layouts.get(handle.m_handle);
            if (res.m_layout != VK_NULL_HANDLE)
                vkDestroyDescriptorSetLayout(m_device, res.m_layout, nullptr);
            res = {};
            m_set_layouts.release(handle.m_handle);
        }

        DescriptorSetHandle VulkanDevice::createDescriptorSet(const DescriptorSetCreateInfo &info)
        {
            VkSetLayoutRes &layout_res = m_set_layouts.get(info.m_layout.m_handle);

            VkDescriptorSetAllocateInfo alloc_info{};
            alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc_info.descriptorPool = m_descriptor_pool;
            alloc_info.descriptorSetCount = 1;
            alloc_info.pSetLayouts = &layout_res.m_layout;

            uint32_t idx = m_sets.allocate();
            VkSetRes &res = m_sets.get(idx);
            VK_CHECK(vkAllocateDescriptorSets(m_device, &alloc_info, &res.m_set));

            // 写入
            VkWriteDescriptorSet writes[kMaxDescriptorBindings];
            VkDescriptorBufferInfo buffer_infos[kMaxDescriptorBindings];
            VkDescriptorImageInfo image_infos[kMaxDescriptorBindings];

            for (uint32_t i = 0; i < info.m_write_count; ++i)
            {
                const DescriptorWrite &w = info.m_writes[i];
                VkWriteDescriptorSet &write = writes[i];
                write = {};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = res.m_set;
                write.dstBinding = w.m_binding;
                write.descriptorCount = 1;

                // 查 layout 中该 binding 的类型
                DescriptorType type = DescriptorType::UniformBuffer;
                for (uint32_t j = 0; j < layout_res.m_info.m_binding_count; ++j)
                {
                    if (layout_res.m_info.m_bindings[j].m_binding == w.m_binding)
                    {
                        type = layout_res.m_info.m_bindings[j].m_type;
                        break;
                    }
                }

                if (type == DescriptorType::UniformBuffer)
                {
                    VkBufferRes &buf = m_buffers.get(w.m_buffer.m_handle);
                    buffer_infos[i] = {buf.m_buffer, w.m_buffer_offset, w.m_buffer_size ? w.m_buffer_size : buf.m_size};
                    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                    write.pBufferInfo = &buffer_infos[i];
                }
                else
                {
                    VkTextureRes &tex = m_textures.get(w.m_texture.m_handle);
                    VkSamplerRes &smp = m_samplers.get(w.m_sampler.m_handle);
                    image_infos[i] = {smp.m_sampler, tex.m_default_view, tex.m_info.m_format == Format::D32_Float || tex.m_info.m_format == Format::D24_S8 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    write.pImageInfo = &image_infos[i];
                }
            }

            if (info.m_write_count > 0)
                vkUpdateDescriptorSets(m_device, info.m_write_count, writes, 0, nullptr);

            return {idx};
        }

        void VulkanDevice::destroyDescriptorSet(DescriptorSetHandle handle)
        {
            VkSetRes &res = m_sets.get(handle.m_handle);
            if (res.m_set != VK_NULL_HANDLE)
                vkFreeDescriptorSets(m_device, m_descriptor_pool, 1, &res.m_set);
            res = {};
            m_sets.release(handle.m_handle);
        }

        //-----------------------------------------------------------
        // RenderTarget
        //-----------------------------------------------------------
        RenderTargetHandle VulkanDevice::createRenderTarget(const RenderTargetCreateInfo &info)
        {
            uint32_t idx = m_render_targets.allocate();
            VkRenderTargetRes &res = m_render_targets.get(idx);
            res = {};

            VkImageView attachments[kMaxColorAttachments + 1];
            uint32_t attachment_count = 0;
            uint32_t width = 0, height = 0;

            RenderTargetLayout layout{};
            layout.m_color_count = info.m_color_count;

            for (uint32_t i = 0; i < info.m_color_count; ++i)
            {
                VkTextureRes &tex = m_textures.get(info.m_colors[i].m_texture.m_handle);
                TextureInfo &ti = tex.m_info;
                layout.m_color_formats[i] = ti.m_format;

                uint32_t w = ti.m_width >> info.m_colors[i].m_mip_level;
                uint32_t h = ti.m_height >> info.m_colors[i].m_mip_level;
                width = w ? w : 1;
                height = h ? h : 1;

                // 单 mip/layer 视图
                VkImageViewCreateInfo view_info{};
                view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                view_info.image = tex.m_image;
                view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
                view_info.format = toVkFormat(ti.m_format);
                view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, info.m_colors[i].m_mip_level, 1, info.m_colors[i].m_layer, 1};

                VkImageView view;
                VK_CHECK(vkCreateImageView(m_device, &view_info, nullptr, &view));
                res.m_owned_views.push_back(view);
                attachments[attachment_count++] = view;
            }

            if (info.m_has_depth)
            {
                VkTextureRes &tex = m_textures.get(info.m_depth.m_texture.m_handle);
                layout.m_depth_format = tex.m_info.m_format;
                attachments[attachment_count++] = tex.m_default_view;
            }
            else
            {
                layout.m_depth_format = Format::Undefined;
            }

            res.m_render_pass = getOrCreateRenderPass(layout, true);
            res.m_layout = layout;
            res.m_width = width;
            res.m_height = height;

            VkFramebufferCreateInfo fb_info{};
            fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb_info.renderPass = res.m_render_pass;
            fb_info.attachmentCount = attachment_count;
            fb_info.pAttachments = attachments;
            fb_info.width = width;
            fb_info.height = height;
            fb_info.layers = 1;
            VK_CHECK(vkCreateFramebuffer(m_device, &fb_info, nullptr, &res.m_framebuffer));

            return {idx};
        }

        void VulkanDevice::destroyRenderTarget(RenderTargetHandle handle)
        {
            VkRenderTargetRes &res = m_render_targets.get(handle.m_handle);
            if (res.m_framebuffer != VK_NULL_HANDLE)
                vkDestroyFramebuffer(m_device, res.m_framebuffer, nullptr);
            for (auto view : res.m_owned_views)
                vkDestroyImageView(m_device, view, nullptr);
            res = {};
            m_render_targets.release(handle.m_handle);
        }

        //-----------------------------------------------------------
        // Frame
        //-----------------------------------------------------------
        void VulkanDevice::beginFrame()
        {
            GENESISASSERT_MSG(!m_in_frame, "nested beginFrame");
            vkWaitForFences(m_device, 1, &m_in_flight[m_current_frame], VK_TRUE, UINT64_MAX);

            VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, m_image_available[m_current_frame], VK_NULL_HANDLE, &m_image_index);
            if (result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                GenesisPrint("[RHI:Vulkan] swapchain out of date (recreate unsupported, fixed-size window)\n");
            }

            vkResetFences(m_device, 1, &m_in_flight[m_current_frame]);
            vkResetCommandBuffer(m_frame_cmds[m_current_frame], 0);

            VkCommandBufferBeginInfo begin_info{};
            begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            VK_CHECK(vkBeginCommandBuffer(m_frame_cmds[m_current_frame], &begin_info));

            m_recording_cmd = m_frame_cmds[m_current_frame];
            m_in_frame = true;
        }

        void VulkanDevice::endFrame()
        {
            GENESISASSERT_MSG(m_in_frame, "endFrame without beginFrame");
            GENESISASSERT_MSG(!m_in_pass, "render pass still open at endFrame");

            VkCommandBuffer cmd = m_frame_cmds[m_current_frame];

            // 截图: 在 submit 前记录 copy 命令
            VkBuffer capture_buffer = VK_NULL_HANDLE;
            VmaAllocation capture_alloc = VK_NULL_HANDLE;
            if (m_capture_pending)
            {
                uint64_t image_size = (uint64_t)m_width * m_height * 4;

                VkBufferCreateInfo buf_info{};
                buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                buf_info.size = image_size;
                buf_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                VmaAllocationCreateInfo alloc_info{};
                alloc_info.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
                alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
                VK_CHECK(vmaCreateBuffer(m_vma, &buf_info, &alloc_info, &capture_buffer, &capture_alloc, nullptr));

                transitionImageLayout(cmd, m_swapchain_images[m_image_index], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1, 1, false);

                VkBufferImageCopy copy{};
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copy.imageExtent = {m_width, m_height, 1};
                vkCmdCopyImageToBuffer(cmd, m_swapchain_images[m_image_index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, capture_buffer, 1, &copy);

                transitionImageLayout(cmd, m_swapchain_images[m_image_index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 1, 1, false);
            }

            VK_CHECK(vkEndCommandBuffer(cmd));

            VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.waitSemaphoreCount = 1;
            submit_info.pWaitSemaphores = &m_image_available[m_current_frame];
            submit_info.pWaitDstStageMask = &wait_stage;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &cmd;
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = &m_render_finished[m_current_frame];
            VK_CHECK(vkQueueSubmit(m_graphics_queue, 1, &submit_info, m_in_flight[m_current_frame]));

            if (m_capture_pending)
            {
                vkWaitForFences(m_device, 1, &m_in_flight[m_current_frame], VK_TRUE, UINT64_MAX);

                VmaAllocationInfo alloc_info{};
                vmaGetAllocationInfo(m_vma, capture_alloc, &alloc_info);

                // BGRA -> RGBA 转换
                std::vector<uint8_t> pixels((size_t)m_width * m_height * 4);
                const uint8_t *src = (const uint8_t *)alloc_info.pMappedData;
                for (uint32_t i = 0; i < m_width * m_height; ++i)
                {
                    pixels[i * 4 + 0] = src[i * 4 + 2];
                    pixels[i * 4 + 1] = src[i * 4 + 1];
                    pixels[i * 4 + 2] = src[i * 4 + 0];
                    pixels[i * 4 + 3] = 255;
                }
                stbi_write_png(m_capture_path.c_str(), m_width, m_height, 4, pixels.data(), m_width * 4);
                GenesisPrint("[RHI:Vulkan] screenshot saved: %s\n", m_capture_path.c_str());

                vmaDestroyBuffer(m_vma, capture_buffer, capture_alloc);
                m_capture_pending = false;
            }

            VkPresentInfoKHR present_info{};
            present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            present_info.waitSemaphoreCount = 1;
            present_info.pWaitSemaphores = &m_render_finished[m_current_frame];
            present_info.swapchainCount = 1;
            present_info.pSwapchains = &m_swapchain;
            present_info.pImageIndices = &m_image_index;
            vkQueuePresentKHR(m_graphics_queue, &present_info);

            m_current_frame = (m_current_frame + 1) % kMaxFramesInFlight;
            m_in_frame = false;
            m_recording_cmd = VK_NULL_HANDLE;
        }

        //-----------------------------------------------------------
        // Render pass 录制
        //-----------------------------------------------------------
        void VulkanDevice::beginRenderPass(RenderTargetHandle target, const float clearColor[4], float clearDepth, bool clear)
        {
            GENESISASSERT_MSG(m_recording_cmd != VK_NULL_HANDLE, "beginRenderPass outside command recording");
            GENESISASSERT_MSG(!m_in_pass, "nested render pass");

            VkRenderPassBeginInfo pass_info{};
            pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;

            VkClearValue clear_values[kMaxColorAttachments + 1];
            uint32_t clear_count = 0;
            uint32_t width = m_width, height = m_height;
            RenderTargetLayout layout;

            if (target.isSwapchain())
            {
                layout = m_main_layout;
                pass_info.renderPass = getOrCreateRenderPass(layout, clear);
                pass_info.framebuffer = m_swapchain_framebuffers[m_image_index];
            }
            else
            {
                VkRenderTargetRes &rt = m_render_targets.get(target.m_handle);
                layout = rt.m_layout;
                pass_info.renderPass = getOrCreateRenderPass(layout, clear);
                pass_info.framebuffer = rt.m_framebuffer;
                width = rt.m_width;
                height = rt.m_height;
            }

            for (uint32_t i = 0; i < layout.m_color_count; ++i)
            {
                clear_values[clear_count].color = {{clearColor[0], clearColor[1], clearColor[2], clearColor[3]}};
                ++clear_count;
            }
            if (isDepthFormat(layout.m_depth_format))
            {
                clear_values[clear_count].depthStencil = {clearDepth, 0};
                ++clear_count;
            }

            pass_info.renderArea = {{0, 0}, {width, height}};
            pass_info.clearValueCount = clear_count;
            pass_info.pClearValues = clear_values;

            vkCmdBeginRenderPass(m_recording_cmd, &pass_info, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport{0, 0, (float)width, (float)height, 0.0f, 1.0f};
            vkCmdSetViewport(m_recording_cmd, 0, 1, &viewport);
            VkRect2D scissor{{0, 0}, {width, height}};
            vkCmdSetScissor(m_recording_cmd, 0, 1, &scissor);

            m_in_pass = true;
        }

        void VulkanDevice::endRenderPass()
        {
            GENESISASSERT_MSG(m_in_pass, "endRenderPass without begin");
            vkCmdEndRenderPass(m_recording_cmd);
            m_in_pass = false;
        }

        void VulkanDevice::bindPipeline(PipelineHandle pipeline)
        {
            VkPipelineRes &res = m_pipelines.get(pipeline.m_handle);
            vkCmdBindPipeline(m_recording_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res.m_pipeline);
            m_current_pipeline_layout = res.m_layout;
        }

        void VulkanDevice::bindDescriptorSet(uint32_t slot, DescriptorSetHandle set)
        {
            VkSetRes &res = m_sets.get(set.m_handle);
            GENESISASSERT_MSG(m_current_pipeline_layout != VK_NULL_HANDLE, "bindDescriptorSet before bindPipeline");
            vkCmdBindDescriptorSets(m_recording_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_current_pipeline_layout, slot, 1, &res.m_set, 0, nullptr);
        }

        void VulkanDevice::bindVertexBuffer(BufferHandle buffer, uint64_t offset)
        {
            VkBufferRes &res = m_buffers.get(buffer.m_handle);
            VkDeviceSize vk_offset = offset;
            vkCmdBindVertexBuffers(m_recording_cmd, 0, 1, &res.m_buffer, &vk_offset);
        }

        void VulkanDevice::bindIndexBuffer(BufferHandle buffer, uint64_t offset)
        {
            VkBufferRes &res = m_buffers.get(buffer.m_handle);
            vkCmdBindIndexBuffer(m_recording_cmd, res.m_buffer, offset, VK_INDEX_TYPE_UINT32);
        }

        void VulkanDevice::draw(uint32_t vertexCount, uint32_t firstVertex)
        {
            vkCmdDraw(m_recording_cmd, vertexCount, 1, firstVertex, 0);
        }

        void VulkanDevice::drawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset)
        {
            vkCmdDrawIndexed(m_recording_cmd, indexCount, 1, firstIndex, vertexOffset, 0);
        }

        //-----------------------------------------------------------
        // 回读
        //-----------------------------------------------------------
        bool VulkanDevice::readbackTexture(TextureHandle handle, uint8_t *outRGBA, uint32_t rowPitch)
        {
            VkTextureRes &res = m_textures.get(handle.m_handle);
            TextureInfo &ti = res.m_info;

            uint64_t image_size = (uint64_t)ti.m_width * ti.m_height * 4;
            VkBufferCreateInfo buf_info{};
            buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buf_info.size = image_size;
            buf_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            VmaAllocationCreateInfo alloc_info{};
            alloc_info.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
            alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

            VkBuffer buffer;
            VmaAllocation allocation;
            VK_CHECK(vmaCreateBuffer(m_vma, &buf_info, &alloc_info, &buffer, &allocation, nullptr));

            VkCommandBuffer cmd = beginOneTimeCommands();
            transitionImageLayout(cmd, res.m_image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ti.m_mip_levels, ti.m_array_layers, false);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {ti.m_width, ti.m_height, 1};
            vkCmdCopyImageToBuffer(cmd, res.m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
            transitionImageLayout(cmd, res.m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ti.m_mip_levels, ti.m_array_layers, false);
            endOneTimeCommands(cmd);

            VmaAllocationInfo result_info{};
            vmaGetAllocationInfo(m_vma, allocation, &result_info);
            const uint8_t *src = (const uint8_t *)result_info.pMappedData;
            for (uint32_t y = 0; y < ti.m_height; ++y)
            {
                for (uint32_t x = 0; x < ti.m_width; ++x)
                {
                    const uint8_t *s = src + (y * ti.m_width + x) * 4;
                    uint8_t *d = outRGBA + y * rowPitch + x * 4;
                    d[0] = s[2];
                    d[1] = s[1];
                    d[2] = s[0];
                    d[3] = 255;
                }
            }
            vmaDestroyBuffer(m_vma, buffer, allocation);
            return true;
        }

        void VulkanDevice::requestScreenCapture(const char *pngPath)
        {
            m_capture_path = pngPath;
            m_capture_pending = true;
        }

        bool VulkanDevice::captureScreen(const char *pngPath)
        {
            requestScreenCapture(pngPath);
            return true;
        }

        void VulkanDevice::waitIdle()
        {
            vkDeviceWaitIdle(m_device);
        }

    } // namespace rhi
} // namespace Genesis
