/*
 * Genesis Render - built-in ECS components
 *
 * Every component declares `static constexpr const char *kComponentName`,
 * which ComponentPool<T> uses for diagnostics and which the Lua binding layer
 * uses to look components up by name.
 *
 * Components are pure data: they never call into the RHI or the renderer.
 * Systems (TransformSystem, ScriptSystem, RenderSystem) own all behaviour.
 */
#ifndef __GENESIS_ECS_COMPONENTS_HPP__
#define __GENESIS_ECS_COMPONENTS_HPP__

#include "ecs/ecs.hpp"
#include "resource/resource_types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Genesis
{
    namespace ECS
    {
        //==================================================================
        // Transform: local TRS + cached matrices.
        // `world_matrix` is maintained by TransformSystem (parent * local).
        //==================================================================
        struct TransformComponent
        {
            static constexpr const char *kComponentName = "Transform";

            Math::Vec3 position = Math::Vec3(0.0f);
            Math::Quat rotation = Math::Quat::identity();
            Math::Vec3 scale = Math::Vec3(1.0f);

            Math::Mat4 local_matrix = Math::Mat4::identity();
            Math::Mat4 world_matrix = Math::Mat4::identity();

            bool local_dirty = true;
            bool world_dirty = true;

            void setPosition(const Math::Vec3 &p) { position = p; local_dirty = true; }
            void setEulerDegrees(float pitch_x, float yaw_y, float roll_z)
            {
                rotation = Math::Quat::fromEulerYXZ(Math::toRadians(yaw_y), Math::toRadians(pitch_x), Math::toRadians(roll_z));
                local_dirty = true;
            }
            Math::Vec3 eulerAnglesDegrees() const
            {
                // Inverse of setEulerDegrees, i.e. of R = Ry(yaw)*Rx(pitch)*Rz(roll):
                //   M12 = -sin(pitch)
                //   M02 =  sin(yaw) * cos(pitch)
                //   M22 =  cos(yaw) * cos(pitch)
                //   M10 =  cos(pitch) * sin(roll)
                //   M11 =  cos(pitch) * cos(roll)
                // Storage is column major: M(row,col) == m[col * 4 + row].
                Math::Mat4 m = Math::Mat4::fromQuat(rotation);
                float sin_pitch = Math::clampf(-m.m[9], -1.0f, 1.0f); // -M12
                float pitch = std::asin(sin_pitch);
                float yaw = 0.0f;
                float roll = 0.0f;
                if (std::fabs(sin_pitch) < 0.99999f)
                {
                    yaw = std::atan2(m.m[8], m.m[10]); // M02, M22
                    roll = std::atan2(m.m[1], m.m[5]); // M10, M11
                }
                else
                {
                    // Gimbal lock: only the difference of yaw and roll survives.
                    yaw = std::atan2(-m.m[6], m.m[5]);
                }
                return Math::Vec3(Math::toDegrees(pitch), Math::toDegrees(yaw), Math::toDegrees(roll));
            }
            void setScale(const Math::Vec3 &s) { scale = s; local_dirty = true; }
            void rotateAroundAxis(const Math::Vec3 &axis, float radians)
            {
                rotation = (rotation * Math::Quat::fromAxisAngle(axis, radians)).normalized();
                local_dirty = true;
            }
            Math::Vec3 worldPosition() const { return world_matrix.translation(); }
            Math::Vec3 forward() const { return -world_matrix.zAxis(); }
            Math::Vec3 right() const { return world_matrix.xAxis(); }
            Math::Vec3 up() const { return world_matrix.yAxis(); }

            void refreshLocalMatrix()
            {
                if (!local_dirty) return;
                local_matrix = Math::Mat4::compose(position, rotation, scale);
                local_dirty = false;
                world_dirty = true;
            }
        };

        //==================================================================
        // Mesh: a renderable reference to an imported model.
        //==================================================================
        struct MeshComponent
        {
            static constexpr const char *kComponentName = "Mesh";

            std::shared_ptr<ModelData> model;
            // Empty == draw every mesh of the model.
            std::vector<int32_t> mesh_indices;
            // Per-mesh material override (index into RenderScene::materials),
            // -1 keeps the material that came with the model.
            std::vector<int32_t> material_overrides;

            bool visible = true;
            bool cast_shadow = true;
            bool receive_shadow = true;
            bool wireframe = false;

            // Filled by the renderer so that CPU picking can identify the entity.
            uint32_t pick_id = 0;
        };

        //==================================================================
        // Camera
        //==================================================================
        struct CameraComponent
        {
            static constexpr const char *kComponentName = "Camera";

            float fov_degrees = 60.0f;
            float near_plane = 0.05f;
            float far_plane = 500.0f;
            float aspect_override = 0.0f; // 0 => derive from the viewport
            bool orthographic = false;
            float ortho_size = 10.0f;

            bool active = true;
            bool clear_color_buffer = true;
            bool clear_depth_buffer = true;
            Math::Color clear_color = Math::Color(0.06f, 0.07f, 0.09f, 1.0f);

            // Post processing toggles consumed by the renderer
            bool enable_tonemap = true;
            bool enable_gamma = true;
            float exposure = 1.0f;
            float gamma = 2.2f;
        };

        //==================================================================
        // Light
        //==================================================================
        enum class LightType : uint32_t
        {
            Directional = 0,
            Point = 1,
            Spot = 2,
        };

        const char *lightTypeName(LightType type);

        struct LightComponent
        {
            static constexpr const char *kComponentName = "Light";

            LightType type = LightType::Directional;
            Math::Color color = Math::Color(1.0f, 1.0f, 1.0f, 1.0f);
            // Directional lights use illuminance-like units (nits after the
            // shader's normalisation); point/spot use candela-ish units.
            float intensity = 3.0f;
            float range = 25.0f;
            float spot_inner_degrees = 25.0f;
            float spot_outer_degrees = 35.0f;
            bool cast_shadow = false;
            bool active = true;
        };

        //==================================================================
        // Environment (IBL + skybox)
        //==================================================================
        struct EnvironmentComponent
        {
            static constexpr const char *kComponentName = "Environment";

            std::string hdr_equirect_path; // Radiance HDR equirectangular map
            float env_intensity = 1.0f;
            float env_rotation_degrees = 0.0f;
            bool show_skybox = true;
            bool use_ibl = true;
            // Renderer sets this to false after (re)baking the IBL cubemaps.
            bool ibl_dirty = true;
            // Baking resolution (per cubemap face)
            uint32_t cubemap_size = 256;
            uint32_t irradiance_size = 64;
            uint32_t prefiltered_size = 256;
            uint32_t brdf_lut_size = 256;
        };

        //==================================================================
        // Script (Lua or JavaScript)
        //==================================================================
        enum class ScriptLanguage : uint32_t
        {
            Lua = 0,
            JavaScript = 1,
        };

        struct ScriptComponent
        {
            static constexpr const char *kComponentName = "Script";

            std::string script_path; // .lua / .js file (preferred)
            std::string inline_code; // used when script_path is empty
            // Optional asset root (a mounted package).  `asset()` inside the
            // script resolves relative paths against it.
            std::string asset_root;
            ScriptLanguage language = ScriptLanguage::Lua;
            bool enabled = true;
            bool loaded = false;
            // Internal handles managed by the script systems.
            int32_t lua_ref = -1;
            int64_t js_ref = -1;
        };

        //==================================================================
        // Animation
        //==================================================================
        // 2D sprite-sheet sequence animation.  The texture is a grid of
        // columns x rows frames; the AnimationSystem advances `time` and
        // draws the current UV rect through the engine batch path.
        struct SpriteAnimationComponent
        {
            static constexpr const char *kComponentName = "SpriteAnimation";

            std::string texture;          // sprite sheet path
            uint32_t columns = 4;
            uint32_t rows = 1;
            uint32_t frame_count = 4;
            float fps = 12.0f;
            bool loop = true;
            bool playing = true;
            bool screen_space = true;     // false: project the entity position
            float size = 160.0f;          // pixels (screen) / world units
            Math::Vec3 screen_position = Math::Vec3(40.0f, 40.0f, 0.0f);
            Math::Color tint = Math::Color(1.0f, 1.0f, 1.0f, 1.0f);
            bool depth_test = false;

            // Runtime (written by AnimationSystem).
            float time = 0.0f;
            uint32_t frame = 0;
        };

        // 3D vertex animation performed by a compute shader.  The system
        // creates an output vertex buffer, dispatches the kernel every frame
        // and the mesh pass draws the animated buffer instead of the rest pose.
        struct VertexAnimationComponent
        {
            static constexpr const char *kComponentName = "VertexAnimation";

            std::string shader = "anim/vertex_anim.comp";
            float amplitude = 0.22f;
            float frequency = 2.2f;
            float speed = 2.0f;
            bool enabled = true;
        };

        //==================================================================
        // Physics: rigid body, collider and hinge joint data.
        //
        // The components are pure data.  PhysicsSystem owns the solver state
        // (see physics/physics.hpp) and writes dynamic transforms back into
        // TransformComponent every frame.
        //==================================================================
        enum class BodyType : uint32_t
        {
            Static = 0,
            Dynamic = 1,
            Kinematic = 2,
        };

        enum class ColliderShape : uint32_t
        {
            Sphere = 0,
            Box = 1,
            Plane = 2,
            Capsule = 3,
        };

        const char *bodyTypeName(BodyType type);
        const char *colliderShapeName(ColliderShape shape);

        struct RigidBodyComponent
        {
            static constexpr const char *kComponentName = "RigidBody";

            BodyType type = BodyType::Dynamic;
            float mass = 1.0f;
            float restitution = 0.3f;
            float friction = 0.55f;
            float linear_damping = 0.04f;
            float angular_damping = 0.08f;
            float gravity_scale = 1.0f;
            Math::Vec3 linear_velocity = Math::Vec3(0.0f);
            Math::Vec3 angular_velocity = Math::Vec3(0.0f);
            bool use_gravity = true;
            bool allow_sleep = true;
            bool sleeping = false;
            float sleep_timer = 0.0f; // solver bookkeeping, preserved across frames
            bool active = true;
        };

        struct ColliderComponent
        {
            static constexpr const char *kComponentName = "Collider";

            ColliderShape shape = ColliderShape::Box;
            // Box: half extents.  Sphere: `radius`.  Capsule: `radius` +
            // `height` (total, along local Y).  Plane: infinite, uses the
            // body transform's +Y as normal.
            Math::Vec3 half_extents = Math::Vec3(0.5f);
            float radius = 0.5f;
            float height = 1.0f;
            Math::Vec3 center = Math::Vec3(0.0f);
            bool is_trigger = false;
            bool debug_draw = true;
        };

        struct HingeJointComponent
        {
            static constexpr const char *kComponentName = "HingeJoint";

            // Empty / dead / no RigidBody -> anchored to the world.
            ECS::EntityId other = kInvalidEntityId;
            // World-space anchor point and (initially world-space) hinge axis.
            Math::Vec3 anchor = Math::Vec3(0.0f);
            Math::Vec3 axis = Math::Vec3(0.0f, 1.0f, 0.0f);
            bool enable_limit = false;
            float min_angle_degrees = -180.0f;
            float max_angle_degrees = 180.0f;
            bool enable_motor = false;
            float motor_speed = 0.0f;   // rad/s
            float motor_force = 0.0f;   // max angular impulse per step

            // Solver bookkeeping (radians, accumulated around `axis`).
            float current_angle = 0.0f;
        };

        //==================================================================
        // SDF text label drawn by the renderer at the entity transform.
        //   world space (default): `size` in world units, camera facing when
        //                          `billboard`, otherwise in the entity's XY
        //                          plane;
        //   screen space         : anchored at the projected entity position
        //                          plus `offset.xy` pixels, `size` in pixels.
        //==================================================================
        struct TextComponent
        {
            static constexpr const char *kComponentName = "Text";

            std::string text = "Text";
            float size = 0.25f;
            Math::Color color = Math::Color(1.0f, 1.0f, 1.0f, 1.0f);
            float outline = 0.0f; // outline width as a fraction of the SDF range (0..0.4)
            Math::Color outline_color = Math::Color(0.0f, 0.0f, 0.0f, 1.0f);
            Math::Vec3 offset = Math::Vec3(0.0f);
            uint32_t align = 1; // 0 left, 1 centre, 2 right
            bool billboard = true;
            bool screen_space = false;
            bool depth_test = true;
            bool visible = true;
        };

        //==================================================================
        // Tag / metadata (handy for tests, editor filtering and Lua queries)
        //==================================================================
        struct TagComponent
        {
            static constexpr const char *kComponentName = "Tag";
            std::string tag;
            uint32_t layer = 0;
        };

        //==================================================================
        // Simple orbit/fly controller driven by InputSystem state.
        // Kept as a component so that the demo (and Lua) can retarget it.
        //==================================================================
        struct CameraControllerComponent
        {
            static constexpr const char *kComponentName = "CameraController";

            enum class Mode : uint32_t
            {
                Orbit = 0,
                Fly = 1,
                None = 2,
            };
            Mode mode = Mode::Orbit;

            Math::Vec3 orbit_target = Math::Vec3(0.0f);
            float orbit_distance = 6.0f;
            float orbit_yaw_degrees = 35.0f;
            float orbit_pitch_degrees = 20.0f;

            float move_speed = 4.0f;
            float look_sensitivity = 0.15f;
            float zoom_sensitivity = 0.5f;
            bool dragging = false;
        };

    } // namespace ECS
} // namespace Genesis

#endif /* __GENESIS_ECS_COMPONENTS_HPP__ */
