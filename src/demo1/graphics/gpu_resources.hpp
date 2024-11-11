/*
 * @Author: Yan Dongbao
 * @Date: 2024-11-08 21:00:23
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-11-12 15:02:57
 * @version:
 */
#ifndef __VULKAN_RENDERER_GPU_RESOURCES_H__
#define __VULKAN_RENDERER_GPU_RESOURCES_H__

#include "foundation_layer/platform.hpp"
#include "graphics/gpu_enum.hpp"
#include <vulkan/vulkan.h>
#include "external/vk_mem_alloc.h"

namespace Genesis
{
    // handles
    typedef uint32_t ResourceHandle;
    static const uint32_t invalid_resource_handle = 0xffffffff;

    struct BufferHandle
    {
        ResourceHandle m_handle;
    };

    struct TextureHandle
    {
        ResourceHandle m_handle;
    };

    struct ShaderStateHandle
    {
        ResourceHandle m_handle;
    };

    struct SamplerHandle
    {
        ResourceHandle m_handle;
    };

    static BufferHandle invalid_buffer = {invalid_resource_handle};
    static TextureHandle invalid_texture = {invalid_resource_handle};
    static ShaderStateHandle invalid_shader = {invalid_resource_handle};
    static SamplerHandle invalid_sampler = {invalid_resource_handle};

    static const uint8_t s_max_shader_stages = 4;
    static const uint8_t s_max_outputs = 8;

    //-----------------------------------------------------------
    //---------------------[Resources Define]-------------------|
    //-----------------------------------------------------------
    /*Buffer*/
    struct Buffer
    {
        VkBuffer m_vk_buffer;
        VkDeviceMemory m_vk_device_memory;
        VkDeviceSize m_vk_device_size;

        // https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
        VmaAllocation m_vma_allocation;

        VkBufferUsageFlags m_type_usage = 0;
        uint32_t m_size = 0;
        uint32_t m_global_offset = 0;

        BufferHandle m_handle;
        BufferHandle m_parent_handle;

        const char *m_name = nullptr;
    };

    /*Sampler*/
    struct Sampler
    {
        VkSampler m_vk_sampler;

        VkFilter m_min_filter = VK_FILTER_NEAREST;
        VkFilter m_mag_filter = VK_FILTER_NEAREST;
        VkSamplerMipmapMode m_mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

        VkSamplerAddressMode m_address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode m_address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode m_address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;

        const char *m_name = nullptr;
    };

    /*Texture*/
    struct Texture
    {
        VkImage m_vk_image;
        VkImageView m_vk_image_view;
        VkFormat m_vk_format;
        VkImageLayout m_vk_image_layout;

        VmaAllocation m_vma_allocation;

        uint16_t m_width = 1;
        uint16_t m_height = 1;
        uint16_t m_depth = 1;
        uint8_t m_mipmaps = 1;
        uint8_t m_flags = 0;

        TextureHandle m_handle;
        TextureType::Enum m_type = TextureType::Texture2D;
        Sampler *m_sampler = nullptr;

        const char *m_name = nullptr;
    };

    /*ShaderState*/
    struct ShaderStage
    {
        VkShaderStageFlags m_type = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
        uint32_t m_size = 0;

        const char *m_name = nullptr;
    };

    //-----------------------------------------------------------
    //-----------------[Resources Create Info]  ----------------|
    //-----------------------------------------------------------
    class BufferCreateInfo
    {
    public:
        BufferCreateInfo &set(VkBufferUsageFlags flags, ResourceUsageType::Enum usage, uint32_t size);
        BufferCreateInfo &setName(const char *name);
        BufferCreateInfo &setData(const void *data);

    private:
        VkBufferUsageFlags type_flags_ = 0;
        uint32_t size_ = 0;

        void *data_ = nullptr;
        const char *name_ = nullptr;
    };

    class TextureCreateInfo
    {
    public:
        TextureCreateInfo &setFormatType(VkFormat format, TextureType::Enum type);
        TextureCreateInfo &setName(const char *name);
        TextureCreateInfo &setFlags(uint8_t flags, uint8_t mipmaps);
        TextureCreateInfo &setSize(uint16_t width, uint16_t height, uint16_t depth);
        TextureCreateInfo &setData(const void *data);

    private:
        TextureType::Enum type_ = TextureType::Texture2D;
        VkFormat format_ = VK_FORMAT_UNDEFINED;
        uint16_t width_ = 1;
        uint16_t height_ = 1;
        uint16_t depth_ = 1;
        uint8_t flags_ = 0;
        uint8_t mipmaps_ = 1;

        void *data_ = nullptr;
        const char *name_ = nullptr;
    };

    class SamplerCreateInfo
    {
    public:
        SamplerCreateInfo &setMipmap(VkFilter min_filter, VkFilter mag_filter, VkSamplerMipmapMode mipmap_mode);
        SamplerCreateInfo &setAddressMode(VkSamplerAddressMode u, VkSamplerAddressMode v, VkSamplerAddressMode w);
        SamplerCreateInfo &setName(const char *name);

    private:
        VkFilter min_filter_ = VK_FILTER_NEAREST;
        VkFilter mag_filter_ = VK_FILTER_NEAREST;
        VkSamplerMipmapMode mipmap_mode_ = VK_SAMPLER_MIPMAP_MODE_NEAREST;

        VkSamplerAddressMode address_mode_u_ = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode address_mode_v_ = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode address_mode_w_ = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        const char *name_ = nullptr;
    };

    class ShaderStateCreateInfo
    {
    public:
        ShaderStateCreateInfo &reset();
        ShaderStateCreateInfo &setName(const char *name);
        ShaderStateCreateInfo &setStage(const char *data, uint32_t size, VkShaderStageFlags type);

    private:
        uint32_t stage_count_ = 0;

        ShaderStage stages_[s_max_shader_stages];
        const char *name_ = nullptr;
    };
}

#endif /* __VULKAN_RENDERER_GPU_RESOURCES_H__ */