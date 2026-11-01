/*
 * Genesis Render - V8 JavaScript scripting implementation
 */
#include "script/js_system.hpp"

#include "engine/engine.hpp"
#include "foundation_layer/genesis_log.hpp"
#include "physics/physics.hpp"
#include "reflection/engine_bindings.hpp"
#include "resource/resource_types.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#ifndef GENESIS_ENABLE_V8
#define GENESIS_ENABLE_V8 0
#endif

#if GENESIS_ENABLE_V8
#include <v8.h>
#include <libplatform/libplatform.h>
#endif

namespace Genesis
{
    namespace Script
    {
#if GENESIS_ENABLE_V8
        namespace
        {
            v8::Platform *g_platform = nullptr;
            bool g_v8_ready = false;
            std::once_flag g_v8_once;

            void initializeV8()
            {
                std::call_once(g_v8_once, []()
                {
                    v8::V8::InitializeICUDefaultLocation("genesis");
                    v8::V8::InitializeExternalStartupData("");
                    g_platform = v8::platform::NewDefaultPlatform().release();
                    v8::V8::InitializePlatform(g_platform);
                    g_v8_ready = v8::V8::Initialize();
                    if (!g_v8_ready) GEN_LOG_ERROR("[JS] V8::Initialize failed");
                });
            }

            std::string utf8(v8::Isolate *isolate, v8::Local<v8::Value> value)
            {
                if (value.IsEmpty() || !value->IsString()) return {};
                v8::String::Utf8Value text(isolate, value);
                return *text ? std::string(*text, text.length()) : std::string();
            }

            std::string propName(v8::Isolate *isolate, v8::Local<v8::Name> name)
            {
                v8::Local<v8::Value> value = name;
                return utf8(isolate, value);
            }

            double numberProperty(v8::Local<v8::Context> context, v8::Local<v8::Object> object, const char *key,
                                  double fallback)
            {
                v8::Local<v8::Value> value;
                if (!object->Get(context, v8::String::NewFromUtf8(context->GetIsolate(), key).ToLocalChecked())
                         .ToLocal(&value))
                    return fallback;
                return value->IsNumber() ? value->NumberValue(context).FromMaybe(fallback) : fallback;
            }

            bool hasProperty(v8::Local<v8::Context> context, v8::Local<v8::Object> object, const char *key)
            {
                v8::Local<v8::Value> value;
                if (!object->Get(context, v8::String::NewFromUtf8(context->GetIsolate(), key).ToLocalChecked())
                         .ToLocal(&value))
                    return false;
                return !value->IsUndefined() && !value->IsNull();
            }
        } // namespace

        //==================================================================
        struct JsObject
        {
            Reflect::ObjectRef ref;
        };

        struct JSSystem::Impl
        {
            JSSystem *self = nullptr;
            ECS::World *world = nullptr;
            Render::RenderSystem *render = nullptr;
            Platform::InputSystem *input = nullptr;

            v8::Isolate *isolate = nullptr;
            std::unique_ptr<v8::ArrayBuffer::Allocator> allocator;
            v8::Global<v8::Context> console_context;
            v8::Global<v8::ObjectTemplate> object_template;

            struct Wrapper
            {
                v8::Global<v8::Object> object;
                JsObject *raw = nullptr;
            };
            std::unordered_map<std::string, std::unique_ptr<Wrapper>> wrappers;

            struct Instance
            {
                v8::Global<v8::Context> context;
                v8::Global<v8::Function> start;
                v8::Global<v8::Function> tick;
                bool has_start = false;
                bool has_tick = false;
                float time_scale = 1.0f;
            };
            std::unordered_map<uint32_t, std::unique_ptr<Instance>> instances;

            void report(const std::string &message) { self->reportError(message); }
            std::string errorMessage(v8::TryCatch &try_catch);
            v8::Local<v8::Context> createContext();
            void installGlobals(v8::Local<v8::Context> context, ECS::Entity script_entity,
                                const std::string &asset_root);
            v8::Local<v8::Value> wrapObject(const Reflect::ObjectRef &ref);
            v8::Local<v8::Value> toJs(const Reflect::Value &value);
            Reflect::Value toReflect(v8::Local<v8::Value> value);
            v8::Local<v8::Function> makeMethod(v8::Local<v8::Context> context, const Reflect::MethodInfo *method);
            bool runSource(v8::Local<v8::Context> context, const std::string &source, const std::string &name);
            bool callHook(Instance &instance, const char *name, int argc, v8::Local<v8::Value> *argv);
            void resetInstance(uint32_t id);
            void clearWrappers();
            void clearInstances();
        };

        //==================================================================
        // Callback helpers
        //==================================================================
        namespace
        {
            JSSystem::Impl *implOf(v8::Isolate *isolate)
            {
                return static_cast<JSSystem::Impl *>(isolate->GetData(0));
            }

            JsObject *jsObjectOf(v8::Local<v8::Object> object)
            {
                if (object.IsEmpty() || object->InternalFieldCount() < 1) return nullptr;
                return static_cast<JsObject *>(object->GetAlignedPointerFromInternalField(0));
            }

            void setGlobal(v8::Local<v8::Context> context, const char *name, v8::Local<v8::Value> value)
            {
                context->Global()
                    ->Set(context, v8::String::NewFromUtf8(context->GetIsolate(), name).ToLocalChecked(), value)
                    .Check();
            }

            v8::Local<v8::String> makeString(v8::Isolate *isolate, const std::string &text)
            {
                return v8::String::NewFromUtf8(isolate, text.c_str(), v8::NewStringType::kNormal, (int)text.size())
                    .ToLocalChecked();
            }

            //--------------------------------------------------------------
            // Math / value constructors are provided by the JS prelude; the
            // returned objects carry a hidden __kind marker so that the
            // generic RTTI conversion can disambiguate quat vs vec4.
            //--------------------------------------------------------------
            void throwTypeError(v8::Isolate *isolate, const std::string &message)
            {
                isolate->ThrowException(v8::Exception::TypeError(makeString(isolate, message)));
            }

            void methodCallback(const v8::FunctionCallbackInfo<v8::Value> &info)
            {
                v8::Isolate *isolate = info.GetIsolate();
                JSSystem::Impl *impl = implOf(isolate);
                auto *method = static_cast<const Reflect::MethodInfo *>(info.Data().As<v8::External>()->Value());
                if (!impl || !method) return;
                v8::HandleScope scope(isolate);
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                v8::Local<v8::Object> self_object = info.This();
                JsObject *object = jsObjectOf(self_object);
                if (!object || !object->ref.valid())
                {
                    throwTypeError(isolate, std::string("method '") + method->name +
                                                "' must be called as obj." + method->name + "(...)");
                    return;
                }
                std::vector<Reflect::Value> args;
                args.reserve((size_t)info.Length());
                for (int i = 0; i < info.Length(); ++i) args.push_back(impl->toReflect(info[i]));
                Reflect::Value result = method->invoke(object->ref.get(), args);
                info.GetReturnValue().Set(impl->toJs(result));
            }

            void freeFunctionCallback(const v8::FunctionCallbackInfo<v8::Value> &info)
            {
                v8::Isolate *isolate = info.GetIsolate();
                JSSystem::Impl *impl = implOf(isolate);
                auto *method = static_cast<const Reflect::MethodInfo *>(info.Data().As<v8::External>()->Value());
                if (!impl || !method) return;
                std::vector<Reflect::Value> args;
                args.reserve((size_t)info.Length());
                for (int i = 0; i < info.Length(); ++i) args.push_back(impl->toReflect(info[i]));
                info.GetReturnValue().Set(impl->toJs(method->invoke(nullptr, args)));
            }

            void constructorCallback(const v8::FunctionCallbackInfo<v8::Value> &info)
            {
                v8::Isolate *isolate = info.GetIsolate();
                JSSystem::Impl *impl = implOf(isolate);
                auto *type = static_cast<const Reflect::TypeInfo *>(info.Data().As<v8::External>()->Value());
                if (!impl || !type || !type->constructor)
                {
                    throwTypeError(isolate, "type is not constructible");
                    return;
                }
                std::vector<Reflect::Value> args;
                args.reserve((size_t)info.Length());
                for (int i = 0; i < info.Length(); ++i) args.push_back(impl->toReflect(info[i]));
                info.GetReturnValue().Set(impl->toJs(type->constructor(args)));
            }

            void objectGetter(v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value> &info)
            {
                v8::Isolate *isolate = info.GetIsolate();
                JSSystem::Impl *impl = implOf(isolate);
                if (!impl) return;
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                v8::Local<v8::Object> self_object = info.Holder();
                JsObject *object = jsObjectOf(self_object);
                if (!object) return;
                const std::string key = propName(isolate, property);

                if (key == "__type")
                {
                    info.GetReturnValue().Set(makeString(isolate, object->ref.type ? object->ref.type->name : "object"));
                    return;
                }
                if (key == "__valid")
                {
                    info.GetReturnValue().Set(v8::Boolean::New(isolate, object->ref.valid()));
                    return;
                }
                if (key == "toString")
                {
                    v8::Local<v8::Function> fn = v8::FunctionTemplate::New(
                        isolate, [](const v8::FunctionCallbackInfo<v8::Value> &args)
                        {
                            JsObject *o = jsObjectOf(args.This());
                            std::string text = "object";
                            if (o)
                            {
                                Reflect::Value name;
                                text = o->ref.type ? o->ref.type->name : "object";
                                if (o->ref.valid() && Reflect::getProperty(o->ref, "name", name, nullptr))
                                    text += " '" + name.asString() + "'";
                            }
                            args.GetReturnValue().Set(makeString(args.GetIsolate(), text));
                        })->GetFunction(context).ToLocalChecked();
                    info.GetReturnValue().Set(fn);
                    return;
                }

                // Real own properties (set by scripts) win over RTTI.
                v8::Maybe<bool> has_real = self_object->HasRealNamedProperty(context, property);
                if (has_real.FromMaybe(false))
                {
                    v8::Local<v8::Value> value;
                    if (self_object->Get(context, property).ToLocal(&value)) info.GetReturnValue().Set(value);
                    return;
                }

                if (!object->ref.type)
                {
                    info.GetReturnValue().Set(v8::Undefined(isolate));
                    return;
                }
                if (const Reflect::MethodInfo *method = object->ref.type->findMethod(key))
                {
                    info.GetReturnValue().Set(impl->makeMethod(context, method));
                    return;
                }
                Reflect::Value out;
                std::string error;
                if (Reflect::getProperty(object->ref, key, out, &error))
                    info.GetReturnValue().Set(impl->toJs(out));
                else
                    info.GetReturnValue().Set(v8::Undefined(isolate));
            }

            void objectSetter(v8::Local<v8::Name> property, v8::Local<v8::Value> value,
                              const v8::PropertyCallbackInfo<v8::Value> &info)
            {
                v8::Isolate *isolate = info.GetIsolate();
                JSSystem::Impl *impl = implOf(isolate);
                if (!impl) return;
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                JsObject *object = jsObjectOf(info.Holder());
                if (!object || !object->ref.type) return;
                const std::string key = propName(isolate, property);

                if (const Reflect::PropertyInfo *p = object->ref.type->findProperty(key))
                {
                    if (p->readOnly())
                    {
                        throwTypeError(isolate, object->ref.type->name + "." + key + " is read only");
                        return;
                    }
                    Reflect::setProperty(object->ref, key, impl->toReflect(value), nullptr);
                    info.GetReturnValue().Set(value);
                    return;
                }
                // Dynamic component access is read only: assigning it would
                // shadow the component.
                if (object->ref.type->dynamic_get)
                {
                    Reflect::Value ignored;
                    if (object->ref.type->dynamic_get(object->ref.get(), key, ignored))
                    {
                        throwTypeError(isolate, object->ref.type->name + "." + key + " is a component reference");
                        return;
                    }
                }
                // Any other property becomes a regular JS field on the wrapper.
                info.Holder()->CreateDataProperty(context, property, value).FromMaybe(false);
                info.GetReturnValue().Set(value);
            }

            void objectEnumerator(const v8::PropertyCallbackInfo<v8::Array> &info)
            {
                v8::Isolate *isolate = info.GetIsolate();
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                JsObject *object = jsObjectOf(info.Holder());
                std::vector<std::string> keys;
                if (object && object->ref.type)
                {
                    for (const Reflect::PropertyInfo *p : object->ref.type->allProperties()) keys.push_back(p->name);
                    for (const Reflect::MethodInfo *m : object->ref.type->allMethods()) keys.push_back(m->name);
                    if (object->ref.type->dynamic_keys && object->ref.valid())
                        for (const std::string &k : object->ref.type->dynamic_keys(object->ref.get())) keys.push_back(k);
                }
                v8::Local<v8::Array> array = v8::Array::New(isolate, (int)keys.size());
                for (uint32_t i = 0; i < keys.size(); ++i)
                    array->Set(context, i, makeString(isolate, keys[i])).Check();
                info.GetReturnValue().Set(array);
            }

            void printCallback(const v8::FunctionCallbackInfo<v8::Value> &info)
            {
                v8::Isolate *isolate = info.GetIsolate();
                v8::String::Utf8Value text(isolate, info[0]);
                std::string out;
                for (int i = 0; i < info.Length(); ++i)
                {
                    if (i) out += " ";
                    v8::String::Utf8Value part(isolate, info[i]);
                    out += *part ? std::string(*part, part.length()) : std::string("undefined");
                }
                GEN_LOG_INFO("[JS] %s", out.c_str());
                (void)text;
            }

            void assetCallback(const v8::FunctionCallbackInfo<v8::Value> &info)
            {
                v8::Isolate *isolate = info.GetIsolate();
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                const std::string rel = utf8(isolate, info[0]);
                std::string root;
                v8::Local<v8::Value> root_value;
                if (context->Global()
                        ->Get(context, v8::String::NewFromUtf8(isolate, "__asset_root").ToLocalChecked())
                        .ToLocal(&root_value))
                    root = utf8(isolate, root_value);
                Reflect::EngineContext &ctx = Reflect::context();
                if (root.empty() && ctx.engine) root = ctx.engine->config().asset_dir;
                info.GetReturnValue().Set(makeString(isolate, ResourceIO::joinPath(root, rel)));
            }

            void assetTextCallback(const v8::FunctionCallbackInfo<v8::Value> &info)
            {
                v8::Isolate *isolate = info.GetIsolate();
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                const std::string rel = utf8(isolate, info[0]);
                std::string root;
                v8::Local<v8::Value> root_value;
                if (context->Global()
                        ->Get(context, v8::String::NewFromUtf8(isolate, "__asset_root").ToLocalChecked())
                        .ToLocal(&root_value))
                    root = utf8(isolate, root_value);
                Reflect::EngineContext &ctx = Reflect::context();
                if (root.empty() && ctx.engine) root = ctx.engine->config().asset_dir;
                std::string text;
                const std::string full = ResourceIO::joinPath(root, rel);
                if (ResourceIO::fileExists(full)) ResourceIO::readFileText(full, text);
                info.GetReturnValue().Set(makeString(isolate, text));
            }

            void entityCallback(const v8::FunctionCallbackInfo<v8::Value> &info)
            {
                JSSystem::Impl *impl = implOf(info.GetIsolate());
                Reflect::EngineContext &ctx = Reflect::context();
                if (!impl || !ctx.world || info.Length() < 1 || !info[0]->IsNumber()) return;
                const int64_t id = (int64_t)info[0]->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(-1.0);
                info.GetReturnValue().Set(impl->toJs(Reflect::entityValue(ctx.world, ctx.world->entityFromId((ECS::EntityId)id))));
            }

            void elapsedCallback(const v8::FunctionCallbackInfo<v8::Value> &info)
            {
                JSSystem::Impl *impl = implOf(info.GetIsolate());
                info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(), impl && impl->self ? impl->self->elapsed() : 0.0));
            }
            void frameCallback(const v8::FunctionCallbackInfo<v8::Value> &info)
            {
                JSSystem::Impl *impl = implOf(info.GetIsolate());
                info.GetReturnValue().Set(v8::Number::New(info.GetIsolate(),
                                                          (double)(impl && impl->self ? impl->self->frameIndex() : 0)));
            }
            void setTimeScaleCallback(const v8::FunctionCallbackInfo<v8::Value> &info)
            {
                JSSystem::Impl *impl = implOf(info.GetIsolate());
                if (impl && impl->self && info.Length() >= 1 && info[0]->IsNumber())
                    impl->self->setTimeScale((float)info[0]->NumberValue(info.GetIsolate()->GetCurrentContext()).FromMaybe(1.0));
            }
        } // namespace

        //==================================================================
        std::string JSSystem::Impl::errorMessage(v8::TryCatch &try_catch)
        {
            std::string message;
            v8::Local<v8::Value> exception = try_catch.Exception();
            if (!exception.IsEmpty())
            {
                v8::String::Utf8Value text(isolate, exception);
                if (*text) message = std::string(*text, text.length());
            }
            if (message.empty())
            {
                v8::Local<v8::Message> m = try_catch.Message();
                if (!m.IsEmpty())
                {
                    v8::String::Utf8Value text(isolate, m->Get());
                    if (*text) message = std::string(*text, text.length());
                }
            }
            return message.empty() ? "unknown JavaScript error" : message;
        }

        v8::Local<v8::Context> JSSystem::Impl::createContext()
        {
            v8::Local<v8::Context> context = v8::Context::New(isolate);
            v8::Context::Scope scope(context);
            return context;
        }

        v8::Local<v8::Value> JSSystem::Impl::wrapObject(const Reflect::ObjectRef &ref)
        {
            if (!ref.valid()) return v8::Null(isolate);
            char pointer[32];
            std::snprintf(pointer, sizeof(pointer), "%llx", (unsigned long long)(uintptr_t)ref.get());
            const std::string key = std::string(ref.type ? ref.type->name : "object") + "#" +
                                    std::to_string(ref.identity) + "@" + pointer;
            auto it = wrappers.find(key);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            if (it != wrappers.end()) return it->second->object.Get(isolate);
            v8::Local<v8::ObjectTemplate> templ = object_template.Get(isolate);
            v8::Local<v8::Object> object;
            if (!templ->NewInstance(context).ToLocal(&object)) return v8::Null(isolate);
            JsObject *raw = new JsObject{ref};
            object->SetAlignedPointerInInternalField(0, raw);
            auto wrapper = std::make_unique<Wrapper>();
            wrapper->object.Reset(isolate, object);
            wrapper->raw = raw;
            wrappers.emplace(key, std::move(wrapper));
            return object;
        }

        v8::Local<v8::Value> JSSystem::Impl::toJs(const Reflect::Value &value)
        {
            using VT = Reflect::ValueType;
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            switch (value.type())
            {
            case VT::Nil: return v8::Null(isolate);
            case VT::Bool: return v8::Boolean::New(isolate, value.asBool());
            case VT::Int: return v8::Number::New(isolate, (double)value.asInt());
            case VT::Float: return v8::Number::New(isolate, value.asFloat());
            case VT::String: return makeString(isolate, value.asString());
            case VT::Vec2:
            case VT::Vec3:
            case VT::Vec4:
            {
                v8::Local<v8::Object> object = v8::Object::New(isolate);
                const char *xyzw[] = {"x", "y", "z", "w"};
                const int count = value.type() == VT::Vec2 ? 2 : value.type() == VT::Vec3 ? 3 : 4;
                for (int i = 0; i < count; ++i)
                    object->Set(context, makeString(isolate, xyzw[i]), v8::Number::New(isolate, value.raw()[i])).Check();
                const char *proto_name = value.type() == VT::Vec2 ? "__vec2_proto"
                                         : value.type() == VT::Vec3 ? "__vec3_proto" : "__vec4_proto";
                v8::Local<v8::Value> proto;
                if (context->Global()->Get(context, makeString(isolate, proto_name)).ToLocal(&proto) &&
                    proto->IsObject())
                    object->SetPrototype(context, proto).FromMaybe(false);
                return object;
            }
            case VT::Color:
            {
                v8::Local<v8::Object> object = v8::Object::New(isolate);
                const char *rgba[] = {"r", "g", "b", "a"};
                for (int i = 0; i < 4; ++i)
                    object->Set(context, makeString(isolate, rgba[i]), v8::Number::New(isolate, value.raw()[i])).Check();
                v8::Local<v8::Value> proto;
                if (context->Global()->Get(context, makeString(isolate, "__color_proto")).ToLocal(&proto) &&
                    proto->IsObject())
                    object->SetPrototype(context, proto).FromMaybe(false);
                return object;
            }
            case VT::Quat:
            {
                v8::Local<v8::Object> object = v8::Object::New(isolate);
                const char *xyzw[] = {"x", "y", "z", "w"};
                for (int i = 0; i < 4; ++i)
                    object->Set(context, makeString(isolate, xyzw[i]), v8::Number::New(isolate, value.raw()[i])).Check();
                object->Set(context, makeString(isolate, "__kind"), makeString(isolate, "quat")).Check();
                return object;
            }
            case VT::Mat4:
            {
                v8::Local<v8::Array> array = v8::Array::New(isolate, 16);
                for (int i = 0; i < 16; ++i)
                    array->Set(context, (uint32_t)i, v8::Number::New(isolate, value.raw()[i])).Check();
                array->Set(context, makeString(isolate, "__kind"), makeString(isolate, "mat4")).Check();
                return array;
            }
            case VT::Object:
            {
                const Reflect::ObjectRef *ref = value.asObject();
                return ref ? wrapObject(*ref) : v8::Local<v8::Value>(v8::Null(isolate));
            }
            case VT::List:
            {
                const auto &items = value.asList();
                v8::Local<v8::Array> array = v8::Array::New(isolate, (int)items.size());
                for (uint32_t i = 0; i < items.size(); ++i) array->Set(context, i, toJs(items[i])).Check();
                return array;
            }
            }
            return v8::Undefined(isolate);
        }

        Reflect::Value JSSystem::Impl::toReflect(v8::Local<v8::Value> value)
        {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            if (value.IsEmpty() || value->IsUndefined() || value->IsNull()) return Reflect::Value();
            if (value->IsBoolean()) return Reflect::Value(value->BooleanValue(isolate));
            if (value->IsNumber())
            {
                const double number = value->NumberValue(context).FromMaybe(0.0);
                if (value->IsInt32() || value->IsUint32()) return Reflect::Value((int64_t)number);
                return Reflect::Value(number);
            }
            if (value->IsString()) return Reflect::Value(utf8(isolate, value));
            if (value->IsArray())
            {
                v8::Local<v8::Array> array = value.As<v8::Array>();
                const uint32_t length = array->Length();
                if (length == 16)
                {
                    float m[16] = {0};
                    for (uint32_t i = 0; i < 16; ++i)
                    {
                        v8::Local<v8::Value> item;
                        if (array->Get(context, i).ToLocal(&item) && item->IsNumber())
                            m[i] = (float)item->NumberValue(context).FromMaybe(0.0);
                    }
                    return Reflect::Value(Math::Mat4(m));
                }
                bool numeric = length >= 2 && length <= 4;
                std::vector<Reflect::Value> items;
                for (uint32_t i = 0; i < length; ++i)
                {
                    v8::Local<v8::Value> item;
                    if (!array->Get(context, i).ToLocal(&item)) continue;
                    if (!item->IsNumber()) numeric = false;
                    items.push_back(toReflect(item));
                }
                if (numeric && length >= 2)
                {
                    float c[4] = {0, 0, 0, 0};
                    for (uint32_t i = 0; i < length; ++i) c[i] = (float)items[i].asFloat();
                    if (length == 2) return Reflect::Value(Math::Vec2(c[0], c[1]));
                    if (length == 3) return Reflect::Value(Math::Vec3(c[0], c[1], c[2]));
                    return Reflect::Value(Math::Vec4(c[0], c[1], c[2], c[3]));
                }
                return Reflect::Value::list(std::move(items));
            }
            if (value->IsObject())
            {
                v8::Local<v8::Object> object = value.As<v8::Object>();
                JsObject *wrapped = jsObjectOf(object);
                if (wrapped) return Reflect::Value(wrapped->ref);

                const std::string kind = hasProperty(context, object, "__kind")
                                             ? utf8(isolate, object->Get(context, makeString(isolate, "__kind"))
                                                                 .ToLocalChecked())
                                             : std::string();
                const bool has_x = hasProperty(context, object, "x");
                if (has_x || kind == "quat")
                {
                    const double x = numberProperty(context, object, "x", 0.0);
                    const double y = numberProperty(context, object, "y", 0.0);
                    const double z = numberProperty(context, object, "z", 0.0);
                    const double w = numberProperty(context, object, "w", 1.0);
                    if (kind == "quat") return Reflect::Value(Math::Quat((float)x, (float)y, (float)z, (float)w));
                    if (hasProperty(context, object, "w"))
                        return Reflect::Value(Math::Vec4((float)x, (float)y, (float)z, (float)w));
                    if (hasProperty(context, object, "z")) return Reflect::Value(Math::Vec3((float)x, (float)y, (float)z));
                    return Reflect::Value(Math::Vec2((float)x, (float)y));
                }
                if (hasProperty(context, object, "r"))
                {
                    return Reflect::Value(Math::Color(
                        (float)numberProperty(context, object, "r", 1.0), (float)numberProperty(context, object, "g", 1.0),
                        (float)numberProperty(context, object, "b", 1.0), (float)numberProperty(context, object, "a", 1.0)));
                }
            }
            return Reflect::Value();
        }

        v8::Local<v8::Function> JSSystem::Impl::makeMethod(v8::Local<v8::Context> context,
                                                           const Reflect::MethodInfo *method)
        {
            v8::Local<v8::FunctionTemplate> templ = v8::FunctionTemplate::New(
                isolate, methodCallback, v8::External::New(isolate, const_cast<Reflect::MethodInfo *>(method)));
            return templ->GetFunction(context).ToLocalChecked();
        }

        //==================================================================
        // Globals
        //==================================================================
        namespace
        {
            const char *kPrelude = R"JS(
(function(g){
  'use strict';
  // ---- vector prototypes ------------------------------------------------
  function V2(x,y){ this.x=x; this.y=y; }
  V2.prototype.length = function(){ return Math.hypot(this.x, this.y); };
  V2.prototype.dot = function(o){ return this.x*o.x + this.y*o.y; };
  V2.prototype.toString = function(){ return 'vec2(' + this.x + ', ' + this.y + ')'; };
  function V3(x,y,z){ this.x=x; this.y=y; this.z=z; }
  V3.prototype.length = function(){ return Math.hypot(this.x, this.y, this.z); };
  V3.prototype.normalized = function(){ var l=this.length(); return l<1e-8 ? vec3(0,0,0) : vec3(this.x/l,this.y/l,this.z/l); };
  V3.prototype.dot = function(o){ return this.x*o.x + this.y*o.y + this.z*o.z; };
  V3.prototype.cross = function(o){ return vec3(this.y*o.z-this.z*o.y, this.z*o.x-this.x*o.z, this.x*o.y-this.y*o.x); };
  V3.prototype.toString = function(){ return 'vec3(' + this.x + ', ' + this.y + ', ' + this.z + ')'; };
  function V4(x,y,z,w){ this.x=x; this.y=y; this.z=z; this.w=w; }
  V4.prototype.toString = function(){ return 'vec4(' + this.x + ', ' + this.y + ', ' + this.z + ', ' + this.w + ')'; };
  function C(r,g,b,a){ this.r=r; this.g=g; this.b=b; this.a=a; }
  C.prototype.lerp = function(o,t){ return color(this.r+(o.r-this.r)*t, this.g+(o.g-this.g)*t, this.b+(o.b-this.b)*t, this.a+(o.a-this.a)*t); };
  C.prototype.toString = function(){ return 'color(' + this.r + ', ' + this.g + ', ' + this.b + ', ' + this.a + ')'; };
  function Q(x,y,z,w){ this.x=x; this.y=y; this.z=z; this.w=w; }
  Q.prototype.toString = function(){ return 'quat(' + this.x + ', ' + this.y + ', ' + this.z + ', ' + this.w + ')'; };
  g.__vec2_proto = V2.prototype;
  g.__vec3_proto = V3.prototype;
  g.__vec4_proto = V4.prototype;
  g.__color_proto = C.prototype;
  g.vec2 = function(x,y){ return new V2(x||0, y===undefined?x||0:y); };
  g.vec3 = function(x,y,z){ if (y===undefined) { y=x; z=x; } return new V3(x||0, y||0, z||0); };
  g.vec4 = function(x,y,z,w){ return new V4(x||0,y||0,z||0,w||0); };
  g.color = function(r,g,b,a){ return new C(r===undefined?1:r, g===undefined?r||1:g, b===undefined?r||1:b, a===undefined?1:a); };
  g.quat = function(x,y,z,w){ var q = new Q(x||0,y||0,z||0,w===undefined?1:w); q.__kind='quat'; return q; };
  g.mat4 = function(){
    var args = Array.prototype.slice.call(arguments);
    if (args.length === 1 && Array.isArray(args[0])) args = args[0];
    var out = args.slice(0,16);
    while (out.length < 16) out.push(out.length % 5 === 0 ? 1 : 0);
    out.__kind = 'mat4';
    return out;
  };
  g.euler = g.__math_euler;
  g.axis_angle = g.__math_axis_angle;
  g.look_rotation = g.__math_look_rotation;
  g.trs = g.__math_trs;
  delete g.__math_euler; delete g.__math_axis_angle; delete g.__math_look_rotation; delete g.__math_trs;

  // ---- legacy genesis facade (capability parity with Lua) ---------------
  g.genesis = {
    log_info: function(m){ print(m); },
    log_warn: function(m){ print('[warn] ' + m); },
    log_error: function(m){ print('[error] ' + m); },
    elapsed: function(){ return elapsed(); },
    frame: function(){ return frame(); },
    set_time_scale: function(v){ set_time_scale(v); },
    entity_create: function(n){ return world.create(n || 'js_entity').id; },
    entity_destroy: function(id){ var e = entity(id); if (e) e.destroy(); },
    entity_find: function(n){ var e = world.find(n); return e ? e.id : null; },
    entity_count: function(){ return world.entity_count; },
    entity_name: function(id){ var e = entity(id); return e ? e.name : null; },
    get_position: function(id){ var e = entity(id); if (!e) return [0,0,0]; var p = e.Transform.position; return [p.x,p.y,p.z]; },
    set_position: function(id,x,y,z){ var e = entity(id); if (e) e.Transform.position = vec3(x,y,z); },
    translate: function(id,x,y,z){ var e = entity(id); if (e) e.Transform.translate(vec3(x,y,z)); },
    get_rotation: function(id){ var e = entity(id); if (!e) return [0,0,0]; var r = e.Transform.euler; return [r.x,r.y,r.z]; },
    set_rotation: function(id,x,y,z){ var e = entity(id); if (e) e.Transform.euler = vec3(x,y,z); },
    rotate: function(id,x,y,z){ var e = entity(id); if (e) e.Transform.rotate(vec3(x,y,z)); },
    get_scale: function(id){ var e = entity(id); if (!e) return [1,1,1]; var s = e.Transform.scale; return [s.x,s.y,s.z]; },
    set_scale: function(id,x,y,z){ var e = entity(id); if (e) e.Transform.scale = vec3(x, y===undefined?x:y, z===undefined?x:z); },
    set_parent: function(c,p){ var ce = entity(c), pe = entity(p); if (ce && pe) ce.parent = pe; },
    input_key: function(n){ return input.key(n); },
    input_key_pressed: function(n){ return input.keyPressed(n); },
    input_mouse: function(b){ return input.button(b||0); },
    input_mouse_pos: function(){ var m = input.mouse; return [m.x, m.y]; },
    input_mouse_delta: function(){ var m = input.mouse_delta; return [m.x, m.y]; },
    input_wheel: function(){ return input.wheel; },
    render_set_exposure: function(v){ settings.exposure = v; },
    render_set_gamma: function(v){ settings.gamma = v; },
    render_set_tonemap: function(v){ settings.tonemap = v; },
    render_set_shading: function(v){ settings.shading = v; },
    render_set_wireframe: function(v){ settings.wireframe = !!v; },
    render_set_skybox: function(v){ settings.skybox = !!v; },
    render_set_ambient: function(r,g,b){ settings.ambient_color = color(r,g,b,1); },
    render_set_ibl: function(v){ settings.ibl = !!v; },
    render_set_env_intensity: function(v){ settings.env_intensity = v; },
    render_set_env_rotation: function(v){ settings.env_rotation = v; },
    render_bake_ibl: function(path){ return renderer.bakeIBL(path || ''); },
    render_draw_line: function(ax,ay,az,bx,by,bz,r,g,b){ renderer.drawLine(vec3(ax,ay,az), vec3(bx,by,bz), color(r===undefined?1:r,g===undefined?1:g,b===undefined?1:b,1)); },
    render_draw_box: function(cx,cy,cz,hx,hy,hz,r,g,b){ renderer.drawBox(vec3(cx,cy,cz), vec3(hx,hy,hz), color(r===undefined?1:r,g===undefined?1:g,b===undefined?1:b,1)); },
    render_stats: function(){ return { draw_calls: renderer.draw_calls, triangles: renderer.triangles, frame: renderer.frame, backend: renderer.backend }; },
    create_camera: function(x,y,z,fov){ var e = world.create('JSCamera'); e.Transform.position = vec3(x||0,y||0,z===undefined?6:z); var c = e.add('Camera'); c.fov = fov || 60; return e.id; },
    create_light: function(x,y,z,type,r,g,b,intensity){ var e = world.create('JSLight'); e.Transform.position = vec3(x||0,y===undefined?4:y,z||0); var l = e.add('Light'); l.type = type===0?'directional':(type===1?'point':'spot'); l.color = color(r===undefined?1:r, g===undefined?1:g, b===undefined?1:b,1); l.intensity = intensity===undefined?4:intensity; return e.id; },
    light_set: function(id,intensity,r,g,b){ var e=entity(id); if(!e) return; var l=e.get('Light'); if(!l) return; if(intensity!==undefined) l.intensity=intensity; if(r!==undefined) l.color=color(r,g,b===undefined?g:b,1); },
    create_environment: function(hdr){ var e=world.create('JSEnvironment'); var env=e.add('Environment'); if(hdr) env.hdr=hdr; env.skybox=true; env.ibl=true; return e.id; },
    has_component: function(id,name){ var e=entity(id); return e ? e.has(name) : false; },
    load_model: function(path,x,y,z,scale){ var mesh=renderer.loadModel(path); if(!mesh) return null; var e=world.create('JSModel'); e.setMesh(mesh, null); e.Transform.position=vec3(x||0,y||0,z||0); e.Transform.scale=vec3(scale===undefined?1:scale); return e.id; },
    model_info: function(id){ var e=entity(id); if(!e) return null; var m=e.get('Mesh'); return m ? { name:m.model, importer:m.importer, meshes:m.mesh_count } : null; },
    material_count: function(){ return renderer.material_count; },
    material_set_color: function(index,r,g,b,a){ var m=renderer.material(index); if(m) m.base_color=color(r,g,b,a===undefined?1:a); },
    material_set_pbr: function(index,metallic,roughness){ var m=renderer.material(index); if(m){ m.metallic=metallic; m.roughness=roughness; } },
    material_set_emissive: function(index,r,g,b,strength){ var m=renderer.material(index); if(m){ m.emissive=color(r,g,b,1); m.emissive_strength=strength===undefined?1:strength; } },
    cmd_clear: function(){ renderer.immediate('overlay').clear(); renderer.immediate('after_transparent').clear(); },
    cmd_fill2d: function(x,y,w,h,r,g,b,a){ renderer.immediate('overlay').drawRect(x,y,w,h,color(r===undefined?1:r,g===undefined?1:g,b===undefined?1:b,a===undefined?1:a)); },
    cmd_line2d: function(x0,y0,x1,y1,r,g,b){ renderer.immediate('overlay').drawLine(x0,y0,x1,y1,color(r===undefined?1:r,g===undefined?1:g,b===undefined?1:b,1),2); },
    cmd_quad3d: function(x,y,z,w,h,r,g,b){ renderer.immediate('after_transparent').drawQuad3D(vec3(x,y,z), vec2(w===undefined?0.2:w,h===undefined?0.2:h), color(r===undefined?1:r,g===undefined?1:g,b===undefined?1:b,1), true); },
    post_effect: function(e){ settings.post_effect = e; },
    create_mesh: function(kind,x,y,z,r,g,b,metal,rough){ var m = renderer.createMaterial('js_' + (kind||'box')); m.base_color = color(r===undefined?0.8:r, g===undefined?0.8:g, b===undefined?0.8:b, 1); m.metallic = metal||0; m.roughness = rough===undefined?0.5:rough; var e = world.create(kind||'box'); e.setMesh(renderer.createMesh(kind||'box', 1), m); e.Transform.position = vec3(x||0,y||0,z||0); return e.id; },
    cmd_clear: function(){ },
    text2d: function(x,y,size,text,r,g,b){ renderer.immediate('overlay').drawText(text, x, y, size||24, color(r===undefined?1:r,g===undefined?1:g,b===undefined?1:b,1)); },
    text3d: function(x,y,z,size,text,r,g,b){ renderer.immediate('after_transparent').drawText3D(text, vec3(x,y,z), size||0.2, color(r===undefined?1:r,g===undefined?1:g,b===undefined?1:b,1)); }
  };
})(globalThis);
)JS";
        } // namespace

        void JSSystem::Impl::installGlobals(v8::Local<v8::Context> context, ECS::Entity script_entity,
                                            const std::string &asset_root)
        {
            v8::Context::Scope scope(context);
            Reflect::EngineContext &ctx = Reflect::context();
            if (ctx.engine)
            {
                setGlobal(context, "engine", toJs(Reflect::refRaw(ctx.engine)));
                setGlobal(context, "physics", toJs(Reflect::refRaw(&ctx.engine->physics())));
            }
            if (ctx.world) setGlobal(context, "world", toJs(Reflect::refRaw(ctx.world)));
            if (ctx.render)
            {
                setGlobal(context, "renderer", toJs(Reflect::refRaw(ctx.render)));
                setGlobal(context, "settings", toJs(Reflect::refRaw(&ctx.render->settings())));
            }
            if (ctx.input) setGlobal(context, "input", toJs(Reflect::refRaw(ctx.input)));
            if (ctx.world && script_entity.valid())
                setGlobal(context, "this", toJs(Reflect::entityValue(ctx.world, script_entity)));

            setGlobal(context, "__asset_root", makeString(isolate, asset_root));
            setGlobal(context, "print", v8::FunctionTemplate::New(isolate, printCallback)
                                            ->GetFunction(context)
                                            .ToLocalChecked());
            setGlobal(context, "asset", v8::FunctionTemplate::New(isolate, assetCallback)->GetFunction(context).ToLocalChecked());
            setGlobal(context, "asset_text", v8::FunctionTemplate::New(isolate, assetTextCallback)
                                                 ->GetFunction(context)
                                                 .ToLocalChecked());
            setGlobal(context, "entity", v8::FunctionTemplate::New(isolate, entityCallback)->GetFunction(context).ToLocalChecked());
            setGlobal(context, "elapsed", v8::FunctionTemplate::New(isolate, elapsedCallback)->GetFunction(context).ToLocalChecked());
            setGlobal(context, "frame", v8::FunctionTemplate::New(isolate, frameCallback)->GetFunction(context).ToLocalChecked());
            setGlobal(context, "set_time_scale", v8::FunctionTemplate::New(isolate, setTimeScaleCallback)
                                                      ->GetFunction(context)
                                                      .ToLocalChecked());

            // Every reflected free function (math helpers) under a private
            // name; the prelude gives them friendly aliases.
            for (const auto &entry : Reflect::Registry::instance().functions())
            {
                v8::Local<v8::FunctionTemplate> templ = v8::FunctionTemplate::New(
                    isolate, freeFunctionCallback,
                    v8::External::New(isolate, const_cast<Reflect::MethodInfo *>(&entry.second)));
                setGlobal(context, ("__math_" + entry.first).c_str(), templ->GetFunction(context).ToLocalChecked());
            }

            // reflect.*
            v8::Local<v8::Object> reflect = v8::Object::New(isolate);
            auto setMethod = [&](v8::Local<v8::Object> target, const char *name,
                                 v8::FunctionCallback callback, v8::Local<v8::Value> data)
            {
                v8::Local<v8::Function> fn =
                    v8::FunctionTemplate::New(isolate, callback, data)->GetFunction(context).ToLocalChecked();
                target->Set(context, makeString(isolate, name), fn).Check();
            };
            setMethod(reflect, "types",
                      [](const v8::FunctionCallbackInfo<v8::Value> &info)
                      {
                          auto types = Reflect::Registry::instance().types();
                          v8::Local<v8::Array> array = v8::Array::New(info.GetIsolate(), (int)types.size());
                          for (uint32_t i = 0; i < types.size(); ++i)
                              array->Set(info.GetIsolate()->GetCurrentContext(), i,
                                         makeString(info.GetIsolate(), types[i]->name))
                                  .Check();
                          info.GetReturnValue().Set(array);
                      },
                      v8::Local<v8::Value>());
            setMethod(reflect, "describe",
                      [](const v8::FunctionCallbackInfo<v8::Value> &info)
                      {
                          v8::Isolate *iso = info.GetIsolate();
                          v8::Local<v8::Context> c = iso->GetCurrentContext();
                          JSSystem::Impl *impl = implOf(iso);
                          if (!impl || info.Length() < 1) return;
                          const Reflect::TypeInfo *type = nullptr;
                          if (info[0]->IsString())
                              type = Reflect::Registry::instance().find(utf8(iso, info[0]));
                          else if (info[0]->IsObject())
                          {
                              JsObject *o = jsObjectOf(info[0].As<v8::Object>());
                              if (o) type = o->ref.type;
                          }
                          if (!type) { info.GetReturnValue().Set(v8::Null(iso)); return; }
                          v8::Local<v8::Object> out = v8::Object::New(iso);
                          out->Set(c, makeString(iso, "name"), makeString(iso, type->name)).Check();
                          out->Set(c, makeString(iso, "description"), makeString(iso, type->description)).Check();
                          out->Set(c, makeString(iso, "constructible"), v8::Boolean::New(iso, type->constructor != nullptr)).Check();
                          v8::Local<v8::Array> props = v8::Array::New(iso);
                          uint32_t pi = 0;
                          for (const Reflect::PropertyInfo *p : type->allProperties())
                          {
                              v8::Local<v8::Object> item = v8::Object::New(iso);
                              item->Set(c, makeString(iso, "name"), makeString(iso, p->name)).Check();
                              item->Set(c, makeString(iso, "type"), makeString(iso, Reflect::valueTypeName(p->type))).Check();
                              item->Set(c, makeString(iso, "readonly"), v8::Boolean::New(iso, p->readOnly())).Check();
                              item->Set(c, makeString(iso, "description"), makeString(iso, p->description)).Check();
                              props->Set(c, pi++, item).Check();
                          }
                          out->Set(c, makeString(iso, "properties"), props).Check();
                          v8::Local<v8::Array> methods = v8::Array::New(iso);
                          uint32_t mi = 0;
                          for (const Reflect::MethodInfo *m : type->allMethods())
                          {
                              v8::Local<v8::Object> item = v8::Object::New(iso);
                              item->Set(c, makeString(iso, "name"), makeString(iso, m->name)).Check();
                              item->Set(c, makeString(iso, "signature"), makeString(iso, m->signature)).Check();
                              item->Set(c, makeString(iso, "description"), makeString(iso, m->description)).Check();
                              methods->Set(c, mi++, item).Check();
                          }
                          out->Set(c, makeString(iso, "methods"), methods).Check();
                          info.GetReturnValue().Set(out);
                      },
                      v8::Local<v8::Value>());
            setMethod(reflect, "typeof",
                      [](const v8::FunctionCallbackInfo<v8::Value> &info)
                      {
                          if (info.Length() >= 1 && info[0]->IsObject())
                          {
                              JsObject *o = jsObjectOf(info[0].As<v8::Object>());
                              info.GetReturnValue().Set(makeString(info.GetIsolate(), o && o->ref.type ? o->ref.type->name : "object"));
                          }
                          else info.GetReturnValue().Set(makeString(info.GetIsolate(), "value"));
                      },
                      v8::Local<v8::Value>());
            setMethod(reflect, "valid",
                      [](const v8::FunctionCallbackInfo<v8::Value> &info)
                      {
                          JsObject *o = info.Length() >= 1 && info[0]->IsObject() ? jsObjectOf(info[0].As<v8::Object>()) : nullptr;
                          info.GetReturnValue().Set(v8::Boolean::New(info.GetIsolate(), o && o->ref.valid()));
                      },
                      v8::Local<v8::Value>());
            setMethod(reflect, "get",
                      [](const v8::FunctionCallbackInfo<v8::Value> &info)
                      {
                          v8::Isolate *iso = info.GetIsolate();
                          JSSystem::Impl *impl = implOf(iso);
                          JsObject *o = info.Length() >= 1 && info[0]->IsObject() ? jsObjectOf(info[0].As<v8::Object>()) : nullptr;
                          if (!impl || !o || info.Length() < 2) { info.GetReturnValue().Set(v8::Undefined(iso)); return; }
                          Reflect::Value out;
                          if (Reflect::getProperty(o->ref, utf8(iso, info[1]), out, nullptr))
                              info.GetReturnValue().Set(impl->toJs(out));
                          else info.GetReturnValue().Set(v8::Undefined(iso));
                      },
                      v8::Local<v8::Value>());
            setMethod(reflect, "set",
                      [](const v8::FunctionCallbackInfo<v8::Value> &info)
                      {
                          v8::Isolate *iso = info.GetIsolate();
                          JSSystem::Impl *impl = implOf(iso);
                          JsObject *o = info.Length() >= 2 && info[0]->IsObject() ? jsObjectOf(info[0].As<v8::Object>()) : nullptr;
                          if (!impl || !o || info.Length() < 3) { info.GetReturnValue().Set(v8::Boolean::New(iso, false)); return; }
                          const bool ok = Reflect::setProperty(o->ref, utf8(iso, info[1]), impl->toReflect(info[2]), nullptr);
                          info.GetReturnValue().Set(v8::Boolean::New(iso, ok));
                      },
                      v8::Local<v8::Value>());
            setMethod(reflect, "call",
                      [](const v8::FunctionCallbackInfo<v8::Value> &info)
                      {
                          v8::Isolate *iso = info.GetIsolate();
                          JSSystem::Impl *impl = implOf(iso);
                          JsObject *o = info.Length() >= 2 && info[0]->IsObject() ? jsObjectOf(info[0].As<v8::Object>()) : nullptr;
                          if (!impl || !o) { info.GetReturnValue().Set(v8::Undefined(iso)); return; }
                          std::vector<Reflect::Value> args;
                          for (int i = 2; i < info.Length(); ++i) args.push_back(impl->toReflect(info[i]));
                          Reflect::Value out;
                          if (Reflect::callMethod(o->ref, utf8(iso, info[1]), args, out, nullptr))
                              info.GetReturnValue().Set(impl->toJs(out));
                          else info.GetReturnValue().Set(v8::Undefined(iso));
                      },
                      v8::Local<v8::Value>());
            setMethod(reflect, "new",
                      [](const v8::FunctionCallbackInfo<v8::Value> &info)
                      {
                          v8::Isolate *iso = info.GetIsolate();
                          JSSystem::Impl *impl = implOf(iso);
                          if (!impl || info.Length() < 1) return;
                          const Reflect::TypeInfo *type = Reflect::Registry::instance().find(utf8(iso, info[0]));
                          if (!type || !type->constructor) { info.GetReturnValue().Set(v8::Null(iso)); return; }
                          std::vector<Reflect::Value> args;
                          for (int i = 1; i < info.Length(); ++i) args.push_back(impl->toReflect(info[i]));
                          info.GetReturnValue().Set(impl->toJs(type->constructor(args)));
                      },
                      v8::Local<v8::Value>());
            setGlobal(context, "reflect", reflect);

            // TypeName.new(...) namespaces.
            for (const Reflect::TypeInfo *type : Reflect::Registry::instance().types())
            {
                if (!type->constructor) continue;
                v8::Local<v8::Value> existing;
                v8::Local<v8::Object> namespace_object = v8::Object::New(isolate);
                if (context->Global()->Get(context, makeString(isolate, type->name)).ToLocal(&existing) && existing->IsObject())
                    namespace_object = existing.As<v8::Object>();
                v8::Local<v8::Function> ctor = v8::FunctionTemplate::New(
                    isolate, constructorCallback, v8::External::New(isolate, const_cast<Reflect::TypeInfo *>(type)))
                                                   ->GetFunction(context)
                                                   .ToLocalChecked();
                namespace_object->Set(context, makeString(isolate, "new"), ctor).Check();
                setGlobal(context, type->name.c_str(), namespace_object);
            }

            v8::Local<v8::Script> prelude;
            if (v8::Script::Compile(context, makeString(isolate, kPrelude)).ToLocal(&prelude))
            {
                v8::TryCatch tc(isolate);
                if (prelude->Run(context).IsEmpty())
                    report("[JS] prelude failed: " + errorMessage(tc));
            }
            else
            {
                report("[JS] prelude does not compile");
            }
        }

        bool JSSystem::Impl::runSource(v8::Local<v8::Context> context, const std::string &source,
                                       const std::string &name)
        {
            v8::Context::Scope scope(context);
            v8::TryCatch try_catch(isolate);
            v8::Local<v8::Script> script;
            v8::Local<v8::String> source_string = makeString(isolate, source);
            v8::ScriptOrigin origin(isolate, makeString(isolate, name));
            if (!v8::Script::Compile(context, source_string, &origin).ToLocal(&script))
            {
                report("[JS] " + name + ": compile error: " + errorMessage(try_catch));
                return false;
            }
            v8::Local<v8::Value> result;
            if (!script->Run(context).ToLocal(&result))
            {
                report("[JS] " + name + ": " + errorMessage(try_catch));
                return false;
            }
            return true;
        }

        bool JSSystem::Impl::callHook(Instance &instance, const char *name, int argc, v8::Local<v8::Value> *argv)
        {
            v8::Global<v8::Function> &fn = std::string(name) == "start" ? instance.start : instance.tick;
            const bool has = std::string(name) == "start" ? instance.has_start : instance.has_tick;
            if (!has || fn.IsEmpty()) return false;
            v8::Local<v8::Context> context = instance.context.Get(isolate);
            v8::Context::Scope scope(context);
            v8::TryCatch try_catch(isolate);
            v8::Local<v8::Value> result;
            if (!fn.Get(isolate)->Call(context, context->Global(), argc, argv).ToLocal(&result))
            {
                report(std::string("[JS] ") + name + "() failed: " + errorMessage(try_catch));
                return false;
            }
            return true;
        }

        void JSSystem::Impl::resetInstance(uint32_t id)
        {
            instances.erase(id);
        }

        void JSSystem::Impl::clearWrappers()
        {
            for (auto &kv : wrappers) delete kv.second->raw;
            wrappers.clear();
        }

        void JSSystem::Impl::clearInstances()
        {
            instances.clear();
        }

        //==================================================================
        // JSSystem
        //==================================================================
        JSSystem::JSSystem() : impl_(std::make_unique<Impl>()) {}
        JSSystem::~JSSystem()
        {
            shutdown();
        }

        bool JSSystem::initialize(ECS::World *world, Render::RenderSystem *render, Platform::InputSystem *input)
        {
            world_ = world;
            render_ = render;
            input_ = input;
            initializeV8();
            if (!g_v8_ready)
            {
                GEN_LOG_WARN("[JS] V8 unavailable - JavaScript disabled");
                return false;
            }

            impl_->self = this;
            impl_->world = world;
            impl_->render = render;
            impl_->input = input;
            impl_->allocator = std::unique_ptr<v8::ArrayBuffer::Allocator>(v8::ArrayBuffer::Allocator::NewDefaultAllocator());
            v8::Isolate::CreateParams params;
            params.array_buffer_allocator = impl_->allocator.get();
            impl_->isolate = v8::Isolate::New(params);
            if (!impl_->isolate) return false;
            impl_->isolate->SetData(0, impl_.get());
            {
                v8::Isolate::Scope isolate_scope(impl_->isolate);
                v8::HandleScope handle_scope(impl_->isolate);
                v8::Local<v8::ObjectTemplate> object_template = v8::ObjectTemplate::New(impl_->isolate);
                object_template->SetInternalFieldCount(1);
                object_template->SetHandler(v8::NamedPropertyHandlerConfiguration(
                    objectGetter, objectSetter, nullptr, nullptr, objectEnumerator, v8::Local<v8::Value>()));
                impl_->object_template.Reset(impl_->isolate, object_template);

                v8::Local<v8::Context> console = impl_->createContext();
                impl_->installGlobals(console, ECS::nullEntity(), "");
                impl_->console_context.Reset(impl_->isolate, console);
            }
            available_ = true;
            GEN_LOG_INFO("[JS] V8 ready (V8 %s), %zu reflected types", v8::V8::GetVersion(),
                         Reflect::Registry::instance().types().size());
            return true;
        }

        void JSSystem::shutdown()
        {
            if (!impl_ || !impl_->isolate) return;
            impl_->clearInstances();
            if (!impl_->console_context.IsEmpty()) impl_->console_context.Reset();
            if (!impl_->object_template.IsEmpty()) impl_->object_template.Reset();
            impl_->clearWrappers();
            impl_->isolate->Dispose();
            impl_->isolate = nullptr;
            impl_->allocator.reset();
            available_ = false;
            world_ = nullptr;
        }

        bool JSSystem::available() const
        {
#if GENESIS_ENABLE_V8
            return available_ && impl_ && impl_->isolate;
#else
            return false;
#endif
        }

        size_t JSSystem::loadedScripts() const
        {
            return impl_ ? impl_->instances.size() : 0;
        }

        void JSSystem::reportError(const std::string &message)
        {
            ++error_count_;
            errors_.push_back(message);
            if (errors_.size() > 100) errors_.erase(errors_.begin(), errors_.begin() + (errors_.size() - 100));
            GEN_LOG_ERROR("[JS] %s", message.c_str());
        }

        bool JSSystem::executeString(const std::string &code, const std::string &name)
        {
            if (!available()) return false;
            v8::Isolate::Scope isolate_scope(impl_->isolate);
            v8::HandleScope handle_scope(impl_->isolate);
            v8::Local<v8::Context> context = impl_->console_context.Get(impl_->isolate);
            v8::Context::Scope context_scope(context);
            Reflect::EngineContext &ctx = Reflect::context();
            if (ctx.engine)
            {
                const std::string &root = ctx.engine->packageRoot();
                if (!root.empty())
                    setGlobal(context, "__asset_root", makeString(impl_->isolate, root));
            }
            return impl_->runSource(context, code, name);
        }

        bool JSSystem::executeInScript(ECS::Entity entity, const std::string &code, const std::string &name)
        {
            if (!available()) return false;
            auto it = impl_->instances.find(entity.id);
            if (it == impl_->instances.end()) return false;
            v8::Isolate::Scope isolate_scope(impl_->isolate);
            v8::HandleScope handle_scope(impl_->isolate);
            v8::Local<v8::Context> context = it->second->context.Get(impl_->isolate);
            return impl_->runSource(context, code, name);
        }

        bool JSSystem::loadComponentScript(ECS::Entity entity, ECS::ScriptComponent &component)
        {
            if (!available()) return false;
            std::string source;
            std::string name;
            if (!component.script_path.empty())
            {
                if (!ResourceIO::readFileText(component.script_path, source))
                {
                    reportError("cannot read JS file: " + component.script_path);
                    return false;
                }
                name = component.script_path;
            }
            else if (!component.inline_code.empty())
            {
                source = component.inline_code;
                name = "inline.js";
            }
            else
            {
                reportError("script component has neither a path nor inline code");
                return false;
            }

            v8::Isolate::Scope isolate_scope(impl_->isolate);
            v8::HandleScope handle_scope(impl_->isolate);
            v8::Local<v8::Context> context = impl_->createContext();
            {
                v8::Context::Scope context_scope(context);
                impl_->installGlobals(context, entity, component.asset_root);
            }
            if (!impl_->runSource(context, source, name))
            {
                return false;
            }

            auto instance = std::make_unique<Impl::Instance>();
            instance->context.Reset(impl_->isolate, context);
            {
                v8::Context::Scope context_scope(context);
                v8::Local<v8::Value> start_value;
                if (context->Global()->Get(context, makeString(impl_->isolate, "start")).ToLocal(&start_value) &&
                    start_value->IsFunction())
                {
                    instance->start.Reset(impl_->isolate, start_value.As<v8::Function>());
                    instance->has_start = true;
                }
                v8::Local<v8::Value> tick_value;
                if (context->Global()->Get(context, makeString(impl_->isolate, "tick")).ToLocal(&tick_value) &&
                    tick_value->IsFunction())
                {
                    instance->tick.Reset(impl_->isolate, tick_value.As<v8::Function>());
                    instance->has_tick = true;
                }
            }

            const uint32_t key = entity.id;
            impl_->instances[key] = std::move(instance);
            component.js_ref = (int64_t)key + 1;
            component.loaded = true;

            Impl::Instance &stored = *impl_->instances[key];
            if (stored.has_start)
            {
                v8::Local<v8::Value> argument = v8::Number::New(impl_->isolate, (double)entity.id);
                impl_->callHook(stored, "start", 1, &argument);
            }
            GEN_LOG_INFO("[JS] loaded script for entity %u (%s)", entity.id, name.c_str());
            return true;
        }

        void JSSystem::unloadComponentScript(ECS::ScriptComponent &component)
        {
            if (!impl_ || component.js_ref < 0) return;
            impl_->resetInstance((uint32_t)(component.js_ref - 1));
            component.js_ref = -1;
            component.loaded = false;
        }

        void JSSystem::update(ECS::World &world, float delta_time)
        {
            if (!available()) return;
            const auto start = std::chrono::steady_clock::now();
            elapsed_ += delta_time * time_scale_;
            ++frame_index_;

            v8::Isolate::Scope isolate_scope(impl_->isolate);
            v8::HandleScope handle_scope(impl_->isolate);

            // Compile scripts that have not been loaded yet.  Only entity
            // handles are stored: loading a script runs user code that can add
            // or remove entities and therefore move the component pools.
            std::vector<ECS::Entity> pending;
            world.each<ECS::ScriptComponent>([&](ECS::Entity e, ECS::ScriptComponent &c)
                                            {
                                                if (c.enabled && !c.loaded && c.language == ECS::ScriptLanguage::JavaScript &&
                                                    (!c.script_path.empty() || !c.inline_code.empty()))
                                                    pending.push_back(e);
                                            });
            for (ECS::Entity e : pending)
            {
                if (ECS::ScriptComponent *c = world.getComponent<ECS::ScriptComponent>(e))
                    loadComponentScript(e, *c);
            }

            // Call tick for every loaded script, snapshotting first because a
            // script may destroy entities.
            std::vector<ECS::Entity> ticking;
            world.each<ECS::ScriptComponent>([&](ECS::Entity e, ECS::ScriptComponent &c)
                                            {
                                                if (c.enabled && c.loaded && c.language == ECS::ScriptLanguage::JavaScript)
                                                    ticking.push_back(e);
                                            });
            const float scaled = delta_time * time_scale_;
            for (ECS::Entity entity : ticking)
            {
                if (!world.isAlive(entity)) continue;
                auto it = impl_->instances.find(entity.id);
                if (it == impl_->instances.end()) continue;
                v8::HandleScope scope(impl_->isolate);
                v8::Local<v8::Value> args[2] = {
                    v8::Number::New(impl_->isolate, (double)scaled),
                    v8::Number::New(impl_->isolate, (double)entity.id)};
                impl_->callHook(*it->second, "tick", 2, args);
            }

            // Drop instances for entities that no longer exist.
            std::unordered_set<uint32_t> alive;
            for (ECS::Entity e : world.entities()) alive.insert(e.id);
            for (auto it = impl_->instances.begin(); it != impl_->instances.end();)
            {
                if (alive.find(it->first) == alive.end()) it = impl_->instances.erase(it);
                else ++it;
            }

            const auto end = std::chrono::steady_clock::now();
            last_tick_ms_ = std::chrono::duration<double, std::milli>(end - start).count();
        }
#else
        struct JSSystem::Impl {};
        JSSystem::JSSystem() : impl_(std::make_unique<Impl>()) {}
        JSSystem::~JSSystem() = default;
        bool JSSystem::initialize(ECS::World *world, Render::RenderSystem *render, Platform::InputSystem *input)
        {
            (void)world; (void)render; (void)input;
            GEN_LOG_WARN("[JS] built without V8 (GENESIS_ENABLE_JS=OFF)");
            return false;
        }
        void JSSystem::shutdown() {}
        bool JSSystem::available() const { return false; }
        size_t JSSystem::loadedScripts() const { return 0; }
        void JSSystem::reportError(const std::string &message) { (void)message; }
        bool JSSystem::executeString(const std::string &code, const std::string &name) { (void)code; (void)name; return false; }
        bool JSSystem::executeInScript(ECS::Entity entity, const std::string &code, const std::string &name)
        {
            (void)entity; (void)code; (void)name; return false;
        }

        bool JSSystem::loadComponentScript(ECS::Entity entity, ECS::ScriptComponent &component)
        {
            (void)entity; (void)component; return false;
        }
        void JSSystem::unloadComponentScript(ECS::ScriptComponent &component) { (void)component; }
        void JSSystem::update(ECS::World &world, float delta_time) { (void)world; (void)delta_time; }
#endif

    } // namespace Script
} // namespace Genesis
