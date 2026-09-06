/*
 * Genesis Render - Engine
 * script_system.cpp : Lua bindings + lifecycle.
 */
#include "script_system.hpp"
#include "foundation_layer/log.hpp"

#include <SDL2/SDL.h>
#include <fstream>
#include <sstream>
#include <cmath>

namespace Genesis
{
// Lua-facing entity handle (copied by value into Lua).
struct LuaEntity
{
    Entity e;
    Scene *scene = nullptr;
};

ScriptSystem::ScriptSystem()
{
    name = "ScriptSystem";
    lua_ = std::make_unique<sol::state>();
    lua_->open_libraries(sol::lib::base, sol::lib::math, sol::lib::string,
                         sol::lib::table, sol::lib::utf8);
    bindAPI();
}
ScriptSystem::~ScriptSystem() = default;

void ScriptSystem::bindAPI()
{
    sol::usertype<LuaEntity> et = lua_->new_usertype<LuaEntity>("Entity");
    et["name"] = [](LuaEntity &s) { return s.scene ? s.scene->entityName(s.e) : std::string(); };
    et["getPosition"] = [](LuaEntity &s) -> std::tuple<float, float, float> {
        if (auto *t = s.scene->getComponent<TransformComponent>(s.e))
            return {t->position.x, t->position.y, t->position.z};
        return {0, 0, 0};
    };
    et["setPosition"] = [](LuaEntity &s, float x, float y, float z) {
        if (auto *t = s.scene->getComponent<TransformComponent>(s.e))
            t->position = Vec3(x, y, z);
    };
    et["translate"] = [](LuaEntity &s, float x, float y, float z) {
        if (auto *t = s.scene->getComponent<TransformComponent>(s.e))
            t->position += Vec3(x, y, z);
    };
    et["rotateY"] = [](LuaEntity &s, float deg) {
        if (auto *t = s.scene->getComponent<TransformComponent>(s.e))
            t->rotation = quatMul(quatFromAxisAngle(Vec3(0, 1, 0), radians(deg)), t->rotation);
    };
    et["rotateAxis"] = [](LuaEntity &s, float x, float y, float z, float deg) {
        if (auto *t = s.scene->getComponent<TransformComponent>(s.e))
            t->rotation = quatMul(quatFromAxisAngle(normalize(Vec3(x, y, z)), radians(deg)),
                                  t->rotation);
    };
    et["setLightColor"] = [](LuaEntity &s, float r, float g, float b) {
        if (auto *l = s.scene->getComponent<PointLightComponent>(s.e)) l->color = Vec3(r, g, b);
        if (auto *l = s.scene->getComponent<DirectionalLightComponent>(s.e)) l->color = Vec3(r, g, b);
    };
    et["setLightIntensity"] = [](LuaEntity &s, float v) {
        if (auto *l = s.scene->getComponent<PointLightComponent>(s.e)) l->intensity = v;
        if (auto *l = s.scene->getComponent<DirectionalLightComponent>(s.e)) l->intensity = v;
    };
    et["setBaseColor"] = [](LuaEntity &s, float r, float g, float b) {
        if (auto *mc = s.scene->getComponent<MeshComponent>(s.e))
            if (mc->model && !mc->model->materials.empty())
            {
                mc->model->materials[0]->setBaseColor(Vec3(r, g, b));
                mc->model->materials[0]->dirty = true;
            }
    };
    et["setMetallic"] = [](LuaEntity &s, float v) {
        if (auto *mc = s.scene->getComponent<MeshComponent>(s.e))
            if (mc->model && !mc->model->materials.empty())
                mc->model->materials[0]->setMetallic(v);
    };
    et["setRoughness"] = [](LuaEntity &s, float v) {
        if (auto *mc = s.scene->getComponent<MeshComponent>(s.e))
            if (mc->model && !mc->model->materials.empty())
                mc->model->materials[0]->setRoughness(v);
    };

    sol::table genesis = lua_->create_named_table("Genesis");
    genesis["log"] = [](const std::string &msg) { GenesisPrint("[Lua] %s\n", msg.c_str()); };
    genesis["findEntity"] = [this](const std::string &name) -> LuaEntity {
        Entity e = scene_ ? scene_->findEntityByName(name) : kInvalidEntity;
        return LuaEntity{e, scene_};
    };
    genesis["keyDown"] = [this](const std::string &name) -> bool {
        if (!key_query_) return false;
        SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
        return sc != SDL_SCANCODE_UNKNOWN && key_query_((int)sc);
    };
}

ScriptSystem::Instance &ScriptSystem::instance(Entity e)
{
    uint64_t key = (uint64_t)e.generation << 32 | e.index;
    return instances_[key];
}

void ScriptSystem::reloadAll()
{
    for (auto &[k, v] : instances_) v.valid = false;
    instances_.clear();
    last_error_.clear();
}

void ScriptSystem::update(Scene *scene, float dt)
{
    scene_ = scene;
    time_ += dt;
    (*lua_)["Genesis"]["time"] = time_;
    (*lua_)["dt"] = dt;

    scene->view<LuaScriptComponent>([&](Entity e, LuaScriptComponent &comp) {
        if (!comp.enabled || comp.path.empty()) return;
        Instance &inst = instance(e);
        if (!inst.valid || inst.path != comp.path)
        {
            inst = Instance();
            inst.path = comp.path;
            inst.environment = sol::environment(*lua_, sol::create, lua_->globals());
            std::string source;
            {
                std::ifstream fs(comp.path, std::ios::binary);
                std::stringstream ss;
                ss << fs.rdbuf();
                source = ss.str();
            }
            if (source.empty())
            {
                last_error_ = "cannot open script " + comp.path;
                GenesisPrint("[Lua] %s\n", last_error_.c_str());
                return;
            }
            sol::protected_function_result r = lua_->safe_script(source, inst.environment);
            if (!r.valid())
            {
                sol::error err = r;
                last_error_ = comp.path + ": " + err.what();
                GenesisPrint("[Lua] %s\n", last_error_.c_str());
                return;
            }
            inst.on_start = inst.environment["onStart"];
            inst.on_update = inst.environment["onUpdate"];
            inst.valid = true;
            comp.started = false;
        }
        LuaEntity handle{e, scene};
        if (!inst.started)
        {
            if (inst.on_start.valid())
            {
                sol::protected_function_result r = inst.on_start(handle);
                if (!r.valid())
                {
                    sol::error err = r;
                    last_error_ = comp.path + " onStart: " + err.what();
                    GenesisPrint("[Lua] %s\n", last_error_.c_str());
                }
            }
            inst.started = true;
        }
        if (inst.on_update.valid())
        {
            sol::protected_function_result r = inst.on_update(handle, dt);
            if (!r.valid())
            {
                sol::error err = r;
                last_error_ = comp.path + " onUpdate: " + err.what();
                GenesisPrint("[Lua] %s\n", last_error_.c_str());
            }
        }
    });
}
}
