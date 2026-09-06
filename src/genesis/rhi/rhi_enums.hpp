/*
 * Genesis Render - RHI (Render Hardware Interface) enums
 *
 * Backend-agnostic enumeration types shared by the OpenGL and Vulkan
 * implementations of the RHI.  Values are deliberately ordered so that
 * they can be cheaply translated into native enums by a lookup table.
 */
#ifndef __GENESIS_RHI_ENUMS_HPP__
#define __GENESIS_RHI_ENUMS_HPP__

#include <cstdint>

namespace Genesis
{
    namespace RHI
    {
        enum class BackendType : uint32_t
        {
            OpenGL = 0,
            Vulkan = 1,
        };

        const char *backendTypeName(BackendType type);

        //------------------------------------------------------------------
        // Pixel / vertex formats
        //------------------------------------------------------------------
        enum class PixelFormat : uint32_t
        {
            Undefined = 0,
            R8_UNORM,
            RG8_UNORM,
            RGB8_UNORM,
            RGBA8_UNORM,
            RGBA8_SRGB,
            BGRA8_UNORM,
            R16_SFLOAT,
            RG16_SFLOAT,
            RGBA16_SFLOAT,
            R32_SFLOAT,
            RG32_SFLOAT,
            RGB32_SFLOAT,
            RGBA32_SFLOAT,
            RGBA32_UINT,
            R32_UINT,
            D16_UNORM,
            D24_UNORM_S8_UINT,
            D32_SFLOAT,
            D32_SFLOAT_S8_UINT,
            Count
        };

        uint32_t pixelFormatBytesPerPixel(PixelFormat format);
        uint32_t pixelFormatComponentCount(PixelFormat format);
        bool pixelFormatIsDepth(PixelFormat format);
        bool pixelFormatIsFloat(PixelFormat format);
        bool pixelFormatIsSRGB(PixelFormat format);

        enum class VertexFormat : uint32_t
        {
            Undefined = 0,
            Float1,
            Float2,
            Float3,
            Float4,
            UByte4,
            UByte4Norm,
            Short2,
            Short4,
            Short2Norm,
            Short4Norm,
            UInt1,
            UInt2,
            UInt4,
            Count
        };

        uint32_t vertexFormatSize(VertexFormat format);

        enum class IndexType : uint32_t
        {
            UInt16 = 0,
            UInt32 = 1,
        };

        //------------------------------------------------------------------
        // Texture
        //------------------------------------------------------------------
        enum class TextureType : uint32_t
        {
            Tex2D = 0,
            TexCube = 1,
            Tex3D = 2,
        };

        enum class TextureUsageFlagBits : uint32_t
        {
            None = 0,
            Sampled = 1u << 0,
            RenderTarget = 1u << 1,
            DepthStencil = 1u << 2,
            Storage = 1u << 3,
            TransferSrc = 1u << 4,
            TransferDst = 1u << 5,
        };
        using TextureUsageFlags = uint32_t;

        enum class FilterMode : uint32_t
        {
            Nearest = 0,
            Linear = 1,
        };

        enum class MipmapMode : uint32_t
        {
            Nearest = 0,
            Linear = 1,
        };

        enum class AddressMode : uint32_t
        {
            Repeat = 0,
            MirroredRepeat = 1,
            ClampToEdge = 2,
            ClampToBorder = 3,
        };

        enum class CompareOp : uint32_t
        {
            Never = 0,
            Less,
            Equal,
            LessOrEqual,
            Greater,
            NotEqual,
            GreaterOrEqual,
            Always,
        };

        //------------------------------------------------------------------
        // Buffer
        //------------------------------------------------------------------
        enum class BufferUsageFlagBits : uint32_t
        {
            None = 0,
            Vertex = 1u << 0,
            Index = 1u << 1,
            Uniform = 1u << 2,
            Storage = 1u << 3,
            Indirect = 1u << 4,
            TransferSrc = 1u << 5,
            TransferDst = 1u << 6,
        };
        using BufferUsageFlags = uint32_t;

        enum class MemoryUsage : uint32_t
        {
            GpuOnly = 0,     // device local, written through a staging buffer
            CpuToGpu = 1,    // host visible + coherent, written by CPU each frame
            GpuToCpu = 2,    // host visible, read back by CPU
        };

        //------------------------------------------------------------------
        // Shaders
        //------------------------------------------------------------------
        enum class ShaderStageFlagBits : uint32_t
        {
            None = 0,
            Vertex = 1u << 0,
            Fragment = 1u << 1,
            Compute = 1u << 2,
            Geometry = 1u << 3,
        };
        using ShaderStageFlags = uint32_t;

        // What the shader payload contains.  The OpenGL backend needs GLSL
        // source, the Vulkan backend needs SPIR-V; the build system emits both
        // from a single GLSL source file.
        enum class ShaderCodeType : uint32_t
        {
            GlslSource = 0,
            Spirv = 1,
        };

        //------------------------------------------------------------------
        // Pipeline state
        //------------------------------------------------------------------
        enum class PrimitiveTopology : uint32_t
        {
            TriangleList = 0,
            TriangleStrip = 1,
            LineList = 2,
            LineStrip = 3,
            PointList = 4,
        };

        enum class CullMode : uint32_t
        {
            None = 0,
            Front = 1,
            Back = 2,
            FrontAndBack = 3,
        };

        enum class FrontFace : uint32_t
        {
            CounterClockwise = 0,
            Clockwise = 1,
        };

        enum class BlendFactor : uint32_t
        {
            Zero = 0,
            One,
            SrcColor,
            OneMinusSrcColor,
            DstColor,
            OneMinusDstColor,
            SrcAlpha,
            OneMinusSrcAlpha,
            DstAlpha,
            OneMinusDstAlpha,
            ConstantColor,
            OneMinusConstantColor,
        };

        enum class BlendOp : uint32_t
        {
            Add = 0,
            Subtract,
            ReverseSubtract,
            Min,
            Max,
        };

        enum class ColorComponentFlagBits : uint32_t
        {
            None = 0,
            R = 1u << 0,
            G = 1u << 1,
            B = 1u << 2,
            A = 1u << 3,
            All = R | G | B | A,
        };
        using ColorComponentFlags = uint32_t;

        enum class PolygonMode : uint32_t
        {
            Fill = 0,
            Line = 1,
            Point = 2,
        };

        //------------------------------------------------------------------
        // Render pass
        //------------------------------------------------------------------
        enum class LoadOp : uint32_t
        {
            Load = 0,
            Clear,
            DontCare,
        };

        enum class StoreOp : uint32_t
        {
            Store = 0,
            DontCare,
        };

        // Texture layout barriers.  Meaningful for Vulkan only; the OpenGL
        // backend treats these as no-ops but still tracks the layout so that
        // mip generation / readback can be issued when needed.
        enum class TextureLayout : uint32_t
        {
            Undefined = 0,
            General,
            ColorAttachmentOptimal,
            DepthStencilAttachmentOptimal,
            DepthStencilReadOnlyOptimal,
            ShaderReadOnlyOptimal,
            TransferSrcOptimal,
            TransferDstOptimal,
            PresentSrc,
        };

        //------------------------------------------------------------------
        // Descriptor / resource binding types
        //------------------------------------------------------------------
        enum class ResourceType : uint32_t
        {
            UniformBuffer = 0,
            UniformBufferDynamic,
            StorageBuffer,
            StorageBufferDynamic,
            SampledImage,
            Sampler,
            CombinedImageSampler,
            StorageImage,
        };

        bool resourceTypeIsBuffer(ResourceType type);
        bool resourceTypeIsImage(ResourceType type);

        // Reserved binding slots.
        // The GL backend maps a (set, binding) pair onto a uniform-block index
        // and a texture unit using the constants below, so shader authors only
        // ever need to know set/binding.
        static constexpr uint32_t GL_PUSH_CONSTANT_BLOCK_BINDING = 0;
        static constexpr uint32_t GL_UNIFORM_BLOCK_BASE = 1;
        static constexpr uint32_t GL_BINDING_STRIDE = 8; // blocks/units per set

        inline uint32_t glUniformBlockIndex(uint32_t set, uint32_t binding)
        {
            return GL_UNIFORM_BLOCK_BASE + set * GL_BINDING_STRIDE + binding;
        }
        inline uint32_t glTextureUnit(uint32_t set, uint32_t binding)
        {
            return set * GL_BINDING_STRIDE + binding;
        }

    } // namespace RHI
} // namespace Genesis

#endif /* __GENESIS_RHI_ENUMS_HPP__ */
