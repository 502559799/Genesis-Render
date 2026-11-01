/*
 * Genesis Render - JavaScript scripting (V8)
 *
 * The JavaScript system mirrors the Lua system:
 *
 *   - a ScriptComponent with language == JavaScript is compiled in its own
 *     V8 context (sandboxed globals) and receives the conventional hooks
 *       start(entityId)  /  tick(deltaTime, entityId)
 *   - the same generic RTTI registry is exposed, so every reflected type,
 *     property, method and constructor is reachable, including the full
 *     scene construction API (World, Entity, components, Material,
 *     CommandBuffer, RenderTexture, Renderer, RenderSettings, Input, Physics)
 *   - globals: engine, world, renderer, settings, input, physics, entity(),
 *     asset(), reflect.*, Type.new(...), vec2/vec3/vec4/color/quat/mat4 and
 *     a small `genesis` compatibility facade for the legacy Lua API.
 *
 * V8 is optional at build time (GENESIS_ENABLE_V8).  When it is unavailable
 * the class is present but initialize() returns false.
 */
#ifndef __GENESIS_JS_SYSTEM_HPP__
#define __GENESIS_JS_SYSTEM_HPP__

#include "ecs/components.hpp"
#include "platform_layer/input_system.hpp"
#include "renderer/render_system.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Genesis
{
    namespace Script
    {
        class JSSystem : public ECS::System
        {
        public:
            struct Impl;
            JSSystem();
            ~JSSystem() override;

            bool initialize(ECS::World *world, Render::RenderSystem *render, Platform::InputSystem *input);
            void shutdown();

            const char *name() const override { return "JSSystem"; }
            int32_t order() const override { return 51; }
            void update(ECS::World &world, float delta_time) override;

            bool loadComponentScript(ECS::Entity entity, ECS::ScriptComponent &component);
            void unloadComponentScript(ECS::ScriptComponent &component);

            size_t loadedScripts() const;
            uint32_t errorCount() const { return error_count_; }
            const std::vector<std::string> &errors() const { return errors_; }
            double lastTickMs() const { return last_tick_ms_; }
            float timeScale() const { return time_scale_; }
            void setTimeScale(float s) { time_scale_ = s; }
            float elapsed() const { return elapsed_; }
            uint64_t frameIndex() const { return frame_index_; }
            bool available() const;

            // Executes a snippet in the shared console context.
            bool executeString(const std::string &code, const std::string &name = "=js");
            // Executes a snippet in the context of a loaded script entity
            // (debugging / automated tests; sees the script's globals).
            bool executeInScript(ECS::Entity entity, const std::string &code, const std::string &name = "=script");

            // Called by scripts / tools to evaluate a value's JSON description.
            std::string lastResult() const { return last_result_; }

        private:
            std::unique_ptr<Impl> impl_;
            ECS::World *world_ = nullptr;
            Render::RenderSystem *render_ = nullptr;
            Platform::InputSystem *input_ = nullptr;
            uint32_t error_count_ = 0;
            std::vector<std::string> errors_;
            double last_tick_ms_ = 0.0;
            float time_scale_ = 1.0f;
            float elapsed_ = 0.0f;
            uint64_t frame_index_ = 0;
            std::string last_result_;
            bool available_ = false;

            void reportError(const std::string &message);
        };

    } // namespace Script
} // namespace Genesis

#endif /* __GENESIS_JS_SYSTEM_HPP__ */
