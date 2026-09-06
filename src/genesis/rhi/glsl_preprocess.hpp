/*
 * Genesis Render - Engine
 * glsl_preprocess.hpp : convert the single-source Vulkan-style GLSL
 * (#version 450, layout(set=,binding=), push_constant, gl_VertexIndex ...)
 * into desktop OpenGL GLSL 410 consumed by the GL backend at runtime.
 */
#pragma once

#include <string>
#include <functional>

namespace Genesis
{
    // Resolve #include "file.glsl" lines using a resolver callback that returns
    // the include contents by filename (recursive includes are supported).
    std::string resolveShaderIncludes(
        const std::string &source,
        const std::function<std::string(const std::string &)> &resolver,
        int depth = 0);

    // Vulkan-flavoured GLSL -> OpenGL 4.10 core GLSL.
    std::string preprocessGLSLForOpenGL(const std::string &vulkan_glsl);
}
