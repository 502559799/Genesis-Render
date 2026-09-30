/*
 * Genesis Render - Vulkan backend implementation (device + resources)
 */
#include "rhi/vulkan/vk_backend.hpp"
#include "rhi/vulkan/vk_convert.hpp"
#include "foundation_layer/genesis_log.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#define VMA_IMPLEMENTATION
#include "external/vk_mem_alloc.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <set>

namespace Genesis
{
    namespace RHI
    {
        //==================================================================
        // Format helpers
        //==================================================================
        VkFormat vkPixelFormat(PixelFormat format)
        {
            switch (format)
            {
            case PixelFormat::R8_UNORM: return VK_FORMAT_R8_UNORM;
            case PixelFormat::RG8_UNORM: return VK_FORMAT_R8G8_UNORM;
            case PixelFormat::RGB8_UNORM: return VK_FORMAT_R8G8B8_UNORM;
            case PixelFormat::RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
            case PixelFormat::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
            case PixelFormat::BGRA8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
            case PixelFormat::R16_SFLOAT: return VK_FORMAT_R16_SFLOAT;
            case PixelFormat::RG16_SFLOAT: return VK_FORMAT_R16G16_SFLOAT;
            case PixelFormat::RGBA16_SFLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
            case PixelFormat::R32_SFLOAT: return VK_FORMAT_R32_SFLOAT;
            case PixelFormat::RG32_SFLOAT: return VK_FORMAT_R32G32_SFLOAT;
            case PixelFormat::RGB32_SFLOAT: return VK_FORMAT_R32G32B32_SFLOAT;
            case PixelFormat::RGBA32_SFLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
            case PixelFormat::RGBA32_UINT: return VK_FORMAT_R32G32B32A32_UINT;
            case PixelFormat::R32_UINT: return VK_FORMAT_R32_UINT;
            case PixelFormat::D16_UNORM: return VK_FORMAT_D16_UNORM;
            case PixelFormat::D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;
            case PixelFormat::D32_SFLOAT: return VK_FORMAT_D32_SFLOAT;
            case PixelFormat::D32_SFLOAT_S8_UINT: return VK_FORMAT_D32_SFLOAT_S8_UINT;
            default: return VK_FORMAT_UNDEFINED;
            }
        }

        PixelFormat vkToPixelFormat(VkFormat format)
        {
            switch (format)
            {
            case VK_FORMAT_R8G8B8A8_UNORM: return PixelFormat::RGBA8_UNORM;
            case VK_FORMAT_R8G8B8A8_SRGB: return PixelFormat::RGBA8_SRGB;
            case VK_FORMAT_B8G8R8A8_UNORM: return PixelFormat::BGRA8_UNORM;
            case VK_FORMAT_B8G8R8A8_SRGB: return PixelFormat::BGRA8_UNORM;
            case VK_FORMAT_R16G16B16A16_SFLOAT: return PixelFormat::RGBA16_SFLOAT;
            case VK_FORMAT_R32G32B32A32_SFLOAT: return PixelFormat::RGBA32_SFLOAT;
            case VK_FORMAT_D24_UNORM_S8_UINT: return PixelFormat::D24_UNORM_S8_UINT;
            case VK_FORMAT_D32_SFLOAT: return PixelFormat::D32_SFLOAT;
            case VK_FORMAT_D32_SFLOAT_S8_UINT: return PixelFormat::D32_SFLOAT_S8_UINT;
            default: return PixelFormat::Undefined;
            }
        }

        VkImageAspectFlags vkAspectMask(PixelFormat format)
        {
            switch (format)
            {
            case PixelFormat::D24_UNORM_S8_UINT:
            case PixelFormat::D32_SFLOAT_S8_UINT: return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            case PixelFormat::D16_UNORM:
            case PixelFormat::D32_SFLOAT: return VK_IMAGE_ASPECT_DEPTH_BIT;
            default: return VK_IMAGE_ASPECT_COLOR_BIT;
            }
        }

        VkImageLayout vkTextureLayout(TextureLayout layout)
        {
            switch (layout)
            {
            case TextureLayout::Undefined: return VK_IMAGE_LAYOUT_UNDEFINED;
            case TextureLayout::General: return VK_IMAGE_LAYOUT_GENERAL;
            case TextureLayout::ColorAttachmentOptimal: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            case TextureLayout::DepthStencilAttachmentOptimal: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            case TextureLayout::DepthStencilReadOnlyOptimal: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            case TextureLayout::ShaderReadOnlyOptimal: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            case TextureLayout::TransferSrcOptimal: return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            case TextureLayout::TransferDstOptimal: return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            case TextureLayout::PresentSrc: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            }
            return VK_IMAGE_LAYOUT_UNDEFINED;
        }

        static VkFormat vkVertexFormat(VertexFormat f)
        {
            switch (f)
            {
            case VertexFormat::Float1: return VK_FORMAT_R32_SFLOAT;
            case VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
            case VertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
            case VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
            case VertexFormat::UByte4: return VK_FORMAT_R8G8B8A8_UINT;
            case VertexFormat::UByte4Norm: return VK_FORMAT_R8G8B8A8_UNORM;
            case VertexFormat::Short2: return VK_FORMAT_R16G16_SINT;
            case VertexFormat::Short4: return VK_FORMAT_R16G16B16A16_SINT;
            case VertexFormat::Short2Norm: return VK_FORMAT_R16G16_SNORM;
            case VertexFormat::Short4Norm: return VK_FORMAT_R16G16B16A16_SNORM;
            case VertexFormat::UInt1: return VK_FORMAT_R32_UINT;
            case VertexFormat::UInt2: return VK_FORMAT_R32G32_UINT;
            case VertexFormat::UInt4: return VK_FORMAT_R32G32B32A32_UINT;
            default: return VK_FORMAT_R32G32B32_SFLOAT;
            }
        }

        static VkPrimitiveTopology vkTopology(PrimitiveTopology t)
        {
            switch (t)
            {
            case PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            case PrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
            case PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            }
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        }

        static VkCullModeFlags vkCull(CullMode c)
        {
            switch (c)
            {
            case CullMode::None: return VK_CULL_MODE_NONE;
            case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
            case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
            case CullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
            }
            return VK_CULL_MODE_BACK_BIT;
        }

        static VkCompareOp vkCompare(CompareOp op)
        {
            switch (op)
            {
            case CompareOp::Never: return VK_COMPARE_OP_NEVER;
            case CompareOp::Less: return VK_COMPARE_OP_LESS;
            case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
            case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
            case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
            case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
            case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
            case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
            }
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        }

        static VkBlendFactor vkBlendFactor(BlendFactor f)
        {
            switch (f)
            {
            case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
            case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
            case BlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
            case BlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            case BlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
            case BlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
            case BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
            case BlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            case BlendFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
            case BlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
            case BlendFactor::ConstantColor: return VK_BLEND_FACTOR_CONSTANT_COLOR;
            case BlendFactor::OneMinusConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
            }
            return VK_BLEND_FACTOR_ONE;
        }

        static VkBlendOp vkBlendOp(BlendOp op)
        {
            switch (op)
            {
            case BlendOp::Add: return VK_BLEND_OP_ADD;
            case BlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
            case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
            case BlendOp::Min: return VK_BLEND_OP_MIN;
            case BlendOp::Max: return VK_BLEND_OP_MAX;
            }
            return VK_BLEND_OP_ADD;
        }

        static VkColorComponentFlags vkColorWrite(ColorComponentFlags f)
        {
            VkColorComponentFlags out = 0;
            if (f & (uint32_t)ColorComponentFlagBits::R) out |= VK_COLOR_COMPONENT_R_BIT;
            if (f & (uint32_t)ColorComponentFlagBits::G) out |= VK_COLOR_COMPONENT_G_BIT;
            if (f & (uint32_t)ColorComponentFlagBits::B) out |= VK_COLOR_COMPONENT_B_BIT;
            if (f & (uint32_t)ColorComponentFlagBits::A) out |= VK_COLOR_COMPONENT_A_BIT;
            return out;
        }

        static VkFilter vkFilter(FilterMode m) { return m == FilterMode::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST; }
        static VkSamplerMipmapMode vkMipMode(MipmapMode m) { return m == MipmapMode::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST; }
        static VkSamplerAddressMode vkAddress(AddressMode m)
        {
            switch (m)
            {
            case AddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case AddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case AddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case AddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            }
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }

        static VkImageType vkImageType(TextureType t)
        {
            switch (t)
            {
            case TextureType::Tex2D: return VK_IMAGE_TYPE_2D;
            case TextureType::TexCube: return VK_IMAGE_TYPE_2D;
            case TextureType::Tex3D: return VK_IMAGE_TYPE_3D;
            }
            return VK_IMAGE_TYPE_2D;
        }

        static VkImageViewType vkImageViewType(const TextureDesc &d)
        {
            if (d.type == TextureType::TexCube) return d.array_layers > 1 ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
            if (d.type == TextureType::Tex3D) return VK_IMAGE_VIEW_TYPE_3D;
            return d.array_layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        }

        static VkAttachmentLoadOp vkLoadOp(LoadOp op)
        {
            switch (op)
            {
            case LoadOp::Load: return VK_ATTACHMENT_LOAD_OP_LOAD;
            case LoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
            case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            }
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }
        static VkAttachmentStoreOp vkStoreOp(StoreOp op)
        {
            return op == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        }

        static uint64_t hashRenderPassDescVK(const RenderPassDesc &d)
        {
            uint64_t h = 1469598103934665603ull;
            auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
            mix(d.attachment_count);
            for (uint32_t i = 0; i < d.attachment_count; ++i)
            {
                mix((uint64_t)d.attachments[i].format);
                mix((uint64_t)d.attachments[i].load_op);
                mix((uint64_t)d.attachments[i].store_op);
                mix((uint64_t)d.attachments[i].stencil_load_op);
                mix((uint64_t)d.attachments[i].stencil_store_op);
                mix((uint64_t)d.attachments[i].initial_layout);
                mix((uint64_t)d.attachments[i].final_layout);
                mix(d.attachments[i].is_depth_stencil ? 1u : 0u);
            }
            return h;
        }

        //==================================================================
        // Debug messenger
        //==================================================================
        static VKAPI_ATTR VkBool32 VKAPI_CALL vkDebugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT type,
            const VkDebugUtilsMessengerCallbackDataEXT *data,
            void *)
        {
            if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                GEN_LOG_ERROR("[VK-validation] %s", data->pMessage ? data->pMessage : "");
            else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
                GEN_LOG_WARN("[VK-validation] %s", data->pMessage ? data->pMessage : "");
            (void)type;
            return VK_FALSE;
        }

        //==================================================================
        // Buffer / texture / sampler / layout accessors
        //==================================================================
        void *VkBufferRHI::map(uint64_t offset, uint64_t size)
        {
            (void)offset;
            (void)size;
            return mapped;
        }
        void VkBufferRHI::unmap() {}

        const ResourceBinding *VkResourceLayoutRHI::findBinding(uint32_t set, uint32_t binding) const
        {
            for (const auto &b : desc_.bindings)
                if (b.set == set && b.binding == binding) return &b;
            return nullptr;
        }

        //==================================================================
        // Device lifetime
        //==================================================================
        VkDeviceRHI::VkDeviceRHI() {}
        VkDeviceRHI::~VkDeviceRHI() { shutdown(); }

        bool VkDeviceRHI::initialize(const DeviceDesc &d)
        {
            desc_ = d;
            if (!createInstance()) return false;
            if (desc_.enable_validation && !setupDebugMessenger())
                GEN_LOG_WARN("[VK] validation layers requested but unavailable");
            if (!pickPhysicalDevice()) return false;
            if (!createLogicalDevice()) return false;
            if (!createVmaAllocator()) return false;
            if (!createCommandPools()) return false;
            if (!createDescriptorPool()) return false;
            if (!createSyncObjects()) return false;

            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(physical_device_, &props);
            info_.backend = BackendType::Vulkan;
            info_.device_name = props.deviceName;
            char ver[64];
            snprintf(ver, sizeof(ver), "%u.%u.%u",
                     VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion));
            info_.api_version = ver;
            info_.max_texture_size = props.limits.maxImageDimension2D;
            info_.max_uniform_buffer_bindings = 32;
            info_.max_texture_image_units = props.limits.maxPerStageDescriptorSampledImages;
            info_.max_uniform_buffer_range = props.limits.maxUniformBufferRange;
            info_.min_uniform_buffer_offset_alignment = props.limits.minUniformBufferOffsetAlignment;
            info_.max_push_constant_size = props.limits.maxPushConstantsSize;
            VkPhysicalDeviceFeatures device_features{};
            vkGetPhysicalDeviceFeatures(physical_device_, &device_features);
            info_.supports_compute = true;
            info_.supports_geometry_shader = device_features.geometryShader == VK_TRUE;
            info_.supports_tessellation = device_features.tessellationShader == VK_TRUE;
            info_.supports_multisample = true;
            info_.supports_srgb_framebuffer = true;
            info_.clip_depth_zero_to_one = true;
            info_.present_row0_top = true;
            info_.frames_in_flight = VK_MAX_FRAMES_IN_FLIGHT;
            // D24S8 is optional in Vulkan (Apple GPUs behind MoltenVK lack it);
            // pick the first depth-stencil format that can be rendered to.
            info_.depth_format = PixelFormat::D32_SFLOAT;
            for (PixelFormat candidate : {PixelFormat::D24_UNORM_S8_UINT, PixelFormat::D32_SFLOAT_S8_UINT})
            {
                VkFormatProperties fp;
                vkGetPhysicalDeviceFormatProperties(physical_device_, vkPixelFormat(candidate), &fp);
                if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
                {
                    info_.depth_format = candidate;
                    break;
                }
            }

            GEN_LOG_INFO("[VK] device: %s (Vulkan %s), depth format %u", info_.device_name.c_str(),
                         info_.api_version.c_str(), (unsigned)info_.depth_format);

            if (desc_.native_window)
            {
                window_ = desc_.native_window;
                if (!createSwapchain(desc_.native_window, desc_.width, desc_.height))
                {
                    GEN_LOG_ERROR("[VK] swapchain creation failed");
                    return false;
                }
            }
            initialized_ = true;
            return true;
        }

        bool VkDeviceRHI::createInstance()
        {
            VkApplicationInfo app_info{};
            app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            app_info.pApplicationName = desc_.app_name.c_str();
            app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
            app_info.pEngineName = "Genesis";
            app_info.engineVersion = VK_MAKE_VERSION(2, 0, 0);
            app_info.apiVersion = VK_API_VERSION_1_1;

            // Extensions ---------------------------------------------------
            std::vector<const char *> extensions;
            if (window_ || desc_.native_window)
            {
                unsigned int sdl_ext_count = 0;
                SDL_Vulkan_GetInstanceExtensions(reinterpret_cast<SDL_Window *>(desc_.native_window), &sdl_ext_count, nullptr);
                std::vector<const char *> sdl_exts(sdl_ext_count);
                if (sdl_ext_count > 0)
                    SDL_Vulkan_GetInstanceExtensions(reinterpret_cast<SDL_Window *>(desc_.native_window), &sdl_ext_count, sdl_exts.data());
                for (auto *e : sdl_exts) extensions.push_back(e);
            }
            else
            {
                // Headless: no surface extensions needed.
            }
            if (desc_.enable_validation) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

            // Layers -------------------------------------------------------
            std::vector<const char *> layers;
            if (desc_.enable_validation)
            {
                uint32_t layer_count = 0;
                vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
                std::vector<VkLayerProperties> available(layer_count);
                if (layer_count) vkEnumerateInstanceLayerProperties(&layer_count, available.data());
                for (const auto &l : available)
                {
                    if (strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0)
                    {
                        layers.push_back("VK_LAYER_KHRONOS_validation");
                        break;
                    }
                }
                if (layers.empty())
                {
                    GEN_LOG_WARN("[VK] VK_LAYER_KHRONOS_validation not found; continuing without validation");
                    extensions.erase(std::remove(extensions.begin(), extensions.end(), VK_EXT_DEBUG_UTILS_EXTENSION_NAME), extensions.end());
                }
                else
                {
                    GEN_LOG_INFO("[VK] validation layer VK_LAYER_KHRONOS_validation enabled");
                }
            }

            VkInstanceCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            ci.pApplicationInfo = &app_info;
            ci.enabledExtensionCount = (uint32_t)extensions.size();
            ci.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();
            ci.enabledLayerCount = (uint32_t)layers.size();
            ci.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

#if defined(__APPLE__)
            ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            extensions.push_back("VK_KHR_portability_enumeration");
            extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
            ci.enabledExtensionCount = (uint32_t)extensions.size();
            ci.ppEnabledExtensionNames = extensions.data();
#endif

            VkResult res = vkCreateInstance(&ci, nullptr, &instance_);
            if (res != VK_SUCCESS)
            {
                GEN_LOG_ERROR("[VK] vkCreateInstance failed: %d", (int)res);
                return false;
            }
            return true;
        }

        bool VkDeviceRHI::setupDebugMessenger()
        {
            auto create_fn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT");
            if (!create_fn) return false;
            VkDebugUtilsMessengerCreateInfoEXT ci{};
            ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            ci.pfnUserCallback = vkDebugCallback;
            return create_fn(instance_, &ci, nullptr, &debug_messenger_) == VK_SUCCESS;
        }

        bool VkDeviceRHI::pickPhysicalDevice()
        {
            uint32_t count = 0;
            vkEnumeratePhysicalDevices(instance_, &count, nullptr);
            if (count == 0)
            {
                GEN_LOG_ERROR("[VK] no Vulkan physical devices found");
                return false;
            }
            std::vector<VkPhysicalDevice> devices(count);
            vkEnumeratePhysicalDevices(instance_, &count, devices.data());

            int best_score = -1;
            for (auto dev : devices)
            {
                VkPhysicalDeviceProperties props;
                VkPhysicalDeviceFeatures feats;
                vkGetPhysicalDeviceProperties(dev, &props);
                vkGetPhysicalDeviceFeatures(dev, &feats);

                // Must have a graphics queue
                uint32_t qf_count = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(dev, &qf_count, nullptr);
                std::vector<VkQueueFamilyProperties> qfs(qf_count);
                vkGetPhysicalDeviceQueueFamilyProperties(dev, &qf_count, qfs.data());
                bool has_graphics = false;
                for (const auto &q : qfs)
                    if (q.queueFlags & VK_QUEUE_GRAPHICS_BIT) { has_graphics = true; break; }
                if (!has_graphics) continue;

                int score = 0;
                if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 1000;
                else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 500;
                else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) score += 10;
                score += (int)(props.limits.maxImageDimension2D / 1024);
                if (feats.samplerAnisotropy) score += 20;
                if (score > best_score)
                {
                    best_score = score;
                    physical_device_ = dev;
                }
            }
            if (!physical_device_)
            {
                GEN_LOG_ERROR("[VK] no suitable physical device");
                return false;
            }
            return true;
        }

        bool VkDeviceRHI::createLogicalDevice()
        {
            uint32_t qf_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &qf_count, nullptr);
            std::vector<VkQueueFamilyProperties> qfs(qf_count);
            vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &qf_count, qfs.data());

            for (uint32_t i = 0; i < qf_count; ++i)
            {
                if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    graphics_queue_family_ = i;
                    break;
                }
            }
            if (graphics_queue_family_ == UINT32_MAX) return false;

            present_queue_family_ = graphics_queue_family_;
            if (surface_)
            {
                VkBool32 present_support = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(physical_device_, graphics_queue_family_, surface_, &present_support);
                if (!present_support)
                {
                    for (uint32_t i = 0; i < qf_count; ++i)
                    {
                        VkBool32 ok = VK_FALSE;
                        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device_, i, surface_, &ok);
                        if (ok) { present_queue_family_ = i; break; }
                    }
                }
            }

            std::set<uint32_t> unique_families = {graphics_queue_family_, present_queue_family_};
            float priority = 1.0f;
            std::vector<VkDeviceQueueCreateInfo> queue_infos;
            for (uint32_t family : unique_families)
            {
                VkDeviceQueueCreateInfo qci{};
                qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                qci.queueFamilyIndex = family;
                qci.queueCount = 1;
                qci.pQueuePriorities = &priority;
                queue_infos.push_back(qci);
            }

            // Request features conditionally: only the ones the physical
            // device actually advertises.  MoltenVK (macOS) does not expose
            // wideLines / largePoints and enabling them yields
            // VK_ERROR_FEATURE_NOT_PRESENT at vkCreateDevice; requesting a
            // feature the device cannot provide must never happen.
            VkPhysicalDeviceFeatures device_feats{};
            vkGetPhysicalDeviceFeatures(physical_device_, &device_feats);

            VkPhysicalDeviceFeatures features{};
            // Anisotropy is heavily used by the PBR sampling path; request it
            // when supported (it is on essentially every Vulkan 1.0 device).
            features.samplerAnisotropy = device_feats.samplerAnisotropy;
            // Wireframe (PolygonMode::Line) needs fillModeNonSolid; only ask
            // for it where available (llvmpipe / most GPUs support it).
            features.fillModeNonSolid = device_feats.fillModeNonSolid;
            // Wide lines / large points are non-essential (debug lines fall
            // back to the guaranteed 1px case).  Wide lines are notably NOT
            // exposed by MoltenVK, hence the conditional request.
            features.wideLines = device_feats.wideLines;
            features.largePoints = device_feats.largePoints;
            features.multiViewport = device_feats.multiViewport;
            features.textureCompressionBC = VK_FALSE;

            // Record the feature mask so callers can query capabilities.
            device_features_ = device_feats;

            const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

            VkDeviceCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            ci.queueCreateInfoCount = (uint32_t)queue_infos.size();
            ci.pQueueCreateInfos = queue_infos.data();
            ci.pEnabledFeatures = &features;
            ci.enabledExtensionCount = 1;
            ci.ppEnabledExtensionNames = device_extensions;
#if defined(__APPLE__)
            const char *portability_ext = "VK_KHR_portability_subset";
            const char *all_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, portability_ext};
            ci.enabledExtensionCount = 2;
            ci.ppEnabledExtensionNames = all_exts;
#endif

            VkResult res = vkCreateDevice(physical_device_, &ci, nullptr, &device_);
            if (res != VK_SUCCESS)
            {
                GEN_LOG_ERROR("[VK] vkCreateDevice failed: %d", (int)res);
                return false;
            }
            vkGetDeviceQueue(device_, graphics_queue_family_, 0, &graphics_queue_);
            vkGetDeviceQueue(device_, present_queue_family_, 0, &present_queue_);
            return true;
        }

        bool VkDeviceRHI::createVmaAllocator()
        {
            VmaAllocatorCreateInfo ci{};
            ci.physicalDevice = physical_device_;
            ci.device = device_;
            ci.instance = instance_;
            ci.flags = 0;
            return vmaCreateAllocator(&ci, reinterpret_cast<VmaAllocator *>(&vma_allocator_)) == VK_SUCCESS;
        }

        bool VkDeviceRHI::createCommandPools()
        {
            VkCommandPoolCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            ci.queueFamilyIndex = graphics_queue_family_;
            ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            if (vkCreateCommandPool(device_, &ci, nullptr, &command_pool_) != VK_SUCCESS) return false;
            if (vkCreateCommandPool(device_, &ci, nullptr, &transient_pool_) != VK_SUCCESS) return false;
            return true;
        }

        bool VkDeviceRHI::createDescriptorPool()
        {
            VkDescriptorPoolSize sizes[] = {
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 4096},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 256},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192},
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 2048},
                {VK_DESCRIPTOR_TYPE_SAMPLER, 1024},
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 512},
            };
            VkDescriptorPoolCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            ci.maxSets = 8192;
            ci.poolSizeCount = (uint32_t)(sizeof(sizes) / sizeof(sizes[0]));
            ci.pPoolSizes = sizes;
            return vkCreateDescriptorPool(device_, &ci, nullptr, &descriptor_pool_) == VK_SUCCESS;
        }

        bool VkDeviceRHI::createSyncObjects()
        {
            image_available_.resize(VK_MAX_FRAMES_IN_FLIGHT);
            render_finished_.resize(VK_MAX_FRAMES_IN_FLIGHT);
            in_flight_fences_.resize(VK_MAX_FRAMES_IN_FLIGHT);

            VkSemaphoreCreateInfo sci{};
            sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            VkFenceCreateInfo fci{};
            fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i)
            {
                if (vkCreateSemaphore(device_, &sci, nullptr, &image_available_[i]) != VK_SUCCESS) return false;
                if (vkCreateSemaphore(device_, &sci, nullptr, &render_finished_[i]) != VK_SUCCESS) return false;
                if (vkCreateFence(device_, &fci, nullptr, &in_flight_fences_[i]) != VK_SUCCESS) return false;
            }
            return true;
        }

        //==================================================================
        // Swapchain
        //==================================================================
        bool VkDeviceRHI::createSwapchain(void *native_window, uint32_t width, uint32_t height)
        {
            if (native_window) window_ = native_window;
            if (!window_) window_ = desc_.native_window;
            if (!window_)
            {
                GEN_LOG_ERROR("[VK] createSwapchain without a window");
                return false;
            }
            if (!surface_)
            {
                if (!SDL_Vulkan_CreateSurface(reinterpret_cast<SDL_Window *>(window_), instance_, &surface_))
                {
                    GEN_LOG_ERROR("[VK] SDL_Vulkan_CreateSurface failed: %s", SDL_GetError());
                    return false;
                }
                // Queue family may depend on the surface; re-create the device
                // queues selection if the surface appeared after device creation.
            }
            if (!createSwapchainObjects(width, height)) return false;
            swapchain_valid_ = true;
            initialized_ = true;

            if (frame_command_buffers_.empty())
            {
                for (uint32_t i = 0; i < VK_MAX_FRAMES_IN_FLIGHT; ++i)
                {
                    VkCommandBufferRHI *cb = static_cast<VkCommandBufferRHI *>(createCommandBuffer(CommandBufferUsage::RenderFrame));
                    frame_command_buffers_.push_back(cb);
                }
            }
            return true;
        }

        bool VkDeviceRHI::createSwapchainObjects(uint32_t width, uint32_t height)
        {
            VkSurfaceCapabilitiesKHR caps;
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &caps);

            uint32_t format_count = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr);
            std::vector<VkSurfaceFormatKHR> formats(format_count);
            if (format_count) vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, formats.data());

            VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : formats[0];
            for (const auto &f : formats)
            {
                bool want_srgb = desc_.srgb_backbuffer;
                bool is_srgb = (f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB);
                bool is_unorm = (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM);
                if ((want_srgb && is_srgb) || (!want_srgb && is_unorm))
                {
                    chosen = f;
                    break;
                }
            }
            swapchain_format_ = chosen.format;
            swapchain_format_rhi_ = vkToPixelFormat(chosen.format);

            uint32_t present_count = 0;
            vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &present_count, nullptr);
            std::vector<VkPresentModeKHR> present_modes(present_count);
            if (present_count) vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &present_count, present_modes.data());
            present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
            if (!desc_.vsync)
            {
                for (auto m : present_modes)
                    if (m == VK_PRESENT_MODE_MAILBOX_KHR) { present_mode_ = m; break; }
            }

            VkExtent2D extent = caps.currentExtent;
            if (extent.width == 0xFFFFFFFF)
            {
                extent.width = std::max(caps.minImageExtent.width, std::min(caps.maxImageExtent.width, width));
                extent.height = std::max(caps.minImageExtent.height, std::min(caps.maxImageExtent.height, height));
            }
            swapchain_extent_ = extent;

            uint32_t image_count = caps.minImageCount + 1;
            if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) image_count = caps.maxImageCount;

            VkSwapchainCreateInfoKHR ci{};
            ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            ci.surface = surface_;
            ci.minImageCount = image_count;
            ci.imageFormat = chosen.format;
            ci.imageColorSpace = chosen.colorSpace;
            ci.imageExtent = extent;
            ci.imageArrayLayers = 1;
            ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (graphics_queue_family_ != present_queue_family_)
            {
                uint32_t families[2] = {graphics_queue_family_, present_queue_family_};
                ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
                ci.queueFamilyIndexCount = 2;
                ci.pQueueFamilyIndices = families;
            }
            ci.preTransform = caps.currentTransform;
            ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            ci.presentMode = present_mode_;
            ci.clipped = VK_TRUE;
            ci.oldSwapchain = VK_NULL_HANDLE;

            if (vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_) != VK_SUCCESS)
            {
                GEN_LOG_ERROR("[VK] vkCreateSwapchainKHR failed");
                return false;
            }

            vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr);
            swapchain_images_.resize(image_count);
            vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, swapchain_images_.data());

            swapchain_image_views_.resize(image_count);
            for (uint32_t i = 0; i < image_count; ++i)
            {
                VkImageViewCreateInfo vci{};
                vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                vci.image = swapchain_images_[i];
                vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
                vci.format = swapchain_format_;
                vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                vci.subresourceRange.baseMipLevel = 0;
                vci.subresourceRange.levelCount = 1;
                vci.subresourceRange.baseArrayLayer = 0;
                vci.subresourceRange.layerCount = 1;
                if (vkCreateImageView(device_, &vci, nullptr, &swapchain_image_views_[i]) != VK_SUCCESS) return false;
            }

            createSwapchainRenderPass();

            // The present pass is colour only (tone mapping, 2D overlays and UI).
            swapchain_framebuffers_.resize(image_count);
            for (uint32_t i = 0; i < image_count; ++i)
            {
                VkImageView attachments[1] = {swapchain_image_views_[i]};
                VkFramebufferCreateInfo fci{};
                fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                fci.renderPass = swapchain_render_pass_->render_pass;
                fci.attachmentCount = 1;
                fci.pAttachments = attachments;
                fci.width = extent.width;
                fci.height = extent.height;
                fci.layers = 1;

                VkFramebufferRHI *fb = new VkFramebufferRHI();
                if (vkCreateFramebuffer(device_, &fci, nullptr, &fb->framebuffer) != VK_SUCCESS)
                {
                    delete fb;
                    return false;
                }
                fb->pass = swapchain_render_pass_;
                fb->w = extent.width;
                fb->h = extent.height;
                fb->fb_name = "swapchain-fb";
                fb->attachment_textures.assign(1, nullptr);
                swapchain_framebuffers_[i] = fb;
            }
            return true;
        }

        void VkDeviceRHI::createSwapchainRenderPass()
        {
            if (swapchain_render_pass_) return;
            RenderPassDesc rpd;
            rpd.name = "swapchain";
            rpd.attachment_count = 1;
            rpd.attachments[0].format = swapchain_format_rhi_;
            rpd.attachments[0].load_op = LoadOp::Clear;
            rpd.attachments[0].store_op = StoreOp::Store;
            rpd.attachments[0].initial_layout = TextureLayout::Undefined;
            rpd.attachments[0].final_layout = TextureLayout::PresentSrc;
            rpd.attachments[0].is_depth_stencil = false;
            swapchain_render_pass_ = static_cast<VkRenderPassRHI *>(createRenderPass(rpd));
        }

        void VkDeviceRHI::destroySwapchainObjects()
        {
            for (auto *fb : swapchain_framebuffers_)
            {
                if (!fb) continue;
                vkDestroyFramebuffer(device_, fb->framebuffer, nullptr);
                delete fb;
            }
            swapchain_framebuffers_.clear();
            for (auto view : swapchain_image_views_) vkDestroyImageView(device_, view, nullptr);
            swapchain_image_views_.clear();
            swapchain_images_.clear();
            if (swapchain_)
            {
                vkDestroySwapchainKHR(device_, swapchain_, nullptr);
                swapchain_ = VK_NULL_HANDLE;
            }
            swapchain_valid_ = false;
        }

        void VkDeviceRHI::destroySwapchain()
        {
            if (!device_) return;
            waitIdle();
            destroySwapchainObjects();
            if (surface_)
            {
                vkDestroySurfaceKHR(instance_, surface_, nullptr);
                surface_ = VK_NULL_HANDLE;
            }
        }

        void VkDeviceRHI::resizeSwapchain(uint32_t width, uint32_t height)
        {
            if (!swapchain_valid_ || width == 0 || height == 0) return;
            waitIdle();
            VkRenderPassRHI *keep_pass = swapchain_render_pass_;
            swapchain_render_pass_ = nullptr;
            destroySwapchainObjects();
            if (keep_pass)
            {
                destroyRenderPass(keep_pass);
            }
            createSwapchainObjects(width, height);
        }

        //==================================================================
        // Frame loop
        //==================================================================
        bool VkDeviceRHI::beginFrame()
        {
            if (!swapchain_valid_ && !desc_.headless) return false;
            ++frame_number_;

            VkResult res = vkWaitForFences(device_, 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);
            if (res != VK_SUCCESS) return false;

            if (desc_.headless)
            {
                // Offscreen frames: nothing is presented, so no swapchain
                // image is acquired (it would be presented un-rendered).
                vkResetFences(device_, 1, &in_flight_fences_[current_frame_]);
                VkCommandBufferRHI *cb = frame_command_buffers_[current_frame_];
                vkResetCommandBuffer(cb->cmd, 0);
                cb->begin(CommandBufferUsage::RenderFrame);
                return true;
            }

            res = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, image_available_[current_frame_],
                                        VK_NULL_HANDLE, &image_index_);
            if (res == VK_ERROR_OUT_OF_DATE_KHR || framebuffer_resized_)
            {
                framebuffer_resized_ = false;
                int w = 0, h = 0;
                if (window_) SDL_Vulkan_GetDrawableSize(reinterpret_cast<SDL_Window *>(window_), &w, &h);
                resizeSwapchain((uint32_t)std::max(1, w), (uint32_t)std::max(1, h));
                return beginFrame();
            }
            if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) return false;

            vkResetFences(device_, 1, &in_flight_fences_[current_frame_]);

            VkCommandBufferRHI *cb = frame_command_buffers_[current_frame_];
            vkResetCommandBuffer(cb->cmd, 0);
            cb->begin(CommandBufferUsage::RenderFrame);
            return true;
        }

        void VkDeviceRHI::endFrame()
        {
            if (!swapchain_valid_ && !desc_.headless) return;
            VkCommandBufferRHI *cb = frame_command_buffers_[current_frame_];
            cb->end();

            if (desc_.headless)
            {
                VkSubmitInfo si{};
                si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                si.commandBufferCount = 1;
                si.pCommandBuffers = &cb->cmd;
                if (vkQueueSubmit(graphics_queue_, 1, &si, in_flight_fences_[current_frame_]) != VK_SUCCESS)
                    GEN_LOG_ERROR("[VK] queue submit failed");
                current_frame_ = (current_frame_ + 1) % VK_MAX_FRAMES_IN_FLIGHT;
                return;
            }

            VkCommandBuffer raw = cb->cmd;
            VkSemaphore wait_semaphores[] = {image_available_[current_frame_]};
            VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
            VkSemaphore signal_semaphores[] = {render_finished_[current_frame_]};

            VkSubmitInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.waitSemaphoreCount = 1;
            si.pWaitSemaphores = wait_semaphores;
            si.pWaitDstStageMask = wait_stages;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &raw;
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores = signal_semaphores;

            if (vkQueueSubmit(graphics_queue_, 1, &si, in_flight_fences_[current_frame_]) != VK_SUCCESS)
            {
                GEN_LOG_ERROR("[VK] queue submit failed");
                return;
            }

            VkPresentInfoKHR pi{};
            pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            pi.waitSemaphoreCount = 1;
            pi.pWaitSemaphores = signal_semaphores;
            pi.swapchainCount = 1;
            pi.pSwapchains = &swapchain_;
            pi.pImageIndices = &image_index_;

            VkResult res = vkQueuePresentKHR(present_queue_, &pi);
            if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || framebuffer_resized_)
            {
                framebuffer_resized_ = false;
                int w = 0, h = 0;
                if (window_) SDL_Vulkan_GetDrawableSize(reinterpret_cast<SDL_Window *>(window_), &w, &h);
                resizeSwapchain((uint32_t)std::max(1, w), (uint32_t)std::max(1, h));
            }

            current_frame_ = (current_frame_ + 1) % VK_MAX_FRAMES_IN_FLIGHT;
        }

        void VkDeviceRHI::waitIdle()
        {
            if (device_) vkDeviceWaitIdle(device_);
        }

        void VkDeviceRHI::shutdown()
        {
            if (!device_ && !instance_) return;
            if (device_) waitIdle();

            for (auto *p : command_buffers_)
            {
                if (p->pool && p->cmd) vkFreeCommandBuffers(device_, p->pool, 1, &p->cmd);
                delete p;
            }
            command_buffers_.clear();
            frame_command_buffers_.clear();

            for (auto *p : descriptor_sets_) delete p;
            descriptor_sets_.clear();
            for (auto *p : framebuffers_)
            {
                if (p->framebuffer) vkDestroyFramebuffer(device_, p->framebuffer, nullptr);
                for (VkImageView v : p->owned_views) vkDestroyImageView(device_, v, nullptr);
                delete p;
            }
            framebuffers_.clear();
            for (auto &kv : render_passes_)
            {
                if (kv.second->render_pass) vkDestroyRenderPass(device_, kv.second->render_pass, nullptr);
                delete kv.second;
            }
            render_passes_.clear();
            swapchain_render_pass_ = nullptr;

            while (!pipelines_.empty()) destroyPipeline(pipelines_.back());
            while (!layouts_.empty()) destroyResourceLayout(layouts_.back());
            while (!shaders_.empty()) destroyShader(shaders_.back());
            while (!samplers_.empty()) destroySampler(samplers_.back());
            while (!textures_.empty()) destroyTexture(textures_.back());
            while (!buffers_.empty()) destroyBuffer(buffers_.back());

            for (uint32_t i = 0; i < image_available_.size(); ++i)
            {
                vkDestroySemaphore(device_, image_available_[i], nullptr);
                vkDestroySemaphore(device_, render_finished_[i], nullptr);
                vkDestroyFence(device_, in_flight_fences_[i], nullptr);
            }
            image_available_.clear();
            render_finished_.clear();
            in_flight_fences_.clear();

            destroySwapchainObjects();
            if (descriptor_pool_) { vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr); descriptor_pool_ = VK_NULL_HANDLE; }
            if (command_pool_) { vkDestroyCommandPool(device_, command_pool_, nullptr); command_pool_ = VK_NULL_HANDLE; }
            if (transient_pool_) { vkDestroyCommandPool(device_, transient_pool_, nullptr); transient_pool_ = VK_NULL_HANDLE; }
            if (vma_allocator_) { vmaDestroyAllocator(vmaAlloc(vma_allocator_)); vma_allocator_ = nullptr; }
            if (device_) { vkDestroyDevice(device_, nullptr); device_ = VK_NULL_HANDLE; }
            if (debug_messenger_)
            {
                auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
                if (fn) fn(instance_, debug_messenger_, nullptr);
                debug_messenger_ = VK_NULL_HANDLE;
            }
            if (surface_) { vkDestroySurfaceKHR(instance_, surface_, nullptr); surface_ = VK_NULL_HANDLE; }
            if (instance_) { vkDestroyInstance(instance_, nullptr); instance_ = VK_NULL_HANDLE; }
            initialized_ = false;
        }

        //==================================================================
        // Resource creation
        //==================================================================
        RHIBuffer *VkDeviceRHI::createBuffer(const BufferDesc &desc)
        {
            VkBufferUsageFlags usage = 0;
            if (desc.usage & (uint32_t)BufferUsageFlagBits::Vertex) usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            if (desc.usage & (uint32_t)BufferUsageFlagBits::Index) usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            if (desc.usage & (uint32_t)BufferUsageFlagBits::Uniform) usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            if (desc.usage & (uint32_t)BufferUsageFlagBits::Storage) usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            if (desc.usage & (uint32_t)BufferUsageFlagBits::Indirect) usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
            if (desc.usage & (uint32_t)BufferUsageFlagBits::TransferSrc) usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            if (desc.usage & (uint32_t)BufferUsageFlagBits::TransferDst) usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            if (usage == 0) usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            // Always allow staging copies into any buffer we create.
            usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

            VkBufferCreateInfo bci{};
            bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bci.size = (VkDeviceSize)std::max<uint64_t>(desc.size, 4);
            bci.usage = usage;
            bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo aci{};
            bool has_initial = (desc.initial_data && desc.initial_data_size > 0);
            // A buffer created with initial data must be host-visible and
            // persistently mapped (we copy straight into it).  On MoltenVK a
            // GPU_ONLY allocation cannot be mapped at all (vmaMapMemory returns
            // VK_ERROR_MEMORY_MAP_FAILED), so force CPU_TO_GPU memory whenever
            // data is supplied.
            MemoryUsage mem_usage = desc.memory_usage;
            if (has_initial) mem_usage = MemoryUsage::CpuToGpu;
            switch (mem_usage)
            {
            case MemoryUsage::GpuOnly:
                aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;
                break;
            case MemoryUsage::CpuToGpu:
                aci.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
                aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
                break;
            case MemoryUsage::GpuToCpu:
                aci.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
                aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
                break;
            }

            VkBufferRHI *buf = new VkBufferRHI();
            buf->desc_ = desc;
            VmaAllocationInfo alloc_info;
            VmaAllocation buffer_allocation = VK_NULL_HANDLE;
            VkResult res = vmaCreateBuffer(vmaAlloc(vma_allocator_), &bci, &aci,
                                           &buf->buffer, &buffer_allocation, &alloc_info);
            buf->allocation = buffer_allocation;
            if (res != VK_SUCCESS)
            {
                GEN_LOG_ERROR("[VK] createBuffer('%s') failed: %d", desc.name.c_str(), (int)res);
                delete buf;
                return nullptr;
            }
            if (alloc_info.pMappedData)
            {
                buf->mapped = alloc_info.pMappedData;
                buf->persistently_mapped = true;
            }
            if (desc.initial_data && desc.initial_data_size > 0)
            {
                void *dst = buf->mapped;
                bool need_unmap = false;
                if (!dst)
                {
                    vmaMapMemory(vmaAlloc(vma_allocator_), vmaAllocOf(buf->allocation), &dst);
                    need_unmap = true;
                }
                if (dst)
                {
                    std::memcpy(dst, desc.initial_data, (size_t)std::min<uint64_t>(desc.initial_data_size, desc.size));
                    vmaFlushAllocation(vmaAlloc(vma_allocator_), vmaAllocOf(buf->allocation), 0, (VkDeviceSize)desc.size);
                }
                if (need_unmap)
                    vmaUnmapMemory(vmaAlloc(vma_allocator_), vmaAllocOf(buf->allocation));
            }
            buffers_.push_back(buf);
            return buf;
        }

        RHITexture *VkDeviceRHI::createTexture(const TextureDesc &desc)
        {
            VkImageUsageFlags usage = 0;
            if (desc.usage & (uint32_t)TextureUsageFlagBits::Sampled) usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
            if (desc.usage & (uint32_t)TextureUsageFlagBits::RenderTarget) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            if (desc.usage & (uint32_t)TextureUsageFlagBits::DepthStencil) usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            if (desc.usage & (uint32_t)TextureUsageFlagBits::Storage) usage |= VK_IMAGE_USAGE_STORAGE_BIT;
            if (desc.usage & (uint32_t)TextureUsageFlagBits::TransferSrc) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            if (desc.usage & (uint32_t)TextureUsageFlagBits::TransferDst) usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            if (usage == 0) usage = VK_IMAGE_USAGE_SAMPLED_BIT;
            if (desc.generate_mipmaps) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

            VkImageCreateInfo ici{};
            ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ici.imageType = vkImageType(desc.type);
            ici.format = vkPixelFormat(desc.format);
            ici.extent = {(uint32_t)std::max(1u, desc.width), (uint32_t)std::max(1u, desc.height), (uint32_t)std::max(1u, desc.depth)};
            ici.mipLevels = std::max(1u, desc.mip_levels);
            ici.arrayLayers = desc.layerCount();
            ici.samples = VK_SAMPLE_COUNT_1_BIT;
            ici.tiling = VK_IMAGE_TILING_OPTIMAL;
            ici.usage = usage;
            ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (desc.type == TextureType::TexCube) ici.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

            VmaAllocationCreateInfo aci{};
            aci.usage = (desc.memory_usage == MemoryUsage::GpuToCpu) ? VMA_MEMORY_USAGE_GPU_TO_CPU : VMA_MEMORY_USAGE_GPU_ONLY;

            VkTextureRHI *tex = new VkTextureRHI();
            tex->desc_ = desc;
            VmaAllocationInfo alloc_info;
            VmaAllocation image_allocation = VK_NULL_HANDLE;
            VkResult res = vmaCreateImage(vmaAlloc(vma_allocator_), &ici, &aci,
                                          &tex->image, &image_allocation, &alloc_info);
            tex->allocation = image_allocation;
            if (res != VK_SUCCESS)
            {
                GEN_LOG_ERROR("[VK] createTexture('%s') failed: %d", desc.name.c_str(), (int)res);
                delete tex;
                return nullptr;
            }

            VkImageViewCreateInfo vci{};
            vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vci.image = tex->image;
            vci.viewType = vkImageViewType(desc);
            vci.format = ici.format;
            vci.subresourceRange.aspectMask = vkAspectMask(desc.format);
            vci.subresourceRange.baseMipLevel = 0;
            vci.subresourceRange.levelCount = ici.mipLevels;
            vci.subresourceRange.baseArrayLayer = 0;
            vci.subresourceRange.layerCount = ici.arrayLayers;
            if (vkCreateImageView(device_, &vci, nullptr, &tex->view) != VK_SUCCESS)
            {
                GEN_LOG_ERROR("[VK] createImageView('%s') failed", desc.name.c_str());
                vmaDestroyImage(vmaAlloc(vma_allocator_), tex->image, vmaAllocOf(tex->allocation));
                delete tex;
                return nullptr;
            }

            tex->layout_ = TextureLayout::Undefined;
            textures_.push_back(tex);
            return tex;
        }

        RHISampler *VkDeviceRHI::createSampler(const SamplerDesc &desc)
        {
            VkSamplerCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            ci.magFilter = vkFilter(desc.mag_filter);
            ci.minFilter = vkFilter(desc.min_filter);
            ci.mipmapMode = vkMipMode(desc.mipmap_mode);
            ci.addressModeU = vkAddress(desc.address_u);
            ci.addressModeV = vkAddress(desc.address_v);
            ci.addressModeW = vkAddress(desc.address_w);
            ci.mipLodBias = 0.0f;
            ci.anisotropyEnable = desc.max_anisotropy > 1.0f ? VK_TRUE : VK_FALSE;
            ci.maxAnisotropy = desc.max_anisotropy;
            ci.compareEnable = desc.compare_enable ? VK_TRUE : VK_FALSE;
            ci.compareOp = vkCompare(desc.compare_op);
            ci.minLod = desc.min_lod;
            ci.maxLod = desc.max_lod;
            ci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
            ci.unnormalizedCoordinates = VK_FALSE;

            VkSamplerRHI *s = new VkSamplerRHI();
            s->desc_ = desc;
            if (vkCreateSampler(device_, &ci, nullptr, &s->sampler) != VK_SUCCESS)
            {
                GEN_LOG_ERROR("[VK] createSampler('%s') failed", desc.name.c_str());
                delete s;
                return nullptr;
            }
            samplers_.push_back(s);
            return s;
        }

        RHIShader *VkDeviceRHI::createShader(const ShaderStageDesc &desc)
        {
            if (desc.code_type != ShaderCodeType::Spirv || !desc.code)
            {
                GEN_LOG_ERROR("[VK] shader '%s': Vulkan backend requires SPIR-V", desc.name.c_str());
                return nullptr;
            }
            VkShaderModuleCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            ci.codeSize = (size_t)desc.code_size;
            ci.pCode = reinterpret_cast<const uint32_t *>(desc.code);

            VkShaderRHI *shader = new VkShaderRHI();
            shader->stage_flag = desc.stage;
            shader->shader_name = desc.name;
            if (vkCreateShaderModule(device_, &ci, nullptr, &shader->module) != VK_SUCCESS)
            {
                GEN_LOG_ERROR("[VK] shader module creation failed for '%s'", desc.name.c_str());
                delete shader;
                return nullptr;
            }
            shaders_.push_back(shader);
            return shader;
        }

        RHIResourceLayout *VkDeviceRHI::createResourceLayout(const ResourceLayoutDesc &desc)
        {
            uint32_t max_set = 0;
            for (const auto &b : desc.bindings) max_set = std::max(max_set, b.set);
            uint32_t set_count = desc.bindings.empty() ? 0 : max_set + 1;

            VkResourceLayoutRHI *layout = new VkResourceLayoutRHI();
            layout->desc_ = desc;
            layout->set_layouts.resize(set_count, VK_NULL_HANDLE);

            for (uint32_t s = 0; s < set_count; ++s)
            {
                std::vector<VkDescriptorSetLayoutBinding> bindings;
                for (const auto &b : desc.bindings)
                {
                    if (b.set != s) continue;
                    VkDescriptorSetLayoutBinding lb{};
                    lb.binding = b.binding;
                    lb.descriptorType = vkDescriptorType(b.type);
                    lb.descriptorCount = std::max(1u, b.count);
                    lb.stageFlags = vkStageMask(b.stages);
                    bindings.push_back(lb);
                }
                VkDescriptorSetLayoutCreateInfo ci{};
                ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                ci.bindingCount = (uint32_t)bindings.size();
                ci.pBindings = bindings.data();
                if (vkCreateDescriptorSetLayout(device_, &ci, nullptr, &layout->set_layouts[s]) != VK_SUCCESS)
                {
                    GEN_LOG_ERROR("[VK] descriptor set layout creation failed");
                    delete layout;
                    return nullptr;
                }
            }

            if (desc.push_constant_size > 0)
            {
                VkPushConstantRange range{};
                range.stageFlags = vkStageMask(desc.push_constant_stages);
                range.offset = 0;
                range.size = desc.push_constant_size;
                layout->push_ranges.push_back(range);
            }

            VkPipelineLayoutCreateInfo plci{};
            plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            plci.setLayoutCount = (uint32_t)layout->set_layouts.size();
            plci.pSetLayouts = layout->set_layouts.empty() ? nullptr : layout->set_layouts.data();
            plci.pushConstantRangeCount = (uint32_t)layout->push_ranges.size();
            plci.pPushConstantRanges = layout->push_ranges.empty() ? nullptr : layout->push_ranges.data();
            if (vkCreatePipelineLayout(device_, &plci, nullptr, &layout->pipeline_layout) != VK_SUCCESS)
            {
                GEN_LOG_ERROR("[VK] pipeline layout creation failed");
                delete layout;
                return nullptr;
            }
            layouts_.push_back(layout);
            return layout;
        }

        RHIRenderPass *VkDeviceRHI::createRenderPass(const RenderPassDesc &desc)
        {
            uint64_t hash = hashRenderPassDescVK(desc);
            auto it = render_passes_.find(hash);
            if (it != render_passes_.end()) return it->second;

            std::vector<VkAttachmentDescription> attachments;
            std::vector<VkAttachmentReference> color_refs;
            VkAttachmentReference depth_ref{};
            bool has_depth = false;
            uint32_t color_index = 0;

            for (uint32_t i = 0; i < desc.attachment_count; ++i)
            {
                const AttachmentDesc &ad = desc.attachments[i];
                VkAttachmentDescription att{};
                att.format = vkPixelFormat(ad.format);
                att.samples = VK_SAMPLE_COUNT_1_BIT;
                att.loadOp = vkLoadOp(ad.load_op);
                att.storeOp = vkStoreOp(ad.store_op);
                att.stencilLoadOp = vkLoadOp(ad.stencil_load_op);
                att.stencilStoreOp = vkStoreOp(ad.stencil_store_op);
                att.initialLayout = vkTextureLayout(ad.initial_layout);
                att.finalLayout = vkTextureLayout(ad.final_layout);
                if (ad.is_depth_stencil)
                {
                    // Depth attachments normally live at the end; keep the
                    // natural ordering but make sure the reference index is right.
                    depth_ref.attachment = (uint32_t)attachments.size();
                    depth_ref.layout = vkTextureLayout(ad.final_layout) == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                           ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                           : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    has_depth = true;
                    // initial layout must be defined for depth
                    att.initialLayout = att.initialLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_IMAGE_LAYOUT_UNDEFINED : att.initialLayout;
                }
                else
                {
                    VkAttachmentReference ref{};
                    ref.attachment = (uint32_t)attachments.size();
                    ref.layout = vkTextureLayout(ad.initial_layout) == VK_IMAGE_LAYOUT_GENERAL
                                     ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                     : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    color_refs.push_back(ref);
                    ++color_index;
                }
                attachments.push_back(att);
            }

            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = (uint32_t)color_refs.size();
            subpass.pColorAttachments = color_refs.empty() ? nullptr : color_refs.data();
            if (has_depth) subpass.pDepthStencilAttachment = &depth_ref;

            // Passes are chained freely (render -> sample -> render, blits,
            // readbacks), so order every attachment write against earlier
            // attachment/shader/transfer work and make it visible to later
            // sampling and copies.
            const VkPipelineStageFlags attachment_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            const VkAccessFlags attachment_access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            std::vector<VkSubpassDependency> dependencies(2);
            dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
            dependencies[0].dstSubpass = 0;
            dependencies[0].srcStageMask = attachment_stages | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
            dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                            VK_ACCESS_TRANSFER_WRITE_BIT;
            dependencies[0].dstStageMask = attachment_stages | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            dependencies[0].dstAccessMask = attachment_access | VK_ACCESS_SHADER_READ_BIT;
            dependencies[1].srcSubpass = 0;
            dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            dependencies[1].srcStageMask = attachment_stages;
            dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | attachment_stages;
            dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | attachment_access;

            VkRenderPassCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            ci.attachmentCount = (uint32_t)attachments.size();
            ci.pAttachments = attachments.data();
            ci.subpassCount = 1;
            ci.pSubpasses = &subpass;
            ci.dependencyCount = (uint32_t)dependencies.size();
            ci.pDependencies = dependencies.data();

            VkRenderPassRHI *pass = new VkRenderPassRHI();
            pass->desc_ = desc;
            pass->hash = hash;
            if (vkCreateRenderPass(device_, &ci, nullptr, &pass->render_pass) != VK_SUCCESS)
            {
                GEN_LOG_ERROR("[VK] render pass creation failed ('%s')", desc.name.c_str());
                delete pass;
                return nullptr;
            }
            render_passes_[hash] = pass;
            return pass;
        }

        RHIPipeline *VkDeviceRHI::createPipeline(const PipelineDesc &desc)
        {
            VkPipelineRHI *pipeline = new VkPipelineRHI();
            pipeline->desc_ = desc;
            pipeline->compute_ = desc.is_compute;

            // ---- Shader modules -----------------------------------------
            std::vector<VkPipelineShaderStageCreateInfo> stages;
            for (const auto &sd : desc.shaders)
            {
                if (sd.code_type != ShaderCodeType::Spirv || !sd.code)
                {
                    GEN_LOG_ERROR("[VK] pipeline '%s': stage '%s' has no SPIR-V code", desc.name.c_str(), sd.name.c_str());
                    delete pipeline;
                    return nullptr;
                }
                VkShaderModuleCreateInfo mci{};
                mci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                mci.codeSize = (size_t)sd.code_size;
                mci.pCode = reinterpret_cast<const uint32_t *>(sd.code);
                VkShaderModule module = VK_NULL_HANDLE;
                if (vkCreateShaderModule(device_, &mci, nullptr, &module) != VK_SUCCESS)
                {
                    GEN_LOG_ERROR("[VK] pipeline '%s': shader module creation failed", desc.name.c_str());
                    delete pipeline;
                    return nullptr;
                }
                pipeline->modules.push_back(module);

                VkPipelineShaderStageCreateInfo sci{};
                sci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                sci.stage = vkStage(sd.stage);
                sci.module = module;
                sci.pName = sd.entry.empty() ? "main" : sd.entry.c_str();
                stages.push_back(sci);
            }

            // ---- Descriptor set layouts + pipeline layout ----------------
            uint32_t max_set = 0;
            for (const auto &b : desc.resource_layout.bindings) max_set = std::max(max_set, b.set);
            uint32_t set_count = desc.resource_layout.bindings.empty() ? 0 : max_set + 1;
            std::vector<VkDescriptorSetLayout> set_layouts(set_count, VK_NULL_HANDLE);
            for (uint32_t s = 0; s < set_count; ++s)
            {
                std::vector<VkDescriptorSetLayoutBinding> bindings;
                for (const auto &b : desc.resource_layout.bindings)
                {
                    if (b.set != s) continue;
                    VkDescriptorSetLayoutBinding lb{};
                    lb.binding = b.binding;
                    lb.descriptorType = vkDescriptorType(b.type);
                    lb.descriptorCount = std::max(1u, b.count);
                    lb.stageFlags = vkStageMask(b.stages);
                    bindings.push_back(lb);
                }
                VkDescriptorSetLayoutCreateInfo ci{};
                ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                ci.bindingCount = (uint32_t)bindings.size();
                ci.pBindings = bindings.data();
                if (vkCreateDescriptorSetLayout(device_, &ci, nullptr, &set_layouts[s]) != VK_SUCCESS)
                {
                    GEN_LOG_ERROR("[VK] pipeline '%s': set layout failed", desc.name.c_str());
                    for (auto l : set_layouts) if (l) vkDestroyDescriptorSetLayout(device_, l, nullptr);
                    for (auto m : pipeline->modules) vkDestroyShaderModule(device_, m, nullptr);
                    delete pipeline;
                    return nullptr;
                }
                pipeline->owned_set_layouts.push_back(set_layouts[s]);
            }

            std::vector<VkPushConstantRange> push_ranges;
            if (desc.resource_layout.push_constant_size > 0)
            {
                VkPushConstantRange range{};
                range.stageFlags = vkStageMask(desc.resource_layout.push_constant_stages);
                range.offset = 0;
                range.size = desc.resource_layout.push_constant_size;
                push_ranges.push_back(range);
            }

            VkPipelineLayoutCreateInfo plci{};
            plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            plci.setLayoutCount = (uint32_t)set_layouts.size();
            plci.pSetLayouts = set_layouts.empty() ? nullptr : set_layouts.data();
            plci.pushConstantRangeCount = (uint32_t)push_ranges.size();
            plci.pPushConstantRanges = push_ranges.empty() ? nullptr : push_ranges.data();
            if (vkCreatePipelineLayout(device_, &plci, nullptr, &pipeline->owned_pipeline_layout) != VK_SUCCESS)
            {
                GEN_LOG_ERROR("[VK] pipeline '%s': pipeline layout failed", desc.name.c_str());
                for (auto l : pipeline->owned_set_layouts) vkDestroyDescriptorSetLayout(device_, l, nullptr);
                for (auto m : pipeline->modules) vkDestroyShaderModule(device_, m, nullptr);
                delete pipeline;
                return nullptr;
            }
            pipeline->layout = pipeline->owned_pipeline_layout;

            // ---- Render pass compatibility -------------------------------
            RenderPassDesc rpd;
            rpd.name = desc.name + "-compat";
            uint32_t att_count = 0;
            for (uint32_t i = 0; i < desc.color_format_count && i < 8; ++i)
            {
                if (desc.color_formats[i] == PixelFormat::Undefined) continue;
                rpd.attachments[att_count].format = desc.color_formats[i];
                rpd.attachments[att_count].is_depth_stencil = false;
                rpd.attachments[att_count].load_op = LoadOp::Clear;
                rpd.attachments[att_count].store_op = StoreOp::Store;
                rpd.attachments[att_count].initial_layout = TextureLayout::Undefined;
                rpd.attachments[att_count].final_layout = TextureLayout::ColorAttachmentOptimal;
                ++att_count;
            }
            if (desc.depth_format != PixelFormat::Undefined)
            {
                rpd.attachments[att_count].format = desc.depth_format;
                rpd.attachments[att_count].is_depth_stencil = true;
                rpd.attachments[att_count].load_op = LoadOp::Clear;
                rpd.attachments[att_count].store_op = StoreOp::DontCare;
                rpd.attachments[att_count].stencil_load_op = LoadOp::Clear;
                rpd.attachments[att_count].stencil_store_op = StoreOp::DontCare;
                rpd.attachments[att_count].initial_layout = TextureLayout::Undefined;
                rpd.attachments[att_count].final_layout = TextureLayout::DepthStencilAttachmentOptimal;
                ++att_count;
            }
            rpd.attachment_count = att_count;

            if (desc.is_compute)
            {
                VkComputePipelineCreateInfo ci{};
                ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
                ci.stage = stages.empty() ? VkPipelineShaderStageCreateInfo{} : stages[0];
                ci.layout = pipeline->layout;
                if (vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline->pipeline) != VK_SUCCESS)
                {
                    GEN_LOG_ERROR("[VK] compute pipeline '%s' failed", desc.name.c_str());
                    vkDestroyPipelineLayout(device_, pipeline->owned_pipeline_layout, nullptr);
                    for (auto l : pipeline->owned_set_layouts) vkDestroyDescriptorSetLayout(device_, l, nullptr);
                    for (auto m : pipeline->modules) vkDestroyShaderModule(device_, m, nullptr);
                    delete pipeline;
                    return nullptr;
                }
                pipelines_.push_back(pipeline);
                return pipeline;
            }

            VkRenderPassRHI *pass = static_cast<VkRenderPassRHI *>(createRenderPass(rpd));
            if (!pass)
            {
                vkDestroyPipelineLayout(device_, pipeline->owned_pipeline_layout, nullptr);
                for (auto l : pipeline->owned_set_layouts) vkDestroyDescriptorSetLayout(device_, l, nullptr);
                for (auto m : pipeline->modules) vkDestroyShaderModule(device_, m, nullptr);
                delete pipeline;
                return nullptr;
            }
            pipeline->render_pass = pass->render_pass;

            // ---- Vertex input -------------------------------------------
            VkVertexInputBindingDescription binding{};
            binding.binding = 0;
            binding.stride = desc.vertex_layout.stride;
            binding.inputRate = desc.vertex_layout.per_instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;

            std::vector<VkVertexInputAttributeDescription> attrs;
            for (const auto &a : desc.vertex_layout.attributes)
            {
                VkVertexInputAttributeDescription ad{};
                ad.binding = 0;
                ad.location = a.location;
                ad.format = vkVertexFormat(a.format);
                ad.offset = a.offset;
                attrs.push_back(ad);
            }

            VkPipelineVertexInputStateCreateInfo vertex_input{};
            vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            if (desc.vertex_layout.valid())
            {
                vertex_input.vertexBindingDescriptionCount = 1;
                vertex_input.pVertexBindingDescriptions = &binding;
                vertex_input.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
                vertex_input.pVertexAttributeDescriptions = attrs.data();
            }

            VkPipelineInputAssemblyStateCreateInfo input_assembly{};
            input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            input_assembly.topology = vkTopology(desc.topology);
            input_assembly.primitiveRestartEnable = VK_FALSE;

            VkPipelineViewportStateCreateInfo viewport_state{};
            viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport_state.viewportCount = 1;
            viewport_state.scissorCount = 1;

            VkPipelineRasterizationStateCreateInfo raster{};
            raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            raster.depthClampEnable = desc.rasterizer.depth_clamp ? VK_TRUE : VK_FALSE;
            raster.rasterizerDiscardEnable = VK_FALSE;
            raster.polygonMode = desc.rasterizer.polygon_mode == PolygonMode::Line ? VK_POLYGON_MODE_LINE
                                 : (desc.rasterizer.polygon_mode == PolygonMode::Point ? VK_POLYGON_MODE_POINT : VK_POLYGON_MODE_FILL);
            raster.cullMode = vkCull(desc.rasterizer.cull_mode);
            // The engine keeps GL-style projections (NDC y up, never flipped)
            // on every backend.  Vulkan evaluates winding in framebuffer space
            // whose y axis points down, so an engine counter-clockwise front
            // face is clockwise here.
            raster.frontFace = desc.rasterizer.front_face == FrontFace::CounterClockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
            raster.depthBiasEnable = VK_FALSE;
            raster.lineWidth = desc.rasterizer.line_width;

            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.sampleShadingEnable = VK_FALSE;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo depth_stencil{};
            depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth_stencil.depthTestEnable = desc.depth_stencil.depth_test ? VK_TRUE : VK_FALSE;
            depth_stencil.depthWriteEnable = desc.depth_stencil.depth_write ? VK_TRUE : VK_FALSE;
            depth_stencil.depthCompareOp = vkCompare(desc.depth_stencil.depth_compare);
            depth_stencil.depthBoundsTestEnable = VK_FALSE;
            depth_stencil.stencilTestEnable = desc.depth_stencil.stencil_test ? VK_TRUE : VK_FALSE;
            depth_stencil.front.compareOp = vkCompare(desc.depth_stencil.stencil_compare);
            depth_stencil.front.compareMask = desc.depth_stencil.stencil_read_mask;
            depth_stencil.front.writeMask = desc.depth_stencil.stencil_write_mask;
            depth_stencil.front.reference = desc.depth_stencil.stencil_reference;
            depth_stencil.back = depth_stencil.front;

            std::vector<VkPipelineColorBlendAttachmentState> blend_attachments;
            for (uint32_t i = 0; i < desc.color_format_count && i < 8; ++i)
            {
                if (desc.color_formats[i] == PixelFormat::Undefined) continue;
                const BlendAttachmentState &src = desc.blend.attachments[std::min(i, desc.blend.attachment_count - 1)];
                VkPipelineColorBlendAttachmentState att{};
                att.blendEnable = src.blend_enable ? VK_TRUE : VK_FALSE;
                att.srcColorBlendFactor = vkBlendFactor(src.src_color);
                att.dstColorBlendFactor = vkBlendFactor(src.dst_color);
                att.colorBlendOp = vkBlendOp(src.color_op);
                att.srcAlphaBlendFactor = vkBlendFactor(src.src_alpha);
                att.dstAlphaBlendFactor = vkBlendFactor(src.dst_alpha);
                att.alphaBlendOp = vkBlendOp(src.alpha_op);
                att.colorWriteMask = vkColorWrite((ColorComponentFlags)src.write_mask);
                blend_attachments.push_back(att);
            }

            VkPipelineColorBlendStateCreateInfo color_blend{};
            color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            color_blend.logicOpEnable = VK_FALSE;
            color_blend.attachmentCount = (uint32_t)blend_attachments.size();
            color_blend.pAttachments = blend_attachments.data();
            std::memcpy(color_blend.blendConstants, desc.blend.blend_constant, sizeof(float) * 4);

            VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{};
            dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic.dynamicStateCount = 2;
            dynamic.pDynamicStates = dynamic_states;

            VkGraphicsPipelineCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            ci.stageCount = (uint32_t)stages.size();
            ci.pStages = stages.data();
            ci.pVertexInputState = &vertex_input;
            ci.pInputAssemblyState = &input_assembly;
            ci.pViewportState = &viewport_state;
            ci.pRasterizationState = &raster;
            ci.pMultisampleState = &multisample;
            ci.pDepthStencilState = &depth_stencil;
            ci.pColorBlendState = &color_blend;
            ci.pDynamicState = &dynamic;
            ci.layout = pipeline->layout;
            ci.renderPass = pipeline->render_pass;
            ci.subpass = 0;

            if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline->pipeline) != VK_SUCCESS)
            {
                GEN_LOG_ERROR("[VK] graphics pipeline '%s' creation failed", desc.name.c_str());
                vkDestroyPipelineLayout(device_, pipeline->owned_pipeline_layout, nullptr);
                for (auto l : pipeline->owned_set_layouts) vkDestroyDescriptorSetLayout(device_, l, nullptr);
                for (auto m : pipeline->modules) vkDestroyShaderModule(device_, m, nullptr);
                delete pipeline;
                return nullptr;
            }
            pipelines_.push_back(pipeline);
            return pipeline;
        }

        RHIFramebuffer *VkDeviceRHI::createFramebuffer(const FramebufferDesc &desc)
        {
            VkRenderPassRHI *pass = static_cast<VkRenderPassRHI *>(createRenderPass(desc.render_pass));
            if (!pass) return nullptr;

            std::vector<VkImageView> views;
            std::vector<VkImageView> owned_views;
            std::vector<VkTextureRHI *> attachment_textures;
            for (const auto &a : desc.attachments)
            {
                VkTextureRHI *tex = static_cast<VkTextureRHI *>(a.texture);
                if (!tex) continue;
                attachment_textures.push_back(tex);
                // Framebuffer attachments must be single-mip, single-layer views.
                if (tex->mipLevels() == 1 && tex->layerCount() == 1)
                {
                    views.push_back(tex->view);
                    continue;
                }
                VkImageViewCreateInfo vci{};
                vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                vci.image = tex->image;
                vci.format = vkPixelFormat(tex->format());
                vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
                vci.subresourceRange.aspectMask = vkAspectMask(tex->format());
                vci.subresourceRange.baseMipLevel = a.mip_level;
                vci.subresourceRange.levelCount = 1;
                vci.subresourceRange.baseArrayLayer = a.array_layer;
                vci.subresourceRange.layerCount = 1;
                VkImageView view = VK_NULL_HANDLE;
                if (vkCreateImageView(device_, &vci, nullptr, &view) != VK_SUCCESS)
                {
                    GEN_LOG_ERROR("[VK] framebuffer '%s': image view creation failed", desc.name.c_str());
                    for (VkImageView v : owned_views) vkDestroyImageView(device_, v, nullptr);
                    return nullptr;
                }
                views.push_back(view);
                owned_views.push_back(view);
            }

            VkFramebufferCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ci.renderPass = pass->render_pass;
            ci.attachmentCount = (uint32_t)views.size();
            ci.pAttachments = views.data();
            ci.width = std::max(1u, desc.width);
            ci.height = std::max(1u, desc.height);
            ci.layers = 1;

            VkFramebufferRHI *fb = new VkFramebufferRHI();
            fb->pass = pass;
            fb->w = desc.width;
            fb->h = desc.height;
            fb->fb_name = desc.name;
            fb->attachment_textures = attachment_textures;
            fb->owned_views = owned_views;
            if (vkCreateFramebuffer(device_, &ci, nullptr, &fb->framebuffer) != VK_SUCCESS)
            {
                GEN_LOG_ERROR("[VK] framebuffer '%s' creation failed", desc.name.c_str());
                for (VkImageView v : owned_views) vkDestroyImageView(device_, v, nullptr);
                delete fb;
                return nullptr;
            }
            framebuffers_.push_back(fb);
            return fb;
        }

        RHIDescriptorSet *VkDeviceRHI::createDescriptorSet(RHIResourceLayout *layout, uint32_t set_index)
        {
            VkResourceLayoutRHI *l = static_cast<VkResourceLayoutRHI *>(layout);
            if (!l || l->set_layouts.empty()) return nullptr;
            if (set_index >= l->set_layouts.size())
            {
                GEN_LOG_ERROR("[VK] createDescriptorSet: set index %u out of range (%zu)",
                              set_index, l->set_layouts.size());
                return nullptr;
            }
            VkDescriptorSetAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ai.descriptorPool = descriptor_pool_;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &l->set_layouts[set_index];

            VkDescriptorSetRHI *set = new VkDescriptorSetRHI();
            set->layout_obj = l;
            set->device = this;
            if (vkAllocateDescriptorSets(device_, &ai, &set->set) != VK_SUCCESS)
            {
                GEN_LOG_ERROR("[VK] descriptor set allocation failed");
                delete set;
                return nullptr;
            }
            descriptor_sets_.push_back(set);
            return set;
        }

        RHICommandBuffer *VkDeviceRHI::createCommandBuffer(CommandBufferUsage usage)
        {
            VkCommandBufferAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            ai.commandPool = (usage == CommandBufferUsage::OneTimeSubmit) ? transient_pool_ : command_pool_;
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;

            VkCommandBufferRHI *cb = new VkCommandBufferRHI();
            cb->device = this;
            cb->pool = ai.commandPool;
            if (vkAllocateCommandBuffers(device_, &ai, &cb->cmd) != VK_SUCCESS)
            {
                GEN_LOG_ERROR("[VK] command buffer allocation failed");
                delete cb;
                return nullptr;
            }
            command_buffers_.push_back(cb);
            return cb;
        }

        //==================================================================
        // Destruction
        //==================================================================
        // Returns false when the object was not registered, which lets the
        // destroy* functions ignore accidental double destroys.
        template <typename Container, typename T>
        static bool vkErase(Container &c, T *p)
        {
            auto it = std::find(c.begin(), c.end(), p);
            if (it == c.end()) return false;
            c.erase(it);
            return true;
        }

        void VkDeviceRHI::destroyBuffer(RHIBuffer *p)
        {
            VkBufferRHI *b = static_cast<VkBufferRHI *>(p);
            if (!b || !vkErase(buffers_, b)) return;
            vmaDestroyBuffer(vmaAlloc(vma_allocator_), b->buffer, vmaAllocOf(b->allocation));
            delete b;
        }

        void VkDeviceRHI::destroyTexture(RHITexture *p)
        {
            VkTextureRHI *t = static_cast<VkTextureRHI *>(p);
            if (!t || !vkErase(textures_, t)) return;
            if (t->view) vkDestroyImageView(device_, t->view, nullptr);
            vmaDestroyImage(vmaAlloc(vma_allocator_), t->image, vmaAllocOf(t->allocation));
            delete t;
        }

        void VkDeviceRHI::destroySampler(RHISampler *p)
        {
            VkSamplerRHI *s = static_cast<VkSamplerRHI *>(p);
            if (!s || !vkErase(samplers_, s)) return;
            vkDestroySampler(device_, s->sampler, nullptr);
            delete s;
        }

        void VkDeviceRHI::destroyShader(RHIShader *p)
        {
            VkShaderRHI *s = static_cast<VkShaderRHI *>(p);
            if (!s || !vkErase(shaders_, s)) return;
            vkDestroyShaderModule(device_, s->module, nullptr);
            delete s;
        }

        void VkDeviceRHI::destroyResourceLayout(RHIResourceLayout *p)
        {
            VkResourceLayoutRHI *l = static_cast<VkResourceLayoutRHI *>(p);
            if (!l || !vkErase(layouts_, l)) return;
            if (l->pipeline_layout) vkDestroyPipelineLayout(device_, l->pipeline_layout, nullptr);
            for (auto sl : l->set_layouts) if (sl) vkDestroyDescriptorSetLayout(device_, sl, nullptr);
            delete l;
        }

        void VkDeviceRHI::destroyPipeline(RHIPipeline *p)
        {
            VkPipelineRHI *pl = static_cast<VkPipelineRHI *>(p);
            if (!pl || !vkErase(pipelines_, pl)) return;
            if (pl->pipeline) vkDestroyPipeline(device_, pl->pipeline, nullptr);
            if (pl->owned_pipeline_layout) vkDestroyPipelineLayout(device_, pl->owned_pipeline_layout, nullptr);
            for (auto l : pl->owned_set_layouts) if (l) vkDestroyDescriptorSetLayout(device_, l, nullptr);
            for (auto m : pl->modules) if (m) vkDestroyShaderModule(device_, m, nullptr);
            delete pl;
        }

        void VkDeviceRHI::destroyRenderPass(RHIRenderPass *p)
        {
            VkRenderPassRHI *rp = static_cast<VkRenderPassRHI *>(p);
            if (!rp) return;
            auto it = render_passes_.find(rp->hash);
            if (it != render_passes_.end()) render_passes_.erase(it);
            if (rp->render_pass) vkDestroyRenderPass(device_, rp->render_pass, nullptr);
            delete rp;
        }

        void VkDeviceRHI::destroyFramebuffer(RHIFramebuffer *p)
        {
            VkFramebufferRHI *fb = static_cast<VkFramebufferRHI *>(p);
            if (!fb || !vkErase(framebuffers_, fb)) return;
            if (fb->framebuffer) vkDestroyFramebuffer(device_, fb->framebuffer, nullptr);
            for (VkImageView v : fb->owned_views) vkDestroyImageView(device_, v, nullptr);
            delete fb;
        }

        void VkDeviceRHI::destroyDescriptorSet(RHIDescriptorSet *p)
        {
            VkDescriptorSetRHI *ds = static_cast<VkDescriptorSetRHI *>(p);
            if (!ds || !vkErase(descriptor_sets_, ds)) return;
            if (ds->set && descriptor_pool_) vkFreeDescriptorSets(device_, descriptor_pool_, 1, &ds->set);
            delete ds;
        }

        void VkDeviceRHI::destroyCommandBuffer(RHICommandBuffer *p)
        {
            VkCommandBufferRHI *cb = static_cast<VkCommandBufferRHI *>(p);
            if (!cb || !vkErase(command_buffers_, cb)) return;
            if (cb->cmd && cb->pool) vkFreeCommandBuffers(device_, cb->pool, 1, &cb->cmd);
            delete cb;
        }

        //==================================================================
        // Transfers
        //==================================================================
        VkCommandBuffer VkDeviceRHI::beginSingleTimeCommands()
        {
            VkCommandBufferAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandPool = transient_pool_;
            ai.commandBufferCount = 1;
            VkCommandBuffer cmd;
            vkAllocateCommandBuffers(device_, &ai, &cmd);
            VkCommandBufferBeginInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &bi);
            return cmd;
        }

        void VkDeviceRHI::endSingleTimeCommands(VkCommandBuffer cmd)
        {
            vkEndCommandBuffer(cmd);
            VkSubmitInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &cmd;
            vkQueueSubmit(graphics_queue_, 1, &si, VK_NULL_HANDLE);
            vkQueueWaitIdle(graphics_queue_);
            vkFreeCommandBuffers(device_, transient_pool_, 1, &cmd);
        }

        void VkDeviceRHI::transitionImage(VkCommandBuffer cmd, VkTextureRHI *tex, TextureLayout old_layout, TextureLayout new_layout,
                                          uint32_t base_mip, uint32_t mip_count, uint32_t base_layer, uint32_t layer_count)
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = vkTextureLayout(old_layout);
            barrier.newLayout = vkTextureLayout(new_layout);
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = tex->image;
            barrier.subresourceRange.aspectMask = vkAspectMask(tex->format());
            barrier.subresourceRange.baseMipLevel = base_mip;
            barrier.subresourceRange.levelCount = (mip_count == ~0u) ? tex->mipLevels() - base_mip : mip_count;
            barrier.subresourceRange.baseArrayLayer = base_layer;
            barrier.subresourceRange.layerCount = (layer_count == ~0u) ? tex->layerCount() - base_layer : layer_count;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = 0;

            VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

            if (old_layout == TextureLayout::Undefined && new_layout == TextureLayout::TransferDstOptimal)
            {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            }
            else if (old_layout == TextureLayout::TransferDstOptimal && new_layout == TextureLayout::ShaderReadOnlyOptimal)
            {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }
            else if (old_layout == TextureLayout::Undefined && new_layout == TextureLayout::ShaderReadOnlyOptimal)
            {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            }
            else if (new_layout == TextureLayout::TransferSrcOptimal)
            {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            }
            else if (new_layout == TextureLayout::ColorAttachmentOptimal)
            {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
                src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            }
            else if (new_layout == TextureLayout::TransferDstOptimal)
            {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            }
            else if (new_layout == TextureLayout::DepthStencilAttachmentOptimal)
            {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            }
            else if (new_layout == TextureLayout::General)
            {
                barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            }

            vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        uint32_t VkDeviceRHI::findMemoryType(uint32_t type_filter, VkMemoryPropertyFlags properties) const
        {
            VkPhysicalDeviceMemoryProperties props;
            vkGetPhysicalDeviceMemoryProperties(physical_device_, &props);
            for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
            {
                if ((type_filter & (1 << i)) && (props.memoryTypes[i].propertyFlags & properties) == properties)
                    return i;
            }
            return UINT32_MAX;
        }

        void VkDeviceRHI::writeBuffer(RHIBuffer *buffer, const void *data, uint64_t size, uint64_t offset)
        {
            VkBufferRHI *b = static_cast<VkBufferRHI *>(buffer);
            if (!b || !data || size == 0) return;
            if (b->mapped && !std::getenv("GENESIS_VK_STAGING_WRITES"))
            {
                std::memcpy(reinterpret_cast<uint8_t *>(b->mapped) + offset, data, (size_t)size);
                vmaFlushAllocation(vmaAlloc(vma_allocator_), vmaAllocOf(b->allocation), (VkDeviceSize)offset, (VkDeviceSize)size);
                return;
            }

            BufferDesc staging_desc;
            staging_desc.size = size;
            staging_desc.usage = (uint32_t)BufferUsageFlagBits::TransferSrc;
            staging_desc.memory_usage = MemoryUsage::CpuToGpu;
            staging_desc.name = "staging";
            staging_desc.initial_data = data;
            staging_desc.initial_data_size = size;
            VkBufferRHI *staging = static_cast<VkBufferRHI *>(createBuffer(staging_desc));
            if (!staging) return;

            VkCommandBuffer cmd = beginSingleTimeCommands();
            VkBufferCopy region{};
            region.srcOffset = 0;
            region.dstOffset = offset;
            region.size = (VkDeviceSize)size;
            vkCmdCopyBuffer(cmd, staging->buffer, b->buffer, 1, &region);
            endSingleTimeCommands(cmd);
            destroyBuffer(staging);
        }

        void VkDeviceRHI::writeTexture(RHITexture *texture, const void *data, uint64_t size,
                                       uint32_t width, uint32_t height, uint32_t mip, uint32_t base_layer, uint32_t layer_count)
        {
            VkTextureRHI *t = static_cast<VkTextureRHI *>(texture);
            if (!t || !data || size == 0) return;

            BufferDesc staging_desc;
            staging_desc.size = size;
            staging_desc.usage = (uint32_t)BufferUsageFlagBits::TransferSrc;
            staging_desc.memory_usage = MemoryUsage::CpuToGpu;
            staging_desc.name = "texture-staging";
            staging_desc.initial_data = data;
            staging_desc.initial_data_size = size;
            VkBufferRHI *staging = static_cast<VkBufferRHI *>(createBuffer(staging_desc));
            if (!staging) return;

            VkCommandBuffer cmd = beginSingleTimeCommands();
            TextureLayout old = t->currentLayout();
            transitionImage(cmd, t, old, TextureLayout::TransferDstOptimal, mip, 1, base_layer, layer_count);

            BufferImageCopy region;
            region.buffer_offset = 0;
            region.buffer_row_length = 0;
            region.buffer_image_height = 0;
            region.mip_level = mip;
            region.base_layer = base_layer;
            region.layer_count = layer_count;
            region.width = width;
            region.height = height;
            region.depth = 1;

            VkBufferImageCopy vk_region{};
            vk_region.bufferOffset = 0;
            vk_region.bufferRowLength = 0;
            vk_region.bufferImageHeight = 0;
            vk_region.imageSubresource.aspectMask = vkAspectMask(t->format());
            vk_region.imageSubresource.mipLevel = mip;
            vk_region.imageSubresource.baseArrayLayer = base_layer;
            vk_region.imageSubresource.layerCount = layer_count;
            vk_region.imageOffset = {0, 0, 0};
            vk_region.imageExtent = {width, height, 1};
            vkCmdCopyBufferToImage(cmd, staging->buffer, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &vk_region);

            TextureLayout target_layout = (t->desc_.usage & (uint32_t)TextureUsageFlagBits::RenderTarget)
                                              ? TextureLayout::ColorAttachmentOptimal
                                              : TextureLayout::ShaderReadOnlyOptimal;
            transitionImage(cmd, t, TextureLayout::TransferDstOptimal, target_layout, mip, 1, base_layer, layer_count);
            endSingleTimeCommands(cmd);
            t->setCurrentLayout(target_layout);
            destroyBuffer(staging);
        }

        void VkDeviceRHI::submitImmediate(void (*record_fn)(RHICommandBuffer *, void *), void *user_data)
        {
            VkCommandBuffer cmd = beginSingleTimeCommands();
            VkCommandBufferRHI wrapper;
            wrapper.cmd = cmd;
            wrapper.pool = transient_pool_;
            wrapper.device = this;
            record_fn(&wrapper, user_data);
            endSingleTimeCommands(cmd);
        }

        void VkDeviceRHI::submitAndWait(RHICommandBuffer *cmd)
        {
            VkCommandBufferRHI *vk_cmd = static_cast<VkCommandBufferRHI *>(cmd);
            if (!vk_cmd || !vk_cmd->cmd) return;

            VkFenceCreateInfo fci{};
            fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            VkFence fence = VK_NULL_HANDLE;
            vkCreateFence(device_, &fci, nullptr, &fence);

            VkSubmitInfo si{};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &vk_cmd->cmd;
            if (vkQueueSubmit(graphics_queue_, 1, &si, fence) != VK_SUCCESS)
            {
                GEN_LOG_ERROR("[VK] submitAndWait: queue submit failed");
                if (fence) vkDestroyFence(device_, fence, nullptr);
                return;
            }
            vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
            vkDestroyFence(device_, fence, nullptr);
        }

        //==================================================================
        // Descriptor set writes
        //==================================================================
        void VkDescriptorSetRHI::write(const WriteDescriptorSet *writes, uint32_t count)
        {
            if (!device || !set) return;

            // Pass 1: collect stable info arrays (vector reallocation must not
            // invalidate the pointers stored in VkWriteDescriptorSet).
            std::vector<VkDescriptorBufferInfo> buffer_infos;
            std::vector<VkDescriptorImageInfo> image_infos;
            struct PendingWrite
            {
                uint32_t binding;
                uint32_t array_element;
                ResourceType type;
                bool is_buffer;
                uint32_t info_index;
            };
            std::vector<PendingWrite> pending;

            for (uint32_t i = 0; i < count; ++i)
            {
                const WriteDescriptorSet &w = writes[i];
                if (resourceTypeIsBuffer(w.type))
                {
                    if (!w.buffer_info) continue;
                    for (uint32_t e = 0; e < w.count; ++e)
                    {
                        VkBufferRHI *b = static_cast<VkBufferRHI *>(w.buffer_info[e].buffer);
                        if (!b) continue;
                        VkDescriptorBufferInfo bi{};
                        bi.buffer = b->buffer;
                        bi.offset = w.buffer_info[e].offset;
                        // NOTE: VK_WHOLE_SIZE is illegal for the *dynamic*
                        // descriptor types, so keep the caller supplied range
                        // (RenderSystem always passes the exact block size).
                        bi.range = w.buffer_info[e].range ? (VkDeviceSize)w.buffer_info[e].range : VK_WHOLE_SIZE;
                        buffer_infos.push_back(bi);
                        pending.push_back(PendingWrite{w.binding, w.array_element + e, w.type, true, (uint32_t)buffer_infos.size() - 1});
                    }
                }
                else
                {
                    if (!w.image_info) continue;
                    for (uint32_t e = 0; e < w.count; ++e)
                    {
                        VkTextureRHI *t = static_cast<VkTextureRHI *>(w.image_info[e].texture);
                        VkSamplerRHI *s = static_cast<VkSamplerRHI *>(w.image_info[e].sampler);
                        if (!t) continue;
                        VkDescriptorImageInfo ii{};
                        ii.imageView = t->view;
                        ii.imageLayout = vkTextureLayout(w.image_info[e].layout);
                        ii.sampler = s ? s->sampler : VK_NULL_HANDLE;
                        image_infos.push_back(ii);
                        pending.push_back(PendingWrite{w.binding, w.array_element + e, w.type, false, (uint32_t)image_infos.size() - 1});
                    }
                }
            }

            if (pending.empty()) return;

            // Pass 2: build the VkWriteDescriptorSet array.
            std::vector<VkWriteDescriptorSet> vk_writes(pending.size());
            for (size_t i = 0; i < pending.size(); ++i)
            {
                const PendingWrite &p = pending[i];
                VkWriteDescriptorSet &vw = vk_writes[i];
                vw.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                vw.pNext = nullptr;
                vw.dstSet = set;
                vw.dstBinding = p.binding;
                vw.dstArrayElement = p.array_element;
                vw.descriptorCount = 1;
                vw.descriptorType = vkDescriptorType(p.type);
                vw.pBufferInfo = p.is_buffer ? &buffer_infos[p.info_index] : nullptr;
                vw.pImageInfo = p.is_buffer ? nullptr : &image_infos[p.info_index];
                vw.pTexelBufferView = nullptr;
            }
            vkUpdateDescriptorSets(device->device(), (uint32_t)vk_writes.size(), vk_writes.data(), 0, nullptr);
        }

        //==================================================================
        // Factory lives in rhi_factory.cpp (it needs both backends)
        //==================================================================

    } // namespace RHI
} // namespace Genesis
