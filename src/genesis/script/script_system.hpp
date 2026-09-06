/*
 * Genesis Render - Lua scripting system
 *
 * Design (deliberately different from Piccolo, and documented as such):
 *
 *   Piccolo : one `sol::state` per LuaComponent, the whole embedded script
 *             chunk is re-executed every frame, and only three reflection
 *             bridge functions are exposed (set_float / get_bool / invoke).
 *
 *   Genesis : ONE shared lua_State for the whole world (Lua 5.4, vendored),
 *             each ScriptComponent compiles its chunk once into a sandboxed
 *             environment table, and the engine calls the conventional
 *             entry points:
 *
 *                 function start(entity_id)   -- once after loading
 *                 function tick(delta_time, entity_id) -- every frame
 *
 *             A real API surface is registered under the global `genesis`
 *             table: logging, time, input, entity/transform manipulation,
 *             render settings, stats, debug drawing and model loading.
 *
 * Scripts are loaded from .lua files (ScriptComponent::script_path) or from an
 * inline string (ScriptComponent::inline_code).
 */
#ifndef __GENESIS_SCRIPT_SYSTEM_HPP__
#define __GENESIS_SCRIPT_SYSTEM_HPP__

#include "ecs/components.hpp"
#include "platform_layer/input_system.hpp"
#include "renderer/render_system.hpp"

#include <cstdint>
#include <string>
#include <vector>

extern "C"
{
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

namespace Genesis
{
    namespace Script
    {
        class ScriptSystem : public ECS::System
        {
        public:
            ScriptSystem();
            ~ScriptSystem() override;

            bool initialize(ECS::World *world, Render::RenderSystem *render, Platform::InputSystem *input);
            void shutdown();

            const char *name() const override { return "ScriptSystem"; }
            int32_t order() const override { return 50; }
            void update(ECS::World &world, float delta_time) override;

            // Compiles the script of `component` and calls its start() hook.
            bool loadComponentScript(ECS::Entity entity, ECS::ScriptComponent &component);
            void unloadComponentScript(ECS::ScriptComponent &component);

            lua_State *state() { return lua_state_; }
            size_t loadedScripts() const { return loaded_count_; }
            uint32_t errorCount() const { return error_count_; }
            const std::vector<std::string> &errors() const { return errors_; }
            double lastTickMs() const { return last_tick_ms_; }
            float timeScale() const { return time_scale_; }
            void setTimeScale(float s) { time_scale_ = s; }
            float elapsed() const { return elapsed_; }
            uint64_t frameIndex() const { return frame_index_; }

            // Executes an arbitrary snippet (used by tests and the UI console).
            bool executeString(const std::string &code, const std::string &name = "=input");

        private:
            void registerApi();
            void reportError(const std::string &message);
            bool callHook(ECS::ScriptComponent &component, const char *function, ECS::Entity entity, float delta_time);

            lua_State *lua_state_ = nullptr;
            ECS::World *world_ = nullptr;
            Render::RenderSystem *render_ = nullptr;
            Platform::InputSystem *input_ = nullptr;
            size_t loaded_count_ = 0;
            uint32_t error_count_ = 0;
            std::vector<std::string> errors_;
            double last_tick_ms_ = 0.0;
            float time_scale_ = 1.0f;
            float elapsed_ = 0.0f;
            uint64_t frame_index_ = 0;
        };

    } // namespace Script
} // namespace Genesis

#endif /* __GENESIS_SCRIPT_SYSTEM_HPP__ */
