/*
 * Genesis Render - runtime type information (RTTI / reflection).
 *
 * Every engine object that scripts, the editor UI or tools need to reach is
 * described by a TypeInfo: named properties (getter / setter, value type,
 * range, enum names, description), named methods (type-erased invokers with
 * automatic argument conversion) and an optional constructor.  Types are
 * registered once with ClassBuilder:
 *
 *     Reflect::registerClass<Material>("Material", "PBR / unlit surface")
 *         .property("roughness", &Material::roughness).range(0.0f, 1.0f)
 *         .method("clone", [](Material &m, std::string name) { ... });
 *
 * Values cross the reflection boundary as Reflect::Value (nil, bool, int,
 * float, string, vec2/3/4, quat, colour, mat4, object, list).  An object is an
 * ObjectRef: its type, a resolver returning the live address (components
 * live in ECS pools and move, entities die) and an optional owner that keeps
 * shared objects alive.
 *
 * The Lua binding (script/lua_reflection.cpp) is generic over this module:
 * no engine type needs hand written Lua glue.
 */
#ifndef __GENESIS_REFLECTION_HPP__
#define __GENESIS_REFLECTION_HPP__

#include "foundation_layer/math/math.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

namespace Genesis
{
    namespace Reflect
    {
        struct TypeInfo;

        enum class ValueType : uint8_t
        {
            Nil = 0,
            Bool,
            Int,
            Float,
            String,
            Vec2,
            Vec3,
            Vec4,
            Quat,
            Color,
            Mat4,
            Object,
            List,
        };
        const char *valueTypeName(ValueType t);

        //------------------------------------------------------------------
        struct ObjectRef
        {
            const TypeInfo *type = nullptr;
            std::function<void *()> resolve; // current address, nullptr once the object is gone
            std::shared_ptr<void> holder;    // keeps shared objects alive
            uint64_t identity = 0;           // equality key (address, entity id, ...)

            void *get() const { return resolve ? resolve() : nullptr; }
            bool valid() const { return type && get() != nullptr; }
        };

        //------------------------------------------------------------------
        class Value
        {
        public:
            Value() = default;
            Value(bool v) : type_(ValueType::Bool), i_(v ? 1 : 0) {}
            Value(int v) : type_(ValueType::Int), i_(v) {}
            Value(int64_t v) : type_(ValueType::Int), i_(v) {}
            Value(uint32_t v) : type_(ValueType::Int), i_((int64_t)v) {}
            Value(uint64_t v) : type_(ValueType::Int), i_((int64_t)v) {}
            Value(float v) : type_(ValueType::Float), f_(v) {}
            Value(double v) : type_(ValueType::Float), f_(v) {}
            Value(const char *v) : type_(ValueType::String), s_(v ? v : "") {}
            Value(std::string v) : type_(ValueType::String), s_(std::move(v)) {}
            Value(const Math::Vec2 &v) : type_(ValueType::Vec2) { set(v.x, v.y, 0, 0); }
            Value(const Math::Vec3 &v) : type_(ValueType::Vec3) { set(v.x, v.y, v.z, 0); }
            Value(const Math::Vec4 &v) : type_(ValueType::Vec4) { set(v.x, v.y, v.z, v.w); }
            Value(const Math::Quat &q) : type_(ValueType::Quat) { set(q.x, q.y, q.z, q.w); }
            Value(const Math::Color &c) : type_(ValueType::Color) { set(c.r, c.g, c.b, c.a); }
            Value(const Math::Mat4 &m);
            Value(const ObjectRef &o) : type_(o.type ? ValueType::Object : ValueType::Nil), obj_(std::make_shared<ObjectRef>(o)) {}
            static Value list(std::vector<Value> items)
            {
                Value v;
                v.type_ = ValueType::List;
                v.list_ = std::make_shared<std::vector<Value>>(std::move(items));
                return v;
            }

            ValueType type() const { return type_; }
            bool isNil() const { return type_ == ValueType::Nil; }
            bool isNumber() const { return type_ == ValueType::Int || type_ == ValueType::Float; }
            bool isObject() const { return type_ == ValueType::Object && obj_; }

            // Tolerant conversions (numbers <-> bool, vec3 <-> colour, ...).
            bool asBool() const;
            int64_t asInt() const;
            double asFloat() const;
            std::string asString() const;
            Math::Vec2 asVec2() const;
            Math::Vec3 asVec3() const;
            Math::Vec4 asVec4() const;
            Math::Quat asQuat() const;
            Math::Color asColor() const;
            Math::Mat4 asMat4() const;
            const ObjectRef *asObject() const { return isObject() ? obj_.get() : nullptr; }
            const std::vector<Value> &asList() const;
            const float *raw() const { return v_; }

            std::string toString() const;

        private:
            void set(float a, float b, float c, float d) { v_[0] = a; v_[1] = b; v_[2] = c; v_[3] = d; }
            ValueType type_ = ValueType::Nil;
            int64_t i_ = 0;
            double f_ = 0.0;
            std::string s_;
            float v_[16] = {0};
            std::shared_ptr<ObjectRef> obj_;
            std::shared_ptr<std::vector<Value>> list_;
        };

        //------------------------------------------------------------------
        struct PropertyInfo
        {
            std::string name;
            std::string description;
            ValueType type = ValueType::Nil;
            std::string object_type; // for ValueType::Object
            std::function<Value(void *self)> get;
            std::function<void(void *self, const Value &)> set; // empty => read only
            float min = 0.0f, max = 0.0f;                        // UI range hint (min == max: none)
            std::vector<std::string> enum_names;                 // enum properties are exposed by name
            bool readOnly() const { return !set; }
        };

        struct MethodInfo
        {
            std::string name;
            std::string signature; // human readable, e.g. "(string name) -> Material"
            std::string description;
            std::function<Value(void *self, const std::vector<Value> &args)> invoke;
        };

        struct TypeInfo
        {
            std::string name;
            std::string description;
            const TypeInfo *base = nullptr;
            std::type_index cpp_type = std::type_index(typeid(void));
            std::vector<PropertyInfo> properties;
            std::vector<MethodInfo> methods;
            // Optional: TypeName.new(...) from scripts.
            std::function<Value(const std::vector<Value> &args)> constructor;
            std::string constructor_signature;
            // Optional: extra named members resolved at run time (e.g.
            // entity.Transform).  Return false when `key` is unknown.
            std::function<bool(void *self, const std::string &key, Value &out)> dynamic_get;
            std::function<std::vector<std::string>(void *self)> dynamic_keys;

            const PropertyInfo *findProperty(const std::string &n) const;
            const MethodInfo *findMethod(const std::string &n) const;
            bool isA(const TypeInfo *other) const;
            // All properties / methods including the base chain.
            std::vector<const PropertyInfo *> allProperties() const;
            std::vector<const MethodInfo *> allMethods() const;
        };

        //------------------------------------------------------------------
        class Registry
        {
        public:
            static Registry &instance();

            TypeInfo &add(const std::string &name, std::type_index cpp_type, const std::string &base = "");
            const TypeInfo *find(const std::string &name) const;
            const TypeInfo *find(std::type_index cpp_type) const;
            std::vector<const TypeInfo *> types() const;

            // Free functions exposed to scripts (e.g. "log", "vec3"...).
            void addFunction(const MethodInfo &fn) { functions_[fn.name] = fn; }
            const std::map<std::string, MethodInfo> &functions() const { return functions_; }

        private:
            std::map<std::string, std::unique_ptr<TypeInfo>> types_;
            std::map<std::type_index, TypeInfo *> by_cpp_;
            std::map<std::string, MethodInfo> functions_;
        };

        template <typename T>
        const TypeInfo *typeOf()
        {
            return Registry::instance().find(std::type_index(typeid(T)));
        }

        // Object references ------------------------------------------------
        template <typename T>
        ObjectRef refShared(const std::shared_ptr<T> &p)
        {
            ObjectRef r;
            if (!p) return r;
            r.type = typeOf<T>();
            T *raw = p.get();
            r.resolve = [raw]() -> void * { return raw; };
            r.holder = std::static_pointer_cast<void>(std::const_pointer_cast<std::remove_const_t<T>>(p));
            r.identity = (uint64_t)(uintptr_t)raw;
            return r;
        }

        // Non-owning reference to an object that outlives the script call
        // (engine subsystems, settings).
        template <typename T>
        ObjectRef refRaw(T *p)
        {
            ObjectRef r;
            if (!p) return r;
            r.type = typeOf<T>();
            r.resolve = [p]() -> void * { return p; };
            r.identity = (uint64_t)(uintptr_t)p;
            return r;
        }

        // Invokes a method / reads a property by name (used by bindings and tools).
        bool getProperty(const ObjectRef &obj, const std::string &name, Value &out, std::string *error = nullptr);
        bool setProperty(const ObjectRef &obj, const std::string &name, const Value &value, std::string *error = nullptr);
        bool callMethod(const ObjectRef &obj, const std::string &name, const std::vector<Value> &args, Value &out,
                        std::string *error = nullptr);

        //==================================================================
        // C++ <-> Value conversion traits
        //==================================================================
        template <typename T, typename Enable = void>
        struct Traits;

        template <>
        struct Traits<Value>
        {
            static Value to(const Value &v) { return v; }
            static Value from(const Value &v) { return v; }
            static ValueType type() { return ValueType::Nil; }
        };
        template <>
        struct Traits<bool>
        {
            static Value to(bool v) { return Value(v); }
            static bool from(const Value &v) { return v.asBool(); }
            static ValueType type() { return ValueType::Bool; }
        };
        template <typename T>
        struct Traits<T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
        {
            static Value to(T v) { return Value((int64_t)v); }
            static T from(const Value &v) { return (T)v.asInt(); }
            static ValueType type() { return ValueType::Int; }
        };
        template <typename T>
        struct Traits<T, std::enable_if_t<std::is_floating_point_v<T>>>
        {
            static Value to(T v) { return Value((double)v); }
            static T from(const Value &v) { return (T)v.asFloat(); }
            static ValueType type() { return ValueType::Float; }
        };
        template <typename T>
        struct Traits<T, std::enable_if_t<std::is_enum_v<T>>>
        {
            static Value to(T v) { return Value((int64_t)v); }
            static T from(const Value &v) { return (T)v.asInt(); }
            static ValueType type() { return ValueType::Int; }
        };
        template <>
        struct Traits<std::string>
        {
            static Value to(const std::string &v) { return Value(v); }
            static std::string from(const Value &v) { return v.asString(); }
            static ValueType type() { return ValueType::String; }
        };
#define GENESIS_REFLECT_VALUE_TRAIT(T, VT, AS)                  \
    template <>                                                 \
    struct Traits<T>                                            \
    {                                                           \
        static Value to(const T &v) { return Value(v); }        \
        static T from(const Value &v) { return v.AS(); }        \
        static ValueType type() { return ValueType::VT; }       \
    };
        GENESIS_REFLECT_VALUE_TRAIT(Math::Vec2, Vec2, asVec2)
        GENESIS_REFLECT_VALUE_TRAIT(Math::Vec3, Vec3, asVec3)
        GENESIS_REFLECT_VALUE_TRAIT(Math::Vec4, Vec4, asVec4)
        GENESIS_REFLECT_VALUE_TRAIT(Math::Quat, Quat, asQuat)
        GENESIS_REFLECT_VALUE_TRAIT(Math::Color, Color, asColor)
        GENESIS_REFLECT_VALUE_TRAIT(Math::Mat4, Mat4, asMat4)
#undef GENESIS_REFLECT_VALUE_TRAIT

        template <typename T>
        struct Traits<std::shared_ptr<T>>
        {
            static Value to(const std::shared_ptr<T> &p) { return p ? Value(refShared(p)) : Value(); }
            static std::shared_ptr<T> from(const Value &v)
            {
                const ObjectRef *o = v.asObject();
                if (!o || !o->holder || !o->type || !o->type->isA(typeOf<T>())) return nullptr;
                return std::static_pointer_cast<T>(o->holder);
            }
            static ValueType type() { return ValueType::Object; }
        };

        template <typename T>
        struct Traits<T *, std::enable_if_t<!std::is_same_v<std::remove_cv_t<T>, char>>>
        {
            static Value to(T *p) { return p ? Value(refRaw(p)) : Value(); }
            static T *from(const Value &v)
            {
                const ObjectRef *o = v.asObject();
                if (!o || !o->type || !o->type->isA(typeOf<std::remove_cv_t<T>>())) return nullptr;
                return static_cast<T *>(o->get());
            }
            static ValueType type() { return ValueType::Object; }
        };

        template <typename T>
        struct Traits<std::vector<T>>
        {
            static Value to(const std::vector<T> &items)
            {
                std::vector<Value> out;
                out.reserve(items.size());
                for (const T &i : items) out.push_back(Traits<T>::to(i));
                return Value::list(std::move(out));
            }
            static std::vector<T> from(const Value &v)
            {
                std::vector<T> out;
                if (v.type() != ValueType::List) return out;
                for (const Value &i : v.asList()) out.push_back(Traits<T>::from(i));
                return out;
            }
            static ValueType type() { return ValueType::List; }
        };

        template <typename T>
        using Bare = std::remove_cv_t<std::remove_reference_t<T>>;

        //==================================================================
        // Callable introspection
        //==================================================================
        template <typename F>
        struct FnTraits : FnTraits<decltype(&F::operator())>
        {
        };
        template <typename C, typename R, typename... A>
        struct FnTraits<R (C::*)(A...) const>
        {
            using Ret = R;
            using Args = std::tuple<A...>;
        };
        template <typename C, typename R, typename... A>
        struct FnTraits<R (C::*)(A...)>
        {
            using Ret = R;
            using Args = std::tuple<A...>;
        };
        template <typename R, typename... A>
        struct FnTraits<R (*)(A...)>
        {
            using Ret = R;
            using Args = std::tuple<A...>;
        };

        inline const Value &argAt(const std::vector<Value> &args, size_t i)
        {
            static const Value nil;
            return i < args.size() ? args[i] : nil;
        }

        template <typename T>
        const char *typeLabel()
        {
            using B = Bare<T>;
            if constexpr (std::is_same_v<B, void>) return "void";
            else if constexpr (std::is_same_v<B, bool>) return "bool";
            else if constexpr (std::is_integral_v<B> || std::is_enum_v<B>) return "int";
            else if constexpr (std::is_floating_point_v<B>) return "number";
            else if constexpr (std::is_same_v<B, std::string>) return "string";
            else if constexpr (std::is_same_v<B, Math::Vec2>) return "vec2";
            else if constexpr (std::is_same_v<B, Math::Vec3>) return "vec3";
            else if constexpr (std::is_same_v<B, Math::Vec4>) return "vec4";
            else if constexpr (std::is_same_v<B, Math::Quat>) return "quat";
            else if constexpr (std::is_same_v<B, Math::Color>) return "color";
            else if constexpr (std::is_same_v<B, Math::Mat4>) return "mat4";
            else if constexpr (std::is_same_v<B, Value>) return "any";
            else return "object";
        }

        template <typename Tuple, size_t... I>
        std::string signatureOf(std::index_sequence<I...>, size_t skip)
        {
            std::string s = "(";
            size_t n = 0;
            ((I >= skip ? (s += (n++ ? ", " : ""), s += typeLabel<std::tuple_element_t<I, Tuple>>(), 0) : 0), ...);
            (void)n;
            return s + ")";
        }

        // Calls `f(self, converted args...)` where the first parameter of F is
        // the object reference.
        template <typename Self, typename F, typename R, typename Tuple, size_t... I>
        Value invokeWithSelf(F &f, void *self, const std::vector<Value> &args, std::index_sequence<I...>)
        {
            if constexpr (std::is_void_v<R>)
            {
                f(*static_cast<Self *>(self), Traits<Bare<std::tuple_element_t<I + 1, Tuple>>>::from(argAt(args, I))...);
                return Value();
            }
            else
            {
                return Traits<Bare<R>>::to(
                    f(*static_cast<Self *>(self), Traits<Bare<std::tuple_element_t<I + 1, Tuple>>>::from(argAt(args, I))...));
            }
        }

        template <typename F, typename R, typename Tuple, size_t... I>
        Value invokeFree(F &f, const std::vector<Value> &args, std::index_sequence<I...>)
        {
            if constexpr (std::is_void_v<R>)
            {
                f(Traits<Bare<std::tuple_element_t<I, Tuple>>>::from(argAt(args, I))...);
                return Value();
            }
            else
            {
                return Traits<Bare<R>>::to(f(Traits<Bare<std::tuple_element_t<I, Tuple>>>::from(argAt(args, I))...));
            }
        }

        // Wraps a free callable (lambda / function pointer) into a MethodInfo.
        template <typename F>
        MethodInfo makeFunction(const std::string &name, F fn, const std::string &description = "")
        {
            using FT = FnTraits<std::decay_t<F>>;
            using Tuple = typename FT::Args;
            using R = typename FT::Ret;
            constexpr size_t N = std::tuple_size_v<Tuple>;
            MethodInfo m;
            m.name = name;
            m.description = description;
            m.signature = signatureOf<Tuple>(std::make_index_sequence<N>(), 0) + " -> " + typeLabel<R>();
            m.invoke = [fn](void *, const std::vector<Value> &args) mutable -> Value
            { return invokeFree<F, R, Tuple>(fn, args, std::make_index_sequence<N>()); };
            return m;
        }

        //==================================================================
        // ClassBuilder
        //==================================================================
        template <typename C>
        class ClassBuilder
        {
        public:
            explicit ClassBuilder(TypeInfo &info) : info_(info) {}

            // Member data (read / write).
            template <typename M>
            ClassBuilder &property(const std::string &name, M C::*member, const std::string &description = "")
            {
                PropertyInfo p;
                p.name = name;
                p.description = description;
                p.type = Traits<M>::type();
                p.get = [member](void *self) -> Value { return Traits<M>::to(static_cast<C *>(self)->*member); };
                p.set = [member](void *self, const Value &v) { static_cast<C *>(self)->*member = Traits<M>::from(v); };
                return add(p);
            }

            // Getter / setter lambdas: G = T(const C&), S = void(C&, T).
            template <typename G, typename S,
                      typename = std::enable_if_t<!std::is_convertible_v<S, std::string> && !std::is_member_object_pointer_v<G>>>
            ClassBuilder &property(const std::string &name, G getter, S setter, const std::string &description = "")
            {
                using T = Bare<typename FnTraits<G>::Ret>;
                using SetArg = Bare<std::tuple_element_t<1, typename FnTraits<S>::Args>>;
                PropertyInfo p;
                p.name = name;
                p.description = description;
                p.type = Traits<T>::type();
                p.get = [getter](void *self) -> Value { return Traits<T>::to(getter(*static_cast<C *>(self))); };
                p.set = [setter](void *self, const Value &v) { setter(*static_cast<C *>(self), Traits<SetArg>::from(v)); };
                return add(p);
            }

            template <typename G>
            ClassBuilder &readonly(const std::string &name, G getter, const std::string &description = "")
            {
                using T = Bare<typename FnTraits<G>::Ret>;
                PropertyInfo p;
                p.name = name;
                p.description = description;
                p.type = Traits<T>::type();
                p.get = [getter](void *self) -> Value { return Traits<T>::to(getter(*static_cast<C *>(self))); };
                return add(p);
            }

            // Enum property exposed by name: getter returns the integer value,
            // setter accepts a name or an integer.
            template <typename G, typename S>
            ClassBuilder &enumProperty(const std::string &name, std::vector<std::string> names, G getter, S setter,
                                       const std::string &description = "")
            {
                PropertyInfo p;
                p.name = name;
                p.description = description;
                p.type = ValueType::String;
                p.enum_names = names;
                p.get = [getter, names](void *self) -> Value
                {
                    int64_t v = (int64_t)getter(*static_cast<C *>(self));
                    return (v >= 0 && (size_t)v < names.size()) ? Value(names[(size_t)v]) : Value(v);
                };
                p.set = [setter, names](void *self, const Value &v)
                {
                    int64_t idx = -1;
                    if (v.type() == ValueType::String)
                    {
                        for (size_t i = 0; i < names.size(); ++i)
                            if (names[i] == v.asString()) idx = (int64_t)i;
                    }
                    else if (v.isNumber())
                    {
                        idx = v.asInt();
                    }
                    if (idx >= 0 && (size_t)idx < names.size()) setter(*static_cast<C *>(self), (int)idx);
                };
                return add(p);
            }

            // Methods: member function pointers or lambdas taking C& first.
            template <typename R, typename... A>
            ClassBuilder &method(const std::string &name, R (C::*fn)(A...), const std::string &description = "")
            {
                return method(name, [fn](C &c, A... a) -> R { return (c.*fn)(a...); }, description);
            }
            template <typename R, typename... A>
            ClassBuilder &method(const std::string &name, R (C::*fn)(A...) const, const std::string &description = "")
            {
                return method(name, [fn](C &c, A... a) -> R { return (c.*fn)(a...); }, description);
            }
            template <typename F>
            ClassBuilder &method(const std::string &name, F fn, const std::string &description = "")
            {
                using FT = FnTraits<std::decay_t<F>>;
                using Tuple = typename FT::Args;
                using R = typename FT::Ret;
                constexpr size_t N = std::tuple_size_v<Tuple>;
                static_assert(N >= 1, "reflected methods take the object as their first parameter");
                MethodInfo m;
                m.name = name;
                m.description = description;
                m.signature = signatureOf<Tuple>(std::make_index_sequence<N>(), 1) + " -> " + typeLabel<R>();
                m.invoke = [fn](void *self, const std::vector<Value> &args) mutable -> Value
                { return invokeWithSelf<C, F, R, Tuple>(fn, self, args, std::make_index_sequence<N - 1>()); };
                info_.methods.push_back(m);
                return *this;
            }

            // TypeName.new(args...) for scripts.
            template <typename F>
            ClassBuilder &constructor(F fn, const std::string &signature = "")
            {
                MethodInfo m = makeFunction("new", fn);
                info_.constructor = [inv = m.invoke](const std::vector<Value> &args) { return inv(nullptr, args); };
                info_.constructor_signature = signature.empty() ? m.signature : signature;
                return *this;
            }

            ClassBuilder &range(float lo, float hi)
            {
                if (!info_.properties.empty())
                {
                    info_.properties.back().min = lo;
                    info_.properties.back().max = hi;
                }
                return *this;
            }

            ClassBuilder &dynamic(std::function<bool(C &, const std::string &, Value &)> get,
                                  std::function<std::vector<std::string>(C &)> keys = nullptr)
            {
                info_.dynamic_get = [get](void *self, const std::string &k, Value &out) { return get(*static_cast<C *>(self), k, out); };
                if (keys) info_.dynamic_keys = [keys](void *self) { return keys(*static_cast<C *>(self)); };
                return *this;
            }

            TypeInfo &info() { return info_; }

        private:
            ClassBuilder &add(const PropertyInfo &p)
            {
                info_.properties.push_back(p);
                return *this;
            }
            TypeInfo &info_;
        };

        template <typename C>
        ClassBuilder<C> registerClass(const std::string &name, const std::string &description = "", const std::string &base = "")
        {
            TypeInfo &t = Registry::instance().add(name, std::type_index(typeid(C)), base);
            t.description = description;
            return ClassBuilder<C>(t);
        }

    } // namespace Reflect
} // namespace Genesis

#endif /* __GENESIS_REFLECTION_HPP__ */
