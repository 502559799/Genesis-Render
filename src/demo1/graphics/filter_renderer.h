/*
 * GPU Filter Renderer - Vulkan Post Processing Pipeline
 * Author: Genesis Render Team
 */

#ifndef __GENESIS_FILTER_RENDERER_H__
#define __GENESIS_FILTER_RENDERER_H__

#include <vulkan/vulkan.h>
#include <cstdint>
#include "filter/filter_system.h"

namespace Genesis
{

class GpuDevice;
class CameraTexture;

struct FilterPushConstant {
    int filterType;
    float brightness;
    float contrast;
    float saturation;
    float intensity;
    float sigma;
    float padding[2]; // Align to 32 bytes for push constant
};

class FilterRenderer
{
public:
    FilterRenderer(GpuDevice* device);
    ~FilterRenderer();

    bool init(uint32_t width, uint32_t height);
    void shutdown();

    // Render filter effect from input texture to output texture
    void render(CameraTexture* inputTexture, const FilterParams& params, FilterType filterType);

    VkDescriptorSet getOutputDescriptorSet() const { return output_descriptor_set_; }
    VkImageView getOutputImageView() const { return output_image_view_; }

private:
    void createRenderPass();
    void createOutputTexture();
    void createFramebuffer();
    void createShaders();
    void createPipeline();
    void createVertexBuffer();
    void createDescriptorSetLayout();
    void createDescriptorSet();

    GpuDevice* device_ = nullptr;
    VkDevice vk_device_ = VK_NULL_HANDLE;
    VkPhysicalDevice vk_physical_ = VK_NULL_HANDLE;

    uint32_t width_ = 0;
    uint32_t height_ = 0;

    // Render pass for offscreen filtering
    VkRenderPass render_pass_ = VK_NULL_HANDLE;

    // Output texture
    VkImage output_image_ = VK_NULL_HANDLE;
    VkImageView output_image_view_ = VK_NULL_HANDLE;
    VkDeviceMemory output_memory_ = VK_NULL_HANDLE;
    VkSampler output_sampler_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkDescriptorSet output_descriptor_set_ = VK_NULL_HANDLE;

    // Pipeline
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;

    // Vertex buffer (full screen quad)
    VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory_ = VK_NULL_HANDLE;

    // Shader modules
    VkShaderModule vert_module_ = VK_NULL_HANDLE;
    VkShaderModule frag_module_ = VK_NULL_HANDLE;
};

} // namespace Genesis

#endif // __GENESIS_FILTER_RENDERER_H__
