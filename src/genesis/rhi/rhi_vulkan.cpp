/*
 * Genesis Render - Vulkan RHI backend (Vulkan 1.0+ / MoltenVK, VMA).
 *
 * Windowed : SDL VkSurfaceKHR + swapchain.
 * Headless: no surface, renders into an offscreen RGBA8 screen texture that
 *           readTextureRGBA8() can pull for CI screenshots.
 */
#define VMA_IMPLEMENTATION
#include "external/vk_mem_alloc.h"

#include "rhi/rhi.hpp"
#include "rhi/half.hpp"
#include "foundation_layer/log.hpp"
#include "foundation_layer/assert.hpp"

#include <SDL.h>
#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>
#include <cstdlib>

namespace Genesis {

static constexpr int MAX_FLIGHT = 2;

template <typename T> struct VkPtrHolder { T v; T* operator&() { return &v; } };
#define VKARGS(T, ...) (&VkPtrHolder<T>{T{__VA_ARGS__}})

#define VK_CHECK(expr) do { VkResult _r = (expr); GENESISASSERT_MSG(_r == VK_SUCCESS, "vk failed %d", (int)_r); } while (0)

// ---------------------------------------------------------------------------
// Format mapping
// ---------------------------------------------------------------------------
static VkFormat vkFormat(RHIFormat f) {
    switch (f) {
    case RHIFormat::R8: return VK_FORMAT_R8_UNORM;
    case RHIFormat::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
    case RHIFormat::BGRA8: return VK_FORMAT_B8G8R8A8_UNORM;
    case RHIFormat::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
    case RHIFormat::RG16F: return VK_FORMAT_R16G16_SFLOAT;
    case RHIFormat::RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case RHIFormat::R32F: return VK_FORMAT_R32_SFLOAT;
    case RHIFormat::RG32F: return VK_FORMAT_R32G32_SFLOAT;
    case RHIFormat::D32F: return VK_FORMAT_D32_SFLOAT;
    default: return VK_FORMAT_UNDEFINED;
    }
}
static VkFormat attribFormat(RHIAttrib a) {
    switch (a) {
    case RHIAttrib::Float: return VK_FORMAT_R32_SFLOAT;
    case RHIAttrib::Float2: return VK_FORMAT_R32G32_SFLOAT;
    case RHIAttrib::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
    case RHIAttrib::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}
static bool isDepthFmt(RHIFormat f) { return f == RHIFormat::D32F; }

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------
struct VKBuffer : IBuffer {
    VkBuffer buf = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    void* mappedPtr = nullptr;
    size_t bytes = 0;
    bool host = false;
    size_t size() const override { return bytes; }
    void* mapped() override { return mappedPtr; }
    void upload(size_t off, size_t sz, const void* d) override;
};

struct VKTexture : ITexture {
    VkImage img = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkImageView fullView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    TextureDesc d;
    bool depth = false;
    bool cube = false;
    std::map<std::pair<uint32_t,uint32_t>, VkImageView> views; // (mip,layer)

    const TextureDesc& desc() const override { return d; }
    VkImageView viewFor(uint32_t mip, uint32_t layer, bool depthAspect);
    void uploadRGBA8(const void*) override;
    void uploadRGBA16F(const void*, uint32_t mip=0) override;
    void uploadRGBA32F(const void*) override;
};

struct VKSetLayout : IBindingSetLayout {
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    std::vector<BindingDecl> decls;
};

struct VKBindingSet : IBindingSet {
    struct Slot { VKBuffer* buf = nullptr; size_t offset = 0, range = 0;
                  VKTexture* tex = nullptr; bool dynamic = false; };
    std::map<uint32_t, Slot> slots;
    VKSetLayout* l = nullptr;
    VkDescriptorSet persistentSet = VK_NULL_HANDLE;
    void setBuffer(uint32_t b, IBuffer* buf, size_t off, size_t range) override {
        auto& s = slots[b];
        s.buf = (VKBuffer*)buf; s.offset = off;
        s.range = range ? range : (buf ? (size_t)buf->size() - off : 0);
    }
    void setTexture(uint32_t b, ITexture* t) override { slots[b].tex = (VKTexture*)t; }
};

struct VKProgram : IShaderProgram {
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    std::string name;
};

struct VKPipeline : IPipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VKSetLayout* setLayout = nullptr;
};

struct VKFBO : IFrameBuffer {
    VkFramebuffer fb = VK_NULL_HANDLE;
    uint32_t w = 0, h = 0;
    std::vector<VkImageView> ownedViews;
    VkImage depthImg = VK_NULL_HANDLE;
    VmaAllocation depthAlloc = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    bool present = false;
    bool hasDepth = false;
    std::vector<RHIFormat> colorFmts;
    ~VKFBO() = default;
    uint32_t width() const override { return w; }
    uint32_t height() const override { return h; }
};

class VKDevice;
class VKRenderContext : public IRenderContext {
public:
    VKDevice* dev = nullptr;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VKPipeline* pipe = nullptr;
    void setViewport(uint32_t w, uint32_t h) override;
    void beginPass(IFrameBuffer*, const ClearValue& c, bool cc, bool cd) override;
    void endPass() override;
    void bindPipeline(IPipeline*) override;
    void bindSet(IBindingSet*, const uint32_t*, uint32_t) override;
    void bindVertexBuffer(IBuffer*, uint32_t, size_t) override;
    void bindIndexBuffer(IBuffer*, RHIIndexType, size_t) override;
    void draw(uint32_t, uint32_t, uint32_t) override;
    void drawIndexed(uint32_t, uint32_t, int32_t, uint32_t) override;
};

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------
class VKDevice : public IDevice {
public:
    bool init(const DeviceInitInfo&) override;
    void shutdown() override;
    void waitIdle() override { vkDeviceWaitIdle(device); }

    RHIBackend backend() const override { return RHIBackend::Vulkan; }
    const BackendInfo& info() const override { return binfo; }
    bool isHeadless() const override { return headless; }
    bool vulkanClip() const override { return true; }

    bool beginFrame() override;
    IRenderContext* context() override { return &rc; }
    IFrameBuffer* currentScreenFB() override;
    ITexture* screenColorTexture() override { return (ITexture*)screenTex; }
    void endFrame() override;
    void onResize(uint32_t, uint32_t) override;

    IBuffer* createBuffer(const BufferDesc&, const void*) override;
    ITexture* createTexture(const TextureDesc&) override;
    IBindingSetLayout* createBindingSetLayout(const std::vector<BindingDecl>&) override;
    IBindingSet* createBindingSet(IBindingSetLayout*) override;
    IShaderProgram* createShaderProgram(const std::string&, const std::string&, const std::string&,
                                        const std::vector<uint32_t>&, const std::vector<uint32_t>&) override;
    IPipeline* createPipeline(const PipelineDesc&) override;
    IFrameBuffer* createFrameBuffer(const FrameBufferDesc&) override;
    void generateMipmaps(ITexture*) override;
    std::vector<uint8_t> readTextureRGBA8(ITexture*, uint32_t, int32_t = -1) override;
    void* nativeHandle(const char*) override;

    // internal helpers
    VkCommandBuffer beginOneTime();
    void endOneTime(VkCommandBuffer);
    void transitionColor(VkCommandBuffer, VKTexture*, VkImageLayout from, VkImageLayout to,
                         uint32_t mipCount = VK_REMAINING_MIP_LEVELS);
    void flushRenderTargetImage(VKTexture*);
    void textureSamplingBarrier(ITexture*) override;
    void immediateGraphics(const std::function<void(void*)>& fn) override {
        VkCommandBuffer c = beginOneTime();
        fn(c);
        endOneTime(c);
    }
    RHIFormat screenFormat() const override {
        if (headless) return RHIFormat::RGBA8;
        return (swapFmt == VK_FORMAT_R8G8B8A8_UNORM || swapFmt == VK_FORMAT_R8G8B8A8_SRGB)
                   ? RHIFormat::RGBA8 : RHIFormat::BGRA8;
    }
    VkRenderPass getOrCreateRP(const std::vector<RHIFormat>& colors, RHIFormat depth, bool present, bool load = false);
    void blitToSwapchain(ITexture*) override;
    void copyLayerToCube(ITexture* src, ITexture* dstCube, uint32_t size,
                         uint32_t mip, uint32_t layer) override;
    VkDescriptorSet acquireFrameSet(VKBindingSet*);
    void destroyFBO(VKFBO* f);

public:
    DeviceInitInfo cfg;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice gpu = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VmaAllocator vma = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties gpuProps{};

    bool headless = false;
    SDL_Window* window = nullptr;
    uint32_t fbWidth = 1, fbHeight = 1;

    // swapchain (windowed)
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapFmt = VK_FORMAT_B8G8R8A8_UNORM;
    std::vector<VkImage> swapImgs;
    std::vector<VkImageView> swapViews;
    std::vector<VKFBO*> swapFbos;
    VkRenderPass swapRP = VK_NULL_HANDLE;
    bool swapOutdated = false;

    // headless screen
    VKTexture* screenTex = nullptr;
    VKFBO* screenFbo = nullptr;

    // per-frame
    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore imgAvail = VK_NULL_HANDLE;
        VkSemaphore renderDone = VK_NULL_HANDLE;
        VkDescriptorPool descPool = VK_NULL_HANDLE;
        std::unordered_map<VKBindingSet*, VkDescriptorSet> sets;
    };
    std::array<Frame, MAX_FLIGHT> frames;
    uint32_t frameIdx = 0;
    uint32_t imageIdx = 0;

    VkDescriptorPool imguiPool = VK_NULL_HANDLE;
    VkDescriptorPool persistentPool = VK_NULL_HANDLE;
    VkCommandPool oneTimePool = VK_NULL_HANDLE;

    VKRenderContext rc;
    BackendInfo binfo;
    uint32_t uboAlign = 16;

    std::map<uint64_t, VkRenderPass> rpCache;
    bool debugUtils = false;

    // creation helpers
    void createInstance();
    void pickDevice();
    void createLogicalDevice();
    void createSwapchainResources();
    void destroySwapchainResources();
    VkCommandBuffer currentCmd() { return frames[frameIdx].cmd; }
};

class VKDevice;
static VKDevice* g_vkdev = nullptr;

// ---------------------------------------------------------------------------
// VKBuffer upload
// ---------------------------------------------------------------------------
void VKBuffer::upload(size_t off, size_t sz, const void* d) {
    if (host && mappedPtr) {
        memcpy((uint8_t*)mappedPtr + off, d, sz);
        // HOST_COHERENT chosen for these allocations
        return;
    }
    // staging copy via VMA is handled by the device for device-local buffers;
    // engine only uploads via host-visible buffers currently.
    GenesisPrint("[VK] upload on non-host buffer unsupported\n");
}

VkImageView VKTexture::viewFor(uint32_t mip, uint32_t layer, bool depthAspect) {
    auto key = std::make_pair(mip, layer);
    auto it = views.find(key);
    if (it != views.end()) return it->second;
    VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ci.image = img;
    ci.viewType = VK_IMAGE_VIEW_TYPE_2D; // single face/layer view
    ci.format = vkFormat(d.format);
    ci.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                     VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    ci.subresourceRange.aspectMask = depthAspect ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                 : VK_IMAGE_ASPECT_COLOR_BIT;
    ci.subresourceRange.baseMipLevel = mip;
    ci.subresourceRange.levelCount = 1;
    ci.subresourceRange.baseArrayLayer = layer;
    ci.subresourceRange.layerCount = 1;
    VkImageView v;
    vkCreateImageView(g_vkdev->device, &ci, nullptr, &v);
    views[key] = v;
    return v;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
bool VKDevice::init(const DeviceInitInfo& info) {
    cfg = info;
    headless = info.headless;
    window = (SDL_Window*)info.window_handle;
    fbWidth = info.width; fbHeight = info.height;
    g_vkdev = this;
    rc.dev = this;

    createInstance();
    pickDevice();
    createLogicalDevice();

    VmaVulkanFunctions vf{};
    vf.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vf.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo ai{};
    ai.physicalDevice = gpu;
    ai.device = device;
    ai.instance = instance;
    ai.vulkanApiVersion = VK_API_VERSION_1_0;
    ai.pVulkanFunctions = &vf;
    vmaCreateAllocator(&ai, &vma);

    if (!headless) createSwapchainResources();
    else {
        // offscreen screen target
        TextureDesc td;
        td.width = fbWidth; td.height = fbHeight;
        td.format = RHIFormat::RGBA8;
        td.usage = TU_RenderTarget | TU_Sampled;
        td.filter_min = td.filter_mag = RHIFilter::Nearest;
        td.wrap = RHIWrap::ClampEdge;
        screenTex = (VKTexture*)createTexture(td);
        FrameBufferDesc fd;
        fd.width = fbWidth; fd.height = fbHeight;
        fd.colors.push_back({screenTex, 0, 0});
        screenFbo = (VKFBO*)createFrameBuffer(fd);
    }

    vkCreateCommandPool(device,
        VKARGS(VkCommandPoolCreateInfo, VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr, 0, queueFamily),
        nullptr, &oneTimePool);

    for (auto& f : frames) {
        vkCreateCommandPool(device,
            VKARGS(VkCommandPoolCreateInfo, VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr,
                                       VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, queueFamily),
            nullptr, &f.pool);
        VkCommandBufferAllocateInfo ab{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ab.commandPool = f.pool; ab.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ab.commandBufferCount = 1;
        vkAllocateCommandBuffers(device, &ab, &f.cmd);
        vkCreateFence(device, VKARGS(VkFenceCreateInfo, VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr,
                                                    VK_FENCE_CREATE_SIGNALED_BIT), nullptr, &f.fence);
        if (!headless) {
            vkCreateSemaphore(device, VKARGS(VkSemaphoreCreateInfo, VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO),
                              nullptr, &f.imgAvail);
            vkCreateSemaphore(device, VKARGS(VkSemaphoreCreateInfo, VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO),
                              nullptr, &f.renderDone);
        }
        VkDescriptorPoolSize ps[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 512},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 512},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096},
        };
        vkCreateDescriptorPool(device,
            VKARGS(VkDescriptorPoolCreateInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr,
                VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, 4096, 3, ps), nullptr, &f.descPool);
    }

    // persistent pool for ImGui
    {
        VkDescriptorPoolSize ps[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2000}};
        vkCreateDescriptorPool(device,
            VKARGS(VkDescriptorPoolCreateInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr,
                VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, 2000, 1, ps), nullptr, &imguiPool);
    }
    {
        VkDescriptorPoolSize pps[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,4096},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,4096},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,4096}};
        vkCreateDescriptorPool(device,
            VKARGS(VkDescriptorPoolCreateInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr,
                VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, 4096, 3, pps), nullptr, &persistentPool);
    }

    binfo.backend = RHIBackend::Vulkan;
    binfo.name = "Vulkan";
    binfo.device = gpuProps.deviceName;
    char v[64]; snprintf(v, sizeof v, "Vulkan %u.%u.%u",
                         VK_API_VERSION_MAJOR(gpuProps.apiVersion),
                         VK_API_VERSION_MINOR(gpuProps.apiVersion),
                         VK_API_VERSION_PATCH(gpuProps.apiVersion));
    binfo.version = v;
    return true;
}

void VKDevice::createInstance() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Genesis";
    app.apiVersion = VK_API_VERSION_1_0;

    std::vector<const char*> instExt;
    std::vector<const char*> layers;
#ifdef __APPLE__
    instExt.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    instExt.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#endif
    if (!headless && window) {
        unsigned int n = 0;
        SDL_Vulkan_GetInstanceExtensions(window, &n, nullptr);
        size_t base = instExt.size();
        instExt.resize(base + n);
        SDL_Vulkan_GetInstanceExtensions(window, &n, instExt.data() + base);
    }
    if (cfg.validation) layers.push_back("VK_LAYER_KHRONOS_validation");

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
#ifdef __APPLE__
    ci.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    ci.enabledExtensionCount = (uint32_t)instExt.size();
    ci.ppEnabledExtensionNames = instExt.data();
    ci.enabledLayerCount = (uint32_t)layers.size();
    ci.ppEnabledLayerNames = layers.data();
    VK_CHECK(vkCreateInstance(&ci, nullptr, &instance));
}

void VKDevice::pickDevice() {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance, &n, nullptr);
    GENESISASSERT_MSG(n > 0, "no vulkan device");
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(instance, &n, devs.data());
    // prefer discrete, else first
    gpu = devs[0];
    for (auto d : devs) {
        VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(d, &p);
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { gpu = d; break; }
    }
    vkGetPhysicalDeviceProperties(gpu, &gpuProps);
    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qn, qf.data());
    queueFamily = UINT32_MAX;
    for (uint32_t i = 0; i < qn; ++i)
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { queueFamily = i; break; }
    GENESISASSERT_MSG(queueFamily != UINT32_MAX, "no graphics queue");

    uboAlign = gpuProps.limits.minUniformBufferOffsetAlignment;
}

void VKDevice::createLogicalDevice() {
    float prio = 1.0f;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = queueFamily; q.queueCount = 1; q.pQueuePriorities = &prio;

    std::vector<const char*> ext;
    if (!headless) ext.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
#ifdef __APPLE__
    ext.push_back("VK_KHR_portability_subset");
#endif
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    ci.queueCreateInfoCount = 1; ci.pQueueCreateInfos = &q;
    ci.enabledExtensionCount = (uint32_t)ext.size();
    ci.ppEnabledExtensionNames = ext.data();
    VkResult _cr = vkCreateDevice(gpu, &ci, nullptr, &device);
    VK_CHECK(_cr);
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    if (!headless) {
        SDL_Vulkan_CreateSurface(window, instance, &surface);
    }
}

// ---------------------------------------------------------------------------
// Swapchain
// ---------------------------------------------------------------------------
void VKDevice::createSwapchainResources() {
    VkSurfaceCapabilitiesKHR cap{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &cap);
    uint32_t fn = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &fn, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fn);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &fn, fmts.data());
    VkSurfaceFormatKHR sf = fmts[0];
    for (auto& f : fmts)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_SRGB) { sf = f; break; }
    swapFmt = sf.format;

    VkExtent2D ext = cap.currentExtent;
    if (ext.width == UINT32_MAX) {
        int w = 0, h = 0;
        SDL_Vulkan_GetDrawableSize(window, &w, &h);
        ext.width = std::clamp((uint32_t)w, cap.minImageExtent.width, cap.maxImageExtent.width);
        ext.height = std::clamp((uint32_t)h, cap.minImageExtent.height, cap.maxImageExtent.height);
    }
    fbWidth = ext.width; fbHeight = ext.height;
    uint32_t imgCount = cap.minImageCount + 1;
    if (cap.maxImageCount && imgCount > cap.maxImageCount) imgCount = cap.maxImageCount;

    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface; ci.minImageCount = imgCount;
    ci.imageFormat = sf.format; ci.imageColorSpace = sf.colorSpace;
    ci.imageExtent = ext; ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ci.preTransform = cap.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;
    VK_CHECK(vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain));

    vkGetSwapchainImagesKHR(device, swapchain, &imgCount, nullptr);
    swapImgs.resize(imgCount); swapViews.resize(imgCount); swapFbos.resize(imgCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imgCount, swapImgs.data());
    for (uint32_t i = 0; i < imgCount; ++i) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = swapImgs[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = swapFmt;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device, &vi, nullptr, &swapViews[i]);

        auto* f = new VKFBO();
        f->w = fbWidth; f->h = fbHeight; f->present = true;
        VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fci.renderPass = getOrCreateRP({RHIFormat::RGBA8}, RHIFormat::Unknown, true);
        // ensure swap RP exists with matching concrete format:
        fci.renderPass = swapRP;
        fci.attachmentCount = 1; fci.pAttachments = &swapViews[i];
        fci.width = fbWidth; fci.height = fbHeight; fci.layers = 1;
        vkCreateFramebuffer(device, &fci, nullptr, &f->fb);
        f->ownedViews.push_back(swapViews[i]);
        swapFbos[i] = f;
    }
}

void VKDevice::destroySwapchainResources() {
    vkDeviceWaitIdle(device);
    for (auto* f : swapFbos) { vkDestroyFramebuffer(device, f->fb, nullptr); delete f; }
    swapFbos.clear(); swapViews.clear(); swapImgs.clear();
    if (swapRP) { vkDestroyRenderPass(device, swapRP, nullptr); swapRP = VK_NULL_HANDLE; }
    if (swapchain) { vkDestroySwapchainKHR(device, swapchain, nullptr); swapchain = VK_NULL_HANDLE; }
}

void VKDevice::onResize(uint32_t, uint32_t) {
    if (headless) return;
    swapOutdated = true;
}

// ---------------------------------------------------------------------------
// Render passes
// ---------------------------------------------------------------------------
VkRenderPass VKDevice::getOrCreateRP(const std::vector<RHIFormat>& colors, RHIFormat depthFmt,
                                     bool present, bool load) {
    uint64_t key = present ? 0xF00DF00DULL : 1;
    for (auto c : colors) key = key * 131 + (uint64_t)c;
    key = key * 131 + (uint64_t)depthFmt;
    key = key * 3 + (load ? 1 : 0);
    auto it = rpCache.find(key);
    if (it != rpCache.end()) return it->second;

    std::vector<VkAttachmentDescription> atts;
    std::vector<VkAttachmentReference> colRefs;
    VkAttachmentReference depthRef{};
    bool hasDepth = depthFmt != RHIFormat::Unknown;

    for (size_t i = 0; i < colors.size(); ++i) {
        VkAttachmentDescription a{};
        a.format = present ? swapFmt : vkFormat(colors[i]);
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        a.initialLayout = load
            ? (present ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                       : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            : VK_IMAGE_LAYOUT_UNDEFINED;
        a.finalLayout = present ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        atts.push_back(a);
        colRefs.push_back({(uint32_t)i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    }
    if (hasDepth) {
        VkAttachmentDescription a{};
        a.format = vkFormat(depthFmt);
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        a.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = load ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                               : VK_IMAGE_LAYOUT_UNDEFINED;
        a.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        atts.push_back(a);
        depthRef = {(uint32_t)colors.size(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    }
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = (uint32_t)colRefs.size();
    sub.pColorAttachments = colRefs.data();
    sub.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                     | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                     | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = (uint32_t)atts.size();
    rpci.pAttachments = atts.data();
    rpci.subpassCount = 1; rpci.pSubpasses = &sub;
    rpci.dependencyCount = 1; rpci.pDependencies = &dep;
    VkRenderPass rp;
    VK_CHECK(vkCreateRenderPass(device, &rpci, nullptr, &rp));

    if (present) swapRP = rp;
    rpCache[key] = rp;
    return rp;
}

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------
IBuffer* VKDevice::createBuffer(const BufferDesc& d, const void* data) {
    auto* b = new VKBuffer();
    b->bytes = d.size; b->host = d.host_visible;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = d.size;
    if (d.usage & BU_Vertex) bi.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (d.usage & BU_Index) bi.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (d.usage & BU_Uniform) bi.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    VmaAllocationCreateInfo ai{};
    if (d.host_visible) {
        ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        ai.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        ai.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    } else {
        ai.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
    VmaAllocationInfo info{};
    VK_CHECK(vmaCreateBuffer(vma, &bi, &ai, &b->buf, &b->alloc, &info));
    b->mappedPtr = info.pMappedData;
    if (data) {
        if (d.host_visible) memcpy(b->mappedPtr ? b->mappedPtr : info.pMappedData, data, d.size);
    }
    return b;
}

static VkFilter vkFilter(RHIFilter f) { return f == RHIFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR; }
static VkSamplerAddressMode vkWrap(RHIWrap w) {
    return w == RHIWrap::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT
         : w == RHIWrap::ClampBorder ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
         : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

ITexture* VKDevice::createTexture(const TextureDesc& d) {
    auto* t = new VKTexture();
    t->d = d;
    t->cube = (d.usage & TU_Cube) != 0;
    t->depth = isDepthFmt(d.format);

    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    if (t->cube) ii.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.extent = {d.width, d.height, 1};
    ii.mipLevels = d.mips;
    ii.arrayLayers = t->cube ? 6 : d.layers;
    ii.format = vkFormat(d.format);
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (d.usage & TU_RenderTarget) ii.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (d.usage & TU_Depth || t->depth) ii.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    VmaAllocationCreateInfo ai{};
    ai.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VK_CHECK(vmaCreateImage(vma, &ii, &ai, &t->img, &t->alloc, nullptr));

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = t->img;
    vi.viewType = t->cube ? VK_IMAGE_VIEW_TYPE_CUBE
               : (d.layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
    vi.format = ii.format;
    vi.subresourceRange.aspectMask = t->depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.baseMipLevel = 0;
    vi.subresourceRange.levelCount = d.mips;
    vi.subresourceRange.baseArrayLayer = 0;
    vi.subresourceRange.layerCount = t->cube ? 6 : d.layers;
    VK_CHECK(vkCreateImageView(device, &vi, nullptr, &t->fullView));

    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = vkFilter(d.filter_mag);
    si.minFilter = vkFilter(d.filter_min);
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = si.addressModeV = vkWrap(d.wrap);
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.minLod = 0; si.maxLod = (float)d.mips;
    si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    VK_CHECK(vkCreateSampler(device, &si, nullptr, &t->sampler));
    return t;
}

IBindingSetLayout* VKDevice::createBindingSetLayout(const std::vector<BindingDecl>& decls) {
    auto* l = new VKSetLayout();
    l->decls = decls;
    std::vector<VkDescriptorSetLayoutBinding> bs;
    for (auto& d : decls) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = d.binding;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        switch (d.type) {
        case BindingType::DynamicUBO: b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; break;
        case BindingType::UBO: b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; break;
        case BindingType::Texture2D:
        case BindingType::TextureCube: b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; break;
        }
        bs.push_back(b);
    }
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.bindingCount = (uint32_t)bs.size(); ci.pBindings = bs.data();
    VK_CHECK(vkCreateDescriptorSetLayout(device, &ci, nullptr, &l->layout));
    return l;
}

IBindingSet* VKDevice::createBindingSet(IBindingSetLayout* l) {
    auto* s = new VKBindingSet();
    s->l = (VKSetLayout*)l;
    if (l) for (auto& d : s->l->decls)
        if (d.type == BindingType::DynamicUBO) s->slots[d.binding].dynamic = true;
    return s;
}

IShaderProgram* VKDevice::createShaderProgram(const std::string& name, const std::string&, const std::string&,
                                              const std::vector<uint32_t>& vsSpv,
                                              const std::vector<uint32_t>& fsSpv) {
    auto* p = new VKProgram(); p->name = name;
    auto mk = [&](const std::vector<uint32_t>& spv) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = spv.size() * 4; ci.pCode = spv.data();
        VkShaderModule m; VK_CHECK(vkCreateShaderModule(device, &ci, nullptr, &m));
        return m;
    };
    p->vs = mk(vsSpv); p->fs = mk(fsSpv);
    return p;
}

IPipeline* VKDevice::createPipeline(const PipelineDesc& d) {
    auto* p = new VKPipeline();
    p->setLayout = (VKSetLayout*)d.binding_layout;
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    if (p->setLayout) { plci.setLayoutCount = 1; plci.pSetLayouts = &p->setLayout->layout; }
    VK_CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &p->layout));

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = ((VKProgram*)d.program)->vs;
    stages[0].pName = "main";
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = ((VKProgram*)d.program)->fs;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bind{};
    bind.binding = 0; bind.stride = d.vertex_layout.stride;
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::vector<VkVertexInputAttributeDescription> attrs;
    for (auto& a : d.vertex_layout.attribs)
        attrs.push_back({a.location, 0, attribFormat(a.type), a.offset});
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = d.vertex_layout.stride ? 1u : 0u;
    vi.pVertexBindingDescriptions = d.vertex_layout.stride ? &bind : nullptr;
    vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
    vi.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = d.topology == RHITopology::Lines ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
               : d.topology == RHITopology::TriangleStrip ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
               : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = d.cull == RHICull::None ? VK_CULL_MODE_NONE
                 : d.cull == RHICull::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState ba{};
    ba.colorWriteMask = 0xF;
    ba.blendEnable = d.blend_alpha ? VK_TRUE : VK_FALSE;
    if (d.blend_alpha) {
        ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ba.colorBlendOp = VK_BLEND_OP_ADD;
        ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        ba.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    std::vector<VkPipelineColorBlendAttachmentState> bas(d.color_formats.size(), ba);
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = (uint32_t)bas.size(); cb.pAttachments = bas.data();

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = d.depth_test ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = d.depth_write ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = d.depth_func == RHICompare::LEqual ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;

    VkDynamicState dyns[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyns;

    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.stageCount = 2; ci.pStages = stages;
    ci.pVertexInputState = &vi; ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp; ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms; ci.pColorBlendState = &cb;
    ci.pDepthStencilState = d.depth_format == RHIFormat::Unknown ? nullptr : &ds;
    ci.pDynamicState = &dyn;
    ci.layout = p->layout;
    ci.renderPass = getOrCreateRP(d.color_formats, d.depth_format, false);
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &p->pipeline));
    return p;
}

IFrameBuffer* VKDevice::createFrameBuffer(const FrameBufferDesc& d) {
    auto* f = new VKFBO();
    f->w = d.width; f->h = d.height;
    std::vector<VkImageView> atts;
    std::vector<RHIFormat> colorFmts;
    for (auto& ca : d.colors) {
        auto* t = (VKTexture*)ca.texture;
        VkImageView v = t->viewFor(ca.mip, ca.layer, false);
        atts.push_back(v); f->ownedViews.push_back(v);
        colorFmts.push_back(t->d.format);
    }
    f->colorFmts = colorFmts;
    RHIFormat depthFmt = RHIFormat::Unknown;
    if (d.depth) {
        auto* t = (VKTexture*)d.depth;
        atts.push_back(t->fullView);
        depthFmt = t->d.format;
        f->hasDepth = true;
    } else if (!d.colors.empty()) {
        // own depth image sized to this target (matches GL backend behavior)
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D; ii.extent = {d.width, d.height, 1};
        ii.mipLevels = 1; ii.arrayLayers = 1; ii.format = VK_FORMAT_D32_SFLOAT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        VmaAllocationCreateInfo ai{}; ai.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        vmaCreateImage(vma, &ii, &ai, &f->depthImg, &f->depthAlloc, nullptr);
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = f->depthImg; vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_D32_SFLOAT;
        vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        vkCreateImageView(device, &vi, nullptr, &f->depthView);
        atts.push_back(f->depthView);
        depthFmt = RHIFormat::D32F;
        f->hasDepth = true;
    }
    VkRenderPass rp = getOrCreateRP(colorFmts, depthFmt, false);
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = rp; fci.attachmentCount = (uint32_t)atts.size();
    fci.pAttachments = atts.data(); fci.width = d.width; fci.height = d.height; fci.layers = 1;
    VK_CHECK(vkCreateFramebuffer(device, &fci, nullptr, &f->fb));
    return f;
}

// ---------------------------------------------------------------------------
// Uploads / mips / barriers
// ---------------------------------------------------------------------------
VkCommandBuffer VKDevice::beginOneTime() {
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo ab2{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ab2.commandPool = oneTimePool; ab2.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ab2.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &ab2, &cmd);
    vkBeginCommandBuffer(cmd, VKARGS(VkCommandBufferBeginInfo, VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                                         nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));
    return cmd;
}
void VKDevice::endOneTime(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    vkQueueSubmit(queue, 1, VKARGS(VkSubmitInfo, VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 0, nullptr,
                                           nullptr, 1, &cmd, 0, nullptr), VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, oneTimePool, 1, &cmd);
}

void VKDevice::transitionColor(VkCommandBuffer cmd, VKTexture* t, VkImageLayout from, VkImageLayout to,
                               uint32_t mipCount) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = from; b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = t->img;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount = mipCount;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount = t->cube ? 6 : 1;
    auto stageOf = [](VkImageLayout l) {
        if (l == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) return VK_PIPELINE_STAGE_TRANSFER_BIT;
        if (l == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        if (l == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    };
    auto accOf = [](VkImageLayout l) -> VkAccessFlags {
        if (l == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) return VK_ACCESS_TRANSFER_WRITE_BIT;
        if (l == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) return VK_ACCESS_SHADER_READ_BIT;
        if (l == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        return 0u;
    };
    b.srcAccessMask = accOf(from); b.dstAccessMask = accOf(to);
    vkCmdPipelineBarrier(cmd, stageOf(from), stageOf(to), 0, 0, nullptr, 0, nullptr, 1, &b);
}

void VKDevice::flushRenderTargetImage(VKTexture*) {}

void VKDevice::blitToSwapchain(ITexture*) { /* final draws target the swapchain directly */ }

void VKDevice::copyLayerToCube(ITexture* isrc, ITexture* idst, uint32_t size,
                               uint32_t mip, uint32_t layer) {
    auto* src = (VKTexture*)isrc; auto* dst = (VKTexture*)idst;
    VkCommandBuffer cmd = beginOneTime();
    transitionColor(cmd, src, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1);
    VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0,
                                VK_REMAINING_ARRAY_LAYERS};
    VkImageMemoryBarrier dstBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, 0, 0, 0,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, dst->img, all};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &dstBarrier);
    VkImageCopy region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1};
    region.extent = {size, size, 1};
    vkCmdCopyImage(cmd, src->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dst->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    VkImageMemoryBarrier readBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, 0,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, dst->img, all};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &readBarrier);
    transitionColor(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
    endOneTime(cmd);
}

void VKDevice::textureSamplingBarrier(ITexture*) {
    // Passes finish with the attachment already in SHADER_READ_ONLY (render
    // pass finalLayout); only a cross-pass execution+memory dependency is
    // needed between baking a mip level and sampling it in the next one.
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(frames[frameIdx].cmd,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 1, &mb, 0, nullptr, 0, nullptr);
}

void VKTexture::uploadRGBA8(const void* px) {
    auto* dev = g_vkdev;
    uint32_t layers = cube ? 6u : (uint32_t)d.layers;
    VkDeviceSize sz = (VkDeviceSize)d.width * d.height * 4;
    VkBuffer staging; VmaAllocation sa;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = sz; bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo ai{};
    ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    ai.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VmaAllocationInfo info{};
    vmaCreateBuffer(dev->vma, &bi, &ai, &staging, &sa, &info);
    memcpy(info.pMappedData, px, (size_t)sz);
    VkCommandBuffer cmd = dev->beginOneTime();
    // every mip -> TRANSFER_DST
    VkImageMemoryBarrier all{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    all.image = img;
    all.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, d.mips, 0, layers};
    all.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    all.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    all.srcQueueFamilyIndex = all.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    all.srcAccessMask = 0; all.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &all);
    VkBufferImageCopy r{};
    r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers};
    r.imageExtent = {d.width, d.height, 1};
    vkCmdCopyBufferToImage(cmd, staging, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    // mip0 -> TRANSFER_SRC
    VkImageMemoryBarrier b = all;
    b.subresourceRange.levelCount = 1;
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
    int32_t pw = d.width, ph = d.height;
    for (uint32_t m = 1; m < d.mips; ++m) {
        int32_t w = std::max(1, pw / 2), h = std::max(1, ph / 2);
        VkImageBlit bl{};
        bl.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 0, layers};
        bl.srcOffsets[1] = {pw, ph, 1};
        bl.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, layers};
        bl.dstOffsets[1] = {w, h, 1};
        vkCmdBlitImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, img,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_LINEAR);
        // m-1 src -> SAMPLED
        b.subresourceRange.baseMipLevel = m - 1;
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
        // m dst -> SRC
        b.subresourceRange.baseMipLevel = m;
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b);
        pw = w; ph = h;
    }
    // all mips -> SAMPLED (untouched higher mips are ignored by final layout)
    VkImageMemoryBarrier fin = all;
    fin.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    fin.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    fin.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    fin.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &fin);
    dev->endOneTime(cmd);
    vmaDestroyBuffer(dev->vma, staging, sa);
}
void VKTexture::uploadRGBA16F(const void* pixels, uint32_t mip) {
    auto* dev = g_vkdev;
    uint32_t w=std::max(1u,d.width>>mip),h=std::max(1u,d.height>>mip);
    int comps = d.format==RHIFormat::RG16F ? 2 : 4;
    size_t floats=(size_t)w*h*comps;
    auto hdr=f32ToF16((const float*)pixels,floats);
    VkDeviceSize sz=hdr.size()*2;
    VkBuffer staging; VmaAllocation sa;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size=sz; bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo ai{};
    ai.flags=VMA_ALLOCATION_CREATE_MAPPED_BIT|VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    ai.requiredFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VmaAllocationInfo info{};
    vmaCreateBuffer(dev->vma,&bi,&ai,&staging,&sa,&info);
    memcpy(info.pMappedData,hdr.data(),sz);
    VkCommandBuffer cmd=dev->beginOneTime();
    dev->transitionColor(cmd,this,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1u<<mip);
    VkBufferImageCopy r{};
    r.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,mip,0,1}; r.imageExtent={w,h,1};
    vkCmdCopyBufferToImage(cmd,staging,img,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&r);
    dev->transitionColor(cmd,this,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,1u<<mip);
    dev->endOneTime(cmd);
    vmaDestroyBuffer(dev->vma,staging,sa);
}
void VKTexture::uploadRGBA32F(const void* px) {
    // Desc is RGBA16F; pack f32 -> f16 on CPU and upload (env equirect only).
    auto* dev = g_vkdev;
    const float* src = (const float*)px;
    size_t floats = (size_t)d.width * d.height * 4;
    auto h = f32ToF16(src, floats);
    VkDeviceSize sz = h.size() * 2;
    VkBuffer staging; VmaAllocation sa;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = sz; bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo ai{};
    ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    ai.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    VmaAllocationInfo info{};
    vmaCreateBuffer(dev->vma, &bi, &ai, &staging, &sa, &info);
    memcpy(info.pMappedData, h.data(), (size_t)sz);
    VkCommandBuffer cmd = dev->beginOneTime();
    dev->transitionColor(cmd, this, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, d.mips);
    VkBufferImageCopy r{};
    r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    r.imageExtent = {d.width, d.height, 1};
    vkCmdCopyBufferToImage(cmd, staging, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    if (d.mips == 1) {
        dev->transitionColor(cmd, this, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1);
    }
    dev->endOneTime(cmd);
    vmaDestroyBuffer(dev->vma, staging, sa);
}

void VKDevice::generateMipmaps(ITexture* it) {
    auto* t = (VKTexture*)it;
    if (t->d.mips <= 1) return;
    VkCommandBuffer cmd = beginOneTime();
    int32_t w = t->d.width, h = t->d.height;
    for (uint32_t m = 1; m < t->d.mips; ++m) {
        VkImageBlit bl{};
        uint32_t layers = t->cube ? 6u : (uint32_t)t->d.layers;
        bl.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m - 1, 0, layers};
        bl.srcOffsets[1] = {w, h, 1};
        w = std::max(1, w / 2); h = std::max(1, h / 2);
        bl.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, layers};
        bl.dstOffsets[1] = {w, h, 1};
        vkCmdBlitImage(cmd, t->img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, t->img,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, &bl, VK_FILTER_LINEAR);
    }
    endOneTime(cmd);
}

std::vector<uint8_t> VKDevice::readTextureRGBA8(ITexture* it, uint32_t mip, int32_t layer) {
    auto* t = (VKTexture*)it;
    uint32_t w = std::max(1u, t->d.width >> mip);
    uint32_t h = std::max(1u, t->d.height >> mip);
    VkDeviceSize sz = (VkDeviceSize)w * h * 4;
    VkBuffer buf; VmaAllocation alloc;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = sz; bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo ai{};
    ai.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    vmaCreateBuffer(vma, &bi, &ai, &buf, &alloc, nullptr);
    VkCommandBuffer cmd = beginOneTime();
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.image = t->img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 1};
    b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy r{};
    r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
    r.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(cmd, t->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &r);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &b);
    endOneTime(cmd);
    std::vector<uint8_t> out((size_t)sz);
    void* mapped = nullptr;
    vmaMapMemory(vma, alloc, &mapped);
    memcpy(out.data(), mapped, (size_t)sz);
    vmaUnmapMemory(vma, alloc);
    vmaDestroyBuffer(vma, buf, alloc);
    return out;
}

// ---------------------------------------------------------------------------
// Frame lifecycle
// ---------------------------------------------------------------------------
IFrameBuffer* VKDevice::currentScreenFB() {
    return headless ? (IFrameBuffer*)screenFbo : (IFrameBuffer*)swapFbos[imageIdx];
}

bool VKDevice::beginFrame() {
    Frame& f = frames[frameIdx];
    vkWaitForFences(device, 1, &f.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &f.fence);
    vkResetDescriptorPool(device, f.descPool, 0);
    f.sets.clear();

    if (!headless) {
        if (swapOutdated) {
            destroySwapchainResources();
            createSwapchainResources();
            swapOutdated = false;
        }
        VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, f.imgAvail,
                                           VK_NULL_HANDLE, &imageIdx);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) { swapOutdated = true; return false; }
    }
    vkResetCommandBuffer(f.cmd, 0);
    vkBeginCommandBuffer(f.cmd, VKARGS(VkCommandBufferBeginInfo, VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                                            nullptr, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));
    return true;
}

void VKDevice::endFrame() {
    Frame& f = frames[frameIdx];
    vkEndCommandBuffer(f.cmd);
    if (headless) {
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1; si.pCommandBuffers = &f.cmd;
        vkQueueSubmit(queue, 1, &si, f.fence);
        if (getenv("VK_QUEUE_WAIT")) vkQueueWaitIdle(queue);
    } else {
        VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount = 1; si.pWaitSemaphores = &f.imgAvail;
        si.pWaitDstStageMask = &wait;
        si.commandBufferCount = 1; si.pCommandBuffers = &f.cmd;
        si.signalSemaphoreCount = 1; si.pSignalSemaphores = &f.renderDone;
        vkQueueSubmit(queue, 1, &si, f.fence);
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &f.renderDone;
        pi.swapchainCount = 1; pi.pSwapchains = &swapchain; pi.pImageIndices = &imageIdx;
        VkResult r = vkQueuePresentKHR(queue, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) swapOutdated = true;
    }
    frameIdx = (frameIdx + 1) % MAX_FLIGHT;
}

// ---------------------------------------------------------------------------
// Context
// ---------------------------------------------------------------------------
void VKRenderContext::setViewport(uint32_t w, uint32_t h) {
    VkViewport vp{0, 0, (float)w, (float)h, 0, 1};
    VkRect2D sc{{0,0},{w,h}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
}
void VKRenderContext::beginPass(IFrameBuffer* ifb, const ClearValue& c, bool cc, bool cd) { (void)cd;
    auto* f = (VKFBO*)ifb;
    VkClearValue clears[2]{};
    clears[0].color = {{c.color[0], c.color[1], c.color[2], c.color[3]}};
    clears[1].depthStencil = {c.depth, 0};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = dev->getOrCreateRP(f->colorFmts,
                                       f->hasDepth ? RHIFormat::D32F : RHIFormat::Unknown,
                                       f->present, !cc);
    rp.framebuffer = f->fb;
    rp.renderArea.extent = {f->w, f->h};
    rp.clearValueCount = f->hasDepth ? 2u : 1u;
    rp.pClearValues = clears;
    cmd = dev->currentCmd();
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    setViewport(f->w, f->h);
}
void VKRenderContext::endPass() { vkCmdEndRenderPass(cmd); }
void VKRenderContext::bindPipeline(IPipeline* pp) {
    pipe = (VKPipeline*)pp;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe->pipeline);
}

VkDescriptorSet VKDevice::acquireFrameSet(VKBindingSet* bs) {
    Frame& f = frames[frameIdx];
    auto it = f.sets.find(bs);
    if (it != f.sets.end()) return it->second;
    VkDescriptorSetLayout layout = bs->l->layout;
    VkDescriptorSet set;
    if (bs->persistentSet) return bs->persistentSet;
    vkAllocateDescriptorSets(device,
        VKARGS(VkDescriptorSetAllocateInfo, VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr,
                                       persistentPool, 1, &layout), &bs->persistentSet);
    set = bs->persistentSet;
    // Gather stable storage first.
    std::vector<VkDescriptorBufferInfo> bis;
    std::vector<VkDescriptorImageInfo> iis;
    for (auto& kv : bs->slots) {
        auto& s = kv.second;
            if (s.buf) bis.push_back({s.buf->buf, 0, s.range ? s.range : s.buf->bytes});
        if (s.tex) iis.push_back({s.tex->sampler, s.tex->fullView,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    }
    std::vector<VkWriteDescriptorSet> writes;
    size_t bi = 0, ii = 0;
    for (auto& kv : bs->slots) {
        auto& s = kv.second;
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = set; w.dstBinding = kv.first; w.descriptorCount = 1;
        if (s.buf) {
            w.descriptorType = s.dynamic ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
                                         : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w.pBufferInfo = &bis[bi++];
        } else if (s.tex) {
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = &iis[ii++];
        }
        writes.push_back(w);
    }
    vkUpdateDescriptorSets(device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
    f.sets[bs] = set;
    return set;
}

void VKRenderContext::bindSet(IBindingSet* iset, const uint32_t* dynOff, uint32_t dynCount) {
    auto* bs = (VKBindingSet*)iset;
    VkDescriptorSet set = dev->acquireFrameSet(bs);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe->layout, 0, 1, &set,
                            dynCount, dynOff);
}
void VKRenderContext::bindVertexBuffer(IBuffer* b, uint32_t binding, size_t off) {
    VkBuffer buf = ((VKBuffer*)b)->buf;
    VkDeviceSize o = off;
    vkCmdBindVertexBuffers(cmd, binding, 1, &buf, &o);
}
void VKRenderContext::bindIndexBuffer(IBuffer* b, RHIIndexType t, size_t off) {
    vkCmdBindIndexBuffer(cmd, ((VKBuffer*)b)->buf, off,
                         t == RHIIndexType::UInt32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
}
void VKRenderContext::draw(uint32_t n, uint32_t first, uint32_t inst) {
    vkCmdDraw(cmd, n, inst, first, 0);
}
void VKRenderContext::drawIndexed(uint32_t n, uint32_t first, int32_t vbase, uint32_t inst) {
    vkCmdDrawIndexed(cmd, n, inst, first, vbase, 0);
}

// ---------------------------------------------------------------------------
// Native handles / shutdown
// ---------------------------------------------------------------------------
void* VKDevice::nativeHandle(const char* what) {
    if (!strcmp(what, "vk_instance")) return instance;
    if (!strcmp(what, "vk_physical_device")) return gpu;
    if (!strcmp(what, "vk_device")) return device;
    if (!strcmp(what, "vk_queue")) return queue;
    if (!strcmp(what, "vk_queue_family")) return (void*)(uintptr_t)queueFamily;
    if (!strcmp(what, "vk_descriptor_pool")) return imguiPool;
    if (!strcmp(what, "imgui_render_pass")) return swapRP;
    if (!strcmp(what, "current_cmd")) return currentCmd();
    if (!strcmp(what, "min_image_count")) return (void*)(uintptr_t)(swapImgs.size());
    if (!strcmp(what, "ubo_alignment")) return (void*)(uintptr_t)uboAlign;
    return nullptr;
}

void VKDevice::shutdown() {
    vkDeviceWaitIdle(device);
    if (!headless) destroySwapchainResources();
    if (screenFbo) { vkDestroyFramebuffer(device, screenFbo->fb, nullptr); delete screenFbo; }
    // images/buffers/pipelines are process-lifetime in this engine build
    vkDestroyDescriptorPool(device, imguiPool, nullptr);
    vkDestroyDescriptorPool(device, persistentPool, nullptr);
    for (auto& f : frames) {
        vkDestroyCommandPool(device, f.pool, nullptr);
        vkDestroyFence(device, f.fence, nullptr);
        if (f.imgAvail) vkDestroySemaphore(device, f.imgAvail, nullptr);
        if (f.renderDone) vkDestroySemaphore(device, f.renderDone, nullptr);
        vkDestroyDescriptorPool(device, f.descPool, nullptr);
    }
    for (auto& kv : rpCache) vkDestroyRenderPass(device, kv.second, nullptr);
    rpCache.clear();
    vkDestroyCommandPool(device, oneTimePool, nullptr);
    vmaDestroyAllocator(vma);
    vkDestroyDevice(device, nullptr);
    if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    g_vkdev = nullptr;
}

} // namespace Genesis

Genesis::IDevice* Genesis::createVulkanDevice() { return new Genesis::VKDevice(); }
