/*
 * Camera Texture Upload Implementation
 * Author: Genesis Render Team
 */

#include "camera_texture.h"
#include "gpu_device.hpp"
#include "foundation_layer/log.hpp"
#include "external/imgui/imgui.h"
#include "external/imgui/imgui_impl_vulkan.h"

namespace Genesis
{

static uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    GenesisPrint("Failed to find suitable memory type!\n");
    return 0;
}

CameraTexture::CameraTexture(GpuDevice* device)
    : device_(device)
{
}

CameraTexture::~CameraTexture()
{
    shutdown();
}

bool CameraTexture::init(uint32_t width, uint32_t height)
{
    width_ = width;
    height_ = height;
    staging_size_ = width * height * 4; // BGRA 32-bit

    vk_device_ = device_->getDevice();
    vk_physical_ = device_->getPhysicalDevice();

    createImage();
    createImageView();
    createSampler();
    createStagingBuffer();

    // Create descriptor set for ImGui
    descriptor_set_ = ImGui_ImplVulkan_AddTexture(sampler_, image_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    GenesisPrint("Camera texture created: %dx%d\n", width, height);
    return true;
}

void CameraTexture::shutdown()
{
    if (staging_buffer_) vkDestroyBuffer(vk_device_, staging_buffer_, nullptr);
    if (staging_memory_) vkFreeMemory(vk_device_, staging_memory_, nullptr);
    if (image_) vkDestroyImage(vk_device_, image_, nullptr);
    if (image_view_) vkDestroyImageView(vk_device_, image_view_, nullptr);
    if (memory_) vkFreeMemory(vk_device_, memory_, nullptr);
    if (sampler_) vkDestroySampler(vk_device_, sampler_, nullptr);
    // Note: ImGui descriptor set is cleaned up by ImGui_ImplVulkan_Shutdown
}

void CameraTexture::createImage()
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width_;
    imageInfo.extent.height = height_;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(vk_device_, &imageInfo, nullptr, &image_) != VK_SUCCESS)
    {
        GenesisPrint("Failed to create image!\n");
        return;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(vk_device_, image_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(vk_physical_, memRequirements.memoryTypeBits, 
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(vk_device_, &allocInfo, nullptr, &memory_) != VK_SUCCESS)
    {
        GenesisPrint("Failed to allocate image memory!\n");
        return;
    }

    vkBindImageMemory(vk_device_, image_, memory_, 0);
}

void CameraTexture::createImageView()
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(vk_device_, &viewInfo, nullptr, &image_view_) != VK_SUCCESS)
    {
        GenesisPrint("Failed to create image view!\n");
    }
}

void CameraTexture::createSampler()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(vk_device_, &samplerInfo, nullptr, &sampler_) != VK_SUCCESS)
    {
        GenesisPrint("Failed to create sampler!\n");
    }
}

void CameraTexture::createStagingBuffer()
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = staging_size_;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vk_device_, &bufferInfo, nullptr, &staging_buffer_) != VK_SUCCESS)
    {
        GenesisPrint("Failed to create staging buffer!\n");
        return;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(vk_device_, staging_buffer_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(vk_physical_, memRequirements.memoryTypeBits, 
                                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | 
                                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(vk_device_, &allocInfo, nullptr, &staging_memory_) != VK_SUCCESS)
    {
        GenesisPrint("Failed to allocate staging buffer memory!\n");
        return;
    }

    vkBindBufferMemory(vk_device_, staging_buffer_, staging_memory_, 0);
}

void CameraTexture::transitionLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        GenesisPrint("Unsupported layout transition!\n");
        return;
    }

    vkCmdPipelineBarrier(
        cmd,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

void CameraTexture::copyBufferToImage(VkCommandBuffer cmd, VkBuffer buffer, VkImage image, uint32_t w, uint32_t h)
{
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {w, h, 1};

    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void CameraTexture::uploadFrame(uint8_t* data, uint32_t stride)
{
    if (!data) return;

    // Map staging buffer
    void* mapped;
    if (vkMapMemory(vk_device_, staging_memory_, 0, staging_size_, 0, &mapped) != VK_SUCCESS)
    {
        GenesisPrint("Failed to map staging memory!\n");
        return;
    }

    // Copy pixel data - handle stride if needed
    if (stride == 0 || stride == width_ * 4)
    {
        memcpy(mapped, data, staging_size_);
    }
    else
    {
        uint8_t* dst = (uint8_t*)mapped;
        for (uint32_t y = 0; y < height_; y++)
        {
            memcpy(dst + y * width_ * 4, data + y * stride, width_ * 4);
        }
    }

    vkUnmapMemory(vk_device_, staging_memory_);
    dirty_ = true;
}

void CameraTexture::updateGpu()
{
    if (!dirty_) return;

    VkCommandBuffer cmd = device_->getCurrentCommandBuffer();

    // Transition to transfer dst layout
    if (first_upload_) {
        transitionLayout(cmd, image_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        first_upload_ = false;
    } else {
        transitionLayout(cmd, image_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    }

    // Copy from staging buffer to image
    copyBufferToImage(cmd, staging_buffer_, image_, width_, height_);

    // Transition to shader read only layout
    transitionLayout(cmd, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    dirty_ = false;
}

} // namespace Genesis
