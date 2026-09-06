/*
 * Genesis Render - Engine
 * script_system.hpp : Lua 5.4 scripting via sol2.
 *
 * Each LuaScriptComponent runs in its own sandbox environment. Scripts may
 * define onStart(entity) and onUpdate(entity, dt); a rich Genesis API is
 * exposed (entity transform, lights, materials, input, logging).
 */
#pragma once

#include "ecs/scene.hpp"
#include "ecs/components.hpp"

#include <sol/sol.hpp>
#include <unordered_map>
#include <string>
#include <memory>

namespace Genesis
{
    using KeyQuery = bool (*)(int scancode);

    class ScriptSystem : public System
    {
    public:
        ScriptSystem();
        ~ScriptSystem() override;

        void onAttach(Scene *scene) override { scene_ = scene; }
        void setKeyQuery(KeyQuery q) { key_query_ = q; }
        void update(Scene *scene, float dt) override;

        void reloadAll();
        float time() const { return time_; }
        const std::string &lastError() const { return last_error_; }

    private:
        struct Instance
        {
            std::string path;
            sol::environment environment;
            sol::protected_function on_start;
            sol::protected_function on_update;
            bool started = false;
            bool valid = false;
        };
        void bindAPI();
        Instance &instance(Entity e);

        Scene *scene_ = nullptr;
        std::unique_ptr<sol::state> lua_;
        KeyQuery key_query_ = nullptr;
        float time_ = 0.0f;
        std::unordered_map<uint64_t, Instance> instances_;
        std::string last_error_;
    };
}
