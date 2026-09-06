/*
 * Genesis Render - Engine
 * shader_embeds.hpp : access to shaders embedded at build time by
 *                    cmake/embed_shaders.py (glsl sources + compiled SPIR-V).
 */
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

namespace Genesis
{
    // raw source of any shader file (including shared .glsl includes)
    const char *embeddedShaderGLSL(const std::string &name);
    // compiled SPIR-V for an entry shader; nullptr / size 0 when unavailable
    const uint32_t *embeddedShaderSPV(const std::string &name, size_t &out_bytes);
}
