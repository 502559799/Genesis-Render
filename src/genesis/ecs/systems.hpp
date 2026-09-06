/*
 * Genesis Render - built-in ECS systems
 *
 * Order values (lower runs first):
 *    50  ScriptSystem        (Lua may move entities)
 *    90  CameraControllerSystem
 *   100  TransformSystem     (world matrices)
 *   200  RenderSystem        (defined in renderer/, syncs the render scene)
 */
#ifndef __GENESIS_ECS_SYSTEMS_HPP__
#define __GENESIS_ECS_SYSTEMS_HPP__

#include "ecs/components.hpp"
#include "platform_layer/input_system.hpp"

namespace Genesis
{
    namespace ECS
    {
        //==================================================================
        // TransformSystem
        //
        // Refreshes local matrices and propagates parent -> child world
        // matrices.  Entities are processed in ascending hierarchy depth so
        // that a parent's world matrix is always up to date when its children
        // are visited.
        //==================================================================
        class TransformSystem : public System
        {
        public:
            const char *name() const override { return "TransformSystem"; }
            int32_t order() const override { return 100; }
            void update(World &world, float delta_time) override;

            size_t lastUpdatedCount() const { return last_count_; }

        private:
            size_t last_count_ = 0;
        };

        //==================================================================
        // CameraControllerSystem
        //
        // Drives orbit / fly cameras from the InputSystem.  Mouse look is
        // suppressed while the UI owns the pointer so that dragging a slider
        // never rotates the camera.
        //==================================================================
        class CameraControllerSystem : public System
        {
        public:
            explicit CameraControllerSystem(Platform::InputSystem *input) : input_(input) {}

            const char *name() const override { return "CameraControllerSystem"; }
            int32_t order() const override { return 90; }
            void update(World &world, float delta_time) override;

        private:
            Platform::InputSystem *input_ = nullptr;
        };

        //==================================================================
        // LightType helper (declared in components.hpp)
        //==================================================================
        const char *lightTypeName(LightType type);

    } // namespace ECS
} // namespace Genesis

#endif /* __GENESIS_ECS_SYSTEMS_HPP__ */
