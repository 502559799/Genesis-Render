/*
 * Genesis Render - generic Lua binding of the RTTI registry.
 *
 * Every reflected type becomes scriptable without hand written glue:
 *
 *   objects     : full userdata with the "Genesis.Object" metatable;
 *                 obj.prop / obj.prop = v go through PropertyInfo,
 *                 obj:method(...) through MethodInfo (arguments converted by
 *                 the reflection traits), unknown keys through the type's
 *                 dynamic lookup (entity.Transform)
 *   value types : vec2 / vec3 / vec4 / color / quat / mat4 are Lua tables
 *                 with operator metatables (a + b, v * 2, v:length(), ...)
 *   types       : TypeName.new(...) for types with a constructor
 *   `reflect`   : types(), describe(t), typeof(o), valid(o), get/set/call,
 *                 new(type, ...)
 *   globals     : engine, world, renderer, settings, input, entity(id)
 */
#ifndef __GENESIS_LUA_REFLECTION_HPP__
#define __GENESIS_LUA_REFLECTION_HPP__

#include "reflection/reflection.hpp"

extern "C"
{
#include "lua.h"
}

namespace Genesis
{
    namespace Script
    {
        // Installs metatables, the math prelude, the `reflect` module, type
        // tables, reflected free functions and the engine globals.
        void openReflection(lua_State *L);

        void pushValue(lua_State *L, const Reflect::Value &v);
        Reflect::Value toValue(lua_State *L, int index);

    } // namespace Script
} // namespace Genesis

#endif /* __GENESIS_LUA_REFLECTION_HPP__ */
