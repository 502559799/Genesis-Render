/*
 * Genesis Render - model loading dispatch.
 *   .gltf/.glb -> cgltf
 *   .fbx/.obj/.dae/... -> Assimp (when GENESIS_WITH_ASSIMP)
 */
#pragma once

#include "resource/mesh_data.hpp"
#include <memory>
#include <string>

namespace Genesis {

// Returns nullptr and fills err on failure. Result is merged (see mergeMeshes).
std::shared_ptr<ModelAsset> loadModelFile(const std::string& path, std::string& err);

std::shared_ptr<ModelAsset> loadGLTF(const std::string& path, std::string& err);

#ifdef GENESIS_WITH_ASSIMP
std::shared_ptr<ModelAsset> loadAssimpModel(const std::string& path, std::string& err);
#endif

} // namespace Genesis
