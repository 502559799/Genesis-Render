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
        // Script (Lua)
        //==================================================================
        struct ScriptComponent
        {
            static constexpr const char *kComponentName = "Script";

            std::string script_path; // .lua file (preferred)
            std::string inline_code; // used when script_path is empty
            bool enabled = true;
            bool loaded = false;
            // Internal handle managed by ScriptSystem (Lua registry reference).
            int32_t lua_ref = -1;
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
