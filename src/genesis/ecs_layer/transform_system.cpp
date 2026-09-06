/*
 * @Descripttion: TransformSystem 实现: 层级世界矩阵计算
 */
#include "ecs_layer/components.hpp"

namespace Genesis
{
    namespace ecs
    {
        mat4s TransformSystem::computeWorld(World &world, Entity e)
        {
            TransformComponent *tc = world.getComponent<TransformComponent>(e);
            if (!tc)
                return GLMS_MAT4_IDENTITY;

            mat4s local = tc->localMatrix();
            if (tc->m_parent != kInvalidEntity && world.isAlive(tc->m_parent))
            {
                mat4s parent_world = computeWorld(world, tc->m_parent);
                return glms_mat4_mul(parent_world, local);
            }
            return local;
        }

        void TransformSystem::onUpdate(World &world, float dt)
        {
            (void)dt;
            // 规模较小, 每帧全量重算世界矩阵 (保证父级移动时子级同步)
            auto &pool = world.pool<TransformComponent>();
            for (size_t i = 0; i < pool.m_dense.size(); ++i)
            {
                TransformComponent &tc = pool.m_dense[i];
                tc.m_world = computeWorld(world, pool.m_entities[i]);
                tc.m_dirty = false;
            }
        }

    } // namespace ecs
} // namespace Genesis
