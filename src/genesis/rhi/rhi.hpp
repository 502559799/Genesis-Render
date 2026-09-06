/*
 * Genesis Render - Rendering Hardware Interface
 * Backend-neutral graphics abstraction with Vulkan and OpenGL implementations.
 *
 * Design:
 *  - All GPU objects are exposed as interface pointers (pimpl); backend code
 *    lives in rhi_vulkan.cpp / rhi_opengl.cpp and never leaks into engine code.
 *  - Single descriptor-set model with FLAT, globally-unique binding numbers
 *    shared by both backends (see BindingTable) so one GLSL source compiles for
 *    Vulkan (layout(set=0,binding=N)) and OpenGL (layout(binding=N)).
 *  - Per-draw uniform blocks use DYNAMIC UBO bindings; the engine owns ring
 *    buffers and supplies dynamic offsets at bind time.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace Genesis {

enum class RHIBackend : uint8_t { Vulkan, OpenGL };

enum class RHIFormat : uint8_t {
    Unknown,
    R8,
    RGBA8,
    BGRA8,
    RGBA8_SRGB,
    RG16F,
    RGBA16F,
    R32F,
    RG32F,
    D32F,
};

enum BufferUsage : uint32_t {
    BU_Vertex  = 1 << 0,
    BU_Index   = 1 << 1,
    BU_Uniform = 1 << 2,
};

enum TextureUsage : uint32_t {
    TU_Sampled      = 1 << 0,
    TU_RenderTarget = 1 << 1,
    TU_Depth        = 1 << 2,
    TU_Cube         = 1 << 3,
};

enum class RHIFilter : uint8_t { Nearest, Linear };
enum class RHIWrap : uint8_t { Repeat, ClampEdge, ClampBorder };

enum class RHITopology : uint8_t { Triangles, Lines, TriangleStrip };
enum class RHICull : uint8_t { None, Front, Back };
enum class RHICompare : uint8_t { Never, Less, LEqual, Greater, GEqual, Equal, Always };
enum class RHIIndexType : uint8_t { UInt16, UInt32 };

enum class RHIAttrib : uint8_t {
    Float, Float2, Float3, Float4,
};

struct DeviceInitInfo {
    void* window_handle = nullptr; // SDL_Window* when not headless
    RHIBackend backend = RHIBackend::Vulkan;
    bool headless = false;         // no swapchain, render offscreen (CI/test)
    uint32_t width = 1280;
    uint32_t height = 720;
    bool vsync = true;
    bool validation = false;
};

// ---------------------------------------------------------------------------
// Resource descriptors
// ---------------------------------------------------------------------------
struct BufferDesc {
    size_t size = 0;
    uint32_t usage = 0;           // BufferUsage bits
    bool host_visible = false;    // persistently mapped when true
    const char* name = nullptr;
};

struct TextureDesc {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t mips = 1;
    uint32_t layers = 1;          // 6 when TU_Cube
    RHIFormat format = RHIFormat::RGBA8;
    uint32_t usage = TU_Sampled;
    RHIFilter filter_min = RHIFilter::Linear;
    RHIFilter filter_mag = RHIFilter::Linear;
    RHIWrap wrap = RHIWrap::Repeat;
    const char* name = nullptr;
};

struct IBuffer {
    virtual ~IBuffer() = default;
    virtual size_t size() const = 0;
    // Persistently mapped host pointer for host-visible buffers.
    virtual void* mapped() = 0;
    virtual void upload(size_t offset, size_t size, const void* data) = 0;
};

struct ITexture {
    virtual ~ITexture() = default;
    virtual const TextureDesc& desc() const = 0;
    // Upload tightly packed RGBA8 (2D, mip 0) from CPU memory and build mipmaps.
    virtual void uploadRGBA8(const void* pixels) = 0;
    // Upload tightly packed RGBA float16 (2D equirect, mip 0).
    virtual void uploadRGBA16F(const void* pixels, uint32_t mip = 0) = 0;
    // Upload tightly packed RGBA32F (2D equirect HDR, mip 0), builds mips.
    virtual void uploadRGBA32F(const void* pixels) = 0;
};

// ---------------------------------------------------------------------------
// Binding / pipeline description
// ---------------------------------------------------------------------------
enum class BindingType : uint8_t { DynamicUBO, UBO, Texture2D, TextureCube };

struct BindingDecl {
    uint32_t binding;
    BindingType type;
};

struct IBindingSetLayout {
    virtual ~IBindingSetLayout() = default;
};

struct IBindingSet {
    virtual ~IBindingSet() = default;
    virtual void setBuffer(uint32_t binding, IBuffer* buffer, size_t offset = 0, size_t range = 0) = 0;
    virtual void setTexture(uint32_t binding, ITexture* texture) = 0;
};

struct IShaderProgram {
    virtual ~IShaderProgram() = default;
};

struct VertexAttrib {
    uint32_t location;
    uint32_t offset;
    RHIAttrib type;
};
struct VertexLayout {
    uint32_t stride = 0;
    std::vector<VertexAttrib> attribs;
};

struct PipelineDesc {
    IShaderProgram* program = nullptr;
    VertexLayout vertex_layout;
    RHITopology topology = RHITopology::Triangles;
    RHICull cull = RHICull::None;
    bool depth_test = false;
    bool depth_write = false;
    RHICompare depth_func = RHICompare::Less;
    bool blend_alpha = false;
    std::vector<RHIFormat> color_formats;
    RHIFormat depth_format = RHIFormat::Unknown;
    IBindingSetLayout* binding_layout = nullptr;
};
struct IPipeline {
    virtual ~IPipeline() = default;
};

// Offscreen / swapchain render target.
struct ColorAttachment {
    ITexture* texture = nullptr;
    uint32_t mip = 0;
    uint32_t layer = 0;
};
struct FrameBufferDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<ColorAttachment> colors;
    ITexture* depth = nullptr;
};
struct IFrameBuffer {
    virtual ~IFrameBuffer() = default;
    virtual uint32_t width() const = 0;
    virtual uint32_t height() const = 0;
};

// ---------------------------------------------------------------------------
// Render context (record/draw) and device
// ---------------------------------------------------------------------------
struct ClearValue {
    float color[4] = {0.02f, 0.02f, 0.03f, 1.0f};
    float depth = 1.0f;
};

class IRenderContext {
public:
    virtual ~IRenderContext() = default;
    virtual void setViewport(uint32_t w, uint32_t h) = 0;
    virtual void beginPass(IFrameBuffer* fb, const ClearValue& clear,
                           bool clearColor = true, bool clearDepth = true) = 0;
    virtual void endPass() = 0;
    virtual void bindPipeline(IPipeline* pipeline) = 0;
    virtual void bindSet(IBindingSet* set, const uint32_t* dynamicOffsets,
                         uint32_t dynamicCount) = 0;
    virtual void bindVertexBuffer(IBuffer* buffer, uint32_t binding = 0, size_t offset = 0) = 0;
    virtual void bindIndexBuffer(IBuffer* buffer, RHIIndexType type, size_t offset = 0) = 0;
    virtual void draw(uint32_t vertexCount, uint32_t firstVertex = 0, uint32_t instances = 1) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t firstIndex = 0,
                             int32_t vertexOffset = 0, uint32_t instances = 1) = 0;
};

// Backend capability / info strings for the UI.
struct BackendInfo {
    std::string name;
    std::string device;
    std::string version;
    RHIBackend backend;
};

class IDevice {
public:
    virtual ~IDevice() = default;

    virtual bool init(const DeviceInitInfo& info) = 0;
    virtual void shutdown() = 0;
    virtual void waitIdle() = 0;

    virtual RHIBackend backend() const = 0;
    virtual const BackendInfo& info() const = 0;
    virtual bool isHeadless() const = 0;
    virtual bool vulkanClip() const = 0; // true on Vulkan (y-down, z [0,1])

    // Frame lifecycle. Windowed Vulkan acquires the swapchain image here.
    virtual bool beginFrame() = 0;
    virtual IRenderContext* context() = 0;
    // final target: swapchain framebuffer (windowed) or offscreen screen FBO.
    virtual IFrameBuffer* currentScreenFB() = 0;
    virtual ITexture* screenColorTexture() = 0; // readable LDR target (headless)
    virtual void endFrame() = 0;
    virtual void onResize(uint32_t w, uint32_t h) = 0;

    // Resources
    virtual IBuffer* createBuffer(const BufferDesc& desc, const void* initialData = nullptr) = 0;
    virtual ITexture* createTexture(const TextureDesc& desc) = 0;
    virtual IBindingSetLayout* createBindingSetLayout(const std::vector<BindingDecl>& decls) = 0;
    virtual IBindingSet* createBindingSet(IBindingSetLayout* layout) = 0;
    // GLSL sources for OpenGL; SPIR-V bytecode for Vulkan (one program = vs+fs pair).
    virtual IShaderProgram* createShaderProgram(const std::string& name,
                                                const std::string& vsGlsl,
                                                const std::string& fsGlsl,
                                                const std::vector<uint32_t>& vsSpv,
                                                const std::vector<uint32_t>& fsSpv) = 0;
    virtual IPipeline* createPipeline(const PipelineDesc& desc) = 0;
    virtual IFrameBuffer* createFrameBuffer(const FrameBufferDesc& desc) = 0;

    // Generate the full mip chain (used after render-to-face baking too).
    virtual void generateMipmaps(ITexture* texture) = 0;
    // Make every face/mip of a cube (or a 2D target) readable by subsequent
    // draw calls in the same frame. Used between IBL bake passes.
    virtual void textureSamplingBarrier(ITexture* texture) = 0;
    // Copy a 2D source texture into one cube face layer of dstCube.
    virtual void copyLayerToCube(ITexture* src, ITexture* dstCube, uint32_t size,
                                 uint32_t mip, uint32_t layer) = 0;
    // Copy the shared offscreen LDR target into the swapchain image (no-op
    // in headless mode).
    virtual void blitToSwapchain(ITexture* screen) = 0;
    // Concrete format of the final screen target (BGRA8 on Vulkan swapchains).
    virtual RHIFormat screenFormat() const = 0;

    // Synchronous RGBA8 readback (testing/screenshots). Texture must be a
    // sampled 2D target; inserts required barriers/copies internally.
    virtual std::vector<uint8_t> readTextureRGBA8(ITexture* texture, uint32_t mip = 0,
                                                  int32_t layer = -1) = 0;

    // Run one graphics command outside the frame loop (font uploads etc).
    // Vulkan records into a one-time command and waits; GL invokes directly.
    virtual void immediateGraphics(const std::function<void(void* nativeCmd)>& fn) = 0;

    // Native handles for backend-specific integrations (ImGui).
    virtual void* nativeHandle(const char* what) = 0;
};

// Factory implemented by the two backend translation units.
IDevice* createVulkanDevice();
IDevice* createOpenGLDevice();

inline IDevice* createDevice(RHIBackend backend) {
    return backend == RHIBackend::Vulkan ? createVulkanDevice() : createOpenGLDevice();
}

// ---------------------------------------------------------------------------
// Engine-wide flat binding table (shared by every shader)
// ---------------------------------------------------------------------------
namespace BindingTable {
// Uniform blocks (own binding space on GL; set 0 on Vulkan)
constexpr uint32_t Global    = 0; // per-frame camera/lights/settings
constexpr uint32_t Draw      = 1; // per-draw model + normal matrix
constexpr uint32_t Material  = 2; // per-material PBR params
// Samplers (globally unique units so OpenGL never aliases)
constexpr uint32_t EnvAtlas     = 10;
constexpr uint32_t IrrAtlas     = 11;
constexpr uint32_t PrefAtlas    = 12;
constexpr uint32_t BrdfLUT      = 13;
constexpr uint32_t AlbedoTex    = 20;
constexpr uint32_t NormalTex    = 21;
constexpr uint32_t MetallicRoughTex = 22;
constexpr uint32_t AOTex        = 23;
constexpr uint32_t EmissiveTex  = 24;
constexpr uint32_t SceneColorTex   = 30;
constexpr uint32_t EquirectTex     = 31;
} // namespace BindingTable

} // namespace Genesis
