/*
 * Genesis Render - ECS component declarations.
 * Components are plain data; systems (Scene / ScriptSystem / PBRRenderer)
 * operate on them through Registry views.
 */
#pragma once

#include "core/math.hpp"
#include "ecs/registry.hpp"
#include <string>
#include <vector>

namespace Genesis {

struct GpuModel; // defined in resource/asset_manager.h

struct TagComponent {
    std::string name = "Entity";
    bool visible_in_hierarchy = true;
};

struct TransformComponent {
    Vec3 position{0, 0, 0};
    Vec3 euler{0, 0, 0};       // radians
    Vec3 scale{1, 1, 1};
    Entity parent{0};
    std::vector<Entity> children;

    Mat4 local{1.0f};
    Mat4 world{1.0f};
    bool dirty = true;
};

struct CameraComponent {
    float fov = 60.0f * kDeg2Rad;
    float nearPlane = 0.1f;
    float farPlane = 200.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    Vec3 target{0, 0.5f, 0};  // orbit target
    float distance = 4.0f;
    bool isMain = false;
    bool orbit = true;
};

enum class LightType : uint8_t { Directional, Point, Spot };

struct LightComponent {
    LightType type = LightType::Directional;
    Vec3 color{1, 1, 1};
    float intensity = 3.0f;
    float range = 10.0f;
    bool castShadow = false;
    bool enabled = true;
};

struct MeshComponent {
    GpuModel* model = nullptr;
    int32_t node = -1;      // model node to render (-1 = whole model)
    bool visible = true;
    Vec3 tint{1, 1, 1};
    float tintAlpha = 0.0f; // 0 = ignore tint
};

struct ScriptComponent {
    std::string path;       // lua file under assets/scripts
    bool enabled = true;
    bool started = false;
    int handle = -1;        // script instance id (LuaEngine)
};

} // namespace Genesis
