/*
 * Genesis Render - internal helpers shared by the renderer translation units
 */
#ifndef __GENESIS_RENDER_INTERNAL_HPP__
#define __GENESIS_RENDER_INTERNAL_HPP__

#include "foundation_layer/genesis_log.hpp"
#include "resource/resource_types.hpp"
#include "rhi/rhi.hpp"

#include "genesis_shaders_generated.h"

#include <cstddef>

namespace Genesis
{
    namespace Render
    {
        // One vertex of the batched 2D / 3D primitive and SDF text renderer
        // (shaders/ui/batch.vert).
        struct BatchVertex
        {
            float position[3];
            float uv[2];
            float color[4];
            float params[4];  // x = kind (0 textured, 1 SDF glyph), y = outline width
            float outline[4]; // SDF outline colour
        };
        static_assert(sizeof(BatchVertex) == 68, "BatchVertex: 12 + 8 + 16 + 16 + 16");

        namespace detail
        {
            // Picks the variant of a baked shader that matches the backend:
            // GLSL 410 source (OpenGL), SPIR-V (Vulkan) or MSL (Metal).
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
                else if (backend == RHI::BackendType::Metal)
                {
                    if (!binary->msl || binary->msl_size == 0)
                    {
                        GEN_LOG_ERROR("[Render] shader '%s' has no MSL variant (spirv-cross missing at build time)",
                                      shader_name);
                        return false;
                    }
                    out.code_type = RHI::ShaderCodeType::MslSource;
                    out.code = binary->msl;
                    out.code_size = binary->msl_size;
                    out.entry = "main0";
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

            inline RHI::VertexLayout batchVertexLayout()
            {
                RHI::VertexLayout layout;
                layout.stride = sizeof(BatchVertex);
                layout.attributes = {
                    {0, RHI::VertexFormat::Float3, offsetof(BatchVertex, position)},
                    {1, RHI::VertexFormat::Float2, offsetof(BatchVertex, uv)},
                    {2, RHI::VertexFormat::Float4, offsetof(BatchVertex, color)},
                    {3, RHI::VertexFormat::Float4, offsetof(BatchVertex, params)},
                    {4, RHI::VertexFormat::Float4, offsetof(BatchVertex, outline)},
                };
                return layout;
            }

            // Matches ImDrawVert: vec2 pos, vec2 uv, RGBA8 colour.
            inline RHI::VertexLayout imguiVertexLayout()
            {
                RHI::VertexLayout layout;
                layout.stride = 20;
                layout.attributes = {
                    {0, RHI::VertexFormat::Float2, 0},
                    {1, RHI::VertexFormat::Float2, 8},
                    {2, RHI::VertexFormat::UByte4Norm, 16},
                };
                return layout;
            }

            // Fullscreen quad: two triangles.  uv (0,0) is paired with NDC
            // (-1,-1), which is row 0 of the render target on every backend.
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
