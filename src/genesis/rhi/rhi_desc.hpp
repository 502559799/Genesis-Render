/*
 * Genesis Render - RHI descriptor structs
 *
 * Plain-data descriptions of every GPU object the engine can create.
 * Both backends (OpenGL / Vulkan) consume exactly these structs, so renderer
 * code never mentions a native API type.
 */
#ifndef __GENESIS_RHI_DESC_HPP__
#define __GENESIS_RHI_DESC_HPP__

#include "rhi_enums.hpp"
#include <string>
#include <vector>

namespace Genesis
{
    namespace RHI
    {
        //------------------------------------------------------------------
        // Buffer
        //------------------------------------------------------------------
        struct BufferDesc
        {
            uint64_t size = 0;
            BufferUsageFlags usage = (uint32_t)BufferUsageFlagBits::Vertex;
            MemoryUsage memory_usage = MemoryUsage::GpuOnly;
            const void *initial_data = nullptr;
            uint64_t initial_data_size = 0;
            std::string name;
        };

        //------------------------------------------------------------------
        // Sampler
        //------------------------------------------------------------------
        struct SamplerDesc
        {
            FilterMode min_filter = FilterMode::Linear;
            FilterMode mag_filter = FilterMode::Linear;
            MipmapMode mipmap_mode = MipmapMode::Linear;
            AddressMode address_u = AddressMode::Repeat;
            AddressMode address_v = AddressMode::Repeat;
            AddressMode address_w = AddressMode::Repeat;
            CompareOp compare_op = CompareOp::Always;
            bool compare_enable = false;
            float min_lod = 0.0f;
            float max_lod = 16.0f;
            float border_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float max_anisotropy = 1.0f;
            std::string name;
        };

        //------------------------------------------------------------------
        // Texture
        //------------------------------------------------------------------
        struct TextureDesc
        {
            TextureType type = TextureType::Tex2D;
            PixelFormat format = PixelFormat::RGBA8_UNORM;
            uint32_t width = 1;
            uint32_t height = 1;
            uint32_t depth = 1;
            uint32_t array_layers = 1; // 6 for cubemaps
            uint32_t mip_levels = 1;
            uint32_t sample_count = 1;
            TextureUsageFlags usage = (uint32_t)TextureUsageFlagBits::Sampled;
            MemoryUsage memory_usage = MemoryUsage::GpuOnly;
            bool generate_mipmaps = false;
            std::string name;

            uint32_t layerCount() const
            {
                return type == TextureType::TexCube ? 6u * array_layers : array_layers;
            }
        };

        //------------------------------------------------------------------
        // Shader
        //------------------------------------------------------------------
        struct ShaderStageDesc
        {
            ShaderStageFlagBits stage = ShaderStageFlagBits::Vertex;
            ShaderCodeType code_type = ShaderCodeType::GlslSource;
            const void *code = nullptr; // GLSL source (NUL terminated) or SPIR-V words
            uint64_t code_size = 0;     // bytes
            std::string entry = "main";
            std::string name;
        };

        //------------------------------------------------------------------
        // Vertex layout
        //------------------------------------------------------------------
        struct VertexAttribute
        {
            uint32_t location = 0;
            VertexFormat format = VertexFormat::Float3;
            uint32_t offset = 0;
        };

        struct VertexLayout
        {
            std::vector<VertexAttribute> attributes;
            uint32_t stride = 0;
            bool per_instance = false; // for instanced attribute streams

            bool valid() const { return stride > 0 && !attributes.empty(); }
        };

        //------------------------------------------------------------------
        // Resource (descriptor) layout
        //
        // `name` is mandatory for the OpenGL backend: it is the uniform block
        // name (buffers) or the sampler uniform name (images) used in GLSL.
        // The Vulkan backend uses set/binding only and ignores the name.
        //------------------------------------------------------------------
        struct ResourceBinding
        {
            uint32_t set = 0;
            uint32_t binding = 0;
            ResourceType type = ResourceType::UniformBuffer;
            ShaderStageFlags stages = (uint32_t)ShaderStageFlagBits::Vertex;
            uint32_t count = 1;
            std::string name;
        };

        struct ResourceLayoutDesc
        {
            std::vector<ResourceBinding> bindings;
            uint32_t push_constant_size = 0;
            ShaderStageFlags push_constant_stages = (uint32_t)ShaderStageFlagBits::Vertex |
                                                    (uint32_t)ShaderStageFlagBits::Fragment;
            std::string name;
        };

        //------------------------------------------------------------------
        // Pipeline state
        //------------------------------------------------------------------
        struct RasterizerState
        {
            CullMode cull_mode = CullMode::Back;
            FrontFace front_face = FrontFace::CounterClockwise;
            PolygonMode polygon_mode = PolygonMode::Fill;
            float line_width = 1.0f;
            bool depth_clamp = false;
        };

        struct DepthStencilState
        {
            bool depth_test = true;
            bool depth_write = true;
            CompareOp depth_compare = CompareOp::LessOrEqual;
            bool stencil_test = false;
            CompareOp stencil_compare = CompareOp::Always;
            uint32_t stencil_reference = 0;
            uint32_t stencil_read_mask = 0xff;
            uint32_t stencil_write_mask = 0xff;
        };

        struct BlendAttachmentState
        {
            bool blend_enable = false;
            BlendFactor src_color = BlendFactor::SrcAlpha;
            BlendFactor dst_color = BlendFactor::OneMinusSrcAlpha;
            BlendOp color_op = BlendOp::Add;
            BlendFactor src_alpha = BlendFactor::One;
            BlendFactor dst_alpha = BlendFactor::OneMinusSrcAlpha;
            BlendOp alpha_op = BlendOp::Add;
            ColorComponentFlags write_mask = (uint32_t)ColorComponentFlagBits::All;
        };

        struct BlendState
        {
            BlendAttachmentState attachments[8];
            uint32_t attachment_count = 1;
            float blend_constant[4] = {0, 0, 0, 0};
        };

        struct PipelineDesc
        {
            std::vector<ShaderStageDesc> shaders;
            VertexLayout vertex_layout;
            PrimitiveTopology topology = PrimitiveTopology::TriangleList;
            RasterizerState rasterizer;
            DepthStencilState depth_stencil;
            BlendState blend;
            ResourceLayoutDesc resource_layout; // inlined; the backend caches shared layouts
            bool is_compute = false;
            uint32_t compute_workgroup_x = 1;
            uint32_t compute_workgroup_y = 1;
            uint32_t compute_workgroup_z = 1;

            // Render-pass compatibility (formats the pipeline will be used with)
            PixelFormat color_formats[8] = {PixelFormat::Undefined};
            uint32_t color_format_count = 1;
            PixelFormat depth_format = PixelFormat::Undefined;
            std::string name;
        };

        //------------------------------------------------------------------
        // Render pass / framebuffer
        //------------------------------------------------------------------
        struct AttachmentDesc
        {
            PixelFormat format = PixelFormat::Undefined;
            LoadOp load_op = LoadOp::Clear;
            StoreOp store_op = StoreOp::Store;
            LoadOp stencil_load_op = LoadOp::DontCare;
            StoreOp stencil_store_op = StoreOp::DontCare;
            TextureLayout initial_layout = TextureLayout::Undefined;
            TextureLayout final_layout = TextureLayout::ColorAttachmentOptimal;
            float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            float clear_depth = 1.0f;
            uint32_t clear_stencil = 0;
            bool is_depth_stencil = false;
        };

        struct RenderPassDesc
        {
            AttachmentDesc attachments[9]; // up to 8 color + 1 depth
            uint32_t attachment_count = 0;
            std::string name;

            uint32_t colorAttachmentCount() const
            {
                uint32_t n = 0;
                for (uint32_t i = 0; i < attachment_count; ++i)
                    if (!attachments[i].is_depth_stencil) ++n;
                return n;
            }
        };

        struct FramebufferDesc
        {
            RenderPassDesc render_pass; // matched by value; backends cache the pass object
            struct Attachment
            {
                void *texture = nullptr; // RHITexture* (avoids include cycle)
                uint32_t mip_level = 0;
                uint32_t array_layer = 0;
            };
            std::vector<Attachment> attachments;
            uint32_t width = 1;
            uint32_t height = 1;
            std::string name;
        };

        struct RenderPassBeginInfo
        {
            struct ClearValue
            {
                float color[4] = {0, 0, 0, 1};
                float depth = 1.0f;
                uint32_t stencil = 0;
            };
            ClearValue clears[9];
            uint32_t clear_count = 0;
        };

        //------------------------------------------------------------------
        // Descriptor writes
        //------------------------------------------------------------------
        struct DescriptorBufferInfo
        {
            void *buffer = nullptr; // RHIBuffer*
            uint64_t offset = 0;
            uint64_t range = 0; // 0 == whole buffer
        };

        struct DescriptorImageInfo
        {
            void *texture = nullptr; // RHITexture*
            void *sampler = nullptr; // RHISampler* (optional for SampledImage)
            TextureLayout layout = TextureLayout::ShaderReadOnlyOptimal;
            uint32_t mip_level = 0;
            uint32_t base_array_layer = 0;
            uint32_t layer_count = 1;
        };

        struct WriteDescriptorSet
        {
            uint32_t binding = 0;
            uint32_t array_element = 0;
            uint32_t count = 1;
            ResourceType type = ResourceType::UniformBuffer;
            const DescriptorBufferInfo *buffer_info = nullptr;
            const DescriptorImageInfo *image_info = nullptr;
        };

        //------------------------------------------------------------------
        // Device creation
        //------------------------------------------------------------------
        struct DeviceDesc
        {
            BackendType backend = BackendType::OpenGL;
            void *native_window = nullptr; // SDL_Window*
            uint32_t width = 1280;
            uint32_t height = 720;
            bool enable_validation = false;
            bool headless = false; // no swapchain presentation (offscreen rendering / tests)
            bool vsync = true;
            bool srgb_backbuffer = false;
            std::string app_name = "Genesis";
        };

        struct DeviceInfo
        {
            BackendType backend = BackendType::OpenGL;
            std::string device_name = "unknown";
            std::string api_version = "0.0";
            uint32_t max_texture_size = 4096;
            uint32_t max_uniform_buffer_bindings = 36;
            uint32_t max_texture_image_units = 16;
            uint64_t max_uniform_buffer_range = 16384;
            uint64_t min_uniform_buffer_offset_alignment = 256;
            uint32_t max_push_constant_size = 128;
            bool supports_compute = false;
            bool supports_geometry_shader = false;
            bool supports_tessellation = false;
            bool supports_multisample = true;
            bool supports_srgb_framebuffer = true;
        };

        //------------------------------------------------------------------
        // Viewport / scissor
        //------------------------------------------------------------------
        struct Viewport
        {
            float x = 0.0f;
            float y = 0.0f;
            float width = 1.0f;
            float height = 1.0f;
            float min_depth = 0.0f;
            float max_depth = 1.0f;
        };

        struct Rect2D
        {
            int32_t x = 0;
            int32_t y = 0;
            uint32_t width = 1;
            uint32_t height = 1;
        };

        //------------------------------------------------------------------
        // Image barriers / copies
        //------------------------------------------------------------------
        struct TextureBarrier
        {
            void *texture = nullptr;
            TextureLayout old_layout = TextureLayout::Undefined;
            TextureLayout new_layout = TextureLayout::ShaderReadOnlyOptimal;
            uint32_t base_mip = 0;
            uint32_t mip_count = ~0u;
            uint32_t base_layer = 0;
            uint32_t layer_count = ~0u;
        };

        struct BufferImageCopy
        {
            uint64_t buffer_offset = 0;
            uint32_t buffer_row_length = 0;   // 0 == tightly packed
            uint32_t buffer_image_height = 0; // 0 == tightly packed
            uint32_t mip_level = 0;
            uint32_t base_layer = 0;
            uint32_t layer_count = 1;
            uint32_t offset_x = 0;
            uint32_t offset_y = 0;
            uint32_t offset_z = 0;
            uint32_t width = 1;
            uint32_t height = 1;
            uint32_t depth = 1;
        };

    } // namespace RHI
} // namespace Genesis

#endif /* __GENESIS_RHI_DESC_HPP__ */
