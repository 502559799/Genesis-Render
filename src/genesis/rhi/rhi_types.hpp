/*
 * Genesis Render - Engine
 * rhi_types.hpp : backend-agnostic Render Hardware Interface types.
 *
 * Both the Vulkan and the OpenGL backend implement the IRHIDevice interface
 * declared in rhi.hpp against these enums / desc structs.
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <array>

namespace Genesis
{
    enum class RenderBackend : uint8_t
    {
        OpenGL = 0,
        Vulkan = 1
    };

    inline const char *backendName(RenderBackend b)
    {
        return b == RenderBackend::Vulkan ? "Vulkan" : "OpenGL";
    }

    // ------------------------------------------------------------- handles
    template <typename Tag>
    struct TRHIHandle
    {
        uint32_t id = 0xffffffffu;
        bool valid() const { return id != 0xffffffffu; }
        explicit operator bool() const { return valid(); }
        bool operator==(const TRHIHandle &o) const { return id == o.id; }
        bool operator!=(const TRHIHandle &o) const { return id != o.id; }
    };

    struct BufferTag {};
    struct TextureTag {};
    struct SamplerTag {};
    struct ShaderTag {};
    struct PipelineTag {};
    struct SetTag {};

    using BufferHandle = TRHIHandle<BufferTag>;
    using TextureHandle = TRHIHandle<TextureTag>;
    using SamplerHandle = TRHIHandle<SamplerTag>;
    using ShaderHandle = TRHIHandle<ShaderTag>;
    using PipelineHandle = TRHIHandle<PipelineTag>;
    using DescriptorSetHandle = TRHIHandle<SetTag>;

    static constexpr BufferHandle kInvalidBuffer{};
    static constexpr TextureHandle kInvalidTexture{};
    static constexpr SamplerHandle kInvalidSampler{};
    static constexpr ShaderHandle kInvalidShader{};
    static constexpr PipelineHandle kInvalidPipeline{};
    static constexpr DescriptorSetHandle kInvalidSet{};

    // -------------------------------------------------------------- enums
    enum class Format : uint8_t
    {
        Unknown = 0,
        R8Unorm,
        R8Srgb,
        R8G8B8A8Unorm,
        R8G8B8A8Srgb,
        B8G8R8A8Unorm,
        B8G8R8A8Srgb,
        R16G16B16A16Float,
        R32Float,
        R32G32Float,
        R32G32B32Float,
        R32G32B32A32Float,
        D32Float,
        D24UnormS8Uint
    };

    enum class TextureType : uint8_t
    {
        Texture2D = 0,
        TextureCube = 1
    };

    enum TextureUsage : uint8_t
    {
        TextureUsage_Sampled = 1 << 0,
        TextureUsage_RenderTarget = 1 << 1,
        TextureUsage_DepthStencil = 1 << 2,
        TextureUsage_TransferDst = 1 << 3
    };

    enum BufferUsage : uint8_t
    {
        BufferUsage_Vertex = 1 << 0,
        BufferUsage_Index = 1 << 1,
        BufferUsage_Uniform = 1 << 2,
        BufferUsage_TransferDst = 1 << 3
    };

    enum class IndexType : uint8_t { Uint16 = 0, Uint32 = 1 };
    enum class PrimitiveTopology : uint8_t { Triangles = 0, Lines, TriangleStrip };
    enum class CullMode : uint8_t { None = 0, Front, Back };
    enum class FrontFace : uint8_t { CCW = 0, CW };
    enum class CompareOp : uint8_t
    {
        Never = 0, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always
    };
    enum class BlendMode : uint8_t { Off = 0, Alpha, Additive };
    enum class FilterMode : uint8_t { Nearest = 0, Linear };
    enum class WrapMode : uint8_t { Repeat = 0, ClampEdge, MirroredRepeat };

    enum class ShaderStage : uint8_t { Vertex = 0, Fragment };

    // ------------------------------------------------------------- structs
    struct DeviceCaps
    {
        RenderBackend backend = RenderBackend::OpenGL;
        char device_name[256] = {};
        char api_version[32] = {};
        bool clip_depth_zero_to_one = false; // Vulkan/Metal [0,1]; OpenGL [-1,1]
        bool ndc_y_down = false;             // Vulkan y points down
    };

    struct BufferDesc
    {
        size_t size = 0;
        uint8_t usage = BufferUsage_Vertex;
        bool host_visible = false; // mappable / persistently mapped
        const char *name = "buffer";
    };

    struct TextureDesc
    {
        TextureType type = TextureType::Texture2D;
        Format format = Format::R8G8B8A8Unorm;
        uint32_t width = 1;
        uint32_t height = 1;
        uint16_t mip_levels = 1;
        uint16_t layers = 1; // 6 for cube
        uint8_t usage = TextureUsage_Sampled;
        const char *name = "texture";
    };

    struct SamplerDesc
    {
        FilterMode min_filter = FilterMode::Linear;
        FilterMode mag_filter = FilterMode::Linear;
        FilterMode mip_filter = FilterMode::Linear;
        WrapMode wrap_u = WrapMode::Repeat;
        WrapMode wrap_v = WrapMode::Repeat;
        WrapMode wrap_w = WrapMode::Repeat;
        float max_anisotropy = 1.0f;
        const char *name = "sampler";
    };

    struct VertexAttribute
    {
        uint32_t location = 0;
        Format format = Format::R32G32B32A32Float;
        uint32_t offset = 0;
    };

    struct VertexLayout
    {
        uint32_t stride = 0;
        std::array<VertexAttribute, 8> attributes{};
        uint8_t attribute_count = 0;
        VertexLayout &add(uint32_t location, Format format, uint32_t offset)
        {
            attributes[attribute_count++] = {location, format, offset};
            return *this;
        }
    };

    struct ShaderStageBlob
    {
        ShaderStage stage = ShaderStage::Vertex;
        const uint32_t *spv = nullptr;
        size_t spv_size_bytes = 0;
        const char *glsl = nullptr; // raw, includes resolved by the shader library
    };

    struct ShaderProgramDesc
    {
        const char *name = "shader";
        std::array<ShaderStageBlob, 2> stages{};
        uint8_t stage_count = 0;
    };

    struct PipelineDesc
    {
        ShaderHandle shader;
        VertexLayout vertex_layout;
        PrimitiveTopology topology = PrimitiveTopology::Triangles;
        CullMode cull = CullMode::Back;
        FrontFace front_face = FrontFace::CCW;
        bool depth_test = true;
        bool depth_write = true;
        CompareOp depth_compare = CompareOp::LessEqual;
        BlendMode blend = BlendMode::Off;
        std::array<Format, 4> color_formats{Format::B8G8R8A8Unorm, Format::Unknown,
                                           Format::Unknown, Format::Unknown};
        uint8_t color_format_count = 1;
        Format depth_format = Format::D32Float;
        uint32_t push_constant_size = 0;
        const char *name = "pipeline";
    };

    struct ColorAttachment
    {
        TextureHandle texture;
        uint16_t mip = 0;
        uint16_t layer = 0; // cube face index when texture is a cube
        float clear_color[4] = {0.02f, 0.02f, 0.03f, 1.0f};
        bool clear = true;
        bool store = true;
    };

    struct DepthAttachment
    {
        TextureHandle texture;
        float clear_depth = 1.0f;
        bool clear = true;
    };

    struct SetBinding
    {
        uint32_t binding = 0;
        BufferHandle buffer;
        size_t offset = 0;
        size_t size = 0; // 0 -> whole buffer
        TextureHandle texture;
        SamplerHandle sampler;
    };

    struct DescriptorSetDesc
    {
        // set 0 = frame global (UBO camera/light + IBL), set 1 = material
        uint32_t set_index = 0;
        std::array<SetBinding, 12> bindings{};
        uint8_t binding_count = 0;
        DescriptorSetDesc &addUniform(uint32_t binding, BufferHandle b)
        {
            auto &x = bindings[binding_count++];
            x.binding = binding;
            x.buffer = b;
            return *this;
        }
        DescriptorSetDesc &addTexture(uint32_t binding, TextureHandle t, SamplerHandle s)
        {
            auto &x = bindings[binding_count++];
            x.binding = binding;
            x.texture = t;
            x.sampler = s;
            return *this;
        }
    };

    // Fixed binding convention shared by every shader:
    //   set 0: binding 0 UBO CameraBlock, binding 1 UBO LightingBlock
    //         binding 8  samplerCube irradianceMap
    //         binding 9  samplerCube prefilteredEnvMap
    //         binding 10 sampler2D  brdfLUT
    //         binding 11 samplerCube environmentMap (skybox)
    //   set 1: binding 2 UBO MaterialBlock
    //         binding 4 sampler2D baseColorMap
    //         binding 5 sampler2D normalMap
    //         binding 6 sampler2D occlusionRoughnessMetallicMap (.r AO .g rough .b metal)
    //         binding 7 sampler2D emissiveMap
    //   push constants: mat4 model; mat4 normalMatrix (128 bytes, vertex stage)
    //   OpenGL emulates push constants with a UBO at binding 3.
    static constexpr uint32_t kBindingCamera = 0;
    static constexpr uint32_t kBindingLighting = 1;
    static constexpr uint32_t kBindingMaterial = 2;
    static constexpr uint32_t kBindingPushEmu = 3;
    static constexpr uint32_t kBindingBaseColor = 4;
    static constexpr uint32_t kBindingNormal = 5;
    static constexpr uint32_t kBindingORM = 6;
    static constexpr uint32_t kBindingEmissive = 7;
    static constexpr uint32_t kBindingIrradiance = 8;
    static constexpr uint32_t kBindingPrefilter = 9;
    static constexpr uint32_t kBindingBrdfLUT = 10;
    static constexpr uint32_t kBindingEnvCube = 11;
    static constexpr uint32_t kBindingScene2D = 12; // equirect source / scene blit
    static constexpr uint32_t kPushConstantSize = 2 * 16 * 4; // 128 bytes
}
