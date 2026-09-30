/*
 * Genesis Render - graphics pipeline cache implementation.
 */
#include "renderer/pipeline_cache.hpp"
#include "renderer/render_internal.hpp"
#include "foundation_layer/genesis_log.hpp"

namespace Genesis
{
    namespace Render
    {
        uint64_t PipelineKey::hash() const
        {
            uint64_t h = 1469598103934665603ull;
            auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
            mix((uint64_t)(uintptr_t)vertex_shader);
            mix((uint64_t)(uintptr_t)fragment_shader);
            mix((uint64_t)(uintptr_t)layout);
            mix((uint64_t)vertex_layout);
            mix((uint64_t)topology);
            mix((uint64_t)cull);
            mix((uint64_t)polygon);
            mix(depth_test ? 1 : 0);
            mix(depth_write ? 1 : 0);
            mix((uint64_t)depth_compare);
            mix((uint64_t)blend);
            mix((uint64_t)color_format);
            mix((uint64_t)depth_format);
            return h;
        }

        RHI::VertexLayout PipelineCache::vertexLayout(VertexLayoutKind kind)
        {
            switch (kind)
            {
            case VertexLayoutKind::Mesh: return detail::meshVertexLayout();
            case VertexLayoutKind::Fullscreen: return detail::quadVertexLayout();
            case VertexLayoutKind::Line: return detail::lineVertexLayout();
            case VertexLayoutKind::Batch: return detail::batchVertexLayout();
            case VertexLayoutKind::ImGui: return detail::imguiVertexLayout();
            }
            return detail::meshVertexLayout();
        }

        RHI::RHIPipeline *PipelineCache::get(const PipelineKey &key, const char *debug_name)
        {
            const uint64_t h = key.hash();
            auto it = pipelines_.find(h);
            if (it != pipelines_.end()) return it->second;

            RHI::PipelineDesc d;
            d.name = debug_name ? debug_name : (std::string(key.vertex_shader) + "+" + key.fragment_shader);
            RHI::ShaderStageDesc vs, fs;
            if (!detail::fillShaderStage(key.vertex_shader, device_->backend(), RHI::ShaderStageFlagBits::Vertex, vs) ||
                !detail::fillShaderStage(key.fragment_shader, device_->backend(), RHI::ShaderStageFlagBits::Fragment, fs))
            {
                pipelines_[h] = nullptr;
                return nullptr;
            }
            d.shaders = {vs, fs};
            d.vertex_layout = vertexLayout(key.vertex_layout);
            d.topology = key.topology;
            d.rasterizer.cull_mode = key.cull;
            d.rasterizer.front_face = RHI::FrontFace::CounterClockwise;
            d.rasterizer.polygon_mode = key.polygon;
            d.depth_stencil.depth_test = key.depth_test && key.depth_format != RHI::PixelFormat::Undefined;
            d.depth_stencil.depth_write = key.depth_write && d.depth_stencil.depth_test;
            d.depth_stencil.depth_compare = key.depth_compare;

            RHI::BlendAttachmentState &b = d.blend.attachments[0];
            d.blend.attachment_count = 1;
            switch (key.blend)
            {
            case BlendPreset::Opaque:
                b.blend_enable = false;
                break;
            case BlendPreset::Alpha:
                b.blend_enable = true;
                b.src_color = RHI::BlendFactor::SrcAlpha;
                b.dst_color = RHI::BlendFactor::OneMinusSrcAlpha;
                b.src_alpha = RHI::BlendFactor::One;
                b.dst_alpha = RHI::BlendFactor::OneMinusSrcAlpha;
                break;
            case BlendPreset::Additive:
                b.blend_enable = true;
                b.src_color = RHI::BlendFactor::SrcAlpha;
                b.dst_color = RHI::BlendFactor::One;
                b.src_alpha = RHI::BlendFactor::Zero;
                b.dst_alpha = RHI::BlendFactor::One;
                break;
            case BlendPreset::Premultiplied:
                b.blend_enable = true;
                b.src_color = RHI::BlendFactor::One;
                b.dst_color = RHI::BlendFactor::OneMinusSrcAlpha;
                b.src_alpha = RHI::BlendFactor::One;
                b.dst_alpha = RHI::BlendFactor::OneMinusSrcAlpha;
                break;
            }
            if (key.layout) d.resource_layout = *key.layout;
            d.color_formats[0] = key.color_format;
            d.color_format_count = 1;
            d.depth_format = key.depth_format;

            RHI::RHIPipeline *p = device_->createPipeline(d);
            if (!p) GEN_LOG_ERROR("[Pipeline] failed to create '%s'", d.name.c_str());
            pipelines_[h] = p;
            return p;
        }

        void PipelineCache::shutdown()
        {
            if (!device_) return;
            for (auto &kv : pipelines_)
                if (kv.second) device_->destroyPipeline(kv.second);
            pipelines_.clear();
        }

    } // namespace Render
} // namespace Genesis
