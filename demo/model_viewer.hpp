#pragma once

#include "engine/engine.hpp"
#include <string>
#include <vector>

namespace Demo
{
    // One ECS target at a time. Imported CPU/GPU assets are reused for the
    // lifetime of this viewer (and its Engine), including across scene changes.
    class ModelViewer
    {
    public:
        bool load(Genesis::Engine &engine, const std::string &path, std::string &error);
        void detach(Genesis::Engine &engine);
        void resetTransform(Genesis::Engine &engine);
        void focus(Genesis::Engine &engine, Genesis::ECS::Entity camera);
        Genesis::ECS::Entity target() const { return target_; }
        const std::string &path() const { return path_; }
        static bool accepts(const std::string &path);
        static Genesis::ECS::TransformComponent fittedTransform(const Genesis::Math::AABB &bounds);

    private:
        struct Asset
        {
            std::string path;
            std::shared_ptr<Genesis::ModelData> model;
            std::vector<uint32_t> gpu_meshes;
            Genesis::ECS::TransformComponent transform;
        };
        std::vector<Asset> assets_;
        Genesis::ECS::Entity target_ = Genesis::ECS::nullEntity();
        std::string path_;
    };
}
