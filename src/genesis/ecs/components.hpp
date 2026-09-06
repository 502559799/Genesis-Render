/*
 * Genesis Render - Engine
 * components.hpp : built-in ECS components.
 */
#pragma once

#include "entity.hpp"
#include "math/math.hpp"
#include "resource/model.hpp"

#include <string>
#include <vector>
#include <memory>

namespace Genesis
{
    struct NameComponent
    {
        std::string name = "Entity";
    };

    struct TransformComponent
    {
        Vec3 position{0, 0, 0};
        Quat rotation = Quat::identity();
        Vec3 scale{1, 1, 1};

        Entity parent = kInvalidEntity;
        std::vector<Entity> children;

        Mat4 local;
        Mat4 world;
        Mat4 normal_matrix;
        bool dirty = true;
    };

    struct MeshComponent
    {
        std::shared_ptr<ModelAsset> model;
        Vec4 tint{1, 1, 1, 1};
        bool visible = true;
    };

    struct CameraComponent
    {
        float fov_y = 55.0f;   // degrees
        float near_z = 0.1f;
        float far_z = 300.0f;
        bool orthographic = false;
        float ortho_size = 10.0f;
        bool active = true;
        // runtime orbit control
        float yaw = kPi * 0.25f;
        float pitch = -0.35f;
        float distance = 6.0f;
        Vec3 target{0, 0.5f, 0};
    };

    struct DirectionalLightComponent
    {
        Vec3 color{1, 1, 1};
        float intensity = 3.0f;
    };

    struct PointLightComponent
    {
        Vec3 color{1, 1, 1};
        float intensity = 18.0f;
        float range = 25.0f;
    };

    // Scene-wide image based lighting; the first component of this type wins.
    struct EnvironmentComponent
    {
        std::string equirect_path; // .hdr; empty => procedural
        float intensity = 1.0f;
        bool show_skybox = true;
    };

    struct LuaScriptComponent
    {
        std::string path;
        bool enabled = true;
        bool started = false; // runtime
    };
}
