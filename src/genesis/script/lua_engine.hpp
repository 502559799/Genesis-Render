/*
 * Genesis Render - Lua 5.4 scripting.
 *
 * A script is a Lua file under assets/scripts that returns a table with
 * onStart(entity) / onUpdate(entity, dt) callbacks. The same table is shared
 * between instances (entity id is the first argument), mirroring a lightweight
 * component prototype instead of Piccolo's per-component sol2 state.
 */
#pragma once

#include "ecs/scene.hpp"
#include <string>
#include <unordered_map>

struct lua_State;

namespace Genesis {

class Window;

class LuaEngine {
public:
    LuaEngine();
    ~LuaEngine();

    void setContext(Scene* scene, Window* window) { scene_ = scene; window_ = window; }

    // Ensure script loaded and onStart called for an entity.
    void startScript(Entity e, const std::string& path);
    // Tick every enabled ScriptComponent in the scene.
    void update(float dt);

    bool hasErrors() const { return errorCount_ > 0; }

    // accessed by the C binding free functions in lua_engine.cpp
    Scene* scene_ = nullptr;
    Window* window_ = nullptr;
    double time_ = 0.0;

private:
    int loadFileRef(const std::string& path); // returns registry ref, -1 cached
    void callMethod(int ref, const char* method, Entity e, float dt, bool hasDt);

    lua_State* L_ = nullptr;
    std::unordered_map<std::string, int> fileRefs_; // path -> registry ref
    int errorCount_ = 0;
};

} // namespace Genesis
