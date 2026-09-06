/*
 * @Descripttion: Lua 脚本系统实现
 */
#include "script_layer/script_system.hpp"

#include "application_layer/input.hpp"
#include "foundation_layer/log.hpp"

#include <algorithm>

namespace Genesis
{
    namespace script
    {
        bool ScriptSystem::init(ecs::World *world)
        {
            m_world = world;
            m_lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);

            // vec3 绑定 (union 聚合类型, 用 property lambda 访问成员)
            auto vec3_prop = [](auto getter, auto setter) {
                return sol::property(getter, setter);
            };
            m_lua.new_usertype<vec3s>("vec3",
                                      sol::no_constructor,
                                      "x", vec3_prop([](const vec3s &v) { return v.raw[0]; }, [](vec3s &v, float x) { v.raw[0] = x; }),
                                      "y", vec3_prop([](const vec3s &v) { return v.raw[1]; }, [](vec3s &v, float y) { v.raw[1] = y; }),
                                      "z", vec3_prop([](const vec3s &v) { return v.raw[2]; }, [](vec3s &v, float z) { v.raw[2] = z; }));
            m_lua.set_function("vec3", [](float x, float y, float z) {
                vec3s v;
                v.x = x;
                v.y = y;
                v.z = z;
                return v;
            });

            // 全局 log
            m_lua.set_function("log", [](const std::string &msg) {
                GenesisPrint("[Lua] %s\n", msg.c_str());
            });

            // input 绑定
            sol::table input = m_lua.create_table();
            input.set_function("is_key_down", [](const std::string &key) {
                return Input::instance()->isKeyDown(key.c_str());
            });
            input.set_function("is_mouse_down", [](int button) {
                return Input::instance()->isMouseButtonDown(button);
            });
            m_lua["input"] = input;

            GenesisPrint("[Script] lua script system initialized\n");
            return true;
        }

        void ScriptSystem::shutdown()
        {
            for (auto env : m_environments)
                delete env;
            m_environments.clear();
        }

        void ScriptSystem::bindEntityApi(sol::environment &env, ecs::World &world, ecs::Entity e)
        {
            sol::table entity = m_lua.create_table();

            entity.set_function("get_position", [&world, e]() {
                auto *tc = world.getComponent<ecs::TransformComponent>(e);
                return tc ? tc->m_position : vec3s{{0, 0, 0}};
            });
            entity.set_function("set_position", [&world, e](float x, float y, float z) {
                auto *tc = world.getComponent<ecs::TransformComponent>(e);
                if (tc)
                {
                    tc->m_position = {{x, y, z}};
                    tc->m_dirty = true;
                }
            });
            entity.set_function("translate", [&world, e](float x, float y, float z) {
                auto *tc = world.getComponent<ecs::TransformComponent>(e);
                if (tc)
                {
                    tc->m_position.raw[0] += x;
                    tc->m_position.raw[1] += y;
                    tc->m_position.raw[2] += z;
                    tc->m_dirty = true;
                }
            });
            entity.set_function("set_rotation_euler", [&world, e](float pitch, float yaw, float roll) {
                auto *tc = world.getComponent<ecs::TransformComponent>(e);
                if (tc)
                    tc->setEulerAngles(pitch, yaw, roll);
            });
            entity.set_function("set_scale", [&world, e](float x, float y, float z) {
                auto *tc = world.getComponent<ecs::TransformComponent>(e);
                if (tc)
                {
                    tc->m_scale = {{x, y, z}};
                    tc->m_dirty = true;
                }
            });
            entity.set_function("get_name", [&world, e]() -> std::string {
                auto *nc = world.getComponent<ecs::NameComponent>(e);
                return nc ? std::string(nc->m_name) : std::string();
            });

            env["entity"] = entity;

            // time API
            sol::table time_api = m_lua.create_table();
            time_api["delta"] = 0.0f;
            time_api["elapsed"] = 0.0f;
            env["time"] = time_api;

            // 全局函数转发到 environment
            env["log"] = m_lua["log"];
            env["input"] = m_lua["input"];
        }

        void ScriptSystem::startScript(ecs::World &world, ecs::Entity e, ecs::ScriptComponent &sc)
        {
            sol::environment *env = new sol::environment(m_lua, sol::create, m_lua.globals());
            bindEntityApi(*env, world, e);

            sol::load_result loaded = m_lua.load_file(sc.m_script_path);
            if (!loaded.valid())
            {
                sol::error err = loaded;
                GenesisPrint("[Script] load failed %s: %s\n", sc.m_script_path, err.what());
                delete env;
                return;
            }

            sol::protected_function script = loaded;
            sol::set_environment(*env, script); // 在实体 environment 中执行
            sol::protected_function_result result = script();
            if (!result.valid())
            {
                sol::error err = result;
                GenesisPrint("[Script] run failed %s: %s\n", sc.m_script_path, err.what());
                delete env;
                return;
            }

            sc.m_env = env;
            sc.m_started = true;
            m_environments.push_back(env);

            // on_create
            sol::protected_function on_create = (*env)["on_create"];
            if (on_create.valid())
            {
                auto cr = on_create();
                if (!cr.valid())
                {
                    sol::error err = cr;
                    GenesisPrint("[Script] on_create error %s: %s\n", sc.m_script_path, err.what());
                }
            }
        }

        void ScriptSystem::onUpdate(ecs::World &world, float dt)
        {
            m_elapsed += dt;

            auto &pool = world.pool<ecs::ScriptComponent>();
            for (size_t i = 0; i < pool.m_dense.size(); ++i)
            {
                ecs::ScriptComponent &sc = pool.m_dense[i];
                if (!sc.m_started)
                {
                    startScript(world, pool.m_entities[i], sc);
                    continue;
                }

                sol::environment *env = (sol::environment *)sc.m_env;
                if (!env)
                    continue;

                // 更新时间
                sol::table time_api = (*env)["time"];
                time_api["delta"] = dt;
                time_api["elapsed"] = m_elapsed;

                sol::protected_function on_update = (*env)["on_update"];
                if (on_update.valid())
                {
                    auto result = on_update(dt);
                    if (!result.valid())
                    {
                        sol::error err = result;
                        GenesisPrint("[Script] on_update error %s: %s\n", sc.m_script_path, err.what());
                    }
                }
            }
        }

        void ScriptSystem::reloadAll(ecs::World &world)
        {
            auto &pool = world.pool<ecs::ScriptComponent>();
            for (size_t i = 0; i < pool.m_dense.size(); ++i)
            {
                ecs::ScriptComponent &sc = pool.m_dense[i];
                destroyScript(sc);
            }
            GenesisPrint("[Script] all scripts reloaded\n");
        }

        void ScriptSystem::destroyScript(ecs::ScriptComponent &sc)
        {
            if (sc.m_env)
            {
                sol::environment *env = (sol::environment *)sc.m_env;
                // on_destroy
                sol::protected_function on_destroy = (*env)["on_destroy"];
                if (on_destroy.valid())
                    on_destroy();
                m_environments.erase(std::remove(m_environments.begin(), m_environments.end(), env), m_environments.end());
                delete env;
                sc.m_env = nullptr;
            }
            sc.m_started = false;
        }

        bool ScriptSystem::runString(const std::string &code)
        {
            auto result = m_lua.script(code, [](lua_State *, sol::protected_function_result pfr) {
                return pfr;
            });
            if (!result.valid())
            {
                sol::error err = result;
                GenesisPrint("[Script] runString error: %s\n", err.what());
                return false;
            }
            return true;
        }

    } // namespace script
} // namespace Genesis
