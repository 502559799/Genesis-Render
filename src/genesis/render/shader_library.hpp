/*
 * Genesis Render - shader program cache.
 * Each program is one GLSL source pair compiled at build time to SPIR-V
 * (assets/shaders/*.spv for Vulkan) and at runtime from source for OpenGL.
 */
#pragma once

#include "rhi/rhi.hpp"
#include <string>
#include <unordered_map>

namespace Genesis {

class ShaderLibrary {
public:
    ShaderLibrary(IDevice* device, std::string assetRoot);
    IShaderProgram* load(const std::string& name);
    const std::string& root() const { return root_; }

private:
    IDevice* dev_;
    std::string root_;
    std::unordered_map<std::string, IShaderProgram*> cache_;
};

// Locate an asset directory next to the executable / cwd (env override).
std::string findAssetRoot(const char* argv0);

} // namespace Genesis
