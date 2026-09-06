/*
 * Genesis Render - built-in ECS systems implementation
 */
#include "ecs/systems.hpp"

#include <algorithm>
#include <cmath>

namespace Genesis
{
    namespace ECS
    {
        const char *lightTypeName(LightType type)
        {
            switch (type)
            {
            case LightType::Directional: return "Directional";
            case LightType::Point: return "Point";
            case LightType::Spot: return "Spot";
            }
            return "Unknown";
        }

        //==================================================================
        // TransformSystem
        //==================================================================
        void TransformSystem::update(World &world, float delta_time)
        {
            (void)delta_time;
            auto *pool = world.pool<TransformComponent>();
            if (!pool || pool->count() == 0)
            {
                last_count_ = 0;
                return;
            }

            // Collect (depth, entity) pairs so parents are always processed
            // before their children.
            struct Entry
            {
                uint32_t depth;
                Entity entity;
            };
            std::vector<Entry> entries;
            entries.reserve(pool->count());
            for (EntityId id : pool->entities())
            {
                Entity e{id, 0};
                entries.push_back(Entry{world.depth(e), e});
            }
            std::stable_sort(entries.begin(), entries.end(),
                             [](const Entry &a, const Entry &b) { return a.depth < b.depth; });

            for (const Entry &entry : entries)
            {
                TransformComponent *t = pool->get(entry.entity.id);
                if (!t) continue;
                t->refreshLocalMatrix();

                Entity parent = world.parentOf(entry.entity);
                if (parent.valid())
                {
                    TransformComponent *pt = world.getComponent<TransformComponent>(parent);
                    if (pt)
                    {
                        t->world_matrix = pt->world_matrix * t->local_matrix;
                        t->world_dirty = false;
                        continue;
                    }
                }
                t->world_matrix = t->local_matrix;
                t->world_dirty = false;
            }
            last_count_ = entries.size();
        }

        //==================================================================
        // CameraControllerSystem
        //==================================================================
        void CameraControllerSystem::update(World &world, float delta_time)
        {
            if (!input_) return;

            const bool ui_owns_mouse = input_->ui_wants_mouse;
            const Math::Vec2 mouse_delta = input_->mouseDelta();
            const float wheel = input_->wheelDelta();

            world.view<CameraControllerComponent, TransformComponent>(
                [&](Entity entity, CameraControllerComponent &ctrl, TransformComponent &transform)
                {
                    (void)entity;
                    if (ctrl.mode == CameraControllerComponent::Mode::None) return;

                    const bool dragging = input_->leftDown() || input_->rightDown();

                    if (ctrl.mode == CameraControllerComponent::Mode::Orbit)
                    {
                        if (dragging && !ui_owns_mouse)
                        {
                            ctrl.orbit_yaw_degrees -= mouse_delta.x * ctrl.look_sensitivity;
                            ctrl.orbit_pitch_degrees += mouse_delta.y * ctrl.look_sensitivity;
                            ctrl.orbit_pitch_degrees = Math::clampf(ctrl.orbit_pitch_degrees, -89.0f, 89.0f);
                        }
                        if (wheel != 0.0f && !ui_owns_mouse)
                        {
                            ctrl.orbit_distance *= (1.0f - wheel * 0.1f * ctrl.zoom_sensitivity);
                            ctrl.orbit_distance = Math::clampf(ctrl.orbit_distance, 0.1f, 1000.0f);
                        }

                        float yaw = Math::toRadians(ctrl.orbit_yaw_degrees);
                        float pitch = Math::toRadians(ctrl.orbit_pitch_degrees);
                        // Spherical coordinates around the target.
                        Math::Vec3 dir(std::cos(pitch) * std::sin(yaw),
                                       std::sin(pitch),
                                       std::cos(pitch) * std::cos(yaw));
                        transform.position = ctrl.orbit_target + dir * ctrl.orbit_distance;
                        transform.rotation = Math::Quat::lookRotation(ctrl.orbit_target - transform.position, Math::Vec3(0, 1, 0));
                        transform.scale = Math::Vec3(1.0f);
                    }
                    else // Fly
                    {
                        if (dragging && !ui_owns_mouse)
                        {
                            Math::Vec3 euler = transform.eulerAnglesDegrees();
                            euler.y -= mouse_delta.x * ctrl.look_sensitivity;
                            euler.x = Math::clampf(euler.x + mouse_delta.y * ctrl.look_sensitivity, -89.0f, 89.0f);
                            transform.rotation = Math::Quat::fromEulerYXZ(Math::toRadians(euler.y), Math::toRadians(euler.x), 0.0f);
                        }

                        Math::Vec3 move(0.0f);
                        if (!ui_owns_mouse || !input_->ui_wants_keyboard)
                        {
                            if (input_->wDown()) move += transform.forward();
                            if (input_->sDown()) move -= transform.forward();
                            if (input_->dDown()) move += transform.right();
                            if (input_->aDown()) move -= transform.right();
                            if (input_->spaceDown()) move += Math::Vec3(0, 1, 0);
                            if (input_->ctrlDown()) move -= Math::Vec3(0, 1, 0);
                        }
                        float speed = ctrl.move_speed * (input_->shiftDown() ? 3.0f : 1.0f);
                        if (Math::lengthSquared(move) > 1e-6f)
                            transform.position += Math::normalize(move) * speed * delta_time;
                    }
                    transform.local_dirty = true;
                });
        }

    } // namespace ECS
} // namespace Genesis
