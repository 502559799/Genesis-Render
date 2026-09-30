/*
 * Genesis Render - runtime type information implementation.
 */
#include "reflection/reflection.hpp"

#include <cstdio>
#include <cstring>

namespace Genesis
{
    namespace Reflect
    {
        const char *valueTypeName(ValueType t)
        {
            switch (t)
            {
            case ValueType::Nil: return "nil";
            case ValueType::Bool: return "bool";
            case ValueType::Int: return "int";
            case ValueType::Float: return "number";
            case ValueType::String: return "string";
            case ValueType::Vec2: return "vec2";
            case ValueType::Vec3: return "vec3";
            case ValueType::Vec4: return "vec4";
            case ValueType::Quat: return "quat";
            case ValueType::Color: return "color";
            case ValueType::Mat4: return "mat4";
            case ValueType::Object: return "object";
            case ValueType::List: return "list";
            }
            return "?";
        }

        //==================================================================
        // Value
        //==================================================================
        Value::Value(const Math::Mat4 &m) : type_(ValueType::Mat4)
        {
            std::memcpy(v_, m.m, sizeof(v_));
        }

        bool Value::asBool() const
        {
            switch (type_)
            {
            case ValueType::Bool:
            case ValueType::Int: return i_ != 0;
            case ValueType::Float: return f_ != 0.0;
            case ValueType::String: return !s_.empty() && s_ != "false" && s_ != "0";
            case ValueType::Nil: return false;
            case ValueType::Object: return obj_ && obj_->valid();
            default: return true;
            }
        }

        int64_t Value::asInt() const
        {
            switch (type_)
            {
            case ValueType::Bool:
            case ValueType::Int: return i_;
            case ValueType::Float: return (int64_t)f_;
            case ValueType::String: return (int64_t)std::strtoll(s_.c_str(), nullptr, 10);
            default: return 0;
            }
        }

        double Value::asFloat() const
        {
            switch (type_)
            {
            case ValueType::Bool:
            case ValueType::Int: return (double)i_;
            case ValueType::Float: return f_;
            case ValueType::String: return std::strtod(s_.c_str(), nullptr);
            default: return 0.0;
            }
        }

        std::string Value::asString() const
        {
            if (type_ == ValueType::String) return s_;
            return toString();
        }

        Math::Vec2 Value::asVec2() const
        {
            if (isNumber()) return Math::Vec2((float)asFloat());
            if (type_ >= ValueType::Vec2 && type_ <= ValueType::Color) return Math::Vec2(v_[0], v_[1]);
            return Math::Vec2(0.0f);
        }

        Math::Vec3 Value::asVec3() const
        {
            if (isNumber()) return Math::Vec3((float)asFloat());
            if (type_ == ValueType::Vec2) return Math::Vec3(v_[0], v_[1], 0.0f);
            if (type_ >= ValueType::Vec3 && type_ <= ValueType::Color) return Math::Vec3(v_[0], v_[1], v_[2]);
            return Math::Vec3(0.0f);
        }

        Math::Vec4 Value::asVec4() const
        {
            if (isNumber()) return Math::Vec4((float)asFloat());
            if (type_ == ValueType::Vec2) return Math::Vec4(v_[0], v_[1], 0.0f, 0.0f);
            if (type_ == ValueType::Vec3) return Math::Vec4(v_[0], v_[1], v_[2], 0.0f);
            if (type_ >= ValueType::Vec4 && type_ <= ValueType::Color) return Math::Vec4(v_[0], v_[1], v_[2], v_[3]);
            return Math::Vec4(0.0f);
        }

        Math::Quat Value::asQuat() const
        {
            if (type_ == ValueType::Quat || type_ == ValueType::Vec4) return Math::Quat(v_[0], v_[1], v_[2], v_[3]).normalized();
            if (type_ == ValueType::Vec3)
                // Euler angles in degrees (pitch, yaw, roll) for convenience.
                return Math::Quat::fromEulerYXZ(Math::toRadians(v_[1]), Math::toRadians(v_[0]), Math::toRadians(v_[2]));
            return Math::Quat::identity();
        }

        Math::Color Value::asColor() const
        {
            if (isNumber()) return Math::Color((float)asFloat(), (float)asFloat(), (float)asFloat(), 1.0f);
            if (type_ == ValueType::Vec3) return Math::Color(v_[0], v_[1], v_[2], 1.0f);
            if (type_ == ValueType::Color || type_ == ValueType::Vec4 || type_ == ValueType::Quat)
                return Math::Color(v_[0], v_[1], v_[2], v_[3]);
            return Math::Color(1.0f, 1.0f, 1.0f, 1.0f);
        }

        Math::Mat4 Value::asMat4() const
        {
            if (type_ == ValueType::Mat4) return Math::Mat4(v_);
            if (type_ == ValueType::Vec3) return Math::Mat4::translate(asVec3());
            return Math::Mat4::identity();
        }

        const std::vector<Value> &Value::asList() const
        {
            static const std::vector<Value> empty;
            return list_ ? *list_ : empty;
        }

        std::string Value::toString() const
        {
            char buf[256];
            switch (type_)
            {
            case ValueType::Nil: return "nil";
            case ValueType::Bool: return i_ ? "true" : "false";
            case ValueType::Int: std::snprintf(buf, sizeof(buf), "%lld", (long long)i_); return buf;
            case ValueType::Float: std::snprintf(buf, sizeof(buf), "%g", f_); return buf;
            case ValueType::String: return s_;
            case ValueType::Vec2: std::snprintf(buf, sizeof(buf), "vec2(%g, %g)", v_[0], v_[1]); return buf;
            case ValueType::Vec3: std::snprintf(buf, sizeof(buf), "vec3(%g, %g, %g)", v_[0], v_[1], v_[2]); return buf;
            case ValueType::Vec4: std::snprintf(buf, sizeof(buf), "vec4(%g, %g, %g, %g)", v_[0], v_[1], v_[2], v_[3]); return buf;
            case ValueType::Quat: std::snprintf(buf, sizeof(buf), "quat(%g, %g, %g, %g)", v_[0], v_[1], v_[2], v_[3]); return buf;
            case ValueType::Color: std::snprintf(buf, sizeof(buf), "color(%g, %g, %g, %g)", v_[0], v_[1], v_[2], v_[3]); return buf;
            case ValueType::Mat4: return "mat4(...)";
            case ValueType::Object:
                std::snprintf(buf, sizeof(buf), "%s(%p)", obj_ && obj_->type ? obj_->type->name.c_str() : "object",
                              obj_ ? obj_->get() : nullptr);
                return buf;
            case ValueType::List: std::snprintf(buf, sizeof(buf), "list[%zu]", asList().size()); return buf;
            }
            return "?";
        }

        //==================================================================
        // TypeInfo
        //==================================================================
        const PropertyInfo *TypeInfo::findProperty(const std::string &n) const
        {
            for (const TypeInfo *t = this; t; t = t->base)
                for (const PropertyInfo &p : t->properties)
                    if (p.name == n) return &p;
            return nullptr;
        }

        const MethodInfo *TypeInfo::findMethod(const std::string &n) const
        {
            for (const TypeInfo *t = this; t; t = t->base)
                for (const MethodInfo &m : t->methods)
                    if (m.name == n) return &m;
            return nullptr;
        }

        bool TypeInfo::isA(const TypeInfo *other) const
        {
            if (!other) return false;
            for (const TypeInfo *t = this; t; t = t->base)
                if (t == other) return true;
            return false;
        }

        std::vector<const PropertyInfo *> TypeInfo::allProperties() const
        {
            std::vector<const PropertyInfo *> out;
            std::vector<const TypeInfo *> chain;
            for (const TypeInfo *t = this; t; t = t->base) chain.insert(chain.begin(), t);
            for (const TypeInfo *t : chain)
                for (const PropertyInfo &p : t->properties) out.push_back(&p);
            return out;
        }

        std::vector<const MethodInfo *> TypeInfo::allMethods() const
        {
            std::vector<const MethodInfo *> out;
            std::vector<const TypeInfo *> chain;
            for (const TypeInfo *t = this; t; t = t->base) chain.insert(chain.begin(), t);
            for (const TypeInfo *t : chain)
                for (const MethodInfo &m : t->methods) out.push_back(&m);
            return out;
        }

        //==================================================================
        // Registry
        //==================================================================
        Registry &Registry::instance()
        {
            static Registry r;
            return r;
        }

        TypeInfo &Registry::add(const std::string &name, std::type_index cpp_type, const std::string &base)
        {
            auto it = types_.find(name);
            if (it == types_.end())
            {
                auto info = std::make_unique<TypeInfo>();
                info->name = name;
                it = types_.emplace(name, std::move(info)).first;
            }
            TypeInfo &t = *it->second;
            t.cpp_type = cpp_type;
            if (!base.empty()) t.base = find(base);
            by_cpp_[cpp_type] = &t;
            return t;
        }

        const TypeInfo *Registry::find(const std::string &name) const
        {
            auto it = types_.find(name);
            return it != types_.end() ? it->second.get() : nullptr;
        }

        const TypeInfo *Registry::find(std::type_index cpp_type) const
        {
            auto it = by_cpp_.find(cpp_type);
            return it != by_cpp_.end() ? it->second : nullptr;
        }

        std::vector<const TypeInfo *> Registry::types() const
        {
            std::vector<const TypeInfo *> out;
            for (const auto &kv : types_) out.push_back(kv.second.get());
            return out;
        }

        //==================================================================
        // Generic access
        //==================================================================
        bool getProperty(const ObjectRef &obj, const std::string &name, Value &out, std::string *error)
        {
            void *self = obj.get();
            if (!self || !obj.type)
            {
                if (error) *error = "object is no longer valid";
                return false;
            }
            if (const PropertyInfo *p = obj.type->findProperty(name))
            {
                out = p->get(self);
                return true;
            }
            for (const TypeInfo *t = obj.type; t; t = t->base)
                if (t->dynamic_get && t->dynamic_get(self, name, out)) return true;
            if (error) *error = obj.type->name + " has no property '" + name + "'";
            return false;
        }

        bool setProperty(const ObjectRef &obj, const std::string &name, const Value &value, std::string *error)
        {
            void *self = obj.get();
            if (!self || !obj.type)
            {
                if (error) *error = "object is no longer valid";
                return false;
            }
            const PropertyInfo *p = obj.type->findProperty(name);
            if (!p)
            {
                if (error) *error = obj.type->name + " has no property '" + name + "'";
                return false;
            }
            if (p->readOnly())
            {
                if (error) *error = obj.type->name + "." + name + " is read only";
                return false;
            }
            p->set(self, value);
            return true;
        }

        bool callMethod(const ObjectRef &obj, const std::string &name, const std::vector<Value> &args, Value &out,
                        std::string *error)
        {
            void *self = obj.get();
            if (!self || !obj.type)
            {
                if (error) *error = "object is no longer valid";
                return false;
            }
            const MethodInfo *m = obj.type->findMethod(name);
            if (!m)
            {
                if (error) *error = obj.type->name + " has no method '" + name + "'";
                return false;
            }
            out = m->invoke(self, args);
            return true;
        }

    } // namespace Reflect
} // namespace Genesis
