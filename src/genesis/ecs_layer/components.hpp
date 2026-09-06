/*
 * @Descripttion: 内置组件定义 (对齐 Piccolo component 语义)
 * @version: v1.0
 */
#ifndef __GENESIS_ECS_COMPONENTS_H__
#define __GENESIS_ECS_COMPONENTS_H__

#include "ecs_layer/ecs.hpp"

#include "external/cglm/struct.h"

#include <cstring>

namespace Genesis
{
    namespace ecs
    {
        struct NameComponent
        {
            char m_name[64] = {0};

            void setName(const char *name)
            {
                strncpy(m_name, name, sizeof(m_name) - 1);
                m_name[sizeof(m_name) - 1] = 0;
            }
        };

        struct TransformComponent
        {
            vec3s m_position{{0.0f, 0.0f, 0.0f}};
            vec4s m_rotation{{0.0f, 0.0f, 0.0f, 1.0f}}; // quaternion (x,y,z,w)
            vec3s m_scale{{1.0f, 1.0f, 1.0f}};

            Entity m_parent = kInvalidEntity;
            mat4s m_world = GLMS_MAT4_IDENTITY;
            bool m_dirty = true;

            // 局部矩阵 T * R * S
            mat4s localMatrix() const
            {
                mat4s t = glms_translate_make(m_position);
                versors q{{m_rotation.raw[0], m_rotation.raw[1], m_rotation.raw[2], m_rotation.raw[3]}};
                mat4s r = glms_quat_mat4(q);
                mat4s s = glms_scale_make(m_scale);
                return glms_mat4_mul(glms_mat4_mul(t, r), s);
            }

            void setEulerAngles(float pitch, float yaw, float roll)
            {
                // 欧拉角 (度) -> 四元数, Y-X-Z 顺序
                versors qy = glms_quat(glm_rad(yaw), 0.0f, 1.0f, 0.0f);
                versors qx = glms_quat(glm_rad(pitch), 1.0f, 0.0f, 0.0f);
                versors qz = glms_quat(glm_rad(roll), 0.0f, 0.0f, 1.0f);
                versors q = glms_quat_mul(glms_quat_mul(qy, qx), qz);
                m_rotation.raw[0] = q.raw[0];
                m_rotation.raw[1] = q.raw[1];
                m_rotation.raw[2] = q.raw[2];
                m_rotation.raw[3] = q.raw[3];
                m_dirty = true;
            }
        };

        struct CameraComponent
        {
            float m_fov_degrees = 60.0f;
            float m_near = 0.1f;
            float m_far = 100.0f;
        };

        struct MeshRendererComponent
        {
            uint32_t m_model_id = 0xffffffff; // AssetManager 中的模型 id
            bool m_visible = true;
        };

        struct DirectionalLightComponent
        {
            vec3s m_direction{{0.3f, -1.0f, 0.4f}};
            vec3s m_color{{1.0f, 1.0f, 1.0f}};
            float m_intensity = 3.0f;
        };

        struct ScriptComponent
        {
            char m_script_path[256] = {0};
            void *m_env = nullptr; // sol::environment (script_layer 持有)
            bool m_started = false;

            void setPath(const char *path)
            {
                strncpy(m_script_path, path, sizeof(m_script_path) - 1);
                m_script_path[sizeof(m_script_path) - 1] = 0;
            }
        };

        //-----------------------------------------------------------
        // TransformSystem: 维护世界矩阵 (含层级)
        //-----------------------------------------------------------
        class TransformSystem : public System
        {
        public:
            void onUpdate(World &world, float dt) override;

            // 立即计算单个实体的世界矩阵 (递归父链)
            static mat4s computeWorld(World &world, Entity e);
        };

    } // namespace ecs
} // namespace Genesis

#endif // __GENESIS_ECS_COMPONENTS_H__
