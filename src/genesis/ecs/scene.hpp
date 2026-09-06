/*
 * Genesis Render - Scene: owns the ECS registry, camera/light/sky settings
 * and the transform hierarchy system.
 */
#pragma once

#include "ecs/registry.hpp"
#include "ecs/components.hpp"
#include <string>

namespace Genesis {

struct DirectionalLightSettings {
    Vec3 direction{-0.45f, -1.0f, -0.35f};
    Vec3 color{1.0f, 0.97f, 0.9f};
    float intensity = 3.5f;
    bool enabled = true;
};

struct RenderSettings {
    // direct lighting
    DirectionalLightSettings sun;
    bool directLightEnabled = true;
    // 0 = Cook-Torrance PBR (half-vector / Blinn-Phong H), 1 = classic Blinn-Phong
    int lightingModel = 0;
    // indirect (IBL)
    bool iblEnabled = true;
    float iblIntensity = 1.0f;
    float envRotation = 0.0f;
    bool skyboxEnabled = true;
    float exposure = 1.0f;
    Vec4 clearColor{0.02f, 0.02f, 0.03f, 1.0f};
};

class Scene {
public:
    Registry registry;
    RenderSettings settings;
    std::string name = "GenesisScene";
    Entity mainCamera{0};

    Entity createEntity(const std::string& name = "Entity", Entity parent = kInvalidEntity);
    void reparent(Entity e, Entity newParent);

    void update(float dt);

    // world-space matrices computed in update()
    const Mat4& worldMatrix(Entity e) const;
    Mat4 viewMatrix() const;
    Mat4 projectionMatrix(float aspect, bool vulkanClip) const;
    Vec3 cameraPosition() const;

    Entity findByName(const std::string& name) const;

private:
    void updateTransforms();
    void updateHierarchy(Entity root, const Mat4& parent);
};

} // namespace Genesis
