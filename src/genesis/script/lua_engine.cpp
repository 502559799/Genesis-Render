/*
 * Genesis Render - Lua 5.4 scripting engine.
 */
#include "script/lua_engine.hpp"
#include "application_layer/window.hpp"
#include "foundation_layer/log.hpp"

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

#include <SDL.h>
#include <cmath>

namespace Genesis {

static LuaEngine* selfOf(lua_State* L) {
    lua_getglobal(L, "__genesis_self");
    LuaEngine* self = (LuaEngine*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return self;
}

static int l_log(lua_State* L) {
    const char* s = luaL_checkstring(L, 1);
    GenesisPrint("[lua] %s\n", s);
    return 0;
}

static int l_findEntity(lua_State* L) {
    auto* self = selfOf(L);
    const char* name = luaL_checkstring(L, 1);
    Entity e = self && self->scene_ ? self->scene_->findByName(name) : kInvalidEntity;
    lua_pushinteger(L, e.id);
    return 1;
}

static int l_entityName(lua_State* L) {
    auto* self = selfOf(L);
    Entity e{(uint32_t)luaL_checkinteger(L, 1)};
    auto* tag = self && self->scene_ ? self->scene_->registry.get<TagComponent>(e) : nullptr;
    lua_pushstring(L, tag ? tag->name.c_str() : "");
    return 1;
}

static TransformComponent* getTransform(lua_State* L, int arg = 1) {
    auto* self = selfOf(L);
    if (!self || !self->scene_) return nullptr;
    Entity e{(uint32_t)luaL_checkinteger(L, arg)};
    return self->scene_->registry.get<TransformComponent>(e);
}

static int l_getPos(lua_State* L) {
    auto* t = getTransform(L);
    if (!t) return 0;
    lua_pushnumber(L, t->position.x); lua_pushnumber(L, t->position.y); lua_pushnumber(L, t->position.z);
    return 3;
}
static int l_setPos(lua_State* L) {
    if (auto* t = getTransform(L)) {
        t->position = {(float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                       (float)luaL_checknumber(L, 4)};
    }
    return 0;
}
static int l_getEuler(lua_State* L) {
    auto* t = getTransform(L);
    if (!t) return 0;
    lua_pushnumber(L, t->euler.x * kRad2Deg);
    lua_pushnumber(L, t->euler.y * kRad2Deg);
    lua_pushnumber(L, t->euler.z * kRad2Deg);
    return 3;
}
static int l_setEuler(lua_State* L) {
    if (auto* t = getTransform(L)) {
        t->euler = {(float)luaL_checknumber(L, 2) * kDeg2Rad,
                    (float)luaL_checknumber(L, 3) * kDeg2Rad,
                    (float)luaL_checknumber(L, 4) * kDeg2Rad};
    }
    return 0;
}

static int l_key(lua_State* L) {
    auto* self = selfOf(L);
    int scan = (int)luaL_checkinteger(L, 1);
    bool down = false;
    if (self && self->window_ && scan >= 0 && scan < 512)
        down = self->window_->input().keys[scan];
    lua_pushboolean(L, down);
    return 1;
}
static int l_mouseDX(lua_State* L) {
    auto* self = selfOf(L);
    lua_pushnumber(L, (self && self->window_) ? self->window_->input().mouse_dx : 0.0f);
    return 1;
}
static int l_mouseDY(lua_State* L) {
    auto* self = selfOf(L);
    lua_pushnumber(L, (self && self->window_) ? self->window_->input().mouse_dy : 0.0f);
    return 1;
}
static int l_time(lua_State* L) {
    auto* self = selfOf(L);
    lua_pushnumber(L, self ? self->time_ : 0.0);
    return 1;
}

LuaEngine::LuaEngine() {
    L_ = luaL_newstate();
    luaL_openlibs(L_);

    lua_pushlightuserdata(L_, this);
    lua_setglobal(L_, "__genesis_self");

    lua_newtable(L_);
    lua_pushcfunction(L_, l_log); lua_setfield(L_, -2, "log");
    lua_pushcfunction(L_, l_findEntity); lua_setfield(L_, -2, "findEntity");
    lua_pushcfunction(L_, l_entityName); lua_setfield(L_, -2, "entityName");
    lua_pushcfunction(L_, l_getPos); lua_setfield(L_, -2, "getPosition");
    lua_pushcfunction(L_, l_setPos); lua_setfield(L_, -2, "setPosition");
    lua_pushcfunction(L_, l_getEuler); lua_setfield(L_, -2, "getEuler");
    lua_pushcfunction(L_, l_setEuler); lua_setfield(L_, -2, "setEuler");
    lua_pushcfunction(L_, l_key); lua_setfield(L_, -2, "key");
    lua_pushcfunction(L_, l_mouseDX); lua_setfield(L_, -2, "mouseDX");
    lua_pushcfunction(L_, l_mouseDY); lua_setfield(L_, -2, "mouseDY");
    lua_pushcfunction(L_, l_time); lua_setfield(L_, -2, "time");

    // common scancodes
    lua_newtable(L_);
    auto key = [&](const char* n, int c) { lua_pushinteger(L_, c); lua_setfield(L_, -2, n); };
    key("W", SDL_SCANCODE_W); key("A", SDL_SCANCODE_A); key("S", SDL_SCANCODE_S); key("D", SDL_SCANCODE_D);
    key("Q", SDL_SCANCODE_Q); key("E", SDL_SCANCODE_E); key("R", SDL_SCANCODE_R); key("F", SDL_SCANCODE_F);
    key("UP", SDL_SCANCODE_UP); key("DOWN", SDL_SCANCODE_DOWN);
    key("LEFT", SDL_SCANCODE_LEFT); key("RIGHT", SDL_SCANCODE_RIGHT);
    key("SPACE", SDL_SCANCODE_SPACE);
    lua_setfield(L_, -2, "keys");
    lua_setglobal(L_, "genesis");
}

LuaEngine::~LuaEngine() {
    if (L_) lua_close(L_);
}

int LuaEngine::loadFileRef(const std::string& path) {
    auto it = fileRefs_.find(path);
    if (it != fileRefs_.end()) return it->second;
    if (luaL_dofile(L_, path.c_str()) != LUA_OK) {
        GenesisPrint("[lua] load failed %s: %s\n", path.c_str(), lua_tostring(L_, -1));
        lua_pop(L_, 1);
        fileRefs_[path] = LUA_NOREF;
        return LUA_NOREF;
    }
    // chunk returns the script table
    int ref = LUA_NOREF;
    if (lua_istable(L_, -1)) ref = luaL_ref(L_, LUA_REGISTRYINDEX);
    else lua_pop(L_, 1);
    fileRefs_[path] = ref;
    return ref;
}

void LuaEngine::callMethod(int ref, const char* method, Entity e, float dt, bool hasDt) {
    if (ref == LUA_NOREF) return;
    lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
    lua_getfield(L_, -1, method);
    if (!lua_isfunction(L_, -1)) {
        lua_pop(L_, 2);
        return;
    }
    lua_pushinteger(L_, e.id);
    if (hasDt) lua_pushnumber(L_, dt);
    if (lua_pcall(L_, hasDt ? 2 : 1, 0, 0) != LUA_OK) {
        GenesisPrint("[lua] %s error: %s\n", method, lua_tostring(L_, -1));
        lua_pop(L_, 1);
        errorCount_++;
    }
    lua_pop(L_, 1); // table
}

void LuaEngine::startScript(Entity e, const std::string& path) {
    int ref = loadFileRef(path);
    callMethod(ref, "onStart", e, 0, false);
}

void LuaEngine::update(float dt) {
    if (!scene_) return;
    time_ += dt;
    scene_->registry.view<ScriptComponent>([&](Entity e) {
        auto* sc = scene_->registry.get<ScriptComponent>(e);
        if (!sc->enabled || sc->path.empty()) return;
        int ref = loadFileRef(sc->path);
        sc->handle = ref;
        if (!sc->started) {
            callMethod(ref, "onStart", e, 0, false);
            sc->started = true;
        }
        callMethod(ref, "onUpdate", e, dt, true);
    });
}

} // namespace Genesis
