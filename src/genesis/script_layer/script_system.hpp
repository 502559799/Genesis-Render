/*
 * @Descripttion: Lua 脚本系统 (lua 5.4 + sol2)
 *               对齐 Piccolo LuaComponent 的能力并增强:
 *               每实体独立 environment, 生命周期函数 on_create/on_update/on_destroy,
 *               绑定 transform/input/time/log API
 * @version: v1.0
 */
#ifndef __GENESIS_SCRIPT_SYSTEM_H__
#define __GENESIS_SCRIPT_SYSTEM_H__

#include "ecs_layer/components.hpp"

#include <string>
#include <vector>

#define SOL_ALL_SAFETIES_ON 1
#include "sol2/sol/sol.hpp"

namespace Genesis
{
    namespace script
    {
        class ScriptSystem : public ecs::System
        {
        public:
            ScriptSystem() = default;
            ~ScriptSystem() override = default;

            bool init(ecs::World *world);
            void shutdown();

            void onUpdate(ecs::World &world, float dt) override;

            // 重新加载全部脚本 (UI 热重载)
            void reloadAll(ecs::World &world);

            // 执行一段 lua 代码 (自检用)
            bool runString(const std::string &code);

            uint32_t loadedScriptCount() const { return (uint32_t)m_environments.size(); }

        private:
            void startScript(ecs::World &world, ecs::Entity e, ecs::ScriptComponent &sc);
            void bindEntityApi(sol::environment &env, ecs::World &world, ecs::Entity e);
            void destroyScript(ecs::ScriptComponent &sc);

        private:
            sol::state m_lua;
            ecs::World *m_world = nullptr;
            std::vector<sol::environment *> m_environments;
            float m_elapsed = 0.0f;
        };

    } // namespace script
} // namespace Genesis

#endif // __GENESIS_SCRIPT_SYSTEM_H__
