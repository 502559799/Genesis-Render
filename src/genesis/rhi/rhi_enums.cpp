/*
 * Genesis Render - RHI enum helpers
 */
#include "rhi_enums.hpp"

namespace Genesis
{
    namespace RHI
    {
        const char *backendTypeName(BackendType type)
        {
            switch (type)
            {
            case BackendType::OpenGL: return "OpenGL";
            case BackendType::Vulkan: return "Vulkan";
            case BackendType::Metal: return "Metal";
            }
            return "Unknown";
        }

        uint32_t pixelFormatBytesPerPixel(PixelFormat format)
        {
            switch (format)
            {
            case PixelFormat::R8_UNORM: return 1;
            case PixelFormat::R16_SFLOAT:
            case PixelFormat::RG8_UNORM:
            case PixelFormat::D16_UNORM: return 2;
            case PixelFormat::RGB8_UNORM: return 3;
            case PixelFormat::RGBA8_UNORM:
            case PixelFormat::RGBA8_SRGB:
            case PixelFormat::BGRA8_UNORM:
            case PixelFormat::RG16_SFLOAT:
            case PixelFormat::R32_SFLOAT:
            case PixelFormat::RGBA32_UINT:
            case PixelFormat::R32_UINT:
            case PixelFormat::D24_UNORM_S8_UINT:
            case PixelFormat::D32_SFLOAT: return 4;
            case PixelFormat::RG32_SFLOAT:
            case PixelFormat::D32_SFLOAT_S8_UINT: return 8;
            case PixelFormat::RGB32_SFLOAT: return 12;
            case PixelFormat::RGBA16_SFLOAT:
            case PixelFormat::RGBA32_SFLOAT: return 16;
            default: break;
            }
            return 0;
        }

        uint32_t pixelFormatComponentCount(PixelFormat format)
        {
            switch (format)
            {
            case PixelFormat::R8_UNORM:
            case PixelFormat::R16_SFLOAT:
            case PixelFormat::R32_SFLOAT:
            case PixelFormat::R32_UINT:
            case PixelFormat::D16_UNORM:
            case PixelFormat::D24_UNORM_S8_UINT:
            case PixelFormat::D32_SFLOAT:
            case PixelFormat::D32_SFLOAT_S8_UINT: return 1;
            case PixelFormat::RG8_UNORM:
            case PixelFormat::RG16_SFLOAT:
            case PixelFormat::RG32_SFLOAT: return 2;
            case PixelFormat::RGB8_UNORM:
            case PixelFormat::RGB32_SFLOAT: return 3;
            default: return 4;
            }
        }

        bool pixelFormatIsDepth(PixelFormat format)
        {
            switch (format)
            {
            case PixelFormat::D16_UNORM:
            case PixelFormat::D24_UNORM_S8_UINT:
            case PixelFormat::D32_SFLOAT:
            case PixelFormat::D32_SFLOAT_S8_UINT: return true;
            default: return false;
            }
        }

        bool pixelFormatIsFloat(PixelFormat format)
        {
            switch (format)
            {
            case PixelFormat::R16_SFLOAT:
            case PixelFormat::RG16_SFLOAT:
            case PixelFormat::RGBA16_SFLOAT:
            case PixelFormat::R32_SFLOAT:
            case PixelFormat::RG32_SFLOAT:
            case PixelFormat::RGB32_SFLOAT:
            case PixelFormat::RGBA32_SFLOAT:
            case PixelFormat::D32_SFLOAT:
            case PixelFormat::D32_SFLOAT_S8_UINT: return true;
            default: return false;
            }
        }

        bool pixelFormatIsSRGB(PixelFormat format)
        {
            return format == PixelFormat::RGBA8_SRGB;
        }

        uint32_t vertexFormatSize(VertexFormat format)
        {
            switch (format)
            {
            case VertexFormat::Float1: return 4;
            case VertexFormat::Float2: return 8;
            case VertexFormat::Float3: return 12;
            case VertexFormat::Float4: return 16;
            case VertexFormat::UByte4:
            case VertexFormat::UByte4Norm: return 4;
            case VertexFormat::Short2:
            case VertexFormat::Short2Norm: return 4;
            case VertexFormat::Short4:
            case VertexFormat::Short4Norm: return 8;
            case VertexFormat::UInt1: return 4;
            case VertexFormat::UInt2: return 8;
            case VertexFormat::UInt4: return 16;
            default: return 0;
            }
        }

        bool resourceTypeIsBuffer(ResourceType type)
        {
            switch (type)
            {
            case ResourceType::UniformBuffer:
            case ResourceType::UniformBufferDynamic:
            case ResourceType::StorageBuffer:
            case ResourceType::StorageBufferDynamic: return true;
            default: return false;
            }
        }

        bool resourceTypeIsImage(ResourceType type)
        {
            switch (type)
            {
            case ResourceType::SampledImage:
            case ResourceType::Sampler:
            case ResourceType::CombinedImageSampler:
            case ResourceType::StorageImage: return true;
            default: return false;
            }
        }

    } // namespace RHI
} // namespace Genesis
