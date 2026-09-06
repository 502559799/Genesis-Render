/*
 * Genesis Render - Vulkan enum conversion helpers shared between TUs
 */
#ifndef __GENESIS_VK_CONVERT_HPP__
#define __GENESIS_VK_CONVERT_HPP__

#include "rhi/vulkan/vk_backend.hpp"
#include "external/vk_mem_alloc.h"

namespace Genesis
{
    namespace RHI
    {
        inline VkShaderStageFlagBits vkStage(ShaderStageFlagBits s)
        {
            switch (s)
            {
            case ShaderStageFlagBits::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
            case ShaderStageFlagBits::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
            case ShaderStageFlagBits::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
            case ShaderStageFlagBits::Geometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
            default: return VK_SHADER_STAGE_ALL;
            }
        }

        inline VkShaderStageFlags vkStageMask(ShaderStageFlags flags)
        {
            VkShaderStageFlags out = 0;
            if (flags & (uint32_t)ShaderStageFlagBits::Vertex) out |= VK_SHADER_STAGE_VERTEX_BIT;
            if (flags & (uint32_t)ShaderStageFlagBits::Fragment) out |= VK_SHADER_STAGE_FRAGMENT_BIT;
            if (flags & (uint32_t)ShaderStageFlagBits::Compute) out |= VK_SHADER_STAGE_COMPUTE_BIT;
            if (flags & (uint32_t)ShaderStageFlagBits::Geometry) out |= VK_SHADER_STAGE_GEOMETRY_BIT;
            return out ? out : VK_SHADER_STAGE_ALL;
        }

        inline VkDescriptorType vkDescriptorType(ResourceType type)
        {
            switch (type)
            {
            case ResourceType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case ResourceType::UniformBufferDynamic: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            case ResourceType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            case ResourceType::StorageBufferDynamic: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
            case ResourceType::SampledImage: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            case ResourceType::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
            case ResourceType::CombinedImageSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            case ResourceType::StorageImage: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            }
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        }

        // VMA handles are stored as void* inside the RHI objects so that the
        // public headers do not have to include the (very large) VMA header.
        inline VmaAllocator vmaAlloc(void *allocator) { return reinterpret_cast<VmaAllocator>(allocator); }
        inline VmaAllocation vmaAllocOf(void *allocation) { return reinterpret_cast<VmaAllocation>(allocation); }

    } // namespace RHI
} // namespace Genesis

#endif /* __GENESIS_VK_CONVERT_HPP__ */
