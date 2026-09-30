/*
 * Genesis Render - graphics pipeline cache.
 *
 * Pipelines are baked against the formats of the target they draw into
 * (Vulkan render pass compatibility, Metal attachment formats), and the
 * CommandBuffer can draw into any user render texture, so pipelines are
 * created lazily from a compact key and cached for the lifetime of the
 * renderer.
 */
#ifndef __GENESIS_PIPELINE_CACHE_HPP__
#define __GENESIS_PIPELINE_CACHE_HPP__

#include "rhi/rhi.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace Genesis
{
    namespace Render
    {
        enum class VertexLayoutKind : uint8_t
        {
            Mesh = 0,       // resource/Vertex (88 bytes)
            Fullscreen = 1, // vec2 position + vec2 uv
            Line = 2,       // vec3 position + vec4 colour
            Batch = 3,      // BatchVertex (68 bytes)
            ImGui = 4,      // ImDrawVert (20 bytes)
        };

        enum class BlendPreset : uint8_t
        {
            Opaque = 0,
            Alpha = 1,         // src.a, 1-src.a
            Additive = 2,      // one, one (colour); alpha keeps destination
            Premultiplied = 3, // one, 1-src.a
        };

        struct PipelineKey
        {
            const char *vertex_shader = nullptr;
            const char *fragment_shader = nullptr;
            const RHI::ResourceLayoutDesc *layout = nullptr; // owned by the renderer
            VertexLayoutKind vertex_layout = VertexLayoutKind::Mesh;
            RHI::PrimitiveTopology topology = RHI::PrimitiveTopology::TriangleList;
            RHI::CullMode cull = RHI::CullMode::Back;
            RHI::PolygonMode polygon = RHI::PolygonMode::Fill;
            bool depth_test = true;
            bool depth_write = true;
            RHI::CompareOp depth_compare = RHI::CompareOp::LessOrEqual;
            BlendPreset blend = BlendPreset::Opaque;
            RHI::PixelFormat color_format = RHI::PixelFormat::RGBA16_SFLOAT;
            RHI::PixelFormat depth_format = RHI::PixelFormat::Undefined;

            uint64_t hash() const;
        };

        class PipelineCache
        {
        public:
            void initialize(RHI::RHIDevice *device) { device_ = device; }
            void shutdown();

            // Returns nullptr (and logs once) when the pipeline cannot be built.
            RHI::RHIPipeline *get(const PipelineKey &key, const char *debug_name = nullptr);
            size_t size() const { return pipelines_.size(); }

            static RHI::VertexLayout vertexLayout(VertexLayoutKind kind);

        private:
            RHI::RHIDevice *device_ = nullptr;
            std::unordered_map<uint64_t, RHI::RHIPipeline *> pipelines_;
        };

    } // namespace Render
} // namespace Genesis

#endif /* __GENESIS_PIPELINE_CACHE_HPP__ */
