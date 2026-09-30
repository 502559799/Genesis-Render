/*
 * Genesis Render - generic Lua binding of the RTTI registry.
 *
 * Lua is compiled as C and reports errors with longjmp, so every C function
 * below does its C++ work inside an inner scope and only raises the Lua error
 * (from a plain char buffer) after all C++ objects have been destroyed.
 */
#include "script/lua_reflection.hpp"
#include "reflection/engine_bindings.hpp"
#include "engine/engine.hpp"
#include "foundation_layer/genesis_log.hpp"
#include "renderer/render_system.hpp"

extern "C"
{
#include "lauxlib.h"
#include "lualib.h"
}

#include <cstdio>
#include <cstring>
#include <new>
#include <string>

namespace Genesis
{
    namespace Script
    {
        namespace
        {
            const char *kObjectMeta = "Genesis.Object";
            const char *kValueMetaNames[] = {"genesis.vec2", "genesis.vec3", "genesis.vec4", "genesis.color", "genesis.quat", "genesis.mat4"};

            struct LuaObject
            {
                Reflect::ObjectRef ref;
            };

            LuaObject *checkObject(lua_State *L, int index)
            {
                return static_cast<LuaObject *>(luaL_testudata(L, index, kObjectMeta));
            }

            // Raises a Lua error with `message`; call only when no C++ object
            // with a destructor is alive in the calling frame.
            int raise(lua_State *L, const char *message)
            {
                return luaL_error(L, "%s", message);
            }

            void copyError(char (&buf)[512], const std::string &msg)
            {
                std::snprintf(buf, sizeof(buf), "%s", msg.c_str());
            }

            //--------------------------------------------------------------
            // Value types
            //--------------------------------------------------------------
            bool hasMeta(lua_State *L, int index, const char *meta)
            {
                if (!lua_getmetatable(L, index)) return false;
                luaL_getmetatable(L, meta);
                bool same = lua_rawequal(L, -1, -2);
                lua_pop(L, 2);
                return same;
            }

            float fieldNumber(lua_State *L, int index, const char *key, float fallback)
            {
                lua_getfield(L, index, key);
                float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : fallback;
                lua_pop(L, 1);
                return v;
            }

            bool hasField(lua_State *L, int index, const char *key)
            {
                lua_getfield(L, index, key);
                bool has = !lua_isnil(L, -1);
                lua_pop(L, 1);
                return has;
            }

            void pushVector(lua_State *L, const char *meta, const char *const *keys, const float *v, int n)
            {
                lua_createtable(L, 0, n);
                for (int i = 0; i < n; ++i)
                {
                    lua_pushnumber(L, v[i]);
                    lua_setfield(L, -2, keys[i]);
                }
                luaL_setmetatable(L, meta);
            }
        } // namespace

        void pushValue(lua_State *L, const Reflect::Value &v)
        {
            using VT = Reflect::ValueType;
            static const char *xyzw[] = {"x", "y", "z", "w"};
            static const char *rgba[] = {"r", "g", "b", "a"};
            switch (v.type())
            {
            case VT::Nil: lua_pushnil(L); return;
            case VT::Bool: lua_pushboolean(L, v.asBool() ? 1 : 0); return;
            case VT::Int: lua_pushinteger(L, (lua_Integer)v.asInt()); return;
            case VT::Float: lua_pushnumber(L, v.asFloat()); return;
            case VT::String:
            {
                const std::string s = v.asString();
                lua_pushlstring(L, s.data(), s.size());
                return;
            }
            case VT::Vec2: pushVector(L, kValueMetaNames[0], xyzw, v.raw(), 2); return;
            case VT::Vec3: pushVector(L, kValueMetaNames[1], xyzw, v.raw(), 3); return;
            case VT::Vec4: pushVector(L, kValueMetaNames[2], xyzw, v.raw(), 4); return;
            case VT::Color: pushVector(L, kValueMetaNames[3], rgba, v.raw(), 4); return;
            case VT::Quat: pushVector(L, kValueMetaNames[4], xyzw, v.raw(), 4); return;
            case VT::Mat4:
            {
                lua_createtable(L, 16, 0);
                for (int i = 0; i < 16; ++i)
                {
                    lua_pushnumber(L, v.raw()[i]);
                    lua_rawseti(L, -2, i + 1);
                }
                luaL_setmetatable(L, kValueMetaNames[5]);
                return;
            }
            case VT::Object:
            {
                const Reflect::ObjectRef *ref = v.asObject();
                if (!ref || !ref->valid())
                {
                    lua_pushnil(L);
                    return;
                }
                void *ud = lua_newuserdatauv(L, sizeof(LuaObject), 0);
                new (ud) LuaObject{*ref};
                luaL_setmetatable(L, kObjectMeta);
                return;
            }
            case VT::List:
            {
                const auto &items = v.asList();
                lua_createtable(L, (int)items.size(), 0);
                for (size_t i = 0; i < items.size(); ++i)
                {
                    pushValue(L, items[i]);
                    lua_rawseti(L, -2, (lua_Integer)i + 1);
                }
                return;
            }
            }
            lua_pushnil(L);
        }

        Reflect::Value toValue(lua_State *L, int index)
        {
            index = lua_absindex(L, index);
            switch (lua_type(L, index))
            {
            case LUA_TNIL:
            case LUA_TNONE: return Reflect::Value();
            case LUA_TBOOLEAN: return Reflect::Value(lua_toboolean(L, index) != 0);
            case LUA_TNUMBER:
                if (lua_isinteger(L, index)) return Reflect::Value((int64_t)lua_tointeger(L, index));
                return Reflect::Value((double)lua_tonumber(L, index));
            case LUA_TSTRING: return Reflect::Value(std::string(lua_tostring(L, index)));
            case LUA_TUSERDATA:
                if (LuaObject *o = checkObject(L, index)) return Reflect::Value(o->ref);
                return Reflect::Value();
            case LUA_TTABLE:
            {
                if (hasMeta(L, index, kValueMetaNames[0]))
                    return Reflect::Value(Math::Vec2(fieldNumber(L, index, "x", 0), fieldNumber(L, index, "y", 0)));
                if (hasMeta(L, index, kValueMetaNames[3]) || (hasField(L, index, "r") && hasField(L, index, "g")))
                    return Reflect::Value(Math::Color(fieldNumber(L, index, "r", 1), fieldNumber(L, index, "g", 1),
                                                      fieldNumber(L, index, "b", 1), fieldNumber(L, index, "a", 1)));
                if (hasMeta(L, index, kValueMetaNames[4]))
                    return Reflect::Value(Math::Quat(fieldNumber(L, index, "x", 0), fieldNumber(L, index, "y", 0),
                                                     fieldNumber(L, index, "z", 0), fieldNumber(L, index, "w", 1)));
                if (hasMeta(L, index, kValueMetaNames[5]) || lua_rawlen(L, index) == 16)
                {
                    float m[16];
                    for (int i = 0; i < 16; ++i)
                    {
                        lua_rawgeti(L, index, i + 1);
                        m[i] = (float)lua_tonumber(L, -1);
                        lua_pop(L, 1);
                    }
                    return Reflect::Value(Math::Mat4(m));
                }
                if (hasField(L, index, "x"))
                {
                    const bool z = hasField(L, index, "z"), w = hasField(L, index, "w");
                    if (w || hasMeta(L, index, kValueMetaNames[2]))
                        return Reflect::Value(Math::Vec4(fieldNumber(L, index, "x", 0), fieldNumber(L, index, "y", 0),
                                                         fieldNumber(L, index, "z", 0), fieldNumber(L, index, "w", 0)));
                    if (z || hasMeta(L, index, kValueMetaNames[1]))
                        return Reflect::Value(Math::Vec3(fieldNumber(L, index, "x", 0), fieldNumber(L, index, "y", 0),
                                                         fieldNumber(L, index, "z", 0)));
                    return Reflect::Value(Math::Vec2(fieldNumber(L, index, "x", 0), fieldNumber(L, index, "y", 0)));
                }
                // Plain arrays: numeric {x,y[,z[,w]]} become vectors, anything
                // else a list.
                const lua_Unsigned n = lua_rawlen(L, index);
                bool numeric = n >= 2 && n <= 4;
                for (lua_Unsigned i = 1; numeric && i <= n; ++i)
                {
                    lua_rawgeti(L, index, (lua_Integer)i);
                    numeric = lua_type(L, -1) == LUA_TNUMBER;
                    lua_pop(L, 1);
                }
                if (numeric)
                {
                    float c[4] = {0, 0, 0, 0};
                    for (lua_Unsigned i = 1; i <= n; ++i)
                    {
                        lua_rawgeti(L, index, (lua_Integer)i);
                        c[i - 1] = (float)lua_tonumber(L, -1);
                        lua_pop(L, 1);
                    }
                    if (n == 2) return Reflect::Value(Math::Vec2(c[0], c[1]));
                    if (n == 3) return Reflect::Value(Math::Vec3(c[0], c[1], c[2]));
                    return Reflect::Value(Math::Vec4(c[0], c[1], c[2], c[3]));
                }
                std::vector<Reflect::Value> items;
                for (lua_Unsigned i = 1; i <= n; ++i)
                {
                    lua_rawgeti(L, index, (lua_Integer)i);
                    items.push_back(toValue(L, -1));
                    lua_pop(L, 1);
                }
                return Reflect::Value::list(std::move(items));
            }
            default: return Reflect::Value();
            }
        }

        namespace
        {
            //--------------------------------------------------------------
            // Object metamethods
            //--------------------------------------------------------------
            int objectGc(lua_State *L)
            {
                if (LuaObject *o = checkObject(L, 1)) o->~LuaObject();
                return 0;
            }

            int callBoundMethod(lua_State *L)
            {
                char err[512] = {0};
                {
                    const Reflect::MethodInfo *m = static_cast<const Reflect::MethodInfo *>(lua_touserdata(L, lua_upvalueindex(1)));
                    LuaObject *self = checkObject(L, 1);
                    if (!self || !m)
                    {
                        std::snprintf(err, sizeof(err), "method '%s' must be called with ':' (obj:%s(...))",
                                      m ? m->name.c_str() : "?", m ? m->name.c_str() : "?");
                    }
                    else if (!self->ref.valid())
                    {
                        std::snprintf(err, sizeof(err), "%s:%s called on a destroyed object",
                                      self->ref.type ? self->ref.type->name.c_str() : "object", m->name.c_str());
                    }
                    else
                    {
                        std::vector<Reflect::Value> args;
                        const int top = lua_gettop(L);
                        for (int i = 2; i <= top; ++i) args.push_back(toValue(L, i));
                        Reflect::Value result = m->invoke(self->ref.get(), args);
                        pushValue(L, result);
                        return 1;
                    }
                }
                return raise(L, err);
            }

            int objectIndex(lua_State *L)
            {
                char err[512] = {0};
                {
                    LuaObject *o = checkObject(L, 1);
                    const char *key = lua_tostring(L, 2);
                    if (!o || !key) { lua_pushnil(L); return 1; }
                    if (std::strcmp(key, "__type") == 0)
                    {
                        lua_pushstring(L, o->ref.type ? o->ref.type->name.c_str() : "object");
                        return 1;
                    }
                    if (!o->ref.type)
                    {
                        lua_pushnil(L);
                        return 1;
                    }
                    if (const Reflect::MethodInfo *m = o->ref.type->findMethod(key))
                    {
                        lua_pushlightuserdata(L, const_cast<Reflect::MethodInfo *>(m));
                        lua_pushcclosure(L, callBoundMethod, 1);
                        return 1;
                    }
                    Reflect::Value out;
                    std::string error;
                    if (Reflect::getProperty(o->ref, key, out, &error))
                    {
                        pushValue(L, out);
                        return 1;
                    }
                    if (!o->ref.valid()) copyError(err, error);
                    else
                    {
                        lua_pushnil(L); // unknown keys read as nil (idiomatic Lua)
                        return 1;
                    }
                }
                return raise(L, err);
            }

            int objectNewIndex(lua_State *L)
            {
                char err[512] = {0};
                {
                    LuaObject *o = checkObject(L, 1);
                    const char *key = lua_tostring(L, 2);
                    std::string error;
                    if (!o || !key) return 0;
                    if (Reflect::setProperty(o->ref, key, toValue(L, 3), &error)) return 0;
                    copyError(err, error);
                }
                return raise(L, err);
            }

            int objectToString(lua_State *L)
            {
                LuaObject *o = checkObject(L, 1);
                char buf[256];
                if (!o || !o->ref.type) std::snprintf(buf, sizeof(buf), "object(nil)");
                else
                {
                    Reflect::Value name;
                    std::string label;
                    if (o->ref.valid() && Reflect::getProperty(o->ref, "name", name, nullptr)) label = " '" + name.asString() + "'";
                    std::snprintf(buf, sizeof(buf), "%s(%p)%s", o->ref.type->name.c_str(), o->ref.get(), label.c_str());
                }
                lua_pushstring(L, buf);
                return 1;
            }

            int objectEq(lua_State *L)
            {
                LuaObject *a = checkObject(L, 1), *b = checkObject(L, 2);
                lua_pushboolean(L, a && b && a->ref.type == b->ref.type && a->ref.identity == b->ref.identity);
                return 1;
            }

            //--------------------------------------------------------------
            // reflect.* module
            //--------------------------------------------------------------
            const Reflect::TypeInfo *typeArg(lua_State *L, int index)
            {
                if (LuaObject *o = checkObject(L, index)) return o->ref.type;
                if (lua_type(L, index) == LUA_TSTRING) return Reflect::Registry::instance().find(lua_tostring(L, index));
                return nullptr;
            }

            int reflectTypes(lua_State *L)
            {
                auto types = Reflect::Registry::instance().types();
                lua_createtable(L, (int)types.size(), 0);
                int i = 1;
                for (const Reflect::TypeInfo *t : types)
                {
                    lua_pushstring(L, t->name.c_str());
                    lua_rawseti(L, -2, i++);
                }
                return 1;
            }

            int reflectDescribe(lua_State *L)
            {
                const Reflect::TypeInfo *t = typeArg(L, 1);
                if (!t)
                {
                    lua_pushnil(L);
                    return 1;
                }
                lua_newtable(L);
                lua_pushstring(L, t->name.c_str());
                lua_setfield(L, -2, "name");
                lua_pushstring(L, t->description.c_str());
                lua_setfield(L, -2, "description");
                lua_pushboolean(L, t->constructor ? 1 : 0);
                lua_setfield(L, -2, "constructible");
                auto props = t->allProperties();
                lua_createtable(L, (int)props.size(), 0);
                for (size_t i = 0; i < props.size(); ++i)
                {
                    const Reflect::PropertyInfo *p = props[i];
                    lua_newtable(L);
                    lua_pushstring(L, p->name.c_str());
                    lua_setfield(L, -2, "name");
                    lua_pushstring(L, Reflect::valueTypeName(p->type));
                    lua_setfield(L, -2, "type");
                    lua_pushboolean(L, p->readOnly() ? 1 : 0);
                    lua_setfield(L, -2, "readonly");
                    lua_pushstring(L, p->description.c_str());
                    lua_setfield(L, -2, "description");
                    if (!p->enum_names.empty())
                    {
                        lua_createtable(L, (int)p->enum_names.size(), 0);
                        for (size_t k = 0; k < p->enum_names.size(); ++k)
                        {
                            lua_pushstring(L, p->enum_names[k].c_str());
                            lua_rawseti(L, -2, (lua_Integer)k + 1);
                        }
                        lua_setfield(L, -2, "values");
                    }
                    lua_rawseti(L, -2, (lua_Integer)i + 1);
                }
                lua_setfield(L, -2, "properties");
                auto methods = t->allMethods();
                lua_createtable(L, (int)methods.size(), 0);
                for (size_t i = 0; i < methods.size(); ++i)
                {
                    lua_newtable(L);
                    lua_pushstring(L, methods[i]->name.c_str());
                    lua_setfield(L, -2, "name");
                    lua_pushstring(L, methods[i]->signature.c_str());
                    lua_setfield(L, -2, "signature");
                    lua_pushstring(L, methods[i]->description.c_str());
                    lua_setfield(L, -2, "description");
                    lua_rawseti(L, -2, (lua_Integer)i + 1);
                }
                lua_setfield(L, -2, "methods");
                return 1;
            }

            int reflectTypeOf(lua_State *L)
            {
                LuaObject *o = checkObject(L, 1);
                if (o && o->ref.type) lua_pushstring(L, o->ref.type->name.c_str());
                else lua_pushstring(L, luaL_typename(L, 1));
                return 1;
            }

            int reflectValid(lua_State *L)
            {
                LuaObject *o = checkObject(L, 1);
                lua_pushboolean(L, o && o->ref.valid());
                return 1;
            }

            int reflectGet(lua_State *L)
            {
                char err[512] = {0};
                {
                    LuaObject *o = checkObject(L, 1);
                    const char *key = luaL_checkstring(L, 2);
                    Reflect::Value out;
                    std::string error = "reflect.get expects an object";
                    if (o && Reflect::getProperty(o->ref, key, out, &error))
                    {
                        pushValue(L, out);
                        return 1;
                    }
                    copyError(err, error);
                }
                return raise(L, err);
            }

            int reflectSet(lua_State *L)
            {
                char err[512] = {0};
                {
                    LuaObject *o = checkObject(L, 1);
                    const char *key = luaL_checkstring(L, 2);
                    std::string error = "reflect.set expects an object";
                    if (o && Reflect::setProperty(o->ref, key, toValue(L, 3), &error)) return 0;
                    copyError(err, error);
                }
                return raise(L, err);
            }

            int reflectCall(lua_State *L)
            {
                char err[512] = {0};
                {
                    LuaObject *o = checkObject(L, 1);
                    const char *name = luaL_checkstring(L, 2);
                    std::vector<Reflect::Value> args;
                    for (int i = 3; i <= lua_gettop(L); ++i) args.push_back(toValue(L, i));
                    Reflect::Value out;
                    std::string error = "reflect.call expects an object";
                    if (o && Reflect::callMethod(o->ref, name, args, out, &error))
                    {
                        pushValue(L, out);
                        return 1;
                    }
                    copyError(err, error);
                }
                return raise(L, err);
            }

            int constructType(lua_State *L, const Reflect::TypeInfo *t, int first_arg)
            {
                char err[512] = {0};
                {
                    if (!t || !t->constructor)
                        std::snprintf(err, sizeof(err), "type '%s' has no constructor", t ? t->name.c_str() : "?");
                    else
                    {
                        std::vector<Reflect::Value> args;
                        for (int i = first_arg; i <= lua_gettop(L); ++i) args.push_back(toValue(L, i));
                        pushValue(L, t->constructor(args));
                        return 1;
                    }
                }
                return raise(L, err);
            }

            int reflectNew(lua_State *L)
            {
                return constructType(L, typeArg(L, 1), 2);
            }

            int typeNew(lua_State *L)
            {
                const Reflect::TypeInfo *t = static_cast<const Reflect::TypeInfo *>(lua_touserdata(L, lua_upvalueindex(1)));
                return constructType(L, t, 1);
            }

            int callFunction(lua_State *L)
            {
                const Reflect::MethodInfo *m = static_cast<const Reflect::MethodInfo *>(lua_touserdata(L, lua_upvalueindex(1)));
                std::vector<Reflect::Value> args;
                for (int i = 1; i <= lua_gettop(L); ++i) args.push_back(toValue(L, i));
                Reflect::Value out = m->invoke(nullptr, args);
                args.clear();
                pushValue(L, out);
                return 1;
            }

            int assetPath(lua_State *L)
            {
                const char *rel = luaL_checkstring(L, 1);
                Reflect::EngineContext &ctx = Reflect::context();
                std::string dir = ctx.engine ? ctx.engine->config().asset_dir : std::string("assets");
                std::string full = ResourceIO::joinPath(dir, rel);
                lua_pushlstring(L, full.data(), full.size());
                return 1;
            }

            int entityById(lua_State *L)
            {
                Reflect::EngineContext &ctx = Reflect::context();
                lua_Integer id = luaL_checkinteger(L, 1);
                if (!ctx.world)
                {
                    lua_pushnil(L);
                    return 1;
                }
                pushValue(L, Reflect::entityValue(ctx.world, ctx.world->entityFromId((ECS::EntityId)id)));
                return 1;
            }

            // Operator metatables and constructors of the value types.
            const char *kPrelude = R"LUA(
local M = ...
local V2, V3, V4, C, Q, M4 = M.vec2, M.vec3, M.vec4, M.color, M.quat, M.mat4
local function num(v) return type(v) == "number" end

V2.__index = V2
function vec2(x, y) return setmetatable({x = x or 0, y = y or x or 0}, V2) end
V2.__add = function(a, b) return vec2(a.x + b.x, a.y + b.y) end
V2.__sub = function(a, b) return vec2(a.x - b.x, a.y - b.y) end
V2.__mul = function(a, b) if num(a) then return vec2(a * b.x, a * b.y) elseif num(b) then return vec2(a.x * b, a.y * b) end return vec2(a.x * b.x, a.y * b.y) end
V2.__div = function(a, b) if num(b) then return vec2(a.x / b, a.y / b) end return vec2(a.x / b.x, a.y / b.y) end
V2.__unm = function(a) return vec2(-a.x, -a.y) end
V2.__eq = function(a, b) return a.x == b.x and a.y == b.y end
V2.__tostring = function(a) return string.format("vec2(%.3f, %.3f)", a.x, a.y) end
function V2.length(a) return math.sqrt(a.x * a.x + a.y * a.y) end
function V2.dot(a, b) return a.x * b.x + a.y * b.y end

V3.__index = V3
function vec3(x, y, z) if y == nil then y, z = x, x end return setmetatable({x = x or 0, y = y or 0, z = z or 0}, V3) end
V3.__add = function(a, b) return vec3(a.x + b.x, a.y + b.y, a.z + b.z) end
V3.__sub = function(a, b) return vec3(a.x - b.x, a.y - b.y, a.z - b.z) end
V3.__mul = function(a, b) if num(a) then return vec3(a * b.x, a * b.y, a * b.z) elseif num(b) then return vec3(a.x * b, a.y * b, a.z * b) end return vec3(a.x * b.x, a.y * b.y, a.z * b.z) end
V3.__div = function(a, b) if num(b) then return vec3(a.x / b, a.y / b, a.z / b) end return vec3(a.x / b.x, a.y / b.y, a.z / b.z) end
V3.__unm = function(a) return vec3(-a.x, -a.y, -a.z) end
V3.__eq = function(a, b) return a.x == b.x and a.y == b.y and a.z == b.z end
V3.__tostring = function(a) return string.format("vec3(%.3f, %.3f, %.3f)", a.x, a.y, a.z) end
function V3.length(a) return math.sqrt(a.x * a.x + a.y * a.y + a.z * a.z) end
function V3.normalized(a) local l = V3.length(a) if l < 1e-8 then return vec3(0, 0, 0) end return a / l end
function V3.dot(a, b) return a.x * b.x + a.y * b.y + a.z * b.z end
function V3.cross(a, b) return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x) end
function V3.lerp(a, b, t) return a + (b - a) * t end

V4.__index = V4
function vec4(x, y, z, w) return setmetatable({x = x or 0, y = y or 0, z = z or 0, w = w or 0}, V4) end
V4.__add = function(a, b) return vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w) end
V4.__mul = function(a, b) if num(b) then return vec4(a.x * b, a.y * b, a.z * b, a.w * b) end return vec4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w) end
V4.__tostring = function(a) return string.format("vec4(%.3f, %.3f, %.3f, %.3f)", a.x, a.y, a.z, a.w) end

C.__index = C
function color(r, g, b, a) return setmetatable({r = r or 1, g = g or r or 1, b = b or r or 1, a = a or 1}, C) end
C.__mul = function(a, b) if num(b) then return color(a.r * b, a.g * b, a.b * b, a.a) end return color(a.r * b.r, a.g * b.g, a.b * b.b, a.a * b.a) end
C.__add = function(a, b) return color(a.r + b.r, a.g + b.g, a.b + b.b, a.a) end
C.__tostring = function(a) return string.format("color(%.3f, %.3f, %.3f, %.3f)", a.r, a.g, a.b, a.a) end
function C.lerp(a, b, t) return color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t) end
function C.hsv(h, s, v)
  local i = math.floor(h * 6) local f = h * 6 - i local p, q, t = v * (1 - s), v * (1 - f * s), v * (1 - (1 - f) * s)
  i = i % 6
  if i == 0 then return color(v, t, p) elseif i == 1 then return color(q, v, p) elseif i == 2 then return color(p, v, t)
  elseif i == 3 then return color(p, q, v) elseif i == 4 then return color(t, p, v) end return color(v, p, q)
end

Q.__index = Q
function quat(x, y, z, w) return setmetatable({x = x or 0, y = y or 0, z = z or 0, w = w or 1}, Q) end
Q.__tostring = function(a) return string.format("quat(%.3f, %.3f, %.3f, %.3f)", a.x, a.y, a.z, a.w) end

M4.__index = M4
M4.__tostring = function(a) return "mat4" end
mat4 = M4
)LUA";
        } // namespace

        namespace
        {
            int printToLog(lua_State *L)
            {
                const int n = lua_gettop(L); // before luaL_buffinit, which may push a placeholder
                luaL_Buffer b;
                luaL_buffinit(L, &b);
                for (int i = 1; i <= n; ++i)
                {
                    if (i > 1) luaL_addchar(&b, '\t');
                    luaL_tolstring(L, i, nullptr);
                    luaL_addvalue(&b);
                }
                luaL_pushresult(&b);
                GEN_LOG_INFO("[Lua] %s", lua_tostring(L, -1));
                return 0;
            }
        } // namespace

        void openReflection(lua_State *L)
        {
            Reflect::registerEngineTypes();

            luaL_newmetatable(L, kObjectMeta);
            const luaL_Reg object_meta[] = {{"__index", objectIndex}, {"__newindex", objectNewIndex}, {"__gc", objectGc},
                                            {"__tostring", objectToString}, {"__eq", objectEq}, {nullptr, nullptr}};
            luaL_setfuncs(L, object_meta, 0);
            lua_pop(L, 1);

            // Value type metatables: created here so C++ can tag tables with
            // them; operators are filled in by the Lua prelude.
            lua_newtable(L); // prelude argument
            const char *short_names[] = {"vec2", "vec3", "vec4", "color", "quat", "mat4"};
            for (int i = 0; i < 6; ++i)
            {
                luaL_newmetatable(L, kValueMetaNames[i]);
                lua_setfield(L, -2, short_names[i]);
            }

            // Reflected free functions (math helpers etc.) as globals; the
            // prelude then replaces the plain value constructors with Lua
            // versions that carry operator metatables.
            for (const auto &kv : Reflect::Registry::instance().functions())
            {
                lua_pushlightuserdata(L, const_cast<Reflect::MethodInfo *>(&kv.second));
                lua_pushcclosure(L, callFunction, 1);
                lua_setglobal(L, kv.first.c_str());
            }

            if (luaL_loadstring(L, kPrelude) == LUA_OK)
            {
                lua_insert(L, -2); // chunk below its argument
                if (lua_pcall(L, 1, 0, 0) != LUA_OK)
                {
                    GEN_LOG_ERROR("[Lua] reflection prelude failed: %s", lua_tostring(L, -1));
                    lua_pop(L, 1);
                }
            }
            else
            {
                GEN_LOG_ERROR("[Lua] reflection prelude does not compile: %s", lua_tostring(L, -1));
                lua_pop(L, 2);
            }

            // reflect module
            const luaL_Reg reflect_fns[] = {{"types", reflectTypes}, {"describe", reflectDescribe}, {"typeof", reflectTypeOf},
                                            {"valid", reflectValid}, {"get", reflectGet}, {"set", reflectSet},
                                            {"call", reflectCall}, {"new", reflectNew}, {nullptr, nullptr}};
            luaL_newlib(L, reflect_fns);
            lua_setglobal(L, "reflect");

            // TypeName.new(...) tables
            for (const Reflect::TypeInfo *t : Reflect::Registry::instance().types())
            {
                if (!t->constructor) continue;
                lua_getglobal(L, t->name.c_str());
                if (!lua_istable(L, -1))
                {
                    lua_pop(L, 1);
                    lua_newtable(L);
                }
                lua_pushlightuserdata(L, const_cast<Reflect::TypeInfo *>(t));
                lua_pushcclosure(L, typeNew, 1);
                lua_setfield(L, -2, "new");
                lua_setglobal(L, t->name.c_str());
            }

            // Engine globals
            Reflect::EngineContext &ctx = Reflect::context();
            if (ctx.engine) { pushValue(L, Reflect::refRaw(ctx.engine)); lua_setglobal(L, "engine"); }
            if (ctx.world) { pushValue(L, Reflect::refRaw(ctx.world)); lua_setglobal(L, "world"); }
            if (ctx.render)
            {
                pushValue(L, Reflect::refRaw(ctx.render));
                lua_setglobal(L, "renderer");
                pushValue(L, Reflect::refRaw(&ctx.render->settings()));
                lua_setglobal(L, "settings");
            }
            if (ctx.input) { pushValue(L, Reflect::refRaw(ctx.input)); lua_setglobal(L, "input"); }
            lua_pushcfunction(L, entityById);
            lua_setglobal(L, "entity");
            lua_pushcfunction(L, assetPath);
            lua_setglobal(L, "asset");
            // print() goes to the engine log (visible in the demo log / console).
            lua_pushcfunction(L, printToLog);
            lua_setglobal(L, "print");
        }

    } // namespace Script
} // namespace Genesis
