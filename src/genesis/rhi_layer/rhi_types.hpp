/*
 * @Descripttion: 后端无关的渲染硬件接口(RHI)类型定义
 *               对齐 Piccolo 的 RHI 抽象思路, 但真正做到后端中立:
 *               所有公开类型不出现任何 Vulkan / OpenGL 原生类型
 * @version: v1.0
 */
#ifndef __GENESIS_RHI_TYPES_H__
#define __GENESIS_RHI_TYPES_H__

#include "foundation_layer/platform.hpp"

#include <cstdint>
#include <cstddef>

namespace Genesis
{
    namespace rhi
    {
        //-----------------------------------------------------------
        //------------------------[Handles]-------------------------|
        //-----------------------------------------------------------
        typedef uint32_t ResourceHandle;
        static const ResourceHandle kInvalidHandle = 0xffffffff;

        struct BufferHandle
        {
            ResourceHandle m_handle = kInvalidHandle;
            bool isValid() const { return m_handle != kInvalidHandle; }
            bool operator==(const BufferHandle &o) const { return m_handle == o.m_handle; }
        };
        struct TextureHandle
        {
            ResourceHandle m_handle = kInvalidHandle;
            bool isValid() const { return m_handle != kInvalidHandle; }
            bool operator==(const TextureHandle &o) const { return m_handle == o.m_handle; }
        };
        struct SamplerHandle
        {
            ResourceHandle m_handle = kInvalidHandle;
            bool isValid() const { return m_handle != kInvalidHandle; }
            bool operator==(const SamplerHandle &o) const { return m_handle == o.m_handle; }
        };
        struct ShaderHandle
        {
            ResourceHandle m_handle = kInvalidHandle;
            bool isValid() const { return m_handle != kInvalidHandle; }
            bool operator==(const ShaderHandle &o) const { return m_handle == o.m_handle; }
        };
        struct PipelineHandle
        {
            ResourceHandle m_handle = kInvalidHandle;
            bool isValid() const { return m_handle != kInvalidHandle; }
            bool operator==(const PipelineHandle &o) const { return m_handle == o.m_handle; }
        };
        struct DescriptorSetLayoutHandle
        {
            ResourceHandle m_handle = kInvalidHandle;
            bool isValid() const { return m_handle != kInvalidHandle; }
            bool operator==(const DescriptorSetLayoutHandle &o) const { return m_handle == o.m_handle; }
        };
        struct DescriptorSetHandle
        {
            ResourceHandle m_handle = kInvalidHandle;
            bool isValid() const { return m_handle != kInvalidHandle; }
            bool operator==(const DescriptorSetHandle &o) const { return m_handle == o.m_handle; }
        };
        struct RenderTargetHandle
        {
            ResourceHandle m_handle = kInvalidHandle;
            // invalid handle 表示默认交换链目标
            bool isSwapchain() const { return m_handle == kInvalidHandle; }
            bool isValid() const { return m_handle != kInvalidHandle; }
        };

        //-----------------------------------------------------------
        //-------------------------[Enums]--------------------------|
        //-----------------------------------------------------------
        enum class Backend : uint8_t
        {
            Vulkan,
            OpenGL,
        };

        enum class Format : uint8_t
        {
            Undefined,
            R8_UNorm,
            RG8_UNorm,
            RGBA8_UNorm,
            BGRA8_UNorm,
            RGBA8_sRGB,
            BGRA8_sRGB,
            RG16_Float,
            RGBA16_Float,
            RGBA32_Float,
            D32_Float,
            D24_S8,
            Count
        };

        enum class TextureType : uint8_t
        {
            Texture1D,
            Texture2D,
            Texture3D,
            TextureCube,
        };

        namespace BufferUsage
        {
            enum Enum : uint8_t
            {
                Vertex = 1 << 0,
                Index = 1 << 1,
                Uniform = 1 << 2,
            };
        }

        enum class ShaderStage : uint8_t
        {
            Vertex = 0,
            Fragment = 1,
        };

        namespace ShaderStageFlags
        {
            enum Enum : uint8_t
            {
                Vertex = 1 << 0,
                Fragment = 1 << 1,
                All = Vertex | Fragment,
            };
        }

        enum class PrimitiveTopology : uint8_t
        {
            TriangleList,
            LineList,
        };

        enum class CullMode : uint8_t
        {
            None,
            Front,
            Back,
        };

        enum class FrontFace : uint8_t
        {
            CounterClockwise,
            Clockwise,
        };

        enum class CompareOp : uint8_t
        {
            Never,
            Less,
            LessOrEqual,
            Greater,
            GreaterOrEqual,
            Equal,
            Always,
        };

        enum class Filter : uint8_t
        {
            Nearest,
            Linear,
        };

        enum class AddressMode : uint8_t
        {
            Repeat,
            MirroredRepeat,
            ClampToEdge,
        };

        enum class VertexFormat : uint8_t
        {
            Float,
            Float2,
            Float3,
            Float4,
        };

        enum class DescriptorType : uint8_t
        {
            UniformBuffer,
            CombinedImageSampler, // texture2d / textureCube + sampler
        };

        //-----------------------------------------------------------
        //---------------------[Create Infos]-----------------------|
        //-----------------------------------------------------------
        static const uint8_t kMaxVertexAttributes = 8;
        static const uint8_t kMaxColorAttachments = 4;
        static const uint8_t kMaxDescriptorBindings = 16;
        static const uint8_t kMaxDescriptorSetSlots = 4;

        struct BufferCreateInfo
        {
            uint32_t m_usage = 0; // BufferUsage::Enum 位掩码
            uint64_t m_size = 0;
            const void *m_data = nullptr; // 可选初始数据
            const char *m_name = nullptr;
        };

        struct TextureCreateInfo
        {
            TextureType m_type = TextureType::Texture2D;
            Format m_format = Format::RGBA8_UNorm;
            uint32_t m_width = 1;
            uint32_t m_height = 1;
            uint32_t m_depth = 1;
            uint32_t m_mip_levels = 1;
            uint32_t m_array_layers = 1; // cubemap 为 6
            bool m_render_target = false;
            bool m_generate_mips = false;
            const void *m_data = nullptr; // 可选: 全部 layer/mip 紧密排列
            const char *m_name = nullptr;
        };

        struct SamplerCreateInfo
        {
            Filter m_min_filter = Filter::Linear;
            Filter m_mag_filter = Filter::Linear;
            Filter m_mipmap_filter = Filter::Linear;
            AddressMode m_address_u = AddressMode::Repeat;
            AddressMode m_address_v = AddressMode::Repeat;
            AddressMode m_address_w = AddressMode::Repeat;
            float m_max_lod = 16.0f;
            const char *m_name = nullptr;
        };

        struct ShaderCreateInfo
        {
            ShaderStage m_stage = ShaderStage::Vertex;
            // Vulkan 后端使用 SPIR-V 字节码
            const uint32_t *m_spirv = nullptr;
            size_t m_spirv_size = 0; // 字节数
            // OpenGL 后端使用 GLSL 源码 (不含 #version 行, 由后端补全)
            const char *m_glsl = nullptr;
            const char *m_name = nullptr;
        };

        struct VertexAttribute
        {
            uint32_t m_location = 0;
            VertexFormat m_format = VertexFormat::Float3;
            uint32_t m_offset = 0;
        };

        struct VertexBinding
        {
            uint32_t m_stride = 0;
            VertexAttribute m_attributes[kMaxVertexAttributes];
            uint32_t m_attribute_count = 0;
        };

        // 渲染目标布局: 用于管线与 render pass 匹配
        struct RenderTargetLayout
        {
            Format m_color_formats[kMaxColorAttachments];
            uint32_t m_color_count = 1;
            Format m_depth_format = Format::Undefined;

            bool operator==(const RenderTargetLayout &o) const
            {
                if (m_color_count != o.m_color_count || m_depth_format != o.m_depth_format)
                    return false;
                for (uint32_t i = 0; i < m_color_count; ++i)
                    if (m_color_formats[i] != o.m_color_formats[i])
                        return false;
                return true;
            }
        };

        struct PipelineCreateInfo
        {
            ShaderHandle m_vs;
            ShaderHandle m_fs;
            VertexBinding m_vertex_binding;
            PrimitiveTopology m_topology = PrimitiveTopology::TriangleList;
            CullMode m_cull_mode = CullMode::Back;
            FrontFace m_front_face = FrontFace::CounterClockwise;
            bool m_depth_test = true;
            bool m_depth_write = true;
            CompareOp m_depth_compare = CompareOp::LessOrEqual;
            bool m_blend_enable = false;
            RenderTargetLayout m_render_target_layout;
            DescriptorSetLayoutHandle m_set_layouts[kMaxDescriptorSetSlots];
            uint32_t m_set_layout_count = 0;
            const char *m_name = nullptr;
        };

        struct DescriptorBinding
        {
            uint32_t m_binding = 0;
            DescriptorType m_type = DescriptorType::UniformBuffer;
            uint32_t m_stages = ShaderStageFlags::All;
            const char *m_name = nullptr; // OpenGL 后端按名字查找 uniform
        };

        struct DescriptorSetLayoutCreateInfo
        {
            DescriptorBinding m_bindings[kMaxDescriptorBindings];
            uint32_t m_binding_count = 0;
            const char *m_name = nullptr;
        };

        struct DescriptorWrite
        {
            uint32_t m_binding = 0;
            // uniform buffer
            BufferHandle m_buffer;
            uint64_t m_buffer_offset = 0;
            uint64_t m_buffer_size = 0;
            // texture
            TextureHandle m_texture;
            SamplerHandle m_sampler;
        };

        struct DescriptorSetCreateInfo
        {
            DescriptorSetLayoutHandle m_layout;
            DescriptorWrite m_writes[kMaxDescriptorBindings];
            uint32_t m_write_count = 0;
            const char *m_name = nullptr;
        };

        struct RenderTargetAttachment
        {
            TextureHandle m_texture;
            uint32_t m_mip_level = 0;
            uint32_t m_layer = 0; // cubemap face
        };

        struct RenderTargetCreateInfo
        {
            RenderTargetAttachment m_colors[kMaxColorAttachments];
            uint32_t m_color_count = 0;
            RenderTargetAttachment m_depth;
            bool m_has_depth = false;
            const char *m_name = nullptr;
        };

        struct TextureInfo
        {
            TextureType m_type;
            Format m_format;
            uint32_t m_width, m_height, m_depth;
            uint32_t m_mip_levels, m_array_layers;
        };

        struct DeviceCreateInfo
        {
            Backend m_backend = Backend::Vulkan;
            void *m_window = nullptr; // SDL_Window*
            uint32_t m_width = 1;
            uint32_t m_height = 1;
            bool m_enable_debug = false;
        };

        //-----------------------------------------------------------
        //----------------------[Helpers]---------------------------|
        //-----------------------------------------------------------
        inline uint32_t formatSize(Format format)
        {
            switch (format)
            {
            case Format::R8_UNorm: return 1;
            case Format::RG8_UNorm: return 2;
            case Format::RGBA8_UNorm:
            case Format::BGRA8_UNorm:
            case Format::RGBA8_sRGB:
            case Format::BGRA8_sRGB: return 4;
            case Format::RG16_Float: return 4;
            case Format::RGBA16_Float: return 8;
            case Format::RGBA32_Float: return 16;
            case Format::D32_Float: return 4;
            case Format::D24_S8: return 4;
            default: return 4;
            }
        }

        inline bool isDepthFormat(Format format)
        {
            return format == Format::D32_Float || format == Format::D24_S8;
        }

        // float32 -> float16 转换 (用于 HDR 纹理上传)
        uint16_t floatToHalf(float value);
        // float16 -> float32
        float halfToFloat(uint16_t value);

    } // namespace rhi
} // namespace Genesis

#endif // __GENESIS_RHI_TYPES_H__
