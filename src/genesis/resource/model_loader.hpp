/*
 * Genesis Render - Engine
 * model_loader.hpp : import glTF / GLB / FBX / OBJ models via Open Asset Import
 *                    Library into the engine neutral CPUModel structure.
 */
#pragma once

#include "model.hpp"
#include <string>

namespace Genesis
{
    class ModelLoader
    {
    public:
        // Returns false on failure. Supports glTF 2.0 (.gltf/.glb), FBX (.fbx),
        // OBJ and everything else Assimp was built with.
        static bool load(const std::string &path, CPUModel &out);
    };
}
