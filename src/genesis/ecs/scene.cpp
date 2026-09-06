/*
 * Genesis Render - Scene implementation (entity graph + camera system).
 */
#include "ecs/scene.hpp"
#include <algorithm>
#include <cmath>

namespace Genesis {

Entity Scene::createEntity(const std::string& name, Entity parent) {
    Entity e = registry.create();
    registry.add<TagComponent>(e).name = name;
    registry.add<TransformComponent>(e);
    reparent(e, parent);
    return e;
}

void Scene::reparent(Entity e, Entity newParent) {
    auto* t = registry.get<TransformComponent>(e);
    if (!t) return;
    if (t->parent.valid()) {
        auto* old = registry.get<TransformComponent>(t->parent);
        if (old) {
            auto& c = old->children;
            c.erase(std::remove(c.begin(), c.end(), e), c.end());
        }
    }
    t->parent = newParent;
    if (newParent.valid()) {
        auto* p = registry.get<TransformComponent>(newParent);
        if (p) p->children.push_back(e);
    }
}

Entity Scene::findByName(const std::string& name) const {
    Entity found{0};
    const_cast<Registry&>(registry).view<TagComponent>([&](Entity e) {
        auto* tag = registry.get<TagComponent>(e);
        if (tag && tag->name == name && !found.valid()) found = e;
    });
    return found;
}

void Scene::updateHierarchy(Entity e, const Mat4& parent) {
    auto* t = registry.get<TransformComponent>(e);
    if (!t) return;
    t->local = makeTransform(t->position, t->euler, t->scale);
    t->world = parent * t->local;
    for (Entity c : t->children) updateHierarchy(c, t->world);
}

void Scene::updateTransforms() {
    // Roots = transforms without a valid parent component.
    auto& tp = registry.pool<TransformComponent>();
    for (Entity e : tp.entities()) {
        auto* t = tp.get(e.id & 0x00FFFFFF);
        if (!t) continue;
        if (!t->parent.valid() || !registry.alive(t->parent))
            updateHierarchy(e, Mat4(1.0f));
    }
}

const Mat4& Scene::worldMatrix(Entity e) const {
    static Mat4 identity(1.0f);
    auto* t = registry.get<TransformComponent>(e);
    return t ? t->world : identity;
}

Vec3 Scene::cameraPosition() const {
    auto* cam = registry.get<CameraComponent>(mainCamera);
    auto* tr = registry.get<TransformComponent>(mainCamera);
    if (!cam) return {0, 1, 6};
    if (cam->orbit) {
        float cp = std::cos(cam->pitch);
        Vec3 offset{cp * std::sin(cam->yaw), std::sin(cam->pitch), cp * std::cos(cam->yaw)};
        return cam->target + offset * cam->distance;
    }
    return tr ? tr->position : Vec3{0, 1, 6};
}

Mat4 Scene::viewMatrix() const {
    auto* cam = registry.get<CameraComponent>(mainCamera);
    if (!cam) return makeLookAt({0, 1, 6}, {0, 0.5f, 0}, {0, 1, 0});
    Vec3 eye = cameraPosition();
    Vec3 forward = normalize(cam->target - eye);
    Vec3 center = eye + forward;
    return makeLookAt(eye, center, {0, 1, 0});
}

Mat4 Scene::projectionMatrix(float aspect, bool vulkanClip) const {
    auto* cam = registry.get<CameraComponent>(mainCamera);
    float fov = cam ? cam->fov : 60.0f * kDeg2Rad;
    float zn = cam ? cam->nearPlane : 0.1f;
    float zf = cam ? cam->farPlane : 200.0f;
    return makePerspective(fov, aspect, zn, zf, vulkanClip);
}

void Scene::update(float dt) {
    (void)dt;
    // camera orbit position -> transform (so scripts/UI see it)
    if (mainCamera.valid()) {
        if (auto* cam = registry.get<CameraComponent>(mainCamera)) {
            if (cam->orbit) {
                if (auto* tr = registry.get<TransformComponent>(mainCamera))
                    tr->position = cameraPosition();
            }
        }
    }
    updateTransforms();
}

} // namespace Genesis
