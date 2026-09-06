/*
 * Genesis Render - OpenGL enum conversion helpers
 *
 * Shared by gl_backend.cpp and gl_command_buffer.cpp (inline, header-only, so
 * both translation units see identical conversions).
 */
#ifndef __GENESIS_GL_CONVERT_HPP__
#define __GENESIS_GL_CONVERT_HPP__

#include "rhi/opengl/gl_backend.hpp"

namespace Genesis
{
    namespace RHI
    {
        inline GLenum glBufferTarget(BufferUsageFlags usage)
        {
            if (usage & (uint32_t)BufferUsageFlagBits::Index) return GL_ELEMENT_ARRAY_BUFFER;
            if (usage & (uint32_t)BufferUsageFlagBits::Uniform) return GL_UNIFORM_BUFFER;
            if (usage & (uint32_t)BufferUsageFlagBits::Vertex) return GL_ARRAY_BUFFER;
            if (usage & (uint32_t)BufferUsageFlagBits::Indirect) return GL_DRAW_INDIRECT_BUFFER;
            return GL_ARRAY_BUFFER;
        }

        inline GLenum glUsageHint(MemoryUsage usage)
        {
            switch (usage)
            {
            case MemoryUsage::GpuOnly: return GL_STATIC_DRAW;
            case MemoryUsage::CpuToGpu: return GL_DYNAMIC_DRAW;
            case MemoryUsage::GpuToCpu: return GL_STREAM_READ;
            }
            return GL_STATIC_DRAW;
        }

        inline GLenum glFilter(FilterMode m) { return m == FilterMode::Linear ? GL_LINEAR : GL_NEAREST; }

        inline GLenum glMipFilter(FilterMode min_f, MipmapMode mip)
        {
            if (mip == MipmapMode::Linear)
                return min_f == FilterMode::Linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
            return min_f == FilterMode::Linear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
        }

        inline GLenum glAddress(AddressMode m)
        {
            switch (m)
            {
            case AddressMode::Repeat: return GL_REPEAT;
            case AddressMode::MirroredRepeat: return GL_MIRRORED_REPEAT;
            case AddressMode::ClampToEdge: return GL_CLAMP_TO_EDGE;
            case AddressMode::ClampToBorder: return GL_CLAMP_TO_BORDER;
            }
            return GL_REPEAT;
        }

        inline GLenum glCompareOp(CompareOp op)
        {
            switch (op)
            {
            case CompareOp::Never: return GL_NEVER;
            case CompareOp::Less: return GL_LESS;
            case CompareOp::Equal: return GL_EQUAL;
            case CompareOp::LessOrEqual: return GL_LEQUAL;
            case CompareOp::Greater: return GL_GREATER;
            case CompareOp::NotEqual: return GL_NOTEQUAL;
            case CompareOp::GreaterOrEqual: return GL_GEQUAL;
            case CompareOp::Always: return GL_ALWAYS;
            }
            return GL_ALWAYS;
        }

        inline GLenum glBlendFactor(BlendFactor f)
        {
            switch (f)
            {
            case BlendFactor::Zero: return GL_ZERO;
            case BlendFactor::One: return GL_ONE;
            case BlendFactor::SrcColor: return GL_SRC_COLOR;
            case BlendFactor::OneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
            case BlendFactor::DstColor: return GL_DST_COLOR;
            case BlendFactor::OneMinusDstColor: return GL_ONE_MINUS_DST_COLOR;
            case BlendFactor::SrcAlpha: return GL_SRC_ALPHA;
            case BlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
            case BlendFactor::DstAlpha: return GL_DST_ALPHA;
            case BlendFactor::OneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
            case BlendFactor::ConstantColor: return GL_CONSTANT_COLOR;
            case BlendFactor::OneMinusConstantColor: return GL_ONE_MINUS_CONSTANT_COLOR;
            }
            return GL_ONE;
        }

        inline GLenum glBlendOp(BlendOp op)
        {
            switch (op)
            {
            case BlendOp::Add: return GL_FUNC_ADD;
            case BlendOp::Subtract: return GL_FUNC_SUBTRACT;
            case BlendOp::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
            case BlendOp::Min: return GL_MIN;
            case BlendOp::Max: return GL_MAX;
            }
            return GL_FUNC_ADD;
        }

        inline GLenum glTopology(PrimitiveTopology t)
        {
            switch (t)
            {
            case PrimitiveTopology::TriangleList: return GL_TRIANGLES;
            case PrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
            case PrimitiveTopology::LineList: return GL_LINES;
            case PrimitiveTopology::LineStrip: return GL_LINE_STRIP;
            case PrimitiveTopology::PointList: return GL_POINTS;
            }
            return GL_TRIANGLES;
        }

        inline GLenum glVertexFormatGL(VertexFormat f)
        {
            switch (f)
            {
            case VertexFormat::Float1:
            case VertexFormat::Float2:
            case VertexFormat::Float3:
            case VertexFormat::Float4: return GL_FLOAT;
            case VertexFormat::UByte4:
            case VertexFormat::UByte4Norm: return GL_UNSIGNED_BYTE;
            case VertexFormat::Short2:
            case VertexFormat::Short4:
            case VertexFormat::Short2Norm:
            case VertexFormat::Short4Norm: return GL_SHORT;
            case VertexFormat::UInt1:
            case VertexFormat::UInt2:
            case VertexFormat::UInt4: return GL_UNSIGNED_INT;
            default: return GL_FLOAT;
            }
        }

        inline GLint glVertexComponentCount(VertexFormat f)
        {
            switch (f)
            {
            case VertexFormat::Float1: return 1;
            case VertexFormat::Float2: return 2;
            case VertexFormat::Float3: return 3;
            case VertexFormat::Float4: return 4;
            case VertexFormat::UByte4:
            case VertexFormat::UByte4Norm: return 4;
            case VertexFormat::Short2:
            case VertexFormat::Short2Norm: return 2;
            case VertexFormat::Short4:
            case VertexFormat::Short4Norm: return 4;
            case VertexFormat::UInt1: return 1;
            case VertexFormat::UInt2: return 2;
            case VertexFormat::UInt4: return 4;
            default: return 4;
            }
        }

        inline GLboolean glVertexNormalized(VertexFormat f)
        {
            return (f == VertexFormat::UByte4Norm || f == VertexFormat::Short2Norm || f == VertexFormat::Short4Norm)
                       ? GL_TRUE
                       : GL_FALSE;
        }

    } // namespace RHI
} // namespace Genesis

#endif /* __GENESIS_GL_CONVERT_HPP__ */
