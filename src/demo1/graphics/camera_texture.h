/*
 * Camera Texture Upload Helper
 * Author: Genesis Render Team
 */

#ifndef __GENESIS_CAMERA_TEXTURE_H__
#define __GENESIS_CAMERA_TEXTURE_H__

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace Genesis
{

class GpuDevice;

class CameraTexture
{
public:
    CameraTexture(GpuDevice* device);
    ~CameraTexture();

    bool init(uint32_t width, uint32_t height);
    void shutdown();

    // Upload BGRA pixel data (32-bit per pixel) - copies to staging buffer
    void uploadFrame(uint8_t* data, uint32_t stride = 0);

    // Update GPU texture - call during render pass to record copy commands
    void updateGpu();

    VkDescriptorSet getDescriptorSet() const { return descriptor_set_; }
    uint32_t getWidth() const { return width_; }
    uint32_t getHeight() const { return height_; }
    VkImageView getImageView() const { return image_view_; }
    VkImage getImage() const { return image_; }

private:
    void createImage();
    void createImageView();
    void createSampler();
    void createStagingBuffer();
    void transitionLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkCommandBuffer cmd, VkBuffer buffer, VkImage image, uint32_t w, uint32_t h);

    GpuDevice* device_ = nullptr;
    VkDevice vk_device_ = VK_NULL_HANDLE;
    VkPhysicalDevice vk_physical_ = VK_NULL_HANDLE;

    uint32_t width_ = 0;
    uint32_t height_ = 0;

    VkImage image_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;

    // Staging resources
    VkBuffer staging_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory staging_memory_ = VK_NULL_HANDLE;
    size_t staging_size_ = 0;

    bool dirty_ = false;
    bool first_upload_ = true;
};

} // namespace Genesis

#endif // __GENESIS_CAMERA_TEXTURE_H__
