/*
 * Genesis Render - reflection driven ImGui inspector.
 *
 * Draws an editor widget for every property of any reflected object (the
 * same RTTI the Lua binding uses), so components, materials and settings are
 * editable in the UI without per-type editor code:
 *
 *   bool -> checkbox, number -> slider (with range) / drag, string -> text
 *   field, enum -> combo, vec2/3/4 -> drag, colour -> colour picker,
 *   quat -> euler angles, object -> collapsible nested inspector.
 */
#ifndef __GENESIS_UI_INSPECTOR_HPP__
#define __GENESIS_UI_INSPECTOR_HPP__

#include "reflection/reflection.hpp"

#include <string>
#include <vector>

namespace Genesis
{
    namespace UI
    {
        // Returns true when any property was modified this frame.
        bool inspectObject(const Reflect::ObjectRef &object, const std::vector<std::string> &skip = {}, int depth = 0);
        // Inspector for one named property; returns true when it changed.
        bool inspectProperty(const Reflect::ObjectRef &object, const Reflect::PropertyInfo &property, int depth = 0);
    } // namespace UI
} // namespace Genesis

#endif /* __GENESIS_UI_INSPECTOR_HPP__ */
