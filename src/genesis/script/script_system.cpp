/*
 * Genesis Render - Lua scripting system implementation
 */
#include "script/script_system.hpp"
#include "foundation_layer/genesis_log.hpp"
#include "resource/procedural_mesh.hpp"
#include "resource/resource_types.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

namespace Genesis
{
    namespace Script
    {
        //==================================================================
        // Engine context exposed to the C closures
        //==================================================================
        namespace
        {
            const char *const kContextKey = "genesis.engine_context";

            struct LuaContext
            {
                ScriptSystem *self = nullptr;
                ECS::World *world = nullptr;
                Render::RenderSystem *render = nullptr;
                Platform::InputSystem *input = nullptr;
            };

            LuaContext *context(lua_State *L)
            {
                lua_getfield(L, LUA_REGISTRYINDEX, kContextKey);
                LuaContext *ctx = static_cast<LuaContext *>(lua_touserdata(L, -1));
                lua_pop(L, 1);
                return ctx;
            }

            ECS::Entity toEntity(LuaContext *ctx, lua_Integer id)
            {
                if (!ctx || !ctx->world) return ECS::nullEntity();
                if (id < 0) return ECS::nullEntity();
                return ctx->world->entityFromId((ECS::EntityId)id);
            }

            int pushError(lua_State *L, const char *msg)
            {
                // luaL_error longjmps, so this never actually returns.
                return luaL_error(L, "%s", msg);
            }

            //------------------------------------------------------------------
            // Logging
            //------------------------------------------------------------------
            int lua_log_info(lua_State *L)
            {
                const char *msg = luaL_checkstring(L, 1);
                GEN_LOG_INFO("[lua] %s", msg);
                return 0;
            }
            int lua_log_warn(lua_State *L)
            {
                const char *msg = luaL_checkstring(L, 1);
                GEN_LOG_WARN("[lua] %s", msg);
                return 0;
            }
            int lua_log_error(lua_State *L)
            {
                const char *msg = luaL_checkstring(L, 1);
                GEN_LOG_ERROR("[lua] %s", msg);
                return 0;
            }

            //------------------------------------------------------------------
            // Time
            //------------------------------------------------------------------
            int lua_elapsed(lua_State *L)
            {
                LuaContext *ctx = context(L);
                lua_pushnumber(L, ctx && ctx->self ? ctx->self->elapsed() : 0.0);
                return 1;
            }
            int lua_frame(lua_State *L)
            {
                LuaContext *ctx = context(L);
                lua_pushinteger(L, (lua_Integer)(ctx && ctx->self ? ctx->self->frameIndex() : 0));
                return 1;
            }
            int lua_set_time_scale(lua_State *L)
            {
                LuaContext *ctx = context(L);
                float s = (float)luaL_checknumber(L, 1);
                if (ctx && ctx->self) ctx->self->setTimeScale(s);
                return 0;
            }

            //------------------------------------------------------------------
            // Entities
            //------------------------------------------------------------------
            int lua_entity_create(lua_State *L)
            {
                LuaContext *ctx = context(L);
                const char *name = luaL_optstring(L, 1, "lua_entity");
                if (!ctx || !ctx->world) return pushError(L, "genesis: no world bound");
                ECS::Entity e = ctx->world->createEntity(name);
                ctx->world->addComponent<ECS::TransformComponent>(e);
                lua_pushinteger(L, (lua_Integer)e.id);
                return 1;
            }

            int lua_entity_destroy(lua_State *L)
            {
                LuaContext *ctx = context(L);
                ECS::Entity e = toEntity(ctx, luaL_checkinteger(L, 1));
                if (!ctx || !ctx->world || !ctx->world->isAlive(e)) return 0;
                ctx->world->destroyEntity(e);
                return 0;
            }

            int lua_entity_find(lua_State *L)
            {
                LuaContext *ctx = context(L);
                const char *name = luaL_checkstring(L, 1);
                if (!ctx || !ctx->world) { lua_pushnil(L); return 1; }
                for (ECS::Entity e : ctx->world->entities())
                {
                    if (ctx->world->entityName(e) == name)
                    {
                        lua_pushinteger(L, (lua_Integer)e.id);
                        return 1;
                    }
                }
                lua_pushnil(L);
                return 1;
            }

            int lua_entity_count(lua_State *L)
            {
                LuaContext *ctx = context(L);
                lua_pushinteger(L, (lua_Integer)(ctx && ctx->world ? ctx->world->entityCount() : 0));
                return 1;
            }

            int lua_entity_name(lua_State *L)
            {
                LuaContext *ctx = context(L);
                ECS::Entity e = toEntity(ctx, luaL_checkinteger(L, 1));
                if (!ctx || !ctx->world || !ctx->world->isAlive(e)) { lua_pushnil(L); return 1; }
                lua_pushstring(L, ctx->world->entityName(e).c_str());
                return 1;
            }

            //------------------------------------------------------------------
            // Transforms
            //------------------------------------------------------------------
            int lua_get_position(lua_State *L)
            {
                LuaContext *ctx = context(L);
                ECS::Entity e = toEntity(ctx, luaL_checkinteger(L, 1));
                ECS::TransformComponent *t = ctx && ctx->world ? ctx->world->getComponent<ECS::TransformComponent>(e) : nullptr;
                if (!t) return 0;
                lua_pushnumber(L, t->position.x);
                lua_pushnumber(L, t->position.y);
                lua_pushnumber(L, t->position.z);
                return 3;
            }

            int lua_set_position(lua_State *L)
            {
                LuaContext *ctx = context(L);
                ECS::Entity e = toEntity(ctx, luaL_checkinteger(L, 1));
                ECS::TransformComponent *t = ctx && ctx->world ? ctx->world->getComponent<ECS::TransformComponent>(e) : nullptr;
                if (!t) return 0;
                t->position = Math::Vec3((float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4));
                t->local_dirty = true;
                return 0;
            }

            int lua_translate(lua_State *L)
            {
                LuaContext *ctx = context(L);
                ECS::Entity e = toEntity(ctx, luaL_checkinteger(L, 1));
                ECS::TransformComponent *t = ctx && ctx->world ? ctx->world->getComponent<ECS::TransformComponent>(e) : nullptr;
                if (!t) return 0;
                t->position += Math::Vec3((float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4));
                t->local_dirty = true;
                return 0;
            }

            int lua_get_rotation(lua_State *L)
            {
                LuaContext *ctx = context(L);
                ECS::Entity e = toEntity(ctx, luaL_checkinteger(L, 1));
                ECS::TransformComponent *t = ctx && ctx->world ? ctx->world->getComponent<ECS::TransformComponent>(e) : nullptr;
                if (!t) return 0;
                Math::Vec3 euler = t->eulerAnglesDegrees();
                lua_pushnumber(L, euler.x);
                lua_pushnumber(L, euler.y);
                lua_pushnumber(L, euler.z);
                return 3;
            }

            int lua_set_rotation(lua_State *L)
            {
                LuaContext *ctx = context(L);
                ECS::Entity e = toEntity(ctx, luaL_checkinteger(L, 1));
                ECS::TransformComponent *t = ctx && ctx->world ? ctx->world->getComponent<ECS::TransformComponent>(e) : nullptr;
                if (!t) return 0;
                t->setEulerDegrees((float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4));
                return 0;
            }

            int lua_rotate(lua_State *L)
            {
                LuaContext *ctx = context(L);
                ECS::Entity e = toEntity(ctx, luaL_checkinteger(L, 1));
                ECS::TransformComponent *t = ctx && ctx->world ? ctx->world->getComponent<ECS::TransformComponent>(e) : nullptr;
                if (!t) return 0;
                Math::Vec3 euler = t->eulerAnglesDegrees();
                euler.x += (float)luaL_checknumber(L, 2);
                euler.y += (float)luaL_checknumber(L, 3);
                euler.z += (float)luaL_checknumber(L, 4);
                t->setEulerDegrees(euler.x, euler.y, euler.z);
                return 0;
            }

            int lua_get_scale(lua_State *L)
            {
                LuaContext *ctx = context(L);
                ECS::Entity e = toEntity(ctx, luaL_checkinteger(L, 1));
                ECS::TransformComponent *t = ctx && ctx->world ? ctx->world->getComponent<ECS::TransformComponent>(e) : nullptr;
                if (!t) return 0;
                lua_pushnumber(L, t->scale.x);
                lua_pushnumber(L, t->scale.y);
                lua_pushnumber(L, t->scale.z);
                return 3;
            }

            int lua_set_scale(lua_State *L)
            {
                LuaContext *ctx = context(L);
                ECS::Entity e = toEntity(ctx, luaL_checkinteger(L, 1));
                ECS::TransformComponent *t = ctx && ctx->world ? ctx->world->getComponent<ECS::TransformComponent>(e) : nullptr;
                if (!t) return 0;
                float v = (float)luaL_checknumber(L, 2);
                if (lua_gettop(L) >= 4)
                    t->scale = Math::Vec3(v, (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4));
                else
                    t->scale = Math::Vec3(v);
                t->local_dirty = true;
                return 0;
            }

            int lua_set_parent(lua_State *L)
            {
                LuaContext *ctx = context(L);
                ECS::Entity child = toEntity(ctx, luaL_checkinteger(L, 1));
                ECS::Entity parent = toEntity(ctx, luaL_checkinteger(L, 2));
                if (ctx && ctx->world) ctx->world->setParent(child, parent);
                return 0;
            }

            //------------------------------------------------------------------
            // Lights
            //------------------------------------------------------------------
            int lua_light_set(lua_State *L)
            {
                LuaContext *ctx = context(L);
                ECS::Entity e = toEntity(ctx, luaL_checkinteger(L, 1));
                ECS::LightComponent *light = ctx && ctx->world ? ctx->world->getComponent<ECS::LightComponent>(e) : nullptr;
                if (!light) return 0;
                if (lua_gettop(L) >= 2) light->intensity = (float)luaL_checknumber(L, 2);
                if (lua_gettop(L) >= 5)
                    light->color = Math::Color((float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4),
                                               (float)luaL_checknumber(L, 5), 1.0f);
                return 0;
            }

            //------------------------------------------------------------------
            // Input
            //------------------------------------------------------------------
            SDL_Scancode scanCodeFromName(const char *name)
            {
                struct Pair { const char *n; SDL_Scancode c; };
                static const Pair table[] = {
                    {"w", SDL_SCANCODE_W}, {"a", SDL_SCANCODE_A}, {"s", SDL_SCANCODE_S}, {"d", SDL_SCANCODE_D},
                    {"q", SDL_SCANCODE_Q}, {"e", SDL_SCANCODE_E}, {"r", SDL_SCANCODE_R}, {"f", SDL_SCANCODE_F},
                    {"g", SDL_SCANCODE_G}, {"h", SDL_SCANCODE_H}, {"i", SDL_SCANCODE_I}, {"j", SDL_SCANCODE_J},
                    {"k", SDL_SCANCODE_K}, {"l", SDL_SCANCODE_L}, {"z", SDL_SCANCODE_Z}, {"x", SDL_SCANCODE_X},
                    {"c", SDL_SCANCODE_C}, {"v", SDL_SCANCODE_V}, {"b", SDL_SCANCODE_B}, {"n", SDL_SCANCODE_N},
                    {"m", SDL_SCANCODE_M}, {"p", SDL_SCANCODE_P}, {"o", SDL_SCANCODE_O}, {"u", SDL_SCANCODE_U},
                    {"y", SDL_SCANCODE_Y}, {"t", SDL_SCANCODE_T},
                    {"space", SDL_SCANCODE_SPACE}, {"lshift", SDL_SCANCODE_LSHIFT}, {"rshift", SDL_SCANCODE_RSHIFT},
                    {"shift", SDL_SCANCODE_LSHIFT}, {"lctrl", SDL_SCANCODE_LCTRL}, {"ctrl", SDL_SCANCODE_LCTRL},
                    {"alt", SDL_SCANCODE_LALT}, {"escape", SDL_SCANCODE_ESCAPE}, {"esc", SDL_SCANCODE_ESCAPE},
                    {"tab", SDL_SCANCODE_TAB}, {"enter", SDL_SCANCODE_RETURN}, {"return", SDL_SCANCODE_RETURN},
                    {"up", SDL_SCANCODE_UP}, {"down", SDL_SCANCODE_DOWN}, {"left", SDL_SCANCODE_LEFT}, {"right", SDL_SCANCODE_RIGHT},
                    {"1", SDL_SCANCODE_1}, {"2", SDL_SCANCODE_2}, {"3", SDL_SCANCODE_3}, {"4", SDL_SCANCODE_4},
                    {"5", SDL_SCANCODE_5}, {"6", SDL_SCANCODE_6}, {"7", SDL_SCANCODE_7}, {"8", SDL_SCANCODE_8},
                    {"9", SDL_SCANCODE_9}, {"0", SDL_SCANCODE_0},
                    {"f1", SDL_SCANCODE_F1}, {"f2", SDL_SCANCODE_F2}, {"f3", SDL_SCANCODE_F3}, {"f4", SDL_SCANCODE_F4},
                    {"f5", SDL_SCANCODE_F5}, {"f6", SDL_SCANCODE_F6}, {"f7", SDL_SCANCODE_F7}, {"f8", SDL_SCANCODE_F8},
                };
                for (const auto &p : table)
                    if (std::strcmp(p.n, name) == 0) return p.c;
                return SDL_SCANCODE_UNKNOWN;
            }

            int lua_input_key(lua_State *L)
            {
                LuaContext *ctx = context(L);
                const char *name = luaL_checkstring(L, 1);
                if (!ctx || !ctx->input) { lua_pushboolean(L, 0); return 1; }
                SDL_Scancode code = scanCodeFromName(name);
                lua_pushboolean(L, code != SDL_SCANCODE_UNKNOWN && ctx->input->keyDown(code) ? 1 : 0);
                return 1;
            }

            int lua_input_key_pressed(lua_State *L)
            {
                LuaContext *ctx = context(L);
                const char *name = luaL_checkstring(L, 1);
                if (!ctx || !ctx->input) { lua_pushboolean(L, 0); return 1; }
                SDL_Scancode code = scanCodeFromName(name);
                lua_pushboolean(L, code != SDL_SCANCODE_UNKNOWN && ctx->input->keyPressed(code) ? 1 : 0);
                return 1;
            }

            int lua_input_mouse(lua_State *L)
            {
                LuaContext *ctx = context(L);
                int button = (int)luaL_optinteger(L, 1, 0);
                lua_pushboolean(L, ctx && ctx->input && ctx->input->mouseDown(button) ? 1 : 0);
                return 1;
            }

            int lua_input_mouse_pos(lua_State *L)
            {
                LuaContext *ctx = context(L);
                Math::Vec2 p = ctx && ctx->input ? ctx->input->mousePosition() : Math::Vec2(0, 0);
                lua_pushnumber(L, p.x);
                lua_pushnumber(L, p.y);
                return 2;
            }

            int lua_input_mouse_delta(lua_State *L)
            {
                LuaContext *ctx = context(L);
                Math::Vec2 p = ctx && ctx->input ? ctx->input->mouseDelta() : Math::Vec2(0, 0);
                lua_pushnumber(L, p.x);
                lua_pushnumber(L, p.y);
                return 2;
            }

            int lua_input_wheel(lua_State *L)
            {
                LuaContext *ctx = context(L);
                lua_pushnumber(L, ctx && ctx->input ? ctx->input->wheelDelta() : 0.0);
                return 1;
            }

            //------------------------------------------------------------------
            // Render settings
            //------------------------------------------------------------------
            int lua_render_set_exposure(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (ctx && ctx->render) ctx->render->settings().exposure = (float)luaL_checknumber(L, 1);
                return 0;
            }
            int lua_render_set_gamma(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (ctx && ctx->render) ctx->render->settings().gamma = (float)luaL_checknumber(L, 1);
                return 0;
            }
            int lua_render_set_tonemap(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (ctx && ctx->render)
                    ctx->render->settings().tonemap_mode = (Render::TonemapMode)(int)luaL_checkinteger(L, 1);
                return 0;
            }
            int lua_render_set_shading(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (ctx && ctx->render)
                    ctx->render->settings().shading_mode = (Render::ShadingMode)(int)luaL_checkinteger(L, 1);
                return 0;
            }
            int lua_render_set_wireframe(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (ctx && ctx->render) ctx->render->settings().wireframe = lua_toboolean(L, 1) != 0;
                return 0;
            }
            int lua_render_set_skybox(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (ctx && ctx->render) ctx->render->settings().show_skybox = lua_toboolean(L, 1) != 0;
                return 0;
            }
            int lua_render_set_env_intensity(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (ctx && ctx->render) ctx->render->settings().env_intensity = (float)luaL_checknumber(L, 1);
                return 0;
            }
            int lua_render_set_env_rotation(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (ctx && ctx->render) ctx->render->settings().env_rotation_degrees = (float)luaL_checknumber(L, 1);
                return 0;
            }
            int lua_render_set_ambient(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (ctx && ctx->render)
                    ctx->render->settings().ambient_color = Math::Color((float)luaL_checknumber(L, 1),
                                                                        (float)luaL_checknumber(L, 2),
                                                                        (float)luaL_checknumber(L, 3));
                return 0;
            }
            int lua_render_set_ibl(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (ctx && ctx->render) ctx->render->settings().use_ibl = lua_toboolean(L, 1) != 0;
                return 0;
            }

            int lua_render_stats(lua_State *L)
            {
                LuaContext *ctx = context(L);
                lua_newtable(L);
                if (!ctx || !ctx->render) return 1;
                const Render::RenderStats &s = ctx->render->stats();
                lua_pushinteger(L, s.draw_calls); lua_setfield(L, -2, "draw_calls");
                lua_pushinteger(L, s.triangles); lua_setfield(L, -2, "triangles");
                lua_pushinteger(L, s.opaque_items); lua_setfield(L, -2, "opaque_items");
                lua_pushinteger(L, s.transparent_items); lua_setfield(L, -2, "transparent_items");
                lua_pushinteger(L, s.culled_items); lua_setfield(L, -2, "culled_items");
                lua_pushinteger(L, s.active_lights); lua_setfield(L, -2, "lights");
                lua_pushinteger(L, s.meshes); lua_setfield(L, -2, "meshes");
                lua_pushinteger(L, s.materials); lua_setfield(L, -2, "materials");
                lua_pushnumber(L, s.scene_ms); lua_setfield(L, -2, "scene_ms");
                lua_pushnumber(L, s.present_ms); lua_setfield(L, -2, "present_ms");
                lua_pushinteger(L, (lua_Integer)s.frame_index); lua_setfield(L, -2, "frame");
                lua_pushboolean(L, ctx->render->iblReady() ? 1 : 0); lua_setfield(L, -2, "ibl_ready");
                lua_pushstring(L, RHI::backendTypeName(ctx->render->device()->backend())); lua_setfield(L, -2, "backend");
                return 1;
            }

            int lua_render_draw_line(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (!ctx || !ctx->render) return 0;
                Math::Vec3 a((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3));
                Math::Vec3 b((float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6));
                Math::Color c(1, 1, 1, 1);
                if (lua_gettop(L) >= 9)
                    c = Math::Color((float)luaL_checknumber(L, 7), (float)luaL_checknumber(L, 8), (float)luaL_checknumber(L, 9), 1.0f);
                ctx->render->drawLine(a, b, c);
                return 0;
            }

            int lua_render_draw_box(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (!ctx || !ctx->render) return 0;
                Math::Vec3 c((float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3));
                Math::Vec3 h((float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6));
                Math::Color color(1, 1, 0, 1);
                if (lua_gettop(L) >= 9)
                    color = Math::Color((float)luaL_checknumber(L, 7), (float)luaL_checknumber(L, 8), (float)luaL_checknumber(L, 9), 1.0f);
                ctx->render->drawBox(Math::AABB{c - h, c + h, true}, color);
                return 0;
            }

            int lua_render_bake_ibl(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (!ctx || !ctx->render) { lua_pushboolean(L, 0); return 1; }
                const char *path = luaL_optstring(L, 1, "");
                Render::RenderSystem::IblBakeSettings cfg;
                if (lua_gettop(L) >= 2) cfg.cubemap_size = (uint32_t)luaL_optinteger(L, 2, cfg.cubemap_size);
                bool ok = path[0] ? ctx->render->bakeIBL(path, cfg) : ctx->render->bakeProceduralIBL(cfg);
                lua_pushboolean(L, ok ? 1 : 0);
                return 1;
            }

            //------------------------------------------------------------------
            // Resources
            //------------------------------------------------------------------
            int lua_load_model(lua_State *L)
            {
                LuaContext *ctx = context(L);
                const char *path = luaL_checkstring(L, 1);
                if (!ctx || !ctx->world || !ctx->render) return pushError(L, "genesis: engine context missing");

                std::shared_ptr<ModelData> model = ResourceIO::loadModel(path);
                if (!model)
                {
                    lua_pushnil(L);
                    lua_pushstring(L, "failed to load model");
                    return 2;
                }

                std::string name = ResourceIO::pathFilename(path);
                ECS::Entity entity = ctx->world->createEntity(name);
                ctx->world->addComponent<ECS::TransformComponent>(entity);

                ECS::MeshComponent mesh;
                mesh.model = model;
                mesh.visible = true;
                mesh.pick_id = entity.id + 1;
                ctx->world->addComponent<ECS::MeshComponent>(entity, mesh);

                std::vector<uint32_t> gpu_meshes = ctx->render->scene().uploadModel(*model, name);
                Render::RenderSystem::EntityMeshBinding binding;
                binding.entity = entity;
                binding.mesh_indices = gpu_meshes;
                ctx->render->bindEntityMeshes(binding);

                lua_pushinteger(L, (lua_Integer)entity.id);
                lua_pushinteger(L, (lua_Integer)model->meshes.size());
                return 2;
            }

            int lua_model_info(lua_State *L)
            {
                LuaContext *ctx = context(L);
                ECS::Entity e = toEntity(ctx, luaL_checkinteger(L, 1));
                ECS::MeshComponent *mesh = ctx && ctx->world ? ctx->world->getComponent<ECS::MeshComponent>(e) : nullptr;
                lua_newtable(L);
                if (!mesh || !mesh->model) return 1;
                lua_pushstring(L, mesh->model->name.c_str()); lua_setfield(L, -2, "name");
                lua_pushstring(L, mesh->model->importer.c_str()); lua_setfield(L, -2, "importer");
                lua_pushinteger(L, (lua_Integer)mesh->model->meshes.size()); lua_setfield(L, -2, "meshes");
                lua_pushinteger(L, (lua_Integer)mesh->model->materials.size()); lua_setfield(L, -2, "materials");
                size_t triangles = 0, vertices = 0;
                for (const auto &m : mesh->model->meshes)
                {
                    triangles += m.indices.size() / 3;
                    vertices += m.vertices.size();
                }
                lua_pushinteger(L, (lua_Integer)triangles); lua_setfield(L, -2, "triangles");
                lua_pushinteger(L, (lua_Integer)vertices); lua_setfield(L, -2, "vertices");
                return 1;
            }

            //------------------------------------------------------------------
            // RTTI: create / query full scene objects
            //------------------------------------------------------------------
            int lua_create_camera(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (!ctx || !ctx->world) return 0;
                ECS::Entity e = ctx->world->createEntity("ScriptCamera");
                ECS::TransformComponent t;
                if (lua_gettop(L) >= 3)
                    t.position = Math::Vec3((float)luaL_optnumber(L, 1, 0), (float)luaL_optnumber(L, 2, 0),
                                            (float)luaL_optnumber(L, 3, 6.0));
                else
                    t.position = Math::Vec3(0, 0, 6.0f);
                ctx->world->addComponent<ECS::TransformComponent>(e, t);
                ECS::CameraComponent cam;
                cam.fov_degrees = (float)luaL_optnumber(L, 4, 60.0);
                cam.near_plane = 0.05f;
                cam.far_plane = 300.0f;
                cam.active = true;
                ctx->world->addComponent<ECS::CameraComponent>(e, cam);
                lua_pushinteger(L, (lua_Integer)e.id);
                return 1;
            }

            int lua_create_light(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (!ctx || !ctx->world) return 0;
                ECS::Entity e = ctx->world->createEntity("ScriptLight");
                ECS::TransformComponent t;
                t.position = Math::Vec3((float)luaL_optnumber(L, 1, 0), (float)luaL_optnumber(L, 2, 4),
                                         (float)luaL_optnumber(L, 3, 0));
                ctx->world->addComponent<ECS::TransformComponent>(e, t);
                ECS::LightComponent light;
                light.type = (ECS::LightType)(int)luaL_optinteger(L, 4, 2); // 0 dir,1 point,2 spot
                light.color = Math::Color((float)luaL_optnumber(L, 5, 1), (float)luaL_optnumber(L, 6, 1),
                                           (float)luaL_optnumber(L, 7, 1));
                light.intensity = (float)luaL_optnumber(L, 8, 4);
                light.range = 20.0f;
                ctx->world->addComponent<ECS::LightComponent>(e, light);
                lua_pushinteger(L, (lua_Integer)e.id);
                return 1;
            }

            int lua_create_environment(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (!ctx || !ctx->world) return 0;
                ECS::Entity e = ctx->world->createEntity("ScriptEnvironment");
                ECS::EnvironmentComponent env;
                const char *hdr = luaL_optstring(L, 1, nullptr);
                if (hdr)
                {
                    std::string p(hdr);
                    env.hdr_equirect_path = ResourceIO::fileExists(p) ? p : "";
                }
                env.show_skybox = true;
                env.use_ibl = true;
                env.ibl_dirty = true;
                ctx->world->addComponent<ECS::EnvironmentComponent>(e, env);
                lua_pushinteger(L, (lua_Integer)e.id);
                return 1;
            }

            int lua_has_component(lua_State *L)
            {
                LuaContext *ctx = context(L);
                ECS::Entity e = toEntity(ctx, luaL_checkinteger(L, 1));
                const char *comp = luaL_checkstring(L, 2);
                bool has = false;
                if (ctx && ctx->world && ctx->world->isAlive(e))
                {
                    std::string c(comp);
                    has = (c == "Camera" && ctx->world->getComponent<ECS::CameraComponent>(e)) ||
                          (c == "Light" && ctx->world->getComponent<ECS::LightComponent>(e)) ||
                          (c == "Mesh" && ctx->world->getComponent<ECS::MeshComponent>(e)) ||
                          (c == "Transform" && ctx->world->getComponent<ECS::TransformComponent>(e)) ||
                          (c == "Environment" && ctx->world->getComponent<ECS::EnvironmentComponent>(e)) ||
                          (c == "Script" && ctx->world->getComponent<ECS::ScriptComponent>(e));
                }
                lua_pushboolean(L, has ? 1 : 0);
                return 1;
            }

            // Procedural mesh: genesis.create_mesh(shape, x,y,z, r,g,b, metallic, roughness)
            int lua_create_mesh(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (!ctx || !ctx->world || !ctx->render) return 0;
                const char *shape = luaL_checkstring(L, 1);
                float x = (float)luaL_optnumber(L, 2, 0), y = (float)luaL_optnumber(L, 3, 0), z = (float)luaL_optnumber(L, 4, 0);
                float r = (float)luaL_optnumber(L, 5, 0.8), g = (float)luaL_optnumber(L, 6, 0.8), b = (float)luaL_optnumber(L, 7, 0.8);
                float metallic = (float)luaL_optnumber(L, 8, 0.0);
                float roughness = (float)luaL_optnumber(L, 9, 0.5);

                MaterialData mat;
                mat.base_color_factor = Math::Color(r, g, b, 1.0f);
                mat.metallic_factor = metallic;
                mat.roughness_factor = roughness;

                std::shared_ptr<ModelData> model;
                std::string s(shape);
                if (s == "sphere")
                    model = Procedural::wrapMesh(Procedural::makeSphere(0.5f, 24, 16), mat, s);
                else if (s == "plane")
                    model = Procedural::wrapMesh(Procedural::makePlane(2.0f, 2.0f, 1), mat, s);
                else
                    model = Procedural::wrapMesh(Procedural::makeBox(1.0f, 1.0f, 1.0f), mat, s);

                ECS::Entity e = ctx->world->createEntity(s);
                ECS::TransformComponent t;
                t.position = Math::Vec3(x, y, z);
                ctx->world->addComponent<ECS::TransformComponent>(e, t);
                ECS::MeshComponent mc;
                mc.model = model;
                mc.pick_id = e.id + 1;
                ctx->world->addComponent<ECS::MeshComponent>(e, mc);
                std::vector<uint32_t> gpu = ctx->render->scene().uploadModel(*model, s);
                Render::RenderSystem::EntityMeshBinding binding;
                binding.entity = e;
                binding.mesh_indices = gpu;
                ctx->render->bindEntityMeshes(binding);
                lua_pushinteger(L, (lua_Integer)e.id);
                return 1;
            }

            //------------------------------------------------------------------
            // Materials (real-time, index into RenderScene)
            //------------------------------------------------------------------
            int lua_material_count(lua_State *L)
            {
                LuaContext *ctx = context(L);
                lua_pushinteger(L, (lua_Integer)(ctx && ctx->render ? ctx->render->scene().materialCount() : 0));
                return 1;
            }
            int lua_material_set_color(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (ctx && ctx->render)
                    ctx->render->scene().setMaterialBaseColor((int32_t)luaL_checkinteger(L, 1),
                        Math::Color((float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3),
                                    (float)luaL_checknumber(L, 4), (float)luaL_optnumber(L, 5, 1.0)));
                return 0;
            }
            int lua_material_set_pbr(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (ctx && ctx->render)
                    ctx->render->scene().setMaterialPBR((int32_t)luaL_checkinteger(L, 1),
                        (float)luaL_optnumber(L, 2, 0), (float)luaL_optnumber(L, 3, 1));
                return 0;
            }
            int lua_material_set_emissive(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (ctx && ctx->render)
                    ctx->render->scene().setMaterialEmissive((int32_t)luaL_checkinteger(L, 1),
                        Math::Color((float)luaL_optnumber(L, 2, 0), (float)luaL_optnumber(L, 3, 0),
                                    (float)luaL_optnumber(L, 4, 0)), (float)luaL_optnumber(L, 5, 1));
                return 0;
            }

            //------------------------------------------------------------------
            // Command buffer: free 2D/3D draw commands (organise -> submit)
            //------------------------------------------------------------------
            int lua_cmd_clear(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (ctx && ctx->render) ctx->render->overlay().clearCommands();
                return 0;
            }
            int lua_cmd_fill2d(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (ctx && ctx->render)
                    ctx->render->overlay().fill2D(
                        (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2),
                        (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4),
                        (float)luaL_optnumber(L, 5, 1), (float)luaL_optnumber(L, 6, 1),
                        (float)luaL_optnumber(L, 7, 1), (float)luaL_optnumber(L, 8, 1));
                return 0;
            }
            int lua_cmd_line2d(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (ctx && ctx->render)
                    ctx->render->overlay().line2D(
                        (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2),
                        (float)luaL_checknumber(L, 3), (float)luaL_checknumber(L, 4),
                        (float)luaL_optnumber(L, 5, 1), (float)luaL_optnumber(L, 6, 1),
                        (float)luaL_optnumber(L, 7, 1), (float)luaL_optnumber(L, 8, 1));
                return 0;
            }
            int lua_cmd_quad3d(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (ctx && ctx->render)
                    ctx->render->overlay().quad3D(
                        (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2),
                        (float)luaL_checknumber(L, 3), (float)luaL_optnumber(L, 4, 0.2f),
                        (float)luaL_optnumber(L, 5, 0.2f),
                        (float)luaL_optnumber(L, 6, 1), (float)luaL_optnumber(L, 7, 1),
                        (float)luaL_optnumber(L, 8, 1), (float)luaL_optnumber(L, 9, 1));
                return 0;
            }
            int lua_post_effect(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (ctx && ctx->render)
                    ctx->render->overlay().postEffect((Render::PostEffect)(uint32_t)luaL_optinteger(L, 1, 0));
                return 0;
            }

            //------------------------------------------------------------------
            // SDF text
            //------------------------------------------------------------------
            int lua_text2d(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (!ctx || !ctx->render) return 0;
                const char *str = luaL_checkstring(L, 4);
                ctx->render->overlay().text().drawText2D(
                    (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2),
                    (float)luaL_optnumber(L, 3, 24), str,
                    Math::Color((float)luaL_optnumber(L, 5, 1), (float)luaL_optnumber(L, 6, 1),
                                (float)luaL_optnumber(L, 7, 1), (float)luaL_optnumber(L, 8, 1)),
                    (float)ctx->render->settings().hdr_width, (float)ctx->render->settings().hdr_height);
                return 0;
            }
            int lua_text3d(lua_State *L)
            {
                LuaContext *ctx = context(L);
                if (!ctx || !ctx->render) return 0;
                const char *str = luaL_checkstring(L, 5);
                Math::Mat4 w = Math::Mat4::identity();
                w.m[3] = (float)luaL_checknumber(L, 1);
                w.m[7] = (float)luaL_checknumber(L, 2);
                w.m[11] = (float)luaL_checknumber(L, 3);
                ctx->render->overlay().text().drawText3D(w, str,
                    Math::Color((float)luaL_optnumber(L, 6, 1), (float)luaL_optnumber(L, 7, 1),
                                (float)luaL_optnumber(L, 8, 1), (float)luaL_optnumber(L, 9, 1)),
                    (float)luaL_optnumber(L, 4, 0.2f));
                return 0;
            }

        } // namespace

        //==================================================================
        // ScriptSystem
        //==================================================================
        ScriptSystem::ScriptSystem() {}

        ScriptSystem::~ScriptSystem()
        {
            shutdown();
        }

        bool ScriptSystem::initialize(ECS::World *world, Render::RenderSystem *render, Platform::InputSystem *input)
        {
            world_ = world;
            render_ = render;
            input_ = input;

            lua_state_ = luaL_newstate();
            if (!lua_state_)
            {
                GEN_LOG_ERROR("[Lua] luaL_newstate failed");
                return false;
            }
            // Base + math + string + table are enough for gameplay scripts and
            // keep the sandbox tight (no io/os access).
            luaL_requiref(lua_state_, LUA_GNAME, luaopen_base, 1);
            luaL_requiref(lua_state_, LUA_MATHLIBNAME, luaopen_math, 1);
            luaL_requiref(lua_state_, LUA_STRLIBNAME, luaopen_string, 1);
            luaL_requiref(lua_state_, LUA_TABLIBNAME, luaopen_table, 1);
            lua_pop(lua_state_, 4);

            static LuaContext ctx; // stable address for the registry entry
            ctx.self = this;
            ctx.world = world_;
            ctx.render = render_;
            ctx.input = input_;
            lua_pushlightuserdata(lua_state_, &ctx);
            lua_setfield(lua_state_, LUA_REGISTRYINDEX, kContextKey);

            registerApi();
            GEN_LOG_INFO("[Lua] %s ready (%s)", LUA_VERSION, LUA_RELEASE);
            return true;
        }

        void ScriptSystem::registerApi()
        {
            struct Entry { const char *name; lua_CFunction fn; };
            static const Entry entries[] = {
                {"log_info", lua_log_info},
                {"log_warn", lua_log_warn},
                {"log_error", lua_log_error},
                {"elapsed", lua_elapsed},
                {"frame", lua_frame},
                {"set_time_scale", lua_set_time_scale},
                {"entity_create", lua_entity_create},
                {"entity_destroy", lua_entity_destroy},
                {"entity_find", lua_entity_find},
                {"entity_count", lua_entity_count},
                {"entity_name", lua_entity_name},
                {"get_position", lua_get_position},
                {"set_position", lua_set_position},
                {"translate", lua_translate},
                {"get_rotation", lua_get_rotation},
                {"set_rotation", lua_set_rotation},
                {"rotate", lua_rotate},
                {"get_scale", lua_get_scale},
                {"set_scale", lua_set_scale},
                {"set_parent", lua_set_parent},
                {"light_set", lua_light_set},
                {"input_key", lua_input_key},
                {"input_key_pressed", lua_input_key_pressed},
                {"input_mouse", lua_input_mouse},
                {"input_mouse_pos", lua_input_mouse_pos},
                {"input_mouse_delta", lua_input_mouse_delta},
                {"input_wheel", lua_input_wheel},
                {"render_set_exposure", lua_render_set_exposure},
                {"render_set_gamma", lua_render_set_gamma},
                {"render_set_tonemap", lua_render_set_tonemap},
                {"render_set_shading", lua_render_set_shading},
                {"render_set_wireframe", lua_render_set_wireframe},
                {"render_set_skybox", lua_render_set_skybox},
                {"render_set_env_intensity", lua_render_set_env_intensity},
                {"render_set_env_rotation", lua_render_set_env_rotation},
                {"render_set_ambient", lua_render_set_ambient},
                {"render_set_ibl", lua_render_set_ibl},
                {"render_stats", lua_render_stats},
                {"render_draw_line", lua_render_draw_line},
                {"render_draw_box", lua_render_draw_box},
                {"render_bake_ibl", lua_render_bake_ibl},
                {"load_model", lua_load_model},
                {"model_info", lua_model_info},
                // RTTI scene construction
                {"create_camera", lua_create_camera},
                {"create_light", lua_create_light},
                {"create_environment", lua_create_environment},
                {"create_mesh", lua_create_mesh},
                {"has_component", lua_has_component},
                // materials (real-time)
                {"material_count", lua_material_count},
                {"material_set_color", lua_material_set_color},
                {"material_set_pbr", lua_material_set_pbr},
                {"material_set_emissive", lua_material_set_emissive},
                // command buffer free draws
                {"cmd_clear", lua_cmd_clear},
                {"cmd_fill2d", lua_cmd_fill2d},
                {"cmd_line2d", lua_cmd_line2d},
                {"cmd_quad3d", lua_cmd_quad3d},
                {"post_effect", lua_post_effect},
                // SDF text
                {"text2d", lua_text2d},
                {"text3d", lua_text3d},
            };

            lua_newtable(lua_state_);
            for (const auto &e : entries)
            {
                lua_pushcfunction(lua_state_, e.fn);
                lua_setfield(lua_state_, -2, e.name);
            }
            // Version info so scripts can feature-detect.
            lua_pushstring(lua_state_, GENESIS_ENGINE_VERSION);
            lua_setfield(lua_state_, -2, "version");
            lua_pushstring(lua_state_, LUA_RELEASE);
            lua_setfield(lua_state_, -2, "lua_version");
            lua_setglobal(lua_state_, "genesis");
        }

        void ScriptSystem::shutdown()
        {
            if (!lua_state_) return;
            lua_close(lua_state_);
            lua_state_ = nullptr;
            loaded_count_ = 0;
        }

        void ScriptSystem::reportError(const std::string &message)
        {
            ++error_count_;
            errors_.push_back(message);
            if (errors_.size() > 100) errors_.erase(errors_.begin(), errors_.begin() + (errors_.size() - 100));
            GEN_LOG_ERROR("[Lua] %s", message.c_str());
        }

        bool ScriptSystem::executeString(const std::string &code, const std::string &name)
        {
            if (!lua_state_) return false;
            int status = luaL_loadbuffer(lua_state_, code.c_str(), code.size(), name.c_str());
            if (status != LUA_OK)
            {
                reportError(lua_tostring(lua_state_, -1));
                lua_pop(lua_state_, 1);
                return false;
            }
            status = lua_pcall(lua_state_, 0, LUA_MULTRET, 0);
            if (status != LUA_OK)
            {
                reportError(lua_tostring(lua_state_, -1));
                lua_pop(lua_state_, 1);
                return false;
            }
            return true;
        }

        bool ScriptSystem::loadComponentScript(ECS::Entity entity, ECS::ScriptComponent &component)
        {
            if (!lua_state_) return false;

            std::string source;
            std::string chunk_name;
            if (!component.script_path.empty())
            {
                if (!ResourceIO::readFileText(component.script_path, source))
                {
                    reportError("cannot read script file: " + component.script_path);
                    return false;
                }
                chunk_name = "@" + component.script_path;
            }
            else if (!component.inline_code.empty())
            {
                source = component.inline_code;
                chunk_name = "=inline";
            }
            else
            {
                reportError("script component has neither a path nor inline code");
                return false;
            }

            // Build a sandboxed environment so that several scripts cannot
            // clobber each other's globals.
            lua_newtable(lua_state_);                                  // env
            lua_newtable(lua_state_);                                  // metatable
            lua_pushglobaltable(lua_state_);
            lua_setfield(lua_state_, -2, "__index");
            lua_setmetatable(lua_state_, -2);

            // Lua 5.4 dropped the `env` argument of lua_load: load the chunk
            // first, then replace its _ENV upvalue with our sandbox table.
            struct ChunkReader
            {
                const char *data;
                size_t size;
                bool done;
            };
            ChunkReader reader{source.c_str(), source.size(), false};
            int status = lua_load(lua_state_,
                                  [](lua_State *, void *ud, size_t *size) -> const char *
                                  {
                                      ChunkReader *r = static_cast<ChunkReader *>(ud);
                                      if (r->done) return nullptr;
                                      r->done = true;
                                      *size = r->size;
                                      return r->data;
                                  },
                                  &reader, chunk_name.c_str(), "t");
            if (status != LUA_OK)
            {
                reportError(std::string("load failed: ") + lua_tostring(lua_state_, -1));
                lua_pop(lua_state_, 2);
                return false;
            }
            // stack: env, chunk -> push a copy of env and install it as the
            // chunk's first upvalue (_ENV).  With the copy on top the function
            // sits at relative index -2.
            lua_pushvalue(lua_state_, -2);
            if (!lua_setupvalue(lua_state_, -2, 1))
            {
                reportError("chunk has no _ENV upvalue; cannot sandbox the script");
                lua_pop(lua_state_, 2);
                return false;
            }
            // stack: env, chunk
            status = lua_pcall(lua_state_, 0, 0, 0);
            if (status != LUA_OK)
            {
                reportError(std::string("chunk execution failed: ") + lua_tostring(lua_state_, -1));
                lua_pop(lua_state_, 2);
                return false;
            }
            // stack: env
            lua_pushvalue(lua_state_, -1);
            component.lua_ref = luaL_ref(lua_state_, LUA_REGISTRYINDEX);
            lua_pop(lua_state_, 1);
            component.loaded = true;
            ++loaded_count_;

            // start(entity_id) - keep the Lua stack balanced on every path.
            lua_rawgeti(lua_state_, LUA_REGISTRYINDEX, component.lua_ref); // [env]
            lua_getfield(lua_state_, -1, "start");                          // [env, start]
            if (lua_isfunction(lua_state_, -1))
            {
                lua_pushinteger(lua_state_, (lua_Integer)entity.id);        // [env, start, id]
                if (lua_pcall(lua_state_, 1, 0, 0) != LUA_OK)               // [env] (+err)
                {
                    reportError(std::string("start() failed: ") + lua_tostring(lua_state_, -1));
                    lua_pop(lua_state_, 1);
                }
                lua_pop(lua_state_, 1); // env
            }
            else
            {
                lua_pop(lua_state_, 2); // start + env
            }
            GEN_LOG_INFO("[Lua] loaded script for entity %u (%s)", entity.id, chunk_name.c_str());
            return true;
        }

        void ScriptSystem::unloadComponentScript(ECS::ScriptComponent &component)
        {
            if (!lua_state_ || component.lua_ref < 0) return;
            luaL_unref(lua_state_, LUA_REGISTRYINDEX, component.lua_ref);
            component.lua_ref = -1;
            component.loaded = false;
            if (loaded_count_) --loaded_count_;
        }

        bool ScriptSystem::callHook(ECS::ScriptComponent &component, const char *function, ECS::Entity entity, float delta_time)
        {
            if (!lua_state_ || component.lua_ref < 0) return false;
            // stack: [env]
            lua_rawgeti(lua_state_, LUA_REGISTRYINDEX, component.lua_ref);
            // stack: [env, hook]
            lua_getfield(lua_state_, -1, function);
            if (!lua_isfunction(lua_state_, -1))
            {
                lua_pop(lua_state_, 2); // hook + env
                return false;
            }
            lua_pushnumber(lua_state_, delta_time);
            lua_pushinteger(lua_state_, (lua_Integer)entity.id);
            // stack: [env, hook, dt, id] -> pcall pops hook+args
            int status = lua_pcall(lua_state_, 2, 0, 0);
            if (status != LUA_OK)
            {
                reportError(std::string(function) + "() failed: " + lua_tostring(lua_state_, -1));
                lua_pop(lua_state_, 1); // error message
            }
            lua_pop(lua_state_, 1); // env
            return true;
        }

        void ScriptSystem::update(ECS::World &world, float delta_time)
        {
            if (!lua_state_) return;
            auto start = std::chrono::steady_clock::now();

            elapsed_ += delta_time * time_scale_;
            ++frame_index_;

            // Load scripts that have not been compiled yet.
            world.each<ECS::ScriptComponent>([&](ECS::Entity e, ECS::ScriptComponent &c)
            {
                if (c.enabled && !c.loaded && (!c.script_path.empty() || !c.inline_code.empty()))
                    loadComponentScript(e, c);
            });

            const float scaled = delta_time * time_scale_;
            world.each<ECS::ScriptComponent>([&](ECS::Entity e, ECS::ScriptComponent &c)
            {
                if (!c.enabled || !c.loaded) return;
                callHook(c, "tick", e, scaled);
            });

            auto end = std::chrono::steady_clock::now();
            last_tick_ms_ = std::chrono::duration<double, std::milli>(end - start).count();
        }

    } // namespace Script
} // namespace Genesis
