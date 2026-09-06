/*
 * Genesis Render - Engine
 * vk_rhi.cpp : Vulkan RHI backend (Vulkan 1.0 features; MoltenVK/lavapipe safe).
 */
#include "vk_rhi.hpp"
#include "foundation_layer/log.hpp"

#include <vulkan/vulkan.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include "external/imgui/imgui.h"
#include "external/imgui/imgui_impl_sdl.h"
#include "external/imgui/imgui_impl_vulkan.h"

#include <array>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
#include <unordered_map>
#include <map>
#include <algorithm>

#ifndef VK_KHR_portability_enumeration
#define VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME "VK_KHR_portability_enumeration"
#define VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR (VkInstanceCreateFlags)0x00000001
#endif

namespace Genesis
{
namespace
{
constexpr int kFramesInFlight = 2;

struct VKBuffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    bool host_visible = false;
    void *mapped = nullptr;
    bool alive = false;
};
struct VKTexture
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE; // full view (2D / cube)
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t w = 1, h = 1;
    uint16_t mips = 1;
    bool cube = false, depth = false, rt = false;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    std::map<uint64_t, VkImageView> face_views; // key = mip*16+layer
    VkDescriptorSet imgui_set = VK_NULL_HANDLE;
    bool alive = false;
};
struct VKSampler { VkSampler sampler = VK_NULL_HANDLE; bool alive = false; };
struct VKShader
{
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    bool alive = false;
};
struct VKPipeline
{
    VkPipeline pipeline = VK_NULL_HANDLE;
    bool alive = false;
};
struct VKSet { VkDescriptorSet set = VK_NULL_HANDLE; DescriptorSetDesc desc; bool alive = false; };

struct RPassKey
{
    std::array<VkFormat, 4> color{VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED,
                                 VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED};
    VkFormat depth = VK_FORMAT_UNDEFINED;
    uint8_t color_count = 0;
    bool to_present = false;
    bool operator==(const RPassKey &o) const
    {
        return color == o.color && depth == o.depth && color_count == o.color_count &&
               to_present == o.to_present;
    }
};
struct RPKeyHash
{
    size_t operator()(const RPassKey &k) const
    {
        size_t h = k.depth * 131 + k.color_count * 17 + k.to_present;
        for (auto f : k.color) h ^= f + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
struct FBKey
{
    VkRenderPass rp;
    std::array<VkImageView, 5> views;
    uint32_t w, h;
    bool operator==(const FBKey &o) const
    {
        return rp == o.rp && views == o.views && w == o.w && h == o.h;
    }
};
struct FBHash
{
    size_t operator()(const FBKey &k) const
    {
        size_t h = (size_t)k.rp + k.w * 977 + k.h * 61;
        for (auto v : k.views) h ^= (size_t)v + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
struct FBValue { VkFramebuffer fb = VK_NULL_HANDLE; };

#define VK_CHECK(x)                                                    \
    do { VkResult _r = (x);                                            \
        if (_r != VK_SUCCESS) GenesisPrint("[VK] %s failed: %d (%s:%d)\n", #x, _r, __FILE__, __LINE__); \
    } while (0)

class VKRhi final : public IRHIDevice
{
public:
    bool init(SDL_Window *window, uint32_t width, uint32_t height, bool debug) override
    {
        window_ = window; w_ = width; h_ = height; debug_ = debug;
        if (!createInstance()) return false;
        if (!SDL_Vulkan_CreateSurface(window_, instance_, &surface_))
        {
            GenesisPrint("[VK] CreateSurface failed: %s\n", SDL_GetError());
            return false;
        }
        if (!pickPhysicalDevice()) return false;
        if (!createDevice()) return false;
        createCommandPool();
        createDescriptorPool();
        createSyncObjects();
        createSwapchain();
        createSwapchainObjects();
        createFixedLayouts();
        createDefaultSamplers();
        createDefaultTargets();

        caps_.backend = RenderBackend::Vulkan;
        caps_.clip_depth_zero_to_one = true;
        caps_.ndc_y_down = true;
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(physical_, &p);
        snprintf(caps_.device_name, sizeof(caps_.device_name), "%s", p.deviceName);
        snprintf(caps_.api_version, sizeof(caps_.api_version), "Vulkan %u.%u.%u",
                 VK_API_VERSION_MAJOR(p.apiVersion), VK_API_VERSION_MINOR(p.apiVersion),
                 VK_API_VERSION_PATCH(p.apiVersion));
        GenesisPrint("[VK] device=%s\n", p.deviceName);
        return true;
    }

    void shutdown() override
    {
        if (device_) vkDeviceWaitIdle(device_);
        destroyDefaultTargets();
        for (auto &p : pipelines_) if (p.alive && p.pipeline) { vkDestroyPipeline(device_, p.pipeline, nullptr); p.alive = false; }
        for (auto &s : shaders_) if (s.alive) { destroyShaderObj(s); s.alive = false; }
        for (auto &t : textures_) if (t.alive) { destroyTextureObj(t); t.alive = false; }
        for (auto &s : samplers_) if (s.alive) { vkDestroySampler(device_, s.sampler, nullptr); s.alive = false; }
        for (auto &b : buffers_) if (b.alive) destroyBufferObj(b);
        for (auto &[k, v] : renderpasses_) vkDestroyRenderPass(device_, v, nullptr);
        renderpasses_.clear();
        for (auto &[k, v] : framebuffers_) vkDestroyFramebuffer(device_, v.fb, nullptr);
        framebuffers_.clear();
        if (set_layout_[0]) vkDestroyDescriptorSetLayout(device_, set_layout_[0], nullptr);
        if (set_layout_[1]) vkDestroyDescriptorSetLayout(device_, set_layout_[1], nullptr);
        if (pipeline_layout_) vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
        destroySwapchainObjects();
        destroySwapchain();
        if (descriptor_pool_) vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
        if (transfer_pool_) vkDestroyCommandPool(device_, transfer_pool_, nullptr);
        for (uint32_t i = 0; i < kFramesInFlight; ++i)
        {
            if (frame_pool_[i]) vkDestroyCommandPool(device_, frame_pool_[i], nullptr);
            vkDestroySemaphore(device_, image_avail_[i], nullptr);
            vkDestroySemaphore(device_, render_finished_[i], nullptr);
            vkDestroyFence(device_, in_flight_[i], nullptr);
        }
        if (device_) { vkDestroyDevice(device_, nullptr); device_ = VK_NULL_HANDLE; }
        if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
        if (instance_) vkDestroyInstance(instance_, nullptr);
    }

    const DeviceCaps &caps() const override { return caps_; }
    Format swapchainFormat() const override
    {
        switch (surface_format_.format)
        {
        case VK_FORMAT_R8G8B8A8_UNORM: return Format::R8G8B8A8Unorm;
        case VK_FORMAT_R8G8B8A8_SRGB: return Format::R8G8B8A8Srgb;
        case VK_FORMAT_B8G8R8A8_SRGB: return Format::B8G8R8A8Srgb;
        default: return Format::B8G8R8A8Unorm;
        }
    }

    // ------------------------------------------------------------ swapchain
    void onResize(uint32_t w, uint32_t h) override
    {
        if (w == 0 || h == 0) return;
        vkDeviceWaitIdle(device_);
        w_ = w; h_ = h;
        destroyDefaultTargets();
        recreateSwapchain();
        createDefaultTargets();
    }

    bool beginFrame() override
    {
        vkWaitForFences(device_, 1, &in_flight_[cur_frame_], VK_TRUE, UINT64_MAX);
        VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                          image_avail_[cur_frame_], VK_NULL_HANDLE, &image_index_);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); return false; }
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return false;
        vkResetFences(device_, 1, &in_flight_[cur_frame_]);
        vkResetCommandPool(device_, frame_pool_[cur_frame_], 0);
        cmd_ = cmds_[cur_frame_];
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd_, &bi);
        frame_open_ = true;
        return true;
    }
    uint32_t currentFrameIndex() const override { return cur_frame_; }

    void beginPass(uint32_t colorCount, ColorAttachment *colors, const DepthAttachment *depth) override
    {
        RPassKey key{};
        key.color_count = (uint8_t)colorCount;
        std::array<VkImageView, 5> views{};
        uint32_t pw = 0, ph = 0;
        for (uint32_t i = 0; i < colorCount; ++i)
        {
            VKTexture *t = lookup(colors[i].texture, textures_);
            key.color[i] = t->format;
            views[i] = getFaceView(t, colors[i].mip, colors[i].layer);
            pw = std::max(1u, t->w >> colors[i].mip);
            ph = std::max(1u, t->h >> colors[i].mip);
            transitionColorAttachment(t, views[i], colors[i].mip);
        }
        if (depth && depth->texture.valid())
        {
            VKTexture *t = lookup(depth->texture, textures_);
            key.depth = t->format;
            views[colorCount] = t->view;
            transitionDepthAttachment(t);
        }
        VkRenderPass rp = getRenderPass(key);
        FBKey fbk{rp, views, pw, ph};
        VkFramebuffer fb = getFramebuffer(fbk);

        std::array<VkClearValue, 5> clears{};
        for (uint32_t i = 0; i < colorCount; ++i)
            for (int c = 0; c < 4; ++c) clears[i].color.float32[c] = colors[i].clear_color[c];
        if (depth && depth->texture.valid())
            clears[colorCount].depthStencil = {depth->clear_depth, 0};
        VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpbi.renderPass = rp;
        rpbi.framebuffer = fb;
        rpbi.renderArea.extent = {pw, ph};
        rpbi.clearValueCount = colorCount + (depth && depth->texture.valid() ? 1 : 0);
        rpbi.pClearValues = clears.data();
        vkCmdBeginRenderPass(cmd_, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp{0, 0, (float)pw, (float)ph, 0.f, 1.f};
        VkRect2D sc{{0, 0}, {pw, ph}};
        vkCmdSetViewport(cmd_, 0, 1, &vp);
        vkCmdSetScissor(cmd_, 0, 1, &sc);
    }

    void endPass() override { vkCmdEndRenderPass(cmd_); }

    void beginSwapchainPass(const float clear[4], float clear_depth) override
    {
        (void)clear_depth;
        VkClearValue cv;
        for (int i = 0; i < 4; ++i) cv.color.float32[i] = clear[i];
        VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpbi.renderPass = present_renderpass_;
        rpbi.framebuffer = present_framebuffers_[image_index_];
        rpbi.renderArea.extent = extent_;
        rpbi.clearValueCount = 1;
        rpbi.pClearValues = &cv;
        vkCmdBeginRenderPass(cmd_, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp{0, 0, (float)extent_.width, (float)extent_.height, 0.f, 1.f};
        VkRect2D sc{{0, 0}, extent_};
        vkCmdSetViewport(cmd_, 0, 1, &vp);
        vkCmdSetScissor(cmd_, 0, 1, &sc);
    }
    void endSwapchainPass() override { vkCmdEndRenderPass(cmd_); }

    void endFrame() override
    {
        vkEndCommandBuffer(cmd_);
        VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &image_avail_[cur_frame_];
        si.pWaitDstStageMask = &wait;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd_;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &render_finished_[cur_frame_];
        VK_CHECK(vkQueueSubmit(queue_, 1, &si, in_flight_[cur_frame_]));

        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &render_finished_[cur_frame_];
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain_;
        pi.pImageIndices = &image_index_;
        VkResult r = vkQueuePresentKHR(queue_, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR || framebuffer_resized_)
        {
            framebuffer_resized_ = false;
            recreateSwapchain();
        }
        cur_frame_ = (cur_frame_ + 1) % kFramesInFlight;
        frame_open_ = false;
    }

    // ------------------------------------------------------------- buffers
    BufferHandle createBuffer(const BufferDesc &desc, const void *data) override
    {
        VKBuffer b;
        b.size = desc.size;
        b.host_visible = desc.host_visible;
        VkBufferUsageFlags usage = 0;
        if (desc.usage & BufferUsage_Vertex) usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        if (desc.usage & BufferUsage_Index) usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (desc.usage & BufferUsage_Uniform) usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (desc.usage & BufferUsage_TransferDst) usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        b.usage = usage;
        if (desc.host_visible)
        {
            createBuffer_(desc.size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          b.buffer, b.memory);
            if (data)
            {
                void *p; vkMapMemory(device_, b.memory, 0, desc.size, 0, &p);
                memcpy(p, data, desc.size);
                vkUnmapMemory(device_, b.memory);
            }
        }
        else
        {
            VkBuffer staging; VkDeviceMemory smem;
            createBuffer_(desc.size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          staging, smem);
            void *p; vkMapMemory(device_, smem, 0, desc.size, 0, &p);
            if (data) memcpy(p, data, desc.size); else memset(p, 0, desc.size);
            vkUnmapMemory(device_, smem);
            createBuffer_(desc.size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, b.buffer, b.memory);
            oneTimeTransfer([&](VkCommandBuffer c) {
                VkBufferCopy region{0, 0, desc.size};
                vkCmdCopyBuffer(c, staging, b.buffer, 1, &region);
            });
            vkDestroyBuffer(device_, staging, nullptr);
            vkFreeMemory(device_, smem, nullptr);
        }
        b.alive = true;
        BufferHandle h; h.id = allocIndex(buffers_, b);
        return h;
    }
    void destroyBuffer(BufferHandle h) override { if (auto *b = lookup(h, buffers_)) { destroyBufferObj(*b); b->alive = false; } }
    void writeBuffer(BufferHandle h, size_t off, const void *data, size_t size) override
    {
        auto *b = lookup(h, buffers_);
        if (!b) return;
        if (b->host_visible)
        {
            void *p; vkMapMemory(device_, b->memory, off, size, 0, &p);
            memcpy(p, data, size);
            vkUnmapMemory(device_, b->memory);
        }
        else
        {
            VkBuffer staging; VkDeviceMemory smem;
            createBuffer_(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          staging, smem);
            void *p; vkMapMemory(device_, smem, 0, size, 0, &p);
            memcpy(p, data, size);
            vkUnmapMemory(device_, smem);
            oneTimeTransfer([&](VkCommandBuffer c) {
                VkBufferCopy region{0, off, size};
                vkCmdCopyBuffer(c, staging, b->buffer, 1, &region);
            });
            vkDestroyBuffer(device_, staging, nullptr);
            vkFreeMemory(device_, smem, nullptr);
        }
    }
    void *mapBuffer(BufferHandle h) override
    {
        auto *b = lookup(h, buffers_);
        if (!b || !b->host_visible || b->mapped) return nullptr;
        vkMapMemory(device_, b->memory, 0, b->size, 0, &b->mapped);
        return b->mapped;
    }
    void unmapBuffer(BufferHandle h) override
    {
        auto *b = lookup(h, buffers_);
        if (!b || !b->mapped) return;
        vkUnmapMemory(device_, b->memory);
        b->mapped = nullptr;
    }

    // ------------------------------------------------------------ textures
    TextureHandle createTexture(const TextureDesc &desc, const void *data) override
    {
        VKTexture t;
        t.w = desc.width; t.h = desc.height; t.mips = desc.mip_levels;
        t.cube = desc.type == TextureType::TextureCube;
        t.rt = (desc.usage & TextureUsage_RenderTarget) != 0;
        t.depth = (desc.usage & TextureUsage_DepthStencil) != 0;
        t.format = toVkFormat(desc.format);
        uint32_t layers = t.cube ? 6 : 1;
        VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (t.rt) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (t.depth) usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (data) usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        if (t.cube) ii.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = t.format;
        ii.extent = {desc.width, desc.height, 1};
        ii.mipLevels = desc.mip_levels;
        ii.arrayLayers = layers;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = usage;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VK_CHECK(vkCreateImage(device_, &ii, nullptr, &t.image));
        VkMemoryRequirements req; vkGetImageMemoryRequirements(device_, t.image, &req);
        VkMemoryPropertyFlags props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        VkDeviceMemory mem = allocateMemory(req, props);
        vkBindImageMemory(device_, t.image, mem, 0);
        t.memory = mem;

        // full view
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = t.image;
        vi.viewType = t.cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
        vi.format = t.format;
        vi.subresourceRange = {t.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
                               0, desc.mip_levels, 0, layers};
        VK_CHECK(vkCreateImageView(device_, &vi, nullptr, &t.view));

        if (data) uploadInitialData(t, desc, data);
        // render targets start UNDEFINED; beginPass() inserts the first layout barrier.

        t.alive = true;
        TextureHandle h; h.id = allocIndex(textures_, t);
        return h;
    }

    void uploadTexture2D(TextureHandle, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const void *) override {}
    void uploadCubeFace(TextureHandle, uint32_t, uint32_t, uint32_t, uint32_t, const void *) override {}
    void generateMipmaps(TextureHandle h) override
    {
        auto *t = lookup(h, textures_);
        if (!t || t->mips <= 1) return;
        oneTimeTransfer([&](VkCommandBuffer cmd) {
            transitionLayout(cmd, t->image, t->layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             t->mips, t->cube ? 6 : 1, t->depth);
            int32_t w = (int32_t)t->w, hh = (int32_t)t->h;
            uint32_t layers = t->cube ? 6u : 1u;
            for (uint32_t i = 1; i < t->mips; ++i)
            {
                VkImageBlit blit{};
                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.mipLevel = i - 1;
                blit.srcSubresource.baseArrayLayer = 0;
                blit.srcSubresource.layerCount = layers;
                blit.srcOffsets[1] = {w, hh, 1};
                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel = i;
                blit.dstSubresource.baseArrayLayer = 0;
                blit.dstSubresource.layerCount = layers;
                blit.dstOffsets[1] = {std::max(1, w / 2), std::max(1, hh / 2), 1};
                vkCmdBlitImage(cmd, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
                w = std::max(1, w / 2); hh = std::max(1, hh / 2);
            }
            transitionLayout(cmd, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, t->mips, t->cube ? 6 : 1, false);
            t->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        });
    }
    void destroyTexture(TextureHandle h) override
    {
        if (auto *t = lookup(h, textures_)) { destroyTextureObj(*t); t->alive = false; }
    }

    // ------------------------------------------------------------ samplers
    SamplerHandle createSampler(const SamplerDesc &d) override
    {
        VKSampler s;
        VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        ci.magFilter = d.mag_filter == FilterMode::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        ci.minFilter = d.min_filter == FilterMode::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
        ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        if (d.mip_filter == FilterMode::Nearest) ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        ci.addressModeU = toWrap(d.wrap_u);
        ci.addressModeV = toWrap(d.wrap_v);
        ci.addressModeW = toWrap(d.wrap_w);
        ci.minLod = 0; ci.maxLod = 16.0f;
        ci.anisotropyEnable = VK_FALSE; ci.maxAnisotropy = 1;
        ci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        vkCreateSampler(device_, &ci, nullptr, &s.sampler);
        s.alive = true;
        SamplerHandle h; h.id = allocIndex(samplers_, s);
        return h;
    }
    void destroySampler(SamplerHandle h) override
    {
        if (auto *s = lookup(h, samplers_)) { vkDestroySampler(device_, s->sampler, nullptr); s->alive = false; }
    }

    // ------------------------------------------------------------ shaders/pipelines
    ShaderHandle createShaderProgram(const ShaderProgramDesc &desc) override
    {
        VKShader s;
        for (uint32_t i = 0; i < desc.stage_count; ++i)
        {
            const auto &st = desc.stages[i];
            if (!st.spv) continue;
            VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            mi.codeSize = st.spv_size_bytes;
            mi.pCode = st.spv;
            VkShaderModule m; VK_CHECK(vkCreateShaderModule(device_, &mi, nullptr, &m));
            (st.stage == ShaderStage::Vertex ? s.vert : s.frag) = m;
        }
        s.alive = true;
        ShaderHandle h; h.id = allocIndex(shaders_, s);
        return h;
    }
    void destroyShaderProgram(ShaderHandle h) override
    {
        if (auto *s = lookup(h, shaders_)) { destroyShaderObj(*s); s->alive = false; }
    }

    PipelineHandle createPipeline(const PipelineDesc &d) override
    {
        auto *sh = lookup(d.shader, shaders_);
        VKPipeline out;
        VkPipelineShaderStageCreateInfo stages[2]{};
        uint32_t sc = 0;
        auto makeStage = [&](VkShaderStageFlagBits bit, VkShaderModule mod) {
            VkPipelineShaderStageCreateInfo s{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            s.stage = bit;
            s.module = mod;
            s.pName = "main";
            return s;
        };
        if (sh->vert) stages[sc++] = makeStage(VK_SHADER_STAGE_VERTEX_BIT, sh->vert);
        if (sh->frag) stages[sc++] = makeStage(VK_SHADER_STAGE_FRAGMENT_BIT, sh->frag);

        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        std::vector<VkVertexInputBindingDescription> binds;
        std::vector<VkVertexInputAttributeDescription> attrs;
        if (d.vertex_layout.attribute_count > 0)
        {
            binds.push_back({0, d.vertex_layout.stride, VK_VERTEX_INPUT_RATE_VERTEX});
            for (uint32_t i = 0; i < d.vertex_layout.attribute_count; ++i)
            {
                const auto &a = d.vertex_layout.attributes[i];
                attrs.push_back({a.location, 0, toVkVertexFormat(a.format), a.offset});
            }
            vi.vertexBindingDescriptionCount = (uint32_t)binds.size();
            vi.pVertexBindingDescriptions = binds.data();
            vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
            vi.pVertexAttributeDescriptions = attrs.data();
        }
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = d.topology == PrimitiveTopology::Lines ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                     : d.topology == PrimitiveTopology::TriangleStrip ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
                     : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1; vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = d.cull == CullMode::None ? VK_CULL_MODE_NONE
                      : d.cull == CullMode::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.f;
        rs.depthClampEnable = VK_FALSE;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable = d.depth_format != Format::Unknown && d.depth_test ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = d.depth_write && d.depth_format != Format::Unknown ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp = toVkCompare(d.depth_compare);
        ds.minDepthBounds = 0; ds.maxDepthBounds = 1;
        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask = 0xF;
        if (d.blend == BlendMode::Alpha)
        {
            ba.blendEnable = VK_TRUE;
            ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            ba.colorBlendOp = VK_BLEND_OP_ADD;
            ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            ba.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        else if (d.blend == BlendMode::Additive)
        {
            ba.blendEnable = VK_TRUE;
            ba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            ba.colorBlendOp = VK_BLEND_OP_ADD;
            ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE; ba.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        std::array<VkPipelineColorBlendAttachmentState, 4> bas{ba, ba, ba, ba};
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = d.color_format_count;
        cb.pAttachments = bas.data();
        VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynst{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynst.dynamicStateCount = 2; dynst.pDynamicStates = dyn;

        bool present = d.depth_format == Format::Unknown;
        RPassKey key{};
        key.color_count = d.color_format_count;
        for (uint8_t i = 0; i < d.color_format_count; ++i) key.color[i] = toVkFormat(d.color_formats[i]);
        key.depth = toVkFormat(d.depth_format);
        key.to_present = present;
        VkRenderPass rp = getRenderPass(key);

        VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        ci.stageCount = sc; ci.pStages = stages;
        ci.pVertexInputState = &vi; ci.pInputAssemblyState = &ia;
        ci.pViewportState = &vp; ci.pRasterizationState = &rs;
        ci.pMultisampleState = &ms; ci.pDepthStencilState = present ? nullptr : &ds;
        ci.pColorBlendState = &cb; ci.pDynamicState = &dynst;
        ci.layout = pipeline_layout_;
        ci.renderPass = rp; ci.subpass = 0;
        VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr, &out.pipeline));
        out.alive = true;
        PipelineHandle h; h.id = allocIndex(pipelines_, out);
        return h;
    }
    void destroyPipeline(PipelineHandle h) override
    {
        if (auto *p = lookup(h, pipelines_)) { vkDestroyPipeline(device_, p->pipeline, nullptr); p->alive = false; }
    }

    // ------------------------------------------------------------ sets
    DescriptorSetHandle createDescriptorSet(const DescriptorSetDesc &d) override
    {
        VKSet s;
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = descriptor_pool_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &set_layout_[d.set_index];
        VK_CHECK(vkAllocateDescriptorSets(device_, &ai, &s.set));
        s.alive = true;
        DescriptorSetHandle h; h.id = allocIndex(sets_, s);
        updateDescriptorSet(h, d);
        return h;
    }
    void updateDescriptorSet(DescriptorSetHandle h, const DescriptorSetDesc &d) override
    {
        auto *s = lookup(h, sets_);
        if (!s) return;
        s->desc = d;
        std::vector<VkWriteDescriptorSet> writes;
        std::vector<VkDescriptorBufferInfo> binfos;
        std::vector<VkDescriptorImageInfo> iinfos;
        binfos.reserve(d.binding_count); iinfos.reserve(d.binding_count);
        for (uint32_t i = 0; i < d.binding_count; ++i)
        {
            const auto &b = d.bindings[i];
            if (b.buffer.valid())
            {
                auto *buf = lookup(b.buffer, buffers_);
                binfos.push_back({buf->buffer, 0, b.size ? b.size : buf->size});
                writes.push_back({VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET});
                auto &w = writes.back();
                w.dstSet = s->set; w.dstBinding = b.binding; w.dstArrayElement = 0;
                w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                w.pBufferInfo = &binfos.back();
            }
            if (b.texture.valid())
            {
                auto *tex = lookup(b.texture, textures_);
                auto *sm = lookup(b.sampler, samplers_);
                iinfos.push_back({sm ? sm->sampler : VK_NULL_HANDLE, tex->view,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
                writes.push_back({VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET});
                auto &w = writes.back();
                w.dstSet = s->set; w.dstBinding = b.binding; w.dstArrayElement = 0;
                w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                w.pImageInfo = &iinfos.back();
            }
        }
        vkUpdateDescriptorSets(device_, (uint32_t)writes.size(), writes.data(), 0, nullptr);
    }
    void destroyDescriptorSet(DescriptorSetHandle h) override
    {
        if (auto *s = lookup(h, sets_)) s->alive = false; // pool reset at shutdown
    }

    // ------------------------------------------------------------ commands
    void bindPipeline(PipelineHandle h) override
    {
        cur_pipeline_ = lookup(h, pipelines_);
        if (cur_pipeline_) vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, cur_pipeline_->pipeline);
    }
    void bindDescriptorSet(uint32_t set, DescriptorSetHandle h) override
    {
        auto *s = lookup(h, sets_);
        if (s) vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_,
                                       set, 1, &s->set, 0, nullptr);
    }
    void pushConstants(const void *data, uint32_t size) override
    {
        vkCmdPushConstants(cmd_, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, size, data);
    }
    void bindVertexBuffer(BufferHandle h, size_t off, uint32_t binding) override
    {
        auto *b = lookup(h, buffers_);
        if (!b) return;
        VkDeviceSize o = off;
        vkCmdBindVertexBuffers(cmd_, binding, 1, &b->buffer, &o);
    }
    void bindIndexBuffer(BufferHandle h, IndexType t, size_t off) override
    {
        auto *b = lookup(h, buffers_);
        if (!b) return;
        vkCmdBindIndexBuffer(cmd_, b->buffer, off,
                             t == IndexType::Uint32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
    }
    void setViewport(uint32_t w, uint32_t h) override
    {
        VkViewport vp{0, 0, (float)w, (float)h, 0.f, 1.f};
        VkRect2D sc{{0, 0}, {w, h}};
        vkCmdSetViewport(cmd_, 0, 1, &vp);
        vkCmdSetScissor(cmd_, 0, 1, &sc);
    }
    void draw(uint32_t vc, uint32_t ic, uint32_t first) override
    {
        vkCmdDraw(cmd_, vc, ic, first, 0);
    }
    void drawIndexed(uint32_t ic, uint32_t instances, uint32_t first, int32_t vbase) override
    {
        vkCmdDrawIndexed(cmd_, ic, instances, first, vbase, 0);
    }
    void drawFullscreen() override { vkCmdDraw(cmd_, 3, 1, 0, 0); }

    void readTexturePixels(TextureHandle h, uint32_t w, uint32_t hh, void *out) override
    {
        auto *t = lookup(h, textures_);
        if (!t) return;
        VkDeviceSize size = (VkDeviceSize)w * hh * 4;
        VkBuffer staging; VkDeviceMemory smem;
        createBuffer_(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      staging, smem);
        oneTimeTransfer([&](VkCommandBuffer c) {
            transitionLayout(c, t->image, t->layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1, 1, t->depth);
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {w, hh, 1};
            vkCmdCopyImageToBuffer(c, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &region);
            transitionLayout(c, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, 1, false);
            t->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        });
        void *p; vkMapMemory(device_, smem, 0, size, 0, &p);
        // Vulkan render targets are top-origin, so the row order matches PNG.
        memcpy(out, p, size);
        vkUnmapMemory(device_, smem);
        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, smem, nullptr);
    }

    TextureHandle sceneColor() const override { return scene_color_; }
    TextureHandle sceneDepth() const override { return scene_depth_; }
    SamplerHandle linearSampler() const override { return linear_sampler_; }
    SamplerHandle nearestSampler() const override { return nearest_sampler_; }
    SamplerHandle cubeMipSampler() const override { return cube_mip_sampler_; }

    // ------------------------------------------------------------- imgui
    void imguiBackendInit() override
    {
        ImGui_ImplSDL2_InitForVulkan(window_);
        ImGui_ImplVulkan_InitInfo info{};
        info.Instance = instance_;
        info.PhysicalDevice = physical_;
        info.Device = device_;
        info.QueueFamily = gfx_family_;
        info.Queue = queue_;
        info.DescriptorPool = descriptor_pool_;
        info.MinImageCount = (uint32_t)images_.size();
        info.ImageCount = (uint32_t)images_.size();
        info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        ImGui_ImplVulkan_Init(&info, present_renderpass_);
        VkCommandBuffer cb = beginOneTime(transfer_pool_);
        ImGui_ImplVulkan_CreateFontsTexture(cb);
        endOneTime(cb, transfer_pool_);
        ImGui_ImplVulkan_DestroyFontUploadObjects();
        imgui_ready_ = true;
    }
    void imguiBackendBeginFrame() override
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
    }
    void imguiBackendRenderDrawData(::ImDrawData *d) override
    {
        if (imgui_ready_) ImGui_ImplVulkan_RenderDrawData(d, cmd_);
    }
    void imguiBackendShutdown() override
    {
        if (!imgui_ready_) return;
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        imgui_ready_ = false;
    }
    void *imguiTextureID(TextureHandle th, SamplerHandle sh) override
    {
        auto *t = lookup(th, textures_);
        auto *sm = lookup(sh, samplers_);
        if (!t || !sm) return nullptr;
        if (t->imgui_set == VK_NULL_HANDLE)
        {
            VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            ai.descriptorPool = descriptor_pool_; ai.descriptorSetCount = 1;
            ai.pSetLayouts = &imgui_ds_layout_;
            if (vkAllocateDescriptorSets(device_, &ai, &t->imgui_set) != VK_SUCCESS) return nullptr;
            VkDescriptorImageInfo ii{sm->sampler, t->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = t->imgui_set; w.dstBinding = 0; w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &ii;
            vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
        }
        return (void *)t->imgui_set;
    }

private:
    template <typename T, size_t N>
    uint32_t allocIndex(std::array<T, N> &arr, const T &val)
    {
        for (uint32_t i = 0; i < (uint32_t)arr.size(); ++i)
            if (!arr[i].alive) { arr[i] = val; return i + 1; }
        GenesisPrint("[VK] pool exhausted %zu\n", N);
        return 0xffffffffu;
    }
    template <typename T, size_t N, typename Tag>
    T *lookup(const TRHIHandle<Tag> &h, std::array<T, N> &arr)
    {
        if (h.id == 0xffffffffu || h.id == 0 || h.id > (uint32_t)N) return nullptr;
        return arr[h.id - 1].alive ? &arr[h.id - 1] : nullptr;
    }

    // ------------------------------------------------------------- instance/device
    bool extensionSupported(const std::vector<VkExtensionProperties> &all, const char *name)
    {
        for (auto &e : all) if (!strcmp(e.extensionName, name)) return true;
        return false;
    }
    bool createInstance()
    {
        uint32_t ext_count = 0;
        SDL_Vulkan_GetInstanceExtensions(window_, &ext_count, nullptr);
        std::vector<const char *> exts(ext_count);
        SDL_Vulkan_GetInstanceExtensions(window_, &ext_count, exts.data());

        uint32_t count = 0; vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> props(count);
        vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data());
        bool portability = extensionSupported(props, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        if (portability) exts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        if (debug_ && extensionSupported(props, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "GenesisPBR";
        app.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &app;
        ci.enabledExtensionCount = (uint32_t)exts.size();
        ci.ppEnabledExtensionNames = exts.data();
        if (portability) ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        VkResult r = vkCreateInstance(&ci, nullptr, &instance_);
        if (r != VK_SUCCESS) { GenesisPrint("[VK] vkCreateInstance failed %d\n", r); return false; }
        return true;
    }
    bool pickPhysicalDevice()
    {
        uint32_t n = 0; vkEnumeratePhysicalDevices(instance_, &n, nullptr);
        if (!n) { GenesisPrint("[VK] no physical device\n"); return false; }
        std::vector<VkPhysicalDevice> devs(n);
        vkEnumeratePhysicalDevices(instance_, &n, devs.data());
        int best = -1; int best_score = -1;
        for (uint32_t i = 0; i < n; ++i)
        {
            uint32_t qn = 0; vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qn, nullptr);
            std::vector<VkQueueFamilyProperties> qp(qn);
            vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &qn, qp.data());
            int family = -1;
            for (uint32_t q = 0; q < qn; ++q)
            {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(devs[i], q, surface_, &present);
                if ((qp[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { family = (int)q; break; }
            }
            if (family < 0) continue;
            int score = 1;
            VkPhysicalDeviceProperties pp; vkGetPhysicalDeviceProperties(devs[i], &pp);
            if (pp.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 100;
            if (pp.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 10;
            if (score > best_score) { best_score = score; best = (int)i; gfx_family_ = (uint32_t)family; }
        }
        if (best < 0) { GenesisPrint("[VK] no suitable gpu\n"); return false; }
        physical_ = devs[best];
        return true;
    }
    bool createDevice()
    {
        float prio = 1.f;
        VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        q.queueFamilyIndex = gfx_family_; q.queueCount = 1; q.pQueuePriorities = &prio;
        std::vector<const char *> exts{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        uint32_t n = 0; vkEnumerateDeviceExtensionProperties(physical_, nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> props(n);
        vkEnumerateDeviceExtensionProperties(physical_, nullptr, &n, props.data());
        bool portability = false;
        for (auto &p : props)
            if (!strcmp(p.extensionName, "VK_KHR_portability_subset")) portability = true;
        if (portability) exts.push_back("VK_KHR_portability_subset");
        VkPhysicalDeviceFeatures feat{};
        feat.samplerAnisotropy = VK_FALSE;
        feat.fillModeNonSolid = VK_TRUE;
        VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        ci.queueCreateInfoCount = 1; ci.pQueueCreateInfos = &q;
        ci.enabledExtensionCount = (uint32_t)exts.size(); ci.ppEnabledExtensionNames = exts.data();
        ci.pEnabledFeatures = &feat;
        VkResult r = vkCreateDevice(physical_, &ci, nullptr, &device_);
        if (r != VK_SUCCESS) { GenesisPrint("[VK] vkCreateDevice %d\n", r); return false; }
        vkGetDeviceQueue(device_, gfx_family_, 0, &queue_);
        return true;
    }
    void createCommandPool()
    {
        VkCommandPoolCreateInfo p{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        p.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        p.queueFamilyIndex = gfx_family_;
        vkCreateCommandPool(device_, &p, nullptr, &transfer_pool_);
        for (uint32_t i = 0; i < kFramesInFlight; ++i)
        {
            VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pi.queueFamilyIndex = gfx_family_;
            vkCreateCommandPool(device_, &pi, nullptr, &frame_pool_[i]);
            VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            ai.commandPool = frame_pool_[i]; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;
            vkAllocateCommandBuffers(device_, &ai, &cmds_[i]);
        }
    }
    void createDescriptorPool()
    {
        VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2048},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096},
        };
        VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        ci.maxSets = 4096;
        ci.poolSizeCount = 2; ci.pPoolSizes = sizes;
        vkCreateDescriptorPool(device_, &ci, nullptr, &descriptor_pool_);

        // imgui standalone set layout (1 combined image sampler)
        VkDescriptorSetLayoutBinding b{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                       VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        li.bindingCount = 1; li.pBindings = &b;
        vkCreateDescriptorSetLayout(device_, &li, nullptr, &imgui_ds_layout_);
    }
    void createSyncObjects()
    {
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (uint32_t i = 0; i < kFramesInFlight; ++i)
        {
            vkCreateSemaphore(device_, &si, nullptr, &image_avail_[i]);
            vkCreateSemaphore(device_, &si, nullptr, &render_finished_[i]);
            vkCreateFence(device_, &fi, nullptr, &in_flight_[i]);
        }
    }

    // --------------------------------------------------------- fixed layouts
    void createFixedLayouts()
    {
        VkDescriptorSetLayoutBinding b[16]{};
        auto ubo = [](uint32_t slot) {
            return VkDescriptorSetLayoutBinding{slot, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        };
        auto tex = [](uint32_t slot, VkShaderStageFlags st = VK_SHADER_STAGE_FRAGMENT_BIT) {
            return VkDescriptorSetLayoutBinding{slot, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, st, nullptr};
        };
        // set 0
        VkDescriptorSetLayoutBinding s0[7] = {
            ubo(0), ubo(1), tex(8), tex(9), tex(10), tex(11), tex(12)};
        VkDescriptorSetLayoutCreateInfo l0{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        l0.bindingCount = 7; l0.pBindings = s0;
        vkCreateDescriptorSetLayout(device_, &l0, nullptr, &set_layout_[0]);
        // set 1
        VkDescriptorSetLayoutBinding s1[5] = {
            ubo(2), tex(4), tex(5), tex(6), tex(7)};
        VkDescriptorSetLayoutCreateInfo l1{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        l1.bindingCount = 5; l1.pBindings = s1;
        vkCreateDescriptorSetLayout(device_, &l1, nullptr, &set_layout_[1]);

        VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pli.setLayoutCount = 2; pli.pSetLayouts = set_layout_;
        VkPushConstantRange pc{VK_SHADER_STAGE_VERTEX_BIT, 0, kPushConstantSize};
        pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pc;
        vkCreatePipelineLayout(device_, &pli, nullptr, &pipeline_layout_);
    }

    // ------------------------------------------------------------- swapchain
    VkSurfaceFormatKHR pickFormat(const std::vector<VkSurfaceFormatKHR> &f)
    {
        for (auto &x : f)
            if (x.format == VK_FORMAT_B8G8R8A8_UNORM && x.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                return x;
        return f[0];
    }
    void createSwapchain()
    {
        VkSurfaceCapabilitiesKHR cap;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &cap);
        uint32_t fn = 0; vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &fn, nullptr);
        std::vector<VkSurfaceFormatKHR> fmts(fn);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &fn, fmts.data());
        uint32_t pn = 0; vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &pn, nullptr);
        std::vector<VkPresentModeKHR> pms(pn);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &pn, pms.data());
        VkPresentModeKHR pm = VK_PRESENT_MODE_FIFO_KHR;
        for (auto x : pms) if (x == VK_PRESENT_MODE_MAILBOX_KHR) pm = x;
        VkSurfaceFormatKHR sf = pickFormat(fmts);
        surface_format_ = sf;
        extent_ = cap.currentExtent;
        if (extent_.width == UINT32_MAX)
        {
            extent_.width = std::clamp(w_, cap.minImageExtent.width, cap.maxImageExtent.width);
            extent_.height = std::clamp(h_, cap.minImageExtent.height, cap.maxImageExtent.height);
        }
        uint32_t count = cap.minImageCount + 1;
        if (cap.maxImageCount && count > cap.maxImageCount) count = cap.maxImageCount;
        VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        ci.surface = surface_;
        ci.minImageCount = count;
        ci.imageFormat = sf.format; ci.imageColorSpace = sf.colorSpace;
        ci.imageExtent = extent_; ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ci.preTransform = cap.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = pm; ci.clipped = VK_TRUE;
        VK_CHECK(vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_));
        vkGetSwapchainImagesKHR(device_, swapchain_, &fn, nullptr);
        images_.resize(fn);
        vkGetSwapchainImagesKHR(device_, swapchain_, &fn, images_.data());
    }
    void createSwapchainObjects()
    {
        views_.resize(images_.size());
        present_framebuffers_.resize(images_.size());
        for (size_t i = 0; i < images_.size(); ++i)
        {
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vi.image = images_[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = surface_format_.format;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCreateImageView(device_, &vi, nullptr, &views_[i]);
        }
        // present render pass (color only, final PRESENT layout)
        VkAttachmentDescription c{};
        c.format = surface_format_.format;
        c.samples = VK_SAMPLE_COUNT_1_BIT;
        c.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; c.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        c.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; c.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        c.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; c.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference cref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sp{}; sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.colorAttachmentCount = 1; sp.pColorAttachments = &cref;
        VkSubpassDependency dep{VK_SUBPASS_EXTERNAL, 0,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0};
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = 1; rp.pAttachments = &c; rp.subpassCount = 1; rp.pSubpasses = &sp;
        rp.dependencyCount = 1; rp.pDependencies = &dep;
        vkCreateRenderPass(device_, &rp, nullptr, &present_renderpass_);
        for (size_t i = 0; i < images_.size(); ++i)
        {
            VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fb.renderPass = present_renderpass_;
            fb.attachmentCount = 1; fb.pAttachments = &views_[i];
            fb.width = extent_.width; fb.height = extent_.height; fb.layers = 1;
            vkCreateFramebuffer(device_, &fb, nullptr, &present_framebuffers_[i]);
        }
    }
    void destroySwapchainObjects()
    {
        for (auto fb : present_framebuffers_) if (fb) vkDestroyFramebuffer(device_, fb, nullptr);
        present_framebuffers_.clear();
        if (present_renderpass_) vkDestroyRenderPass(device_, present_renderpass_, nullptr);
        present_renderpass_ = VK_NULL_HANDLE;
        for (auto v : views_) if (v) vkDestroyImageView(device_, v, nullptr);
        views_.clear();
    }
    void destroySwapchain()
    {
        if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    void recreateSwapchain()
    {
        int ww = 0, hh = 0;
        SDL_Vulkan_GetDrawableSize(window_, &ww, &hh);
        int guard = 0;
        while ((ww == 0 || hh == 0) && guard++ < 1000)
        {
            SDL_Event e; SDL_WaitEvent(&e);
            SDL_Vulkan_GetDrawableSize(window_, &ww, &hh);
        }
        vkDeviceWaitIdle(device_);
        for (auto &[k, v] : framebuffers_) vkDestroyFramebuffer(device_, v.fb, nullptr);
        framebuffers_.clear();
        destroySwapchainObjects();
        destroySwapchain();
        w_ = ww; h_ = hh;
        createSwapchain();
        createSwapchainObjects();
    }

    // ------------------------------------------------------------- memory/transfers
    uint32_t findMemoryType(uint32_t typebits, VkMemoryPropertyFlags props)
    {
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(physical_, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
            if ((typebits & (1 << i)) &&
                (mp.memoryTypes[i].propertyFlags & props) == props)
                return i;
        return 0;
    }
    VkDeviceMemory allocateMemory(const VkMemoryRequirements &req, VkMemoryPropertyFlags props)
    {
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, props);
        VkDeviceMemory m; VK_CHECK(vkAllocateMemory(device_, &ai, nullptr, &m));
        return m;
    }
    void createBuffer_(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                       VkBuffer &buf, VkDeviceMemory &mem)
    {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(vkCreateBuffer(device_, &bi, nullptr, &buf));
        VkMemoryRequirements req; vkGetBufferMemoryRequirements(device_, buf, &req);
        mem = allocateMemory(req, props);
        vkBindBufferMemory(device_, buf, mem, 0);
    }
    VkCommandBuffer beginOneTime(VkCommandPool pool)
    {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
        VkCommandBuffer cb; vkAllocateCommandBuffers(device_, &ai, &cb);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi);
        return cb;
    }
    void endOneTime(VkCommandBuffer cb, VkCommandPool pool)
    {
        vkEndCommandBuffer(cb);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1; si.pCommandBuffers = &cb;
        vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue_);
        vkFreeCommandBuffers(device_, pool, 1, &cb);
    }
    void oneTimeTransfer(const std::function<void(VkCommandBuffer)> &fn)
    {
        VkCommandBuffer cb = beginOneTime(transfer_pool_);
        fn(cb);
        endOneTime(cb, transfer_pool_);
    }

    void transitionLayout(VkCommandBuffer c, VkImage img, VkImageLayout from, VkImageLayout to,
                          uint32_t mips, uint32_t layers, bool depth)
    {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = from; b.newLayout = to;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img;
        b.subresourceRange = {depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
                              0, mips, 0, layers};
        VkPipelineStageFlags src = 0, dst = 0;
        if (from == VK_IMAGE_LAYOUT_UNDEFINED && to == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        { src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
          b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; }
        else if (from == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && to == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        { src = VK_PIPELINE_STAGE_TRANSFER_BIT; dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
          b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; }
        else if (to == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        { src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; dst = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
          b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; }
        else if (to == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        { src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; dst = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
          b.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; }
        else if (from == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && to == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        { src = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; dst = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
          b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; }
        else if (from == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && to == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        { src = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
          b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT; }
        else if (from == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && to == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        { src = VK_PIPELINE_STAGE_TRANSFER_BIT; dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
          b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; }
        else { src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; dst = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT; }
        vkCmdPipelineBarrier(c, src, dst, 0, 0, nullptr, 0, nullptr, 1, &b);
    }

    void uploadInitialData(VKTexture &t, const TextureDesc &desc, const void *data)
    {
        VkFormat f = t.format;
        uint32_t bpp = 4;
        if (f == VK_FORMAT_R8_UNORM) bpp = 1;
        else if (f == VK_FORMAT_R16G16B16A16_SFLOAT) bpp = 8;
        else if (f == VK_FORMAT_R32G32_SFLOAT) bpp = 8;
        else if (f == VK_FORMAT_R32G32B32A32_SFLOAT) bpp = 16;
        uint32_t layers = t.cube ? 6 : 1;
        VkDeviceSize face_size = (VkDeviceSize)desc.width * desc.height * bpp;
        VkDeviceSize total = face_size * layers;
        VkBuffer staging; VkDeviceMemory smem;
        createBuffer_(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      staging, smem);
        void *p; vkMapMemory(device_, smem, 0, total, 0, &p);
        if (t.cube)
        {
            const void *const *faces = (const void *const *)data;
            for (uint32_t i = 0; i < 6; ++i) memcpy((uint8_t *)p + i * face_size, faces[i], face_size);
        }
        else memcpy(p, data, face_size);
        vkUnmapMemory(device_, smem);
        oneTimeTransfer([&](VkCommandBuffer c) {
            transitionLayout(c, t.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             desc.mip_levels, layers, t.depth);
            VkBufferImageCopy r{};
            r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers};
            r.imageExtent = {desc.width, desc.height, 1};
            vkCmdCopyBufferToImage(c, staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
            transitionLayout(c, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, desc.mip_levels, layers, false);
            t.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        });
        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, smem, nullptr);
    }

    VkImageView getFaceView(VKTexture *t, uint32_t mip, uint32_t layer)
    {
        if (mip == 0 && (layer == 0 && !t->cube)) return t->view;
        uint64_t key = (uint64_t)mip * 16 + layer;
        auto it = t->face_views.find(key);
        if (it != t->face_views.end()) return it->second;
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = t->image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = t->format;
        vi.subresourceRange = {t->depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
                               mip, 1, layer, 1};
        VkImageView v; vkCreateImageView(device_, &vi, nullptr, &v);
        t->face_views[key] = v;
        return v;
    }
    void transitionColorAttachment(VKTexture *t, VkImageView, uint32_t)
    {
        if (t->layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        {
            oneTimeInFrame([&](VkCommandBuffer c) {
                transitionLayout(c, t->image, t->layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                 t->mips, t->cube ? 6 : 1, t->depth);
            });
            t->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
    }
    void transitionDepthAttachment(VKTexture *t)
    {
        if (t->layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            oneTimeInFrame([&](VkCommandBuffer c) {
                transitionLayout(c, t->image, t->layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                 1, 1, true);
            });
            t->layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }
    }
    void oneTimeInFrame(const std::function<void(VkCommandBuffer)> &fn) { fn(cmd_); }

    VkRenderPass getRenderPass(const RPassKey &key)
    {
        auto it = renderpasses_.find(key);
        if (it != renderpasses_.end()) return it->second;
        std::array<VkAttachmentDescription, 5> att{};
        std::array<VkAttachmentReference, 4> cref{};
        uint32_t ac = 0;
        for (uint32_t i = 0; i < key.color_count; ++i)
        {
            att[i].format = key.color[i];
            att[i].samples = VK_SAMPLE_COUNT_1_BIT;
            att[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            att[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            att[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            att[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            att[i].initialLayout = key.to_present ? VK_IMAGE_LAYOUT_UNDEFINED
                                                  : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            att[i].finalLayout = key.to_present ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                               : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            cref[i] = {i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            ++ac;
        }
        bool has_depth = key.depth != VK_FORMAT_UNDEFINED;
        if (has_depth)
        {
            att[ac].format = key.depth;
            att[ac].samples = VK_SAMPLE_COUNT_1_BIT;
            att[ac].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            att[ac].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            att[ac].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            att[ac].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            att[ac].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            att[ac].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }
        VkAttachmentReference dref{ac, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sp{};
        sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.colorAttachmentCount = key.color_count;
        sp.pColorAttachments = cref.data();
        sp.pDepthStencilAttachment = has_depth ? &dref : nullptr;
        VkSubpassDependency dep{VK_SUBPASS_EXTERNAL, 0,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 0};
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = ac + (has_depth ? 1 : 0);
        rp.pAttachments = att.data();
        rp.subpassCount = 1; rp.pSubpasses = &sp;
        rp.dependencyCount = 1; rp.pDependencies = &dep;
        VkRenderPass v; VK_CHECK(vkCreateRenderPass(device_, &rp, nullptr, &v));
        renderpasses_[key] = v;
        return v;
    }
    VkFramebuffer getFramebuffer(const FBKey &k)
    {
        auto it = framebuffers_.find(k);
        if (it != framebuffers_.end()) return it->second.fb;
        VkImageView att[5];
        uint32_t n = 0;
        for (auto v : k.views) if (v) att[n++] = v;
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = k.rp; fb.attachmentCount = n; fb.pAttachments = att;
        fb.width = k.w; fb.height = k.h; fb.layers = 1;
        VkFramebuffer v; VK_CHECK(vkCreateFramebuffer(device_, &fb, nullptr, &v));
        framebuffers_[k] = {v};
        return v;
    }

    void createDefaultSamplers()
    {
        SamplerDesc lin; linear_sampler_ = createSampler(lin);
        SamplerDesc nr; nr.min_filter = nr.mag_filter = nr.mip_filter = FilterMode::Nearest;
        nearest_sampler_ = createSampler(nr);
        SamplerDesc cm; cm.wrap_u = cm.wrap_v = cm.wrap_w = WrapMode::ClampEdge;
        cube_mip_sampler_ = createSampler(cm);
    }
    void createDefaultTargets()
    {
        TextureDesc c; c.format = Format::R8G8B8A8Unorm; c.width = w_; c.height = h_;
        c.usage = TextureUsage_RenderTarget | TextureUsage_Sampled; c.name = "scene_color";
        scene_color_ = createTexture(c, nullptr);
        TextureDesc d; d.format = Format::D32Float; d.width = w_; d.height = h_;
        d.usage = TextureUsage_DepthStencil; d.name = "scene_depth";
        scene_depth_ = createTexture(d, nullptr);
    }
    void destroyDefaultTargets()
    {
        if (scene_color_.valid()) destroyTexture(scene_color_);
        if (scene_depth_.valid()) destroyTexture(scene_depth_);
    }

    void destroyBufferObj(VKBuffer &b)
    {
        if (b.buffer) vkDestroyBuffer(device_, b.buffer, nullptr);
        if (b.memory) vkFreeMemory(device_, b.memory, nullptr);
    }
    void destroyTextureObj(VKTexture &t)
    {
        for (auto &[k, v] : t.face_views) vkDestroyImageView(device_, v, nullptr);
        if (t.view) vkDestroyImageView(device_, t.view, nullptr);
        if (t.image) vkDestroyImage(device_, t.image, nullptr);
        if (t.memory) vkFreeMemory(device_, t.memory, nullptr);
    }
    void destroyShaderObj(VKShader &s)
    {
        if (s.vert) vkDestroyShaderModule(device_, s.vert, nullptr);
        if (s.frag) vkDestroyShaderModule(device_, s.frag, nullptr);
    }

    static VkFormat toVkFormat(Format f)
    {
        switch (f)
        {
        case Format::R8Unorm: return VK_FORMAT_R8_UNORM;
        case Format::R8G8B8A8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::R8G8B8A8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
        case Format::B8G8R8A8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::B8G8R8A8Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
        case Format::R16G16B16A16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case Format::R32Float: return VK_FORMAT_R32_SFLOAT;
        case Format::R32G32Float: return VK_FORMAT_R32G32_SFLOAT;
        case Format::R32G32B32Float: return VK_FORMAT_R32G32B32_SFLOAT;
        case Format::R32G32B32A32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::D32Float: return VK_FORMAT_D32_SFLOAT;
        case Format::D24UnormS8Uint: return VK_FORMAT_D24_UNORM_S8_UINT;
        default: return VK_FORMAT_R8G8B8A8_UNORM;
        }
    }
    static VkFormat toVkVertexFormat(Format f)
    {
        switch (f)
        {
        case Format::R32Float: return VK_FORMAT_R32_SFLOAT;
        case Format::R32G32Float: return VK_FORMAT_R32G32_SFLOAT;
        case Format::R32G32B32Float: return VK_FORMAT_R32G32B32_SFLOAT;
        case Format::R8G8B8A8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
        default: return VK_FORMAT_R32G32B32A32_SFLOAT;
        }
    }
    static VkCompareOp toVkCompare(CompareOp op)
    {
        return (VkCompareOp)(op == CompareOp::Never ? VK_COMPARE_OP_NEVER
                           : op == CompareOp::Less ? VK_COMPARE_OP_LESS
                           : op == CompareOp::Equal ? VK_COMPARE_OP_EQUAL
                           : op == CompareOp::LessEqual ? VK_COMPARE_OP_LESS_OR_EQUAL
                           : op == CompareOp::Greater ? VK_COMPARE_OP_GREATER
                           : op == CompareOp::NotEqual ? VK_COMPARE_OP_NOT_EQUAL
                           : op == CompareOp::GreaterEqual ? VK_COMPARE_OP_GREATER_OR_EQUAL
                           : VK_COMPARE_OP_ALWAYS);
    }
    static VkSamplerAddressMode toWrap(WrapMode w)
    {
        return w == WrapMode::ClampEdge ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
             : w == WrapMode::MirroredRepeat ? VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT
             : VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }

private:
    static constexpr size_t kMax = 1024;
    std::array<VKBuffer, kMax> buffers_{};
    std::array<VKTexture, kMax> textures_{};
    std::array<VKSampler, 64> samplers_{};
    std::array<VKShader, 128> shaders_{};
    std::array<VKPipeline, 128> pipelines_{};
    std::array<VKSet, 512> sets_{};
    std::unordered_map<RPassKey, VkRenderPass, RPKeyHash> renderpasses_;
    std::unordered_map<FBKey, FBValue, FBHash> framebuffers_;

    SDL_Window *window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t gfx_family_ = 0;
    bool debug_ = false;
    DeviceCaps caps_;
    uint32_t w_ = 1, h_ = 1;

    VkCommandPool transfer_pool_ = VK_NULL_HANDLE;
    VkCommandPool frame_pool_[kFramesInFlight]{};
    VkCommandBuffer cmds_[kFramesInFlight]{};
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_[2]{};
    VkDescriptorSetLayout imgui_ds_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VKPipeline *cur_pipeline_ = nullptr;

    VkSemaphore image_avail_[kFramesInFlight]{}, render_finished_[kFramesInFlight]{};
    VkFence in_flight_[kFramesInFlight]{};
    uint32_t cur_frame_ = 0, image_index_ = 0;
    bool frame_open_ = false, framebuffer_resized_ = false;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> present_framebuffers_;
    VkSurfaceFormatKHR surface_format_{};
    VkExtent2D extent_{1, 1};
    VkRenderPass present_renderpass_ = VK_NULL_HANDLE;

    TextureHandle scene_color_, scene_depth_;
    SamplerHandle linear_sampler_, nearest_sampler_, cube_mip_sampler_;
    bool imgui_ready_ = false;
};
} // namespace

IRHIDevice *createVKRhi() { return new VKRhi(); }
} // namespace Genesis
