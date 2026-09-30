/*
 * Genesis Render - reflection of the engine types.
 */
#include "reflection/engine_bindings.hpp"
#include "engine/engine.hpp"
#include "foundation_layer/genesis_log.hpp"
#include "platform_layer/input_system.hpp"
#include "renderer/render_system.hpp"
#include "resource/procedural_mesh.hpp"

#include <SDL2/SDL.h>

#include <algorithm>

namespace Genesis
{
    namespace Reflect
    {
        EngineContext &context()
        {
            static EngineContext ctx;
            return ctx;
        }

        //==================================================================
        // Entities / components
        //==================================================================
        Value entityValue(ECS::World *world, ECS::Entity e)
        {
            if (!world || !world->isAlive(e)) return Value();
            auto handle = std::make_shared<EntityHandle>();
            handle->world = world;
            handle->entity = e;
            ObjectRef r;
            r.type = typeOf<EntityHandle>();
            EntityHandle *raw = handle.get();
            r.resolve = [raw]() -> void * { return raw->world && raw->world->isAlive(raw->entity) ? raw : nullptr; };
            r.holder = handle;
            r.identity = ((uint64_t)e.generation << 32) | e.id;
            return Value(r);
        }

        bool entityFromValue(const Value &v, ECS::Entity &out)
        {
            if (const ObjectRef *o = v.asObject())
            {
                if (o->type == typeOf<EntityHandle>() && o->holder)
                {
                    out = static_cast<EntityHandle *>(o->holder.get())->entity;
                    return true;
                }
                return false;
            }
            if (v.isNumber() && context().world)
            {
                out = context().world->entityFromId((ECS::EntityId)v.asInt());
                return out.valid();
            }
            return false;
        }

        namespace
        {
            std::vector<ComponentBinding> &bindingsStorage()
            {
                static std::vector<ComponentBinding> b;
                return b;
            }

            template <typename T>
            Value componentRef(ECS::World *world, ECS::Entity e)
            {
                if (!world || !world->getComponent<T>(e)) return Value();
                ObjectRef r;
                r.type = typeOf<T>();
                r.resolve = [world, e]() -> void * { return world->getComponent<T>(e); };
                r.identity = (((uint64_t)e.generation << 32) | e.id) ^ ((uint64_t)(uintptr_t)r.type << 1);
                return Value(r);
            }

            template <typename T>
            void bindComponent()
            {
                ComponentBinding b;
                b.name = T::kComponentName;
                b.type = typeOf<T>();
                b.has = [](ECS::World &w, ECS::Entity e) { return w.hasComponent<T>(e); };
                b.add = [](ECS::World &w, ECS::Entity e)
                {
                    if (!w.hasComponent<T>(e)) w.addComponent<T>(e, T());
                    return componentRef<T>(&w, e);
                };
                b.get = [](ECS::World &w, ECS::Entity e) { return componentRef<T>(&w, e); };
                b.remove = [](ECS::World &w, ECS::Entity e) { w.removeComponent<T>(e); };
                bindingsStorage().push_back(b);
            }

            Render::RenderSystem *renderer() { return context().render; }

            Math::Vec3 eulerOf(const ECS::TransformComponent &t) { return t.eulerAnglesDegrees(); }

            int materialIndexOf(Render::Material *m)
            {
                if (!m || !renderer()) return -1;
                if (m->owner != &renderer()->scene())
                {
                    // Materials created outside the library are registered on use.
                    for (size_t i = 0; i < renderer()->scene().materialCount(); ++i)
                        if (renderer()->scene().material((int32_t)i) == m) return (int)i;
                    return -1;
                }
                return m->index;
            }

            std::shared_ptr<Render::Material> meshMaterial(ECS::World &w, ECS::Entity e, int slot)
            {
                Render::RenderSystem *rs = renderer();
                ECS::MeshComponent *mc = w.getComponent<ECS::MeshComponent>(e);
                if (!rs || !mc) return nullptr;
                if (slot >= 0 && (size_t)slot < mc->material_overrides.size() && mc->material_overrides[slot] >= 0)
                    return rs->scene().materialPtr(mc->material_overrides[slot]);
                const Render::RenderSystem::EntityMeshBinding *b = rs->binding(e);
                if (!b || slot < 0 || (size_t)slot >= b->mesh_indices.size()) return nullptr;
                Render::GpuMesh *gm = rs->scene().mesh(b->mesh_indices[slot]);
                return gm ? rs->scene().materialPtr(gm->material_index) : nullptr;
            }

            void setMeshMaterial(ECS::World &w, ECS::Entity e, int slot, const std::shared_ptr<Render::Material> &m)
            {
                Render::RenderSystem *rs = renderer();
                ECS::MeshComponent *mc = w.getComponent<ECS::MeshComponent>(e);
                if (!rs || !mc || !m || slot < 0) return;
                int32_t index = rs->scene().addMaterial(m);
                if ((size_t)slot >= mc->material_overrides.size()) mc->material_overrides.resize((size_t)slot + 1, -1);
                mc->material_overrides[(size_t)slot] = index;
            }
        } // namespace

        const std::vector<ComponentBinding> &componentBindings() { return bindingsStorage(); }

        const ComponentBinding *findComponent(const std::string &name)
        {
            for (const ComponentBinding &b : bindingsStorage())
                if (b.name == name) return &b;
            return nullptr;
        }

        //==================================================================
        // Mesh assets
        //==================================================================
        MeshAssetPtr createMeshAsset(const std::string &kind, float size, int detail)
        {
            Render::RenderSystem *rs = renderer();
            if (!rs) return nullptr;
            detail = std::max(4, detail);
            size = size > 0.0f ? size : 1.0f;
            MeshData mesh;
            if (kind == "sphere") mesh = Procedural::makeSphere(size * 0.5f, (uint32_t)detail, (uint32_t)std::max(4, detail / 2));
            else if (kind == "plane") mesh = Procedural::makePlane(size, size, 1);
            else if (kind == "torus") mesh = Procedural::makeTorus(size * 0.5f, size * 0.18f, (uint32_t)detail, (uint32_t)std::max(8, detail / 2));
            else if (kind == "box" || kind == "cube") mesh = Procedural::makeBox(size, size, size);
            else
            {
                GEN_LOG_WARN("[Reflect] unknown mesh kind '%s' (sphere | box | plane | torus)", kind.c_str());
                return nullptr;
            }
            MaterialData mat;
            mat.name = kind + "_material";
            mat.base_color_factor = Math::Color(0.8f, 0.8f, 0.8f, 1.0f);
            mat.metallic_factor = 0.0f;
            mat.roughness_factor = 0.5f;
            auto asset = std::make_shared<MeshAsset>();
            asset->name = kind;
            asset->model = Procedural::wrapMesh(mesh, mat, kind);
            asset->gpu_meshes = rs->scene().uploadModel(*asset->model, kind);
            return asset;
        }

        MeshAssetPtr loadMeshAsset(const std::string &path)
        {
            Render::RenderSystem *rs = renderer();
            if (!rs) return nullptr;
            std::shared_ptr<ModelData> model = ResourceIO::loadModel(path);
            if (!model) return nullptr;
            auto asset = std::make_shared<MeshAsset>();
            asset->name = ResourceIO::pathFilename(path);
            asset->model = model;
            asset->gpu_meshes = rs->scene().uploadModel(*model, asset->name);
            return asset;
        }

        bool attachMesh(ECS::Entity e, const MeshAssetPtr &mesh, const std::shared_ptr<Render::Material> &material)
        {
            EngineContext &ctx = context();
            if (!ctx.world || !ctx.render || !mesh || !ctx.world->isAlive(e)) return false;
            if (!ctx.world->hasComponent<ECS::TransformComponent>(e)) ctx.world->addComponent<ECS::TransformComponent>(e);
            ECS::MeshComponent mc;
            mc.model = mesh->model;
            mc.pick_id = e.id + 1;
            if (material)
            {
                int32_t index = ctx.render->scene().addMaterial(material);
                mc.material_overrides.assign(mesh->model->meshes.size(), index);
            }
            ctx.world->addComponent<ECS::MeshComponent>(e, mc);
            Render::RenderSystem::EntityMeshBinding b;
            b.entity = e;
            b.mesh_indices = mesh->gpu_meshes;
            ctx.render->bindEntityMeshes(b);
            return true;
        }

        Render::RenderTargetId targetFromValue(const Value &v)
        {
            if (v.type() == ValueType::String)
            {
                if (v.asString() == "backbuffer" || v.asString() == "screen") return Render::RenderTargetId::backbuffer();
                return Render::RenderTargetId::camera();
            }
            if (auto rt = Traits<Render::RenderTexturePtr>::from(v)) return Render::RenderTargetId::of(rt);
            return Render::RenderTargetId::camera();
        }

        //==================================================================
        // Registration
        //==================================================================
        namespace
        {
            std::vector<std::string> names(std::initializer_list<const char *> list)
            {
                std::vector<std::string> out;
                for (const char *n : list) out.push_back(n);
                return out;
            }

            Math::Color colorArg(const Value &v, const Math::Color &fallback)
            {
                return v.isNil() ? fallback : v.asColor();
            }
            float floatArg(const Value &v, float fallback) { return v.isNil() ? fallback : (float)v.asFloat(); }

            // A transform argument: mat4, or vec3 position.
            Math::Mat4 transformArg(const Value &v)
            {
                if (v.type() == ValueType::Mat4) return v.asMat4();
                if (v.type() == ValueType::Vec3 || v.type() == ValueType::Vec2) return Math::Mat4::translate(v.asVec3());
                return Math::Mat4::identity();
            }

            Render::PostEffect effectArg(const Value &v)
            {
                Render::PostEffect e = Render::PostEffect::None;
                if (v.type() == ValueType::String) Render::postEffectFromName(v.asString(), e);
                else if (v.isNumber() && v.asInt() >= 0 && v.asInt() < (int64_t)Render::PostEffect::Count) e = (Render::PostEffect)v.asInt();
                return e;
            }

            Render::CameraEvent eventArg(const Value &v)
            {
                Render::CameraEvent e = Render::CameraEvent::AfterPost;
                if (v.type() == ValueType::String) Render::cameraEventFromName(v.asString(), e);
                else if (v.isNumber() && v.asInt() >= 0 && v.asInt() < (int64_t)Render::CameraEvent::Count) e = (Render::CameraEvent)v.asInt();
                return e;
            }

            Text::TextAlign alignArg(const Value &v, Text::TextAlign fallback)
            {
                if (v.type() == ValueType::String)
                {
                    if (v.asString() == "center" || v.asString() == "centre") return Text::TextAlign::Center;
                    if (v.asString() == "right") return Text::TextAlign::Right;
                    if (v.asString() == "left") return Text::TextAlign::Left;
                }
                if (v.isNumber()) return (Text::TextAlign)std::clamp<int64_t>(v.asInt(), 0, 2);
                return fallback;
            }

            bool slotArg(const Value &v, Render::TextureSlot &slot)
            {
                if (v.type() == ValueType::String) return Render::textureSlotFromName(v.asString(), slot);
                if (v.isNumber() && v.asInt() >= 0 && v.asInt() < (int64_t)Render::TextureSlot::Count)
                {
                    slot = (Render::TextureSlot)v.asInt();
                    return true;
                }
                return false;
            }

            SDL_Scancode keyArg(const std::string &name)
            {
                if (name == "ctrl") return SDL_SCANCODE_LCTRL;
                if (name == "shift") return SDL_SCANCODE_LSHIFT;
                if (name == "alt") return SDL_SCANCODE_LALT;
                if (name == "esc") return SDL_SCANCODE_ESCAPE;
                return SDL_GetScancodeFromName(name.c_str());
            }

            void registerMath()
            {
                Registry &r = Registry::instance();
                r.addFunction(makeFunction("vec2", [](float x, float y) { return Math::Vec2(x, y); }, "vec2 constructor"));
                r.addFunction(makeFunction("vec3", [](float x, float y, float z) { return Math::Vec3(x, y, z); }, "vec3 constructor"));
                r.addFunction(makeFunction("vec4", [](float x, float y, float z, float w) { return Math::Vec4(x, y, z, w); }, "vec4 constructor"));
                r.addFunction(makeFunction("color", [](Value rr, Value g, Value b, Value a)
                                           {
                                               return Math::Color((float)rr.asFloat(), (float)g.asFloat(), (float)b.asFloat(),
                                                                  a.isNil() ? 1.0f : (float)a.asFloat());
                                           }, "color(r, g, b, [a=1]) - linear RGBA"));
                r.addFunction(makeFunction("euler", [](float pitch, float yaw, float roll)
                                           { return Math::Quat::fromEulerYXZ(Math::toRadians(yaw), Math::toRadians(pitch), Math::toRadians(roll)); },
                                           "quaternion from pitch / yaw / roll in degrees"));
                r.addFunction(makeFunction("axis_angle", [](Math::Vec3 axis, float degrees)
                                           { return Math::Quat::fromAxisAngle(Math::normalize(axis), Math::toRadians(degrees)); },
                                           "quaternion from an axis and an angle in degrees"));
                r.addFunction(makeFunction("look_rotation", [](Math::Vec3 forward, Value up)
                                           { return Math::Quat::lookRotation(Math::normalize(forward), up.isNil() ? Math::Vec3(0, 1, 0) : up.asVec3()); },
                                           "quaternion looking along `forward`"));
                r.addFunction(makeFunction("trs", [](Math::Vec3 t, Value rot, Value scale)
                                           {
                                               Math::Quat q = rot.isNil() ? Math::Quat::identity() : rot.asQuat();
                                               Math::Vec3 s = scale.isNil() ? Math::Vec3(1.0f) : scale.asVec3();
                                               return Math::Mat4::compose(t, q, s);
                                           }, "mat4 from translation, rotation (quat or euler vec3) and scale"));
            }

            void registerEcs()
            {
                registerClass<EntityHandle>("Entity", "Handle to an ECS entity (validated on every access)")
                    .readonly("id", [](EntityHandle &h) { return (int64_t)h.entity.id; })
                    .property("name", [](EntityHandle &h) { return h.world->entityName(h.entity); },
                              [](EntityHandle &h, std::string n) { h.world->setEntityName(h.entity, n); })
                    .property("parent", [](EntityHandle &h) { return h.world->parentOf(h.entity); },
                              [](EntityHandle &h, ECS::Entity p) { h.world->setParent(h.entity, p); })
                    .readonly("children", [](EntityHandle &h) { return h.world->childrenOf(h.entity); })
                    .method("has", [](EntityHandle &h, std::string comp)
                            {
                                const ComponentBinding *b = findComponent(comp);
                                return b && b->has(*h.world, h.entity);
                            }, "true when the entity has the named component")
                    .method("add", [](EntityHandle &h, std::string comp)
                            {
                                const ComponentBinding *b = findComponent(comp);
                                return b ? b->add(*h.world, h.entity) : Value();
                            }, "adds (or returns) the named component")
                    .method("get", [](EntityHandle &h, std::string comp)
                            {
                                const ComponentBinding *b = findComponent(comp);
                                return b ? b->get(*h.world, h.entity) : Value();
                            }, "returns the named component or nil")
                    .method("remove", [](EntityHandle &h, std::string comp)
                            {
                                if (const ComponentBinding *b = findComponent(comp)) b->remove(*h.world, h.entity);
                            })
                    .method("components", [](EntityHandle &h)
                            {
                                std::vector<std::string> out;
                                for (const ComponentBinding &b : componentBindings())
                                    if (b.has(*h.world, h.entity)) out.push_back(b.name);
                                return out;
                            })
                    .method("destroy", [](EntityHandle &h) { h.world->destroyEntity(h.entity); })
                    .method("setMesh", [](EntityHandle &h, MeshAssetPtr mesh, std::shared_ptr<Render::Material> material)
                            { return attachMesh(h.entity, mesh, material); },
                            "renders `mesh` (MeshAsset) with an optional Material on this entity")
                    .method("setMaterial", [](EntityHandle &h, std::shared_ptr<Render::Material> m, Value slot)
                            {
                                ECS::MeshComponent *mc = h.world->getComponent<ECS::MeshComponent>(h.entity);
                                if (!mc || !mc->model) return;
                                if (slot.isNil())
                                    for (size_t i = 0; i < mc->model->meshes.size(); ++i) setMeshMaterial(*h.world, h.entity, (int)i, m);
                                else setMeshMaterial(*h.world, h.entity, (int)slot.asInt(), m);
                            }, "assigns a Material to every sub mesh, or to `slot`")
                    .method("material", [](EntityHandle &h, Value slot)
                            { return meshMaterial(*h.world, h.entity, slot.isNil() ? 0 : (int)slot.asInt()); })
                    .method("lookAt", [](EntityHandle &h, Math::Vec3 target)
                            {
                                ECS::TransformComponent *t = h.world->getComponent<ECS::TransformComponent>(h.entity);
                                if (!t) return;
                                Math::Vec3 dir = target - t->position;
                                if (Math::lengthSquared(dir) < 1e-10f) return;
                                t->rotation = Math::Quat::lookRotation(Math::normalize(dir), Math::Vec3(0, 1, 0));
                                t->local_dirty = true;
                            }, "rotates the transform so -Z points at `target`")
                    .dynamic([](EntityHandle &h, const std::string &key, Value &out)
                             {
                                 const ComponentBinding *b = findComponent(key);
                                 if (!b) return false;
                                 out = b->get(*h.world, h.entity);
                                 return true;
                             },
                             [](EntityHandle &h)
                             {
                                 std::vector<std::string> out;
                                 for (const ComponentBinding &b : componentBindings())
                                     if (b.has(*h.world, h.entity)) out.push_back(b.name);
                                 return out;
                             });

                registerClass<ECS::World>("World", "The ECS world (entities, components, hierarchy)")
                    .readonly("name", [](ECS::World &w) { return w.name(); })
                    .readonly("entity_count", [](ECS::World &w) { return (int64_t)w.entityCount(); })
                    .method("create", [](ECS::World &w, Value name)
                            {
                                ECS::Entity e = w.createEntity(name.isNil() ? "Entity" : name.asString());
                                w.addComponent<ECS::TransformComponent>(e);
                                return entityValue(&w, e);
                            }, "creates an entity (with a Transform)")
                    .method("find", [](ECS::World &w, std::string name)
                            {
                                ECS::Entity e = w.findEntity(name);
                                return e.valid() ? entityValue(&w, e) : Value();
                            }, "first entity with the given name, or nil")
                    .method("entity", [](ECS::World &w, int64_t id) { return entityValue(&w, w.entityFromId((ECS::EntityId)id)); })
                    .method("destroy", [](ECS::World &w, ECS::Entity e) { w.destroyEntity(e); })
                    .method("entities", [](ECS::World &w)
                            {
                                std::vector<Value> out;
                                for (ECS::Entity e : w.entities()) out.push_back(entityValue(&w, e));
                                return Value::list(out);
                            })
                    .method("each", [](ECS::World &w, std::string comp)
                            {
                                std::vector<Value> out;
                                const ComponentBinding *b = findComponent(comp);
                                if (b)
                                    for (ECS::Entity e : w.entities())
                                        if (b->has(w, e)) out.push_back(entityValue(&w, e));
                                return Value::list(out);
                            }, "entities that have the named component");

                using T = ECS::TransformComponent;
                registerClass<T>("Transform", "Local TRS transform (world matrix maintained by TransformSystem)")
                    .property("position", [](T &t) { return t.position; }, [](T &t, Math::Vec3 v) { t.setPosition(v); })
                    .property("rotation", [](T &t) { return t.rotation; }, [](T &t, Math::Quat q) { t.rotation = q.normalized(); t.local_dirty = true; })
                    .property("euler", eulerOf, [](T &t, Math::Vec3 e) { t.setEulerDegrees(e.x, e.y, e.z); }, "pitch / yaw / roll in degrees")
                    .property("scale", [](T &t) { return t.scale; }, [](T &t, Math::Vec3 v) { t.setScale(v); })
                    .readonly("world_position", [](T &t) { return t.worldPosition(); })
                    .readonly("forward", [](T &t) { return t.forward(); })
                    .readonly("right", [](T &t) { return t.right(); })
                    .readonly("up", [](T &t) { return t.up(); })
                    .readonly("world_matrix", [](T &t) { return t.world_matrix; })
                    .method("translate", [](T &t, Math::Vec3 d) { t.setPosition(t.position + d); })
                    .method("rotate", [](T &t, Math::Vec3 degrees)
                            {
                                Math::Quat q = Math::Quat::fromEulerYXZ(Math::toRadians(degrees.y), Math::toRadians(degrees.x), Math::toRadians(degrees.z));
                                t.rotation = (t.rotation * q).normalized();
                                t.local_dirty = true;
                            }, "applies a local rotation given as pitch / yaw / roll degrees")
                    .method("lookAt", [](T &t, Math::Vec3 target)
                            {
                                Math::Vec3 dir = target - t.position;
                                if (Math::lengthSquared(dir) < 1e-10f) return;
                                t.rotation = Math::Quat::lookRotation(Math::normalize(dir), Math::Vec3(0, 1, 0));
                                t.local_dirty = true;
                            });
                bindComponent<T>();

                using Cam = ECS::CameraComponent;
                registerClass<Cam>("Camera", "Perspective / orthographic camera")
                    .property("fov", &Cam::fov_degrees).range(5.0f, 150.0f)
                    .property("near", &Cam::near_plane).range(0.001f, 10.0f)
                    .property("far", &Cam::far_plane).range(1.0f, 5000.0f)
                    .property("orthographic", &Cam::orthographic)
                    .property("ortho_size", &Cam::ortho_size).range(0.1f, 200.0f)
                    .property("aspect_override", &Cam::aspect_override).range(0.0f, 4.0f)
                    .property("active", &Cam::active)
                    .property("clear_color", &Cam::clear_color);
                bindComponent<Cam>();

                using L = ECS::LightComponent;
                registerClass<L>("Light", "Directional / point / spot light")
                    .enumProperty("type", names({"directional", "point", "spot"}), [](L &l) { return (int)l.type; },
                                  [](L &l, int v) { l.type = (ECS::LightType)v; })
                    .property("color", &L::color)
                    .property("intensity", &L::intensity).range(0.0f, 100.0f)
                    .property("range", &L::range).range(0.1f, 200.0f)
                    .property("inner_angle", &L::spot_inner_degrees).range(1.0f, 89.0f)
                    .property("outer_angle", &L::spot_outer_degrees).range(1.0f, 90.0f)
                    .property("active", &L::active);
                bindComponent<L>();

                using M = ECS::MeshComponent;
                registerClass<M>("Mesh", "Renderable model reference")
                    .property("visible", &M::visible)
                    .readonly("model", [](M &m) { return m.model ? m.model->name : std::string(); })
                    .readonly("importer", [](M &m) { return m.model ? m.model->importer : std::string(); })
                    .readonly("mesh_count", [](M &m) { return (int64_t)(m.model ? m.model->meshes.size() : 0); });
                bindComponent<M>();

                using Env = ECS::EnvironmentComponent;
                registerClass<Env>("Environment", "Skybox + image based lighting source")
                    .property("hdr", &Env::hdr_equirect_path, "equirectangular .hdr (empty = procedural sky)")
                    .property("intensity", &Env::env_intensity).range(0.0f, 8.0f)
                    .property("rotation", &Env::env_rotation_degrees).range(0.0f, 360.0f)
                    .property("skybox", &Env::show_skybox)
                    .property("ibl", &Env::use_ibl)
                    .property("cubemap_size", &Env::cubemap_size)
                    .method("rebake", [](Env &e) { e.ibl_dirty = true; }, "re-bakes the IBL cubemaps next frame");
                bindComponent<Env>();

                using Tx = ECS::TextComponent;
                registerClass<Tx>("Text", "SDF text label (world space or screen space)")
                    .property("text", &Tx::text)
                    .property("size", &Tx::size).range(0.01f, 200.0f)
                    .property("color", &Tx::color)
                    .property("outline", &Tx::outline).range(0.0f, 0.4f)
                    .property("outline_color", &Tx::outline_color)
                    .property("offset", &Tx::offset)
                    .enumProperty("align", names({"left", "center", "right"}), [](Tx &t) { return (int)t.align; },
                                  [](Tx &t, int v) { t.align = (uint32_t)v; })
                    .property("billboard", &Tx::billboard)
                    .property("screen_space", &Tx::screen_space)
                    .property("depth_test", &Tx::depth_test)
                    .property("visible", &Tx::visible);
                bindComponent<Tx>();

                using Sc = ECS::ScriptComponent;
                registerClass<Sc>("Script", "Lua script attached to the entity")
                    .property("path", &Sc::script_path)
                    .property("code", &Sc::inline_code)
                    .property("enabled", &Sc::enabled)
                    .readonly("loaded", [](Sc &s) { return s.loaded; });
                bindComponent<Sc>();

                using CC = ECS::CameraControllerComponent;
                registerClass<CC>("CameraController", "Orbit / fly camera controller")
                    .enumProperty("mode", names({"orbit", "fly", "none"}), [](CC &c) { return (int)c.mode; },
                                  [](CC &c, int v) { c.mode = (CC::Mode)v; })
                    .property("target", &CC::orbit_target)
                    .property("distance", &CC::orbit_distance).range(0.2f, 200.0f)
                    .property("yaw", &CC::orbit_yaw_degrees).range(-360.0f, 360.0f)
                    .property("pitch", &CC::orbit_pitch_degrees).range(-89.0f, 89.0f)
                    .property("move_speed", &CC::move_speed).range(0.1f, 50.0f);
                bindComponent<CC>();

                using Tag = ECS::TagComponent;
                registerClass<Tag>("Tag", "Free-form tag / layer")
                    .property("tag", &Tag::tag)
                    .property("layer", &Tag::layer);
                bindComponent<Tag>();
            }

            void registerRendering()
            {
                using Mat = Render::Material;
                registerClass<Mat>("Material", "Editable surface: PBR parameters, textures and render state")
                    .property("name", &Mat::name)
                    .readonly("index", [](Mat &m) { return (int64_t)m.index; })
                    .property("base_color", [](Mat &m) { return m.base_color; }, [](Mat &m, Math::Color c) { m.base_color = c; m.touch(); })
                    .property("metallic", [](Mat &m) { return m.metallic; }, [](Mat &m, float v) { m.metallic = v; m.touch(); }).range(0.0f, 1.0f)
                    .property("roughness", [](Mat &m) { return m.roughness; }, [](Mat &m, float v) { m.roughness = v; m.touch(); }).range(0.0f, 1.0f)
                    .property("emissive", [](Mat &m) { return m.emissive; }, [](Mat &m, Math::Color c) { m.emissive = c; m.touch(); })
                    .property("emissive_strength", [](Mat &m) { return m.emissive_strength; },
                              [](Mat &m, float v) { m.emissive_strength = v; m.touch(); }).range(0.0f, 50.0f)
                    .property("normal_scale", [](Mat &m) { return m.normal_scale; }, [](Mat &m, float v) { m.normal_scale = v; m.touch(); }).range(0.0f, 2.0f)
                    .property("occlusion_strength", [](Mat &m) { return m.occlusion_strength; },
                              [](Mat &m, float v) { m.occlusion_strength = v; m.touch(); }).range(0.0f, 1.0f)
                    .property("alpha_cutoff", [](Mat &m) { return m.alpha_cutoff; }, [](Mat &m, float v) { m.alpha_cutoff = v; m.touch(); }).range(0.0f, 1.0f)
                    .property("uv_scale", [](Mat &m) { return m.uv_scale; }, [](Mat &m, Math::Vec2 v) { m.uv_scale = v; m.touch(); })
                    .property("uv_offset", [](Mat &m) { return m.uv_offset; }, [](Mat &m, Math::Vec2 v) { m.uv_offset = v; m.touch(); })
                    .enumProperty("shading", names({"default", "cook_torrance", "blinn_phong", "unlit"}), [](Mat &m) { return (int)m.shading; },
                                  [](Mat &m, int v) { m.shading = (Render::MaterialShading)v; m.touch(); })
                    .enumProperty("blend", names({"opaque", "masked", "transparent", "additive"}), [](Mat &m) { return (int)m.blend; },
                                  [](Mat &m, int v) { m.blend = (Render::MaterialBlend)v; m.touch(); })
                    .enumProperty("cull", names({"back", "front", "none"}), [](Mat &m) { return (int)m.cull; },
                                  [](Mat &m, int v) { m.cull = (Render::MaterialCull)v; m.touch(); })
                    .property("double_sided", [](Mat &m) { return m.double_sided; }, [](Mat &m, bool v) { m.double_sided = v; m.touch(); })
                    .property("depth_test", [](Mat &m) { return m.depth_test; }, [](Mat &m, bool v) { m.depth_test = v; m.touch(); })
                    .property("depth_write", [](Mat &m) { return m.depth_write; }, [](Mat &m, bool v) { m.depth_write = v; m.touch(); })
                    .method("setTexture", [](Mat &m, Value slot, Value source)
                            {
                                Render::TextureSlot s;
                                if (!slotArg(slot, s)) return false;
                                if (auto rt = Traits<Render::RenderTexturePtr>::from(source))
                                {
                                    m.setRenderTexture(s, rt);
                                    return true;
                                }
                                return m.setTexture(s, source.isNil() ? std::string() : source.asString());
                            }, "slot: base_color | metallic_roughness | normal | occlusion | emissive; source: file path or RenderTexture")
                    .method("clearTexture", [](Mat &m, Value slot)
                            {
                                Render::TextureSlot s;
                                if (slotArg(slot, s)) m.clearTexture(s);
                            })
                    .method("enableTexture", [](Mat &m, Value slot, bool on)
                            {
                                Render::TextureSlot s;
                                if (slotArg(slot, s)) m.setTextureEnabled(s, on);
                            })
                    .method("texture", [](Mat &m, Value slot)
                            {
                                Render::TextureSlot s;
                                return slotArg(slot, s) ? m.textureSource(s) : std::string();
                            }, "source of the texture in `slot`")
                    .method("clone", [](Mat &m, Value name)
                            {
                                Render::RenderSystem *rs = renderer();
                                std::string n = name.isNil() ? m.name + "_copy" : name.asString();
                                return rs ? rs->scene().cloneMaterial(m, n) : nullptr;
                            }, "independent copy registered in the material library")
                    .constructor([](Value name)
                                 {
                                     Render::RenderSystem *rs = renderer();
                                     return rs ? rs->scene().createMaterial(name.isNil() ? "material" : name.asString()) : nullptr;
                                 }, "(string name)");

                using RT = Render::RenderTexture;
                registerClass<RT>("RenderTexture", "Offscreen colour (+depth) target, samplable by materials / blits / UI")
                    .readonly("name", [](RT &t) { return t.name(); })
                    .readonly("width", [](RT &t) { return (int64_t)t.width(); })
                    .readonly("height", [](RT &t) { return (int64_t)t.height(); })
                    .readonly("hdr", [](RT &t) { return t.hdr(); })
                    .readonly("has_depth", [](RT &t) { return t.hasDepth(); })
                    .method("resize", [](RT &t, int64_t w, int64_t h) { return t.resize((uint32_t)std::max<int64_t>(1, w), (uint32_t)std::max<int64_t>(1, h)); })
                    .constructor([](Value name, Value w, Value h, Value hdr, Value depth)
                                 {
                                     Render::RenderSystem *rs = renderer();
                                     if (!rs) return Render::RenderTexturePtr();
                                     return rs->createRenderTexture(name.isNil() ? "render_texture" : name.asString(),
                                                                    (uint32_t)std::max<int64_t>(1, w.isNil() ? 256 : w.asInt()),
                                                                    (uint32_t)std::max<int64_t>(1, h.isNil() ? 256 : h.asInt()),
                                                                    hdr.isNil() ? false : hdr.asBool(), depth.isNil() ? true : depth.asBool());
                                 }, "(string name, int width, int height, [bool hdr=false], [bool depth=true])");

                registerClass<MeshAsset>("MeshAsset", "GPU mesh (procedural or loaded model) usable by entities and command buffers")
                    .readonly("name", [](MeshAsset &m) { return m.name; })
                    .readonly("sub_meshes", [](MeshAsset &m) { return (int64_t)m.gpu_meshes.size(); })
                    .readonly("bounds_min", [](MeshAsset &m) { return m.model ? m.model->bounds.minPoint : Math::Vec3(0.0f); })
                    .readonly("bounds_max", [](MeshAsset &m) { return m.model ? m.model->bounds.maxPoint : Math::Vec3(0.0f); })
                    .method("material", [](MeshAsset &m, Value sub)
                            {
                                Render::RenderSystem *rs = renderer();
                                size_t i = sub.isNil() ? 0 : (size_t)sub.asInt();
                                if (!rs || i >= m.gpu_meshes.size()) return Render::MaterialPtr();
                                Render::GpuMesh *gm = rs->scene().mesh(m.gpu_meshes[i]);
                                return gm ? rs->scene().materialPtr(gm->material_index) : nullptr;
                            }, "the material imported / created with sub mesh `i`");

                using CB = Render::CommandBuffer;
                registerClass<CB>("CommandBuffer", "Recorded rendering commands (organise now, execute on the GPU later)")
                    .property("name", [](CB &c) { return c.name(); }, [](CB &c, std::string n) { c.setName(n); })
                    .readonly("count", [](CB &c) { return (int64_t)c.size(); })
                    .method("clear", [](CB &c) { c.clear(); }, "removes every recorded command")
                    .method("setRenderTarget", [](CB &c, Value target) { c.setRenderTarget(targetFromValue(target)); },
                            "RenderTexture | \"camera\" | \"backbuffer\"")
                    .method("clearRenderTarget", [](CB &c, Value color, Value depth)
                            {
                                c.clearRenderTarget(!color.isNil() && !(color.type() == ValueType::Bool && !color.asBool()),
                                                    colorArg(color, Math::Color(0, 0, 0, 1)), depth.isNil() ? true : depth.asBool());
                            }, "(color or false, [bool depth=true])")
                    .method("setViewport", [](CB &c, float x, float y, float w, float h) { c.setViewport(x, y, w, h); }, "pixels, top-left origin")
                    .method("resetViewport", [](CB &c) { c.resetViewport(); })
                    .method("setCamera", [](CB &c, Math::Vec3 eye, Math::Vec3 target, Value fov, Value n, Value f)
                            { c.setCamera(eye, target, floatArg(fov, 60.0f), floatArg(n, 0.05f), floatArg(f, 500.0f)); },
                            "(vec3 eye, vec3 target, [fov=60], [near], [far])")
                    .method("setViewProjection", [](CB &c, Math::Mat4 v, Math::Mat4 p) { c.setViewProjection(v, p); })
                    .method("setCameraEntity", [](CB &c, ECS::Entity e) { c.setCameraEntity(e); })
                    .method("resetCamera", [](CB &c) { c.resetCamera(); })
                    .method("setDepthTest", [](CB &c, bool on) { c.setDepthTest3D(on); }, "depth test of 3D primitives / text")
                    .method("drawMesh", [](CB &c, MeshAssetPtr mesh, Value transform, std::shared_ptr<Render::Material> material)
                            {
                                if (!mesh) return;
                                Math::Mat4 m = transformArg(transform);
                                for (uint32_t gm : mesh->gpu_meshes) c.drawMesh(gm, m, material);
                            }, "(MeshAsset, mat4 or vec3 position, [Material])")
                    .method("drawEntity", [](CB &c, ECS::Entity e) { c.drawEntity(e); })
                    .method("drawScene", [](CB &c) { c.drawScene(); }, "every visible mesh entity, culled for the current camera")
                    .method("drawSkybox", [](CB &c) { c.drawSkybox(); })
                    .method("blit", [](CB &c, Value src, Value dst, Value effect, Value params, Value tint)
                            {
                                c.blit(targetFromValue(src), targetFromValue(dst), effectArg(effect),
                                       params.isNil() ? Math::Vec4(1.0f, 2.0f, 0.0f, 0.0f) : params.asVec4(),
                                       colorArg(tint, Math::Color::white()));
                            }, "(src, dst, [effect name], [vec4 params: strength, radius, ...], [tint color])")
                    .method("drawRect", [](CB &c, float x, float y, float w, float h, Value color)
                            { c.drawRect(x, y, w, h, colorArg(color, Math::Color::white())); })
                    .method("drawRectOutline", [](CB &c, float x, float y, float w, float h, Value color, Value thickness)
                            { c.drawRectOutline(x, y, w, h, colorArg(color, Math::Color::white()), floatArg(thickness, 1.0f)); })
                    .method("drawLine", [](CB &c, float x0, float y0, float x1, float y1, Value color, Value thickness)
                            { c.drawLine2D(Math::Vec2(x0, y0), Math::Vec2(x1, y1), colorArg(color, Math::Color::white()), floatArg(thickness, 1.0f)); })
                    .method("drawImage", [](CB &c, Render::RenderTexturePtr rt, float x, float y, float w, float h, Value tint)
                            { c.drawImage(rt, x, y, w, h, colorArg(tint, Math::Color::white())); })
                    .method("drawText", [](CB &c, std::string text, float x, float y, Value size, Value color, Value align, Value outline, Value outline_color)
                            {
                                c.drawText(text, x, y, floatArg(size, 24.0f), colorArg(color, Math::Color::white()),
                                           alignArg(align, Text::TextAlign::Left), floatArg(outline, 0.0f),
                                           colorArg(outline_color, Math::Color::black()));
                            }, "(text, x, y, [size px], [color], [align], [outline 0..0.4], [outline color])")
                    .method("drawLine3D", [](CB &c, Math::Vec3 a, Math::Vec3 b, Value color, Value thickness)
                            { c.drawLine3D(a, b, colorArg(color, Math::Color::white()), floatArg(thickness, 0.02f)); })
                    .method("drawBox3D", [](CB &c, Math::Vec3 center, Math::Vec3 size, Value color, Value thickness)
                            {
                                Math::AABB box;
                                box.minPoint = center - size * 0.5f;
                                box.maxPoint = center + size * 0.5f;
                                box.valid = true;
                                c.drawBox3D(box, colorArg(color, Math::Color::white()), floatArg(thickness, 0.02f));
                            })
                    .method("drawQuad3D", [](CB &c, Value where, Math::Vec2 size, Value color, Value billboard)
                            {
                                if (where.type() == ValueType::Mat4) c.drawQuad3D(where.asMat4(), size, colorArg(color, Math::Color::white()));
                                else c.drawQuad3D(where.asVec3(), size, colorArg(color, Math::Color::white()), billboard.isNil() ? true : billboard.asBool());
                            }, "(vec3 center or mat4, vec2 size, [color], [billboard=true])")
                    .method("drawText3D", [](CB &c, std::string text, Value where, Value size, Value color, Value billboard, Value outline)
                            {
                                if (where.type() == ValueType::Mat4)
                                    c.drawText3D(text, where.asMat4(), floatArg(size, 0.3f), colorArg(color, Math::Color::white()),
                                                 Text::TextAlign::Center, floatArg(outline, 0.0f));
                                else
                                    c.drawText3D(text, where.asVec3(), floatArg(size, 0.3f), colorArg(color, Math::Color::white()),
                                                 billboard.isNil() ? true : billboard.asBool(), Text::TextAlign::Center, floatArg(outline, 0.0f));
                            }, "(text, vec3 position or mat4, [size], [color], [billboard=true], [outline])")
                    .constructor([](Value name) { return std::make_shared<CB>(name.isNil() ? "command_buffer" : name.asString()); },
                                 "(string name)");

                using S = Render::RenderSettings;
                registerClass<S>("RenderSettings", "Renderer configuration (live)")
                    .enumProperty("shading", names({"cook_torrance", "blinn_phong", "unlit"}), [](S &s) { return (int)s.shading_mode; },
                                  [](S &s, int v) { s.shading_mode = (Render::ShadingMode)v; })
                    .enumProperty("tonemap", names({"none", "reinhard", "aces"}), [](S &s) { return (int)s.tonemap_mode; },
                                  [](S &s, int v) { s.tonemap_mode = (Render::TonemapMode)v; })
                    .property("exposure", &S::exposure).range(0.05f, 8.0f)
                    .property("gamma", &S::gamma).range(1.0f, 3.0f)
                    .property("saturation", &S::saturation).range(0.0f, 2.0f)
                    .property("vignette", &S::vignette).range(0.0f, 1.0f)
                    .enumProperty("post_effect", names({"none", "invert", "grayscale", "sepia", "blur", "edge", "pixelate", "vignette",
                                                        "scanline", "chromatic", "tint", "posterize"}),
                                  [](S &s) { return (int)s.post_effect; }, [](S &s, int v) { s.post_effect = (Render::PostEffect)v; })
                    .property("post_params", &S::post_params)
                    .property("post_tint", &S::post_tint)
                    .property("ambient_color", &S::ambient_color)
                    .property("ambient_intensity", &S::ambient_intensity).range(0.0f, 4.0f)
                    .property("env_intensity", &S::env_intensity).range(0.0f, 8.0f)
                    .property("env_rotation", &S::env_rotation_degrees).range(0.0f, 360.0f)
                    .property("ibl", &S::use_ibl)
                    .property("skybox", &S::show_skybox)
                    .property("wireframe", &S::wireframe)
                    .property("frustum_culling", &S::frustum_culling)
                    .property("text_components", &S::draw_text_components)
                    .enumProperty("pipeline", names({"forward", "custom"}), [](S &s) { return (int)s.pipeline_mode; },
                                  [](S &s, int v) { s.pipeline_mode = (Render::PipelineMode)v; });

                using RS = Render::RenderSystem;
                registerClass<RS>("Renderer", "The render system: materials, meshes, render textures, command buffers")
                    .readonly("settings", [](RS &r) { return &r.settings(); })
                    .readonly("backend", [](RS &r) { return std::string(RHI::backendTypeName(r.device()->backend())); })
                    .readonly("width", [](RS &r) { return (int64_t)(r.hdrTarget() ? r.hdrTarget()->width() : 0); })
                    .readonly("height", [](RS &r) { return (int64_t)(r.hdrTarget() ? r.hdrTarget()->height() : 0); })
                    .readonly("ibl_ready", [](RS &r) { return r.iblReady(); })
                    .readonly("material_count", [](RS &r) { return (int64_t)r.scene().materialCount(); })
                    .readonly("draw_calls", [](RS &r) { return (int64_t)r.stats().draw_calls; })
                    .readonly("triangles", [](RS &r) { return (int64_t)r.stats().triangles; })
                    .readonly("frame", [](RS &r) { return (int64_t)r.stats().frame_index; })
                    .method("createMaterial", [](RS &r, Value name, Value shading)
                            {
                                auto m = r.scene().createMaterial(name.isNil() ? "material" : name.asString());
                                if (shading.type() == ValueType::String)
                                {
                                    const std::string s = shading.asString();
                                    if (s == "unlit") m->shading = Render::MaterialShading::Unlit;
                                    else if (s == "blinn_phong") m->shading = Render::MaterialShading::BlinnPhong;
                                    else if (s == "cook_torrance") m->shading = Render::MaterialShading::CookTorrance;
                                }
                                return m;
                            }, "(name, [shading: default | cook_torrance | blinn_phong | unlit])")
                    .method("findMaterial", [](RS &r, std::string name) { return r.scene().findMaterial(name); })
                    .method("material", [](RS &r, int64_t index) { return r.scene().materialPtr((int32_t)index); })
                    .method("materials", [](RS &r)
                            {
                                std::vector<Render::MaterialPtr> out;
                                for (size_t i = 0; i < r.scene().materialCount(); ++i) out.push_back(r.scene().materialPtr((int32_t)i));
                                return out;
                            })
                    .method("createMesh", [](RS &, std::string kind, Value size, Value detail)
                            { return createMeshAsset(kind, floatArg(size, 1.0f), detail.isNil() ? 32 : (int)detail.asInt()); },
                            "(sphere | box | plane | torus, [size], [detail])")
                    .method("loadModel", [](RS &, std::string path) { return loadMeshAsset(path); }, "glTF / GLB / FBX / OBJ file")
                    .method("createRenderTexture", [](RS &r, std::string name, int64_t w, int64_t h, Value hdr, Value depth)
                            {
                                return r.createRenderTexture(name, (uint32_t)std::max<int64_t>(1, w), (uint32_t)std::max<int64_t>(1, h),
                                                             hdr.isNil() ? false : hdr.asBool(), depth.isNil() ? true : depth.asBool());
                            })
                    .method("createCommandBuffer", [](RS &, Value name)
                            { return std::make_shared<Render::CommandBuffer>(name.isNil() ? "command_buffer" : name.asString()); })
                    .method("addCommandBuffer", [](RS &r, Value event, Render::CommandBufferPtr cb) { r.addCommandBuffer(eventArg(event), cb); },
                            "(event: before_scene | after_opaque | after_transparent | before_post | after_post | overlay, CommandBuffer)")
                    .method("removeCommandBuffer", [](RS &r, Value event, Render::CommandBufferPtr cb) { r.removeCommandBuffer(eventArg(event), cb); })
                    .method("addCustomCommandBuffer", [](RS &r, Render::CommandBufferPtr cb) { r.addCustomCommandBuffer(cb); },
                            "builds the frame in pipeline = \"custom\" mode")
                    .method("removeCustomCommandBuffer", [](RS &r, Render::CommandBufferPtr cb) { r.removeCustomCommandBuffer(cb); })
                    .method("clearCommandBuffers", [](RS &r) { r.clearCommandBuffers(); })
                    .method("execute", [](RS &r, Render::CommandBufferPtr cb) { r.executeCommandBuffer(cb); }, "runs once on the next frame")
                    .method("immediate", [](RS &r, Value event) { return &r.immediate(eventArg(event)); },
                            "per-frame command buffer of `event` (cleared after every frame)")
                    .method("bakeIBL", [](RS &r, Value path)
                            {
                                return path.isNil() || path.asString().empty() ? r.bakeProceduralIBL({}) : r.bakeIBL(path.asString(), {});
                            })
                    .method("drawLine", [](RS &r, Math::Vec3 a, Math::Vec3 b, Value color) { r.drawLine(a, b, colorArg(color, Math::Color::white())); },
                            "debug line for the current frame (cleared after each frame)")
                    .method("clearDebugLines", [](RS &r) { r.clearDebugLines(); });
            }

            void registerEngine()
            {
                registerClass<Engine>("Engine", "Engine facade (frame loop, window, subsystems)")
                    .readonly("fps", [](Engine &e) { return e.fps(); })
                    .readonly("frame_time_ms", [](Engine &e) { return e.frameTimeMs(); })
                    .readonly("frame", [](Engine &e) { return (int64_t)e.frameCount(); })
                    .readonly("time", [](Engine &e) { return e.uptimeSeconds(); })
                    .readonly("backend", [](Engine &e) { return std::string(RHI::backendTypeName(e.device()->backend())); })
                    .readonly("device", [](Engine &e) { return e.device()->info().device_name; })
                    .readonly("world", [](Engine &e) { return &e.world(); })
                    .readonly("renderer", [](Engine &e) { return &e.render(); })
                    .method("quit", [](Engine &e) { e.requestQuit(); })
                    .method("screenshot", [](Engine &e, std::string path) { return e.saveScreenshot(path); })
                    .method("spawnModel", [](Engine &e, std::string path, Value pos, Value scale, Value name)
                            {
                                ECS::Entity ent = e.spawnModel(path, pos.isNil() ? Math::Vec3(0.0f) : pos.asVec3(), floatArg(scale, 1.0f),
                                                               name.isNil() ? "" : name.asString());
                                return entityValue(&e.world(), ent);
                            }, "(path, [vec3 position], [scale], [name]) -> Entity");

                using In = Platform::InputSystem;
                registerClass<In>("Input", "Keyboard / mouse state of the current frame")
                    .readonly("mouse", [](In &i) { return i.mousePosition(); }, "window points")
                    .readonly("mouse_delta", [](In &i) { return i.mouseDelta(); })
                    .readonly("wheel", [](In &i) { return i.wheelDelta(); })
                    .readonly("ui_wants_mouse", [](In &i) { return i.ui_wants_mouse; })
                    .method("key", [](In &i, std::string name) { return i.keyDown(keyArg(name)); }, "true while the key is held (SDL key names)")
                    .method("keyPressed", [](In &i, std::string name) { return i.keyPressed(keyArg(name)); }, "true on the frame the key went down")
                    .method("button", [](In &i, int64_t button) { return i.mouseDown((int)button); }, "0 left, 1 middle, 2 right")
                    .method("mousePressed", [](In &i, int64_t button) { return i.mousePressed((int)button); });
            }
        } // namespace

        void registerEngineTypes()
        {
            static bool done = false;
            if (done) return;
            done = true;
            registerMath();
            registerEcs();
            registerRendering();
            registerEngine();
        }

    } // namespace Reflect
} // namespace Genesis
