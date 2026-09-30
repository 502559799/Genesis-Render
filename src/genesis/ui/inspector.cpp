/*
 * Genesis Render - reflection driven ImGui inspector.
 */
#include "ui/inspector.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Genesis
{
    namespace UI
    {
        bool inspectProperty(const Reflect::ObjectRef &object, const Reflect::PropertyInfo &p, int depth)
        {
            void *self = object.get();
            if (!self) return false;
            using VT = Reflect::ValueType;
            Reflect::Value v = p.get(self);
            const char *label = p.name.c_str();
            bool changed = false;
            ImGui::PushID(label);
            if (p.readOnly() && v.type() != VT::Object)
            {
                ImGui::TextDisabled("%s", label);
                ImGui::SameLine(160.0f);
                ImGui::TextUnformatted(v.toString().c_str());
                ImGui::PopID();
                return false;
            }
            if (!p.enum_names.empty())
            {
                int current = 0;
                const std::string name = v.asString();
                for (size_t i = 0; i < p.enum_names.size(); ++i)
                    if (p.enum_names[i] == name) current = (int)i;
                if (ImGui::BeginCombo(label, name.c_str()))
                {
                    for (size_t i = 0; i < p.enum_names.size(); ++i)
                    {
                        if (ImGui::Selectable(p.enum_names[i].c_str(), (int)i == current))
                        {
                            p.set(self, Reflect::Value(p.enum_names[i]));
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            else
            {
                const bool ranged = p.max > p.min;
                switch (v.type())
                {
                case VT::Bool:
                {
                    bool b = v.asBool();
                    if (ImGui::Checkbox(label, &b)) { p.set(self, Reflect::Value(b)); changed = true; }
                    break;
                }
                case VT::Int:
                {
                    int i = (int)v.asInt();
                    if (ranged ? ImGui::SliderInt(label, &i, (int)p.min, (int)p.max) : ImGui::DragInt(label, &i))
                    {
                        p.set(self, Reflect::Value((int64_t)i));
                        changed = true;
                    }
                    break;
                }
                case VT::Float:
                {
                    float f = (float)v.asFloat();
                    if (ranged ? ImGui::SliderFloat(label, &f, p.min, p.max, "%.3f") : ImGui::DragFloat(label, &f, 0.01f))
                    {
                        p.set(self, Reflect::Value((double)f));
                        changed = true;
                    }
                    break;
                }
                case VT::String:
                {
                    char buf[512];
                    std::snprintf(buf, sizeof(buf), "%s", v.asString().c_str());
                    if (ImGui::InputText(label, buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        p.set(self, Reflect::Value(std::string(buf)));
                        changed = true;
                    }
                    break;
                }
                case VT::Vec2:
                {
                    Math::Vec2 a = v.asVec2();
                    if (ImGui::DragFloat2(label, &a.x, 0.01f)) { p.set(self, Reflect::Value(a)); changed = true; }
                    break;
                }
                case VT::Vec3:
                {
                    Math::Vec3 a = v.asVec3();
                    if (ImGui::DragFloat3(label, &a.x, 0.02f)) { p.set(self, Reflect::Value(a)); changed = true; }
                    break;
                }
                case VT::Vec4:
                {
                    Math::Vec4 a = v.asVec4();
                    if (ImGui::DragFloat4(label, &a.x, 0.01f)) { p.set(self, Reflect::Value(a)); changed = true; }
                    break;
                }
                case VT::Color:
                {
                    Math::Color c = v.asColor();
                    if (ImGui::ColorEdit4(label, &c.r, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
                    {
                        p.set(self, Reflect::Value(c));
                        changed = true;
                    }
                    break;
                }
                case VT::Quat:
                {
                    // Edited as pitch / yaw / roll; a Vec3 value converts back.
                    const Math::Quat q = v.asQuat();
                    Math::Mat4 m = Math::Mat4::fromQuat(q);
                    float pitch = Math::toDegrees(std::asin(Math::clampf(-m.m[9], -1.0f, 1.0f)));
                    float yaw = Math::toDegrees(std::atan2(m.m[8], m.m[10]));
                    float roll = Math::toDegrees(std::atan2(m.m[1], m.m[5]));
                    float e[3] = {pitch, yaw, roll};
                    if (ImGui::DragFloat3(label, e, 0.5f))
                    {
                        p.set(self, Reflect::Value(Math::Vec3(e[0], e[1], e[2])));
                        changed = true;
                    }
                    break;
                }
                case VT::Object:
                {
                    const Reflect::ObjectRef *child = v.asObject();
                    if (child && child->valid() && depth < 3)
                    {
                        if (ImGui::TreeNode(label, "%s (%s)", label, child->type ? child->type->name.c_str() : "object"))
                        {
                            changed |= inspectObject(*child, {}, depth + 1);
                            ImGui::TreePop();
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("%s: %s", label, v.toString().c_str());
                    }
                    break;
                }
                default:
                    ImGui::TextDisabled("%s: %s", label, v.toString().c_str());
                    break;
                }
            }
            if (!p.description.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.description.c_str());
            ImGui::PopID();
            return changed;
        }

        bool inspectObject(const Reflect::ObjectRef &object, const std::vector<std::string> &skip, int depth)
        {
            if (!object.valid())
            {
                ImGui::TextDisabled("(destroyed)");
                return false;
            }
            bool changed = false;
            for (const Reflect::PropertyInfo *p : object.type->allProperties())
            {
                if (std::find(skip.begin(), skip.end(), p->name) != skip.end()) continue;
                changed |= inspectProperty(object, *p, depth);
            }
            return changed;
        }

    } // namespace UI
} // namespace Genesis
