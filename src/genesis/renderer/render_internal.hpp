/*
 * Genesis Render - internal helpers shared by the render system translation units
 */
#ifndef __GENESIS_RENDER_INTERNAL_HPP__
#define __GENESIS_RENDER_INTERNAL_HPP__

#include "foundation_layer/genesis_log.hpp"
#include "renderer/render_system.hpp"
#include "rhi/rhi.hpp"

#include "genesis_shaders_generated.h"

namespace Genesis
{
    namespace Render
    {
        namespace detail
        {
            inline bool fillShaderStage(const char *shader_name, RHI::BackendType backend,
                                        RHI::ShaderStageFlagBits stage, RHI::ShaderStageDesc &out)
            {
                const Shaders::ShaderBinary *binary = Shaders::findShader(shader_name);
                if (!binary)
                {
                    GEN_LOG_ERROR("[Render] shader '%s' was not baked into the binary (run scripts/build_shaders.py)",
                                  shader_name);
                    return false;
                }
                out.stage = stage;
                out.name = shader_name;
                out.entry = "main";
                if (backend == RHI::BackendType::Vulkan)
                {
                    if (!binary->spirv || binary->spirv_words == 0)
                    {
                        GEN_LOG_ERROR("[Render] shader '%s' has no SPIR-V variant", shader_name);
                        return false;
                    }
                    out.code_type = RHI::ShaderCodeType::Spirv;
                    out.code = binary->spirv;
                    out.code_size = binary->spirv_words * sizeof(uint32_t);
                }
                else
                {
                    out.code_type = RHI::ShaderCodeType::GlslSource;
                    out.code = binary->glsl;
                    out.code_size = binary->glsl_size;
                }
                return true;
            }

            inline RHI::VertexLayout meshVertexLayout()
            {
                RHI::VertexLayout layout;
                layout.stride = kVertexStride;
                layout.attributes = {
                    {0, RHI::VertexFormat::Float3, offsetof(Vertex, position)},
                    {1, RHI::VertexFormat::Float3, offsetof(Vertex, normal)},
                    {2, RHI::VertexFormat::Float4, offsetof(Vertex, tangent)},
                    {3, RHI::VertexFormat::Float2, offsetof(Vertex, uv0)},
                    {4, RHI::VertexFormat::Float2, offsetof(Vertex, uv1)},
                    {5, RHI::VertexFormat::Float4, offsetof(Vertex, joints)},
                    {6, RHI::VertexFormat::Float4, offsetof(Vertex, weights)},
                };
                return layout;
            }

            inline RHI::VertexLayout quadVertexLayout()
            {
                RHI::VertexLayout layout;
                layout.stride = 16;
                layout.attributes = {
                    {0, RHI::VertexFormat::Float2, 0},
                    {1, RHI::VertexFormat::Float2, 8},
                };
                return layout;
            }

            inline RHI::VertexLayout lineVertexLayout()
            {
                RHI::VertexLayout layout;
                layout.stride = 28;
                layout.attributes = {
                    {0, RHI::VertexFormat::Float3, 0},
                    {1, RHI::VertexFormat::Float4, 12},
                };
                return layout;
            }

            // Fullscreen quad: two triangles.  uv (0,0) is paired with NDC
            // (-1,-1), which is row 0 of the render target on BOTH backends.
            struct QuadVertex
            {
                float pos[2];
                float uv[2];
            };

            inline const QuadVertex *quadVertices()
            {
                static const QuadVertex k_vertices[6] = {
                    {{-1.0f, -1.0f}, {0.0f, 0.0f}},
                    {{1.0f, -1.0f}, {1.0f, 0.0f}},
                    {{1.0f, 1.0f}, {1.0f, 1.0f}},
                    {{-1.0f, -1.0f}, {0.0f, 0.0f}},
                    {{1.0f, 1.0f}, {1.0f, 1.0f}},
                    {{-1.0f, 1.0f}, {0.0f, 1.0f}},
                };
                return k_vertices;
            }

            inline uint32_t mipLevelsFor(uint32_t size)
            {
                uint32_t levels = 1;
                while (size > 1)
                {
                    size >>= 1;
                    ++levels;
                }
                return levels;
            }
        } // namespace detail
    }     // namespace Render
} // namespace Genesis

#endif /* __GENESIS_RENDER_INTERNAL_HPP__ */
