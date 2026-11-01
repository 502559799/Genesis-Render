/*
 * Genesis Render - rigid body physics
 *
 * A compact, dependency free impulse based solver:
 *
 *   shapes      sphere | box (OBB) | infinite plane | capsule
 *   broadphase  AABB overlap (n^2; the demos stay in the hundreds of bodies)
 *   narrowphase sphere/sphere, sphere/box, box/box (SAT), capsule variants,
 *               every shape against planes
 *   response    sequential impulses (restitution + Coulomb friction),
 *               positional correction, damping, sleeping
 *   joints      hinge (point constraint + axis lock, optional limits/motor)
 *
 * PhysicsSystem is an ECS system ordered at 95: it runs after the camera
 * controller and before TransformSystem, so the transforms produced here are
 * consumed by the renderer in the same frame.  Components are pure data; all
 * solver state lives in this module.
 */
#ifndef __GENESIS_PHYSICS_HPP__
#define __GENESIS_PHYSICS_HPP__

#include "ecs/components.hpp"
#include "ecs/ecs.hpp"

#include <string>
#include <vector>

namespace Genesis
{
    namespace Render
    {
        class RenderSystem;
    }

    namespace Physics
    {
        struct PhysicsSettings
        {
            bool enabled = true;
            Math::Vec3 gravity = Math::Vec3(0.0f, -9.81f, 0.0f);
            float fixed_timestep = 1.0f / 120.0f;
            uint32_t max_substeps = 6;
            uint32_t solver_iterations = 8;
            bool sleeping = true;
            float sleep_linear = 0.02f;
            float sleep_angular = 0.03f;
            float sleep_time = 0.8f;
            bool debug_draw = false;
        };

        struct Contact
        {
            ECS::Entity a = ECS::nullEntity();
            ECS::Entity b = ECS::nullEntity();
            Math::Vec3 point = Math::Vec3(0.0f);
            Math::Vec3 normal = Math::Vec3(0.0f, 1.0f, 0.0f); // from a to b
            float penetration = 0.0f;
            float impulse = 0.0f;
            bool trigger = false;
        };

        struct RayHit
        {
            ECS::Entity entity = ECS::nullEntity();
            Math::Vec3 point = Math::Vec3(0.0f);
            Math::Vec3 normal = Math::Vec3(0.0f, 1.0f, 0.0f);
            float distance = 0.0f;
        };

        class PhysicsSystem : public ECS::System
        {
        public:
            PhysicsSystem();
            ~PhysicsSystem() override;

            bool initialize(ECS::World *world);
            void shutdown();

            const char *name() const override { return "PhysicsSystem"; }
            int32_t order() const override { return 95; }
            void update(ECS::World &world, float delta_time) override;

            PhysicsSettings &settings() { return settings_; }
            const PhysicsSettings &settings() const { return settings_; }

            size_t bodyCount() const { return bodies_.size(); }
            size_t dynamicBodyCount() const;
            size_t contactCount() const { return contacts_.size(); }
            size_t triggerCount() const { return trigger_count_; }
            const std::vector<Contact> &contacts() const { return contacts_; }
            double stepTimeMs() const { return step_ms_; }
            uint64_t stepCount() const { return step_count_; }

            // Applies an impulse at a world point (used by game scripts).
            bool applyImpulse(ECS::World &world, ECS::Entity entity, const Math::Vec3 &impulse,
                              const Math::Vec3 &world_point);

            // Ray cast against every collider; returns the closest hit.
            bool rayCast(ECS::World &world, const Math::Vec3 &origin, const Math::Vec3 &direction,
                         float max_distance, RayHit &out) const;

            // Draws collider wireframes through the renderer's debug line buffer.
            void drawDebug(Render::RenderSystem &render) const;

            void setPaused(bool paused) { paused_ = paused; }
            bool paused() const { return paused_; }
            // Advances one fixed step even while paused (UI "step" button).
            void stepOnce(ECS::World &world) { if (paused_) step(world, settings_.fixed_timestep); }

            void resetAccumulator() { accumulator_ = 0.0f; }

        private:
            struct Body
            {
                ECS::Entity entity = ECS::nullEntity();
                ECS::BodyType type = ECS::BodyType::Dynamic;
                ECS::ColliderShape shape = ECS::ColliderShape::Box;

                Math::Vec3 position = Math::Vec3(0.0f);
                Math::Quat rotation = Math::Quat::identity();
                Math::Vec3 linear_velocity = Math::Vec3(0.0f);
                Math::Vec3 angular_velocity = Math::Vec3(0.0f);

                Math::Vec3 half_extents = Math::Vec3(0.5f);
                float radius = 0.5f;
                float height = 1.0f;
                Math::Vec3 center = Math::Vec3(0.0f);

                float inv_mass = 0.0f;
                float inv_inertia = 0.0f;
                float restitution = 0.3f;
                float friction = 0.5f;
                float linear_damping = 0.0f;
                float angular_damping = 0.0f;
                float gravity_scale = 1.0f;
                bool use_gravity = true;
                bool has_collider = false;
                bool is_trigger = false;
                bool sleeping = false;
                float sleep_timer = 0.0f;

                Math::Vec3 force = Math::Vec3(0.0f);
                Math::Vec3 torque = Math::Vec3(0.0f);

                bool isDynamic() const { return type == ECS::BodyType::Dynamic; }
                bool isStatic() const { return type == ECS::BodyType::Static; }
            };

            struct Hinge
            {
                ECS::Entity a = ECS::nullEntity();
                ECS::Entity b = ECS::nullEntity(); // may be null (world)
                Math::Vec3 anchor = Math::Vec3(0.0f);
                Math::Vec3 axis = Math::Vec3(0.0f, 1.0f, 0.0f);
                bool enable_limit = false;
                float min_angle = -Math::PI;
                float max_angle = Math::PI;
                bool enable_motor = false;
                float motor_speed = 0.0f;
                float motor_force = 0.0f;
                float current_angle = 0.0f;
            };

            struct Manifold
            {
                int a = -1;
                int b = -1;
                Math::Vec3 normal = Math::Vec3(0.0f, 1.0f, 0.0f);
                float penetration = 0.0f;
                Math::Vec3 point = Math::Vec3(0.0f);
                float impulse = 0.0f;
                bool trigger = false;
            };

            // Body building / synchronisation
            void rebuildBodies(ECS::World &world);
            void rebuildJoints(ECS::World &world);
            int findBody(ECS::Entity entity) const;
            void integrate(Body &body, float dt);
            void writeBack(ECS::World &world);
            void updateSleep(Body &body, float dt);

            // Collision detection
            void collide(std::vector<Manifold> &out);
            bool collidePair(int a, int b, Manifold &out) const;
            bool collidePlane(const Body &other, const Body &plane, int ai, int bi, Manifold &out) const;
            void solveContacts(std::vector<Manifold> &manifolds, float dt);
            void solveJoints(float dt);
            void correctPositions(const std::vector<Manifold> &manifolds);
            void step(ECS::World &world, float dt);

            static bool aabbOverlap(const Body &a, const Body &b);
            static Math::AABB worldAABB(const Body &body);
            static void boxAxes(const Body &body, Math::Vec3 axes[3]);
            static Math::Vec3 support(const Body &body, const Math::Vec3 &dir);
            static void closestSegmentPoints(const Math::Vec3 &p1, const Math::Vec3 &q1, const Math::Vec3 &p2,
                                             const Math::Vec3 &q2, Math::Vec3 &c1, Math::Vec3 &c2);
            static void capsuleSegment(const Body &body, Math::Vec3 &a, Math::Vec3 &b);
            static bool sphereBox(const Math::Vec3 &center, float radius, const Body &box, Math::Vec3 &normal,
                                  float &penetration, Math::Vec3 &point);
            static bool sphereSphere(const Math::Vec3 &ca, float ra, const Math::Vec3 &cb, float rb,
                                     Math::Vec3 &normal, float &penetration, Math::Vec3 &point);
            static bool boxBox(const Body &a, const Body &b, Math::Vec3 &normal, float &penetration, Math::Vec3 &point);

            ECS::World *world_ = nullptr;
            PhysicsSettings settings_;
            bool paused_ = false;
            float accumulator_ = 0.0f;
            std::vector<Body> bodies_;
            std::vector<Hinge> joints_;
            std::vector<Manifold> manifolds_;
            std::vector<Contact> contacts_;
            double step_ms_ = 0.0;
            uint64_t step_count_ = 0;
            size_t trigger_count_ = 0;
        };

    } // namespace Physics
} // namespace Genesis

#endif /* __GENESIS_PHYSICS_HPP__ */
