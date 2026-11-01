/*
 * Genesis Render - JSON scene loader implementation
 */
#include "scene/scene_loader.hpp"

#include "engine/engine.hpp"
#include "foundation_layer/genesis_log.hpp"
#include "physics/physics.hpp"
#include "reflection/engine_bindings.hpp"
#include "renderer/render_system.hpp"
#include "resource/procedural_mesh.hpp"
#include "resource/resource_types.hpp"

#include "external/json.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace Genesis
{
    namespace Scene
    {
        using Json = nlohmann::json;

        namespace
        {
            std::string lower(std::string s)
            {
                for (char &c : s) c = (char)std::tolower((unsigned char)c);
                return s;
            }

            bool fileExists(const std::string &path) { return ResourceIO::fileExists(path); }

            std::string resolve(const std::string &root, const std::string &path)
            {
                if (path.empty()) return path;
                if (path[0] == '/' || (path.size() > 1 && path[1] == ':')) return path;
                return ResourceIO::joinPath(root, path);
            }

            float f(const Json &j, const char *key, float fallback)
            {
                return j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback;
            }

            bool b(const Json &j, const char *key, bool fallback)
            {
                return j.contains(key) && j[key].is_boolean() ? j[key].get<bool>() : fallback;
            }

            std::string s(const Json &j, const char *key, const std::string &fallback = "")
            {
                return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : fallback;
            }

            Math::Vec2 vec2(const Json &j, const char *key, const Math::Vec2 &fallback = Math::Vec2(0.0f))
            {
                if (!j.contains(key) || !j[key].is_array() || j[key].size() < 2) return fallback;
                return Math::Vec2(j[key][0].get<float>(), j[key][1].get<float>());
            }

            Math::Vec3 vec3(const Json &j, const char *key, const Math::Vec3 &fallback = Math::Vec3(0.0f))
            {
                if (!j.contains(key) || !j[key].is_array()) return fallback;
                const Json &a = j[key];
                if (a.size() == 1) return Math::Vec3(a[0].get<float>());
                if (a.size() >= 3) return Math::Vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>());
                if (a.size() == 2) return Math::Vec3(a[0].get<float>(), a[1].get<float>(), 0.0f);
                return fallback;
            }

            Math::Color color(const Json &j, const char *key, const Math::Color &fallback = Math::Color(1, 1, 1, 1))
            {
                if (!j.contains(key) || !j[key].is_array()) return fallback;
                const Json &a = j[key];
                if (a.size() < 3) return fallback;
                return Math::Color(a[0].get<float>(), a[1].get<float>(), a[2].get<float>(),
                                   a.size() > 3 ? a[3].get<float>() : 1.0f);
            }

            Math::Quat quat(const Json &j, const char *key)
            {
                if (!j.contains(key) || !j[key].is_array() || j[key].size() < 4) return Math::Quat::identity();
                const Json &a = j[key];
                return Math::Quat(a[0].get<float>(), a[1].get<float>(), a[2].get<float>(), a[3].get<float>()).normalized();
            }

            Render::MaterialShading shadingFrom(const std::string &name)
            {
                const std::string v = lower(name);
                if (v == "cook_torrance" || v == "cooktorrance" || v == "pbr") return Render::MaterialShading::CookTorrance;
                if (v == "blinn_phong" || v == "blinnphong" || v == "blinn") return Render::MaterialShading::BlinnPhong;
                if (v == "unlit") return Render::MaterialShading::Unlit;
                return Render::MaterialShading::Default;
            }

            Render::MaterialBlend blendFrom(const std::string &name)
            {
                const std::string v = lower(name);
                if (v == "masked") return Render::MaterialBlend::Masked;
                if (v == "transparent" || v == "blend") return Render::MaterialBlend::Transparent;
                if (v == "additive") return Render::MaterialBlend::Additive;
                return Render::MaterialBlend::Opaque;
            }

            Render::MaterialCull cullFrom(const std::string &name)
            {
                const std::string v = lower(name);
                if (v == "front") return Render::MaterialCull::Front;
                if (v == "none") return Render::MaterialCull::None;
                return Render::MaterialCull::Back;
            }

            ECS::BodyType bodyTypeFrom(const std::string &name)
            {
                const std::string v = lower(name);
                if (v == "static") return ECS::BodyType::Static;
                if (v == "kinematic") return ECS::BodyType::Kinematic;
                return ECS::BodyType::Dynamic;
            }

            ECS::ColliderShape shapeFrom(const std::string &name)
            {
                const std::string v = lower(name);
                if (v == "sphere") return ECS::ColliderShape::Sphere;
                if (v == "plane") return ECS::ColliderShape::Plane;
                if (v == "capsule") return ECS::ColliderShape::Capsule;
                return ECS::ColliderShape::Box;
            }

            Reflect::MeshAssetPtr makeMesh(Render::RenderSystem &render, const Json &mesh)
            {
                if (mesh.is_string()) return Reflect::createMeshAsset(mesh.get<std::string>(), 1.0f, 32);
                if (!mesh.is_object()) return nullptr;
                const std::string kind = lower(s(mesh, "kind", "box"));
                const float size = f(mesh, "size", 1.0f);
                const int detail = (int)f(mesh, "detail", 32);
                return Reflect::createMeshAsset(kind, size, detail);
            }

            // Applies the common transform / mesh / material / component data.
            ECS::Entity buildEntity(Engine &engine, const Json &node, const std::string &root,
                                    std::unordered_map<std::string, ECS::Entity> &by_name, LoadResult &result)
            {
                ECS::World &world = engine.world();
                Render::RenderScene &scene = engine.render().scene();
                const std::string name = s(node, "name", "Entity");
                ECS::Entity entity = world.createEntity(name);

                // ---- transform -------------------------------------------
                ECS::TransformComponent transform;
                if (node.contains("transform") && node["transform"].is_object())
                {
                    const Json &t = node["transform"];
                    transform.position = vec3(t, "position");
                    transform.scale = vec3(t, "scale", Math::Vec3(1.0f));
                    if (t.contains("rotation")) transform.rotation = quat(t, "rotation");
                    if (t.contains("euler") && t["euler"].is_array() && t["euler"].size() >= 3)
                        transform.setEulerDegrees(t["euler"][0].get<float>(), t["euler"][1].get<float>(),
                                                  t["euler"][2].get<float>());
                }
                world.addComponent<ECS::TransformComponent>(entity, transform);

                // ---- mesh + material ------------------------------------
                Reflect::MeshAssetPtr mesh;
                if (node.contains("mesh"))
                {
                    if (node["mesh"].is_object() && node["mesh"].contains("model"))
                    {
                        const std::string path = resolve(root, s(node["mesh"], "model"));
                        mesh = Reflect::loadMeshAsset(path);
                        if (!mesh) GEN_LOG_WARN("[Scene] cannot load model '%s'", path.c_str());
                    }
                    else
                    {
                        mesh = makeMesh(engine.render(), node["mesh"]);
                    }
                }
                Render::MaterialPtr material;
                if (node.contains("material") && node["material"].is_string())
                    material = scene.findMaterial(node["material"].get<std::string>());
                if (mesh) Reflect::attachMesh(entity, mesh, material);

                // ---- components -----------------------------------------
                if (node.contains("components") && node["components"].is_object())
                {
                    const Json &c = node["components"];
                    if (c.contains("camera") && c["camera"].is_object())
                    {
                        const Json &j = c["camera"];
                        ECS::CameraComponent cam;
                        cam.fov_degrees = f(j, "fov", cam.fov_degrees);
                        cam.near_plane = f(j, "near", cam.near_plane);
                        cam.far_plane = f(j, "far", cam.far_plane);
                        cam.orthographic = b(j, "orthographic", cam.orthographic);
                        cam.ortho_size = f(j, "ortho_size", cam.ortho_size);
                        cam.active = b(j, "active", cam.active);
                        cam.clear_color = color(j, "clear_color", cam.clear_color);
                        world.addComponent<ECS::CameraComponent>(entity, cam);
                    }
                    if (c.contains("controller") && c["controller"].is_object())
                    {
                        const Json &j = c["controller"];
                        ECS::CameraControllerComponent ctrl;
                        const std::string mode = lower(s(j, "mode", "orbit"));
                        ctrl.mode = mode == "fly" ? ECS::CameraControllerComponent::Mode::Fly
                                   : mode == "none" ? ECS::CameraControllerComponent::Mode::None
                                                    : ECS::CameraControllerComponent::Mode::Orbit;
                        ctrl.orbit_target = vec3(j, "target", ctrl.orbit_target);
                        ctrl.orbit_distance = f(j, "distance", ctrl.orbit_distance);
                        ctrl.orbit_yaw_degrees = f(j, "yaw", ctrl.orbit_yaw_degrees);
                        ctrl.orbit_pitch_degrees = f(j, "pitch", ctrl.orbit_pitch_degrees);
                        ctrl.move_speed = f(j, "move_speed", ctrl.move_speed);
                        world.addComponent<ECS::CameraControllerComponent>(entity, ctrl);
                    }
                    if (c.contains("light") && c["light"].is_object())
                    {
                        const Json &j = c["light"];
                        ECS::LightComponent light;
                        const std::string type = lower(s(j, "type", "directional"));
                        light.type = type == "point" ? ECS::LightType::Point
                                     : type == "spot" ? ECS::LightType::Spot
                                                      : ECS::LightType::Directional;
                        light.color = color(j, "color", light.color);
                        light.intensity = f(j, "intensity", light.intensity);
                        light.range = f(j, "range", light.range);
                        light.spot_inner_degrees = f(j, "inner_angle", light.spot_inner_degrees);
                        light.spot_outer_degrees = f(j, "outer_angle", light.spot_outer_degrees);
                        light.active = b(j, "active", true);
                        world.addComponent<ECS::LightComponent>(entity, light);
                    }
                    if (c.contains("environment") && c["environment"].is_object())
                    {
                        const Json &j = c["environment"];
                        ECS::EnvironmentComponent env;
                        const std::string hdr = s(j, "hdr");
                        if (!hdr.empty())
                        {
                            const std::string full = resolve(root, hdr);
                            env.hdr_equirect_path = fileExists(full) ? full : "";
                        }
                        env.env_intensity = f(j, "intensity", env.env_intensity);
                        env.env_rotation_degrees = f(j, "rotation", env.env_rotation_degrees);
                        env.show_skybox = b(j, "skybox", true);
                        env.use_ibl = b(j, "ibl", true);
                        env.ibl_dirty = true;
                        world.addComponent<ECS::EnvironmentComponent>(entity, env);
                    }
                    if (c.contains("rigidbody") && c["rigidbody"].is_object())
                    {
                        const Json &j = c["rigidbody"];
                        ECS::RigidBodyComponent rb;
                        rb.type = bodyTypeFrom(s(j, "type", "dynamic"));
                        rb.mass = f(j, "mass", rb.mass);
                        rb.restitution = f(j, "restitution", rb.restitution);
                        rb.friction = f(j, "friction", rb.friction);
                        rb.linear_damping = f(j, "linear_damping", rb.linear_damping);
                        rb.angular_damping = f(j, "angular_damping", rb.angular_damping);
                        rb.gravity_scale = f(j, "gravity_scale", rb.gravity_scale);
                        rb.use_gravity = b(j, "use_gravity", true);
                        rb.linear_velocity = vec3(j, "linear_velocity");
                        rb.angular_velocity = vec3(j, "angular_velocity");
                        rb.allow_sleep = b(j, "allow_sleep", true);
                        world.addComponent<ECS::RigidBodyComponent>(entity, rb);
                        ++result.physics_bodies;
                    }
                    if (c.contains("collider") && c["collider"].is_object())
                    {
                        const Json &j = c["collider"];
                        ECS::ColliderComponent col;
                        col.shape = shapeFrom(s(j, "shape", "box"));
                        col.half_extents = vec3(j, "half_extents", col.half_extents);
                        col.radius = f(j, "radius", col.radius);
                        col.height = f(j, "height", col.height);
                        col.center = vec3(j, "center");
                        col.is_trigger = b(j, "is_trigger", false);
                        col.debug_draw = b(j, "debug_draw", true);
                        world.addComponent<ECS::ColliderComponent>(entity, col);
                    }
                    if (c.contains("hinge") && c["hinge"].is_object())
                    {
                        const Json &j = c["hinge"];
                        ECS::HingeJointComponent hinge;
                        if (j.contains("other") && j["other"].is_number())
                            hinge.other = (ECS::EntityId)j["other"].get<int64_t>();
                        hinge.anchor = vec3(j, "anchor", transform.position);
                        hinge.axis = vec3(j, "axis", hinge.axis);
                        hinge.enable_limit = b(j, "enable_limit", false);
                        hinge.min_angle_degrees = f(j, "min_angle", hinge.min_angle_degrees);
                        hinge.max_angle_degrees = f(j, "max_angle", hinge.max_angle_degrees);
                        hinge.enable_motor = b(j, "enable_motor", false);
                        hinge.motor_speed = f(j, "motor_speed", 0.0f);
                        hinge.motor_force = f(j, "motor_force", 0.0f);
                        world.addComponent<ECS::HingeJointComponent>(entity, hinge);
                    }
                    if (c.contains("text") && c["text"].is_object())
                    {
                        const Json &j = c["text"];
                        ECS::TextComponent text;
                        text.text = s(j, "text", "Text");
                        text.size = f(j, "size", text.size);
                        text.color = color(j, "color", text.color);
                        text.offset = vec3(j, "offset");
                        text.outline = f(j, "outline", text.outline);
                        text.outline_color = color(j, "outline_color", text.outline_color);
                        text.billboard = b(j, "billboard", true);
                        text.screen_space = b(j, "screen_space", false);
                        text.depth_test = b(j, "depth_test", true);
                        text.visible = b(j, "visible", true);
                        world.addComponent<ECS::TextComponent>(entity, text);
                    }
                    if (c.contains("tag") && c["tag"].is_object())
                    {
                        ECS::TagComponent tag;
                        tag.tag = s(c["tag"], "tag", "tag");
                        tag.layer = (uint32_t)f(c["tag"], "layer", 0.0f);
                        world.addComponent<ECS::TagComponent>(entity, tag);
                    }
                    if (c.contains("script") && c["script"].is_object())
                    {
                        const Json &j = c["script"];
                        ECS::ScriptComponent sc;
                        const std::string path = s(j, "path");
                        if (!path.empty()) sc.script_path = resolve(root, path);
                        sc.inline_code = s(j, "code");
                        sc.asset_root = root;
                        sc.language = lower(s(j, "language", "lua")) == "js" || lower(s(j, "language")) == "javascript"
                                          ? ECS::ScriptLanguage::JavaScript
                                          : ECS::ScriptLanguage::Lua;
                        if (sc.language == ECS::ScriptLanguage::JavaScript && !engine.hasJS() && !sc.script_path.empty())
                        {
                            std::string lua_path = sc.script_path;
                            const size_t dot = lua_path.find_last_of('.');
                            if (dot != std::string::npos)
                            {
                                lua_path = lua_path.substr(0, dot) + ".lua";
                                if (fileExists(lua_path)) { sc.script_path = lua_path; sc.language = ECS::ScriptLanguage::Lua; }
                            }
                        }
                        sc.enabled = b(j, "enabled", true);
                        world.addComponent<ECS::ScriptComponent>(entity, sc);
                        ++result.scripts;
                    }
                }

                // Shorthand: {"script": "scripts/x.js", "language": "js"}
                if (node.contains("script") && node["script"].is_string())
                {
                    ECS::ScriptComponent sc;
                    sc.script_path = resolve(root, node["script"].get<std::string>());
                    sc.asset_root = root;
                    sc.language = lower(s(node, "language", "lua")) == "js" || lower(s(node, "language")) == "javascript"
                                      ? ECS::ScriptLanguage::JavaScript
                                      : ECS::ScriptLanguage::Lua;
                    world.addComponent<ECS::ScriptComponent>(entity, sc);
                    ++result.scripts;
                }

                by_name[name] = entity;
                ++result.entities;
                return entity;
            }
        } // namespace

        LoadResult loadFromText(Engine &engine, const std::string &json_text, const std::string &asset_root,
                                const std::string &debug_name)
        {
            LoadResult result;
            Json document;
            try
            {
                document = Json::parse(json_text);
            }
            catch (const std::exception &ex)
            {
                result.error = std::string("JSON parse error in ") + debug_name + ": " + ex.what();
                GEN_LOG_ERROR("[Scene] %s", result.error.c_str());
                return result;
            }

            Render::RenderSystem &render = engine.render();
            Render::RenderSettings &settings = render.settings();
            ECS::World &world = engine.world();
            const std::string root = asset_root;

            // ---- render settings ----------------------------------------
            if (document.contains("render") && document["render"].is_object())
            {
                const Json &j = document["render"];
                const std::string tonemap = lower(s(j, "tonemap"));
                if (tonemap == "none") settings.tonemap_mode = Render::TonemapMode::None;
                if (tonemap == "reinhard") settings.tonemap_mode = Render::TonemapMode::Reinhard;
                if (tonemap == "aces") settings.tonemap_mode = Render::TonemapMode::Aces;
                const std::string shading = lower(s(j, "shading"));
                if (shading == "blinn_phong" || shading == "blinn") settings.shading_mode = Render::ShadingMode::BlinnPhong;
                if (shading == "unlit") settings.shading_mode = Render::ShadingMode::Unlit;
                if (shading == "cook_torrance" || shading == "pbr") settings.shading_mode = Render::ShadingMode::CookTorrance;
                settings.exposure = f(j, "exposure", settings.exposure);
                settings.gamma = f(j, "gamma", settings.gamma);
                settings.saturation = f(j, "saturation", settings.saturation);
                settings.vignette = f(j, "vignette", settings.vignette);
                settings.ambient_color = color(j, "ambient", settings.ambient_color);
                settings.ambient_intensity = f(j, "ambient_intensity", settings.ambient_intensity);
                settings.env_intensity = f(j, "env_intensity", settings.env_intensity);
                settings.env_rotation_degrees = f(j, "env_rotation", settings.env_rotation_degrees);
                settings.use_ibl = b(j, "ibl", settings.use_ibl);
                settings.show_skybox = b(j, "skybox", settings.show_skybox);
                settings.frustum_culling = b(j, "frustum_culling", settings.frustum_culling);
                settings.draw_text_components = b(j, "text_components", settings.draw_text_components);
                const std::string pipeline = lower(s(j, "pipeline", "forward"));
                settings.pipeline_mode = pipeline == "custom" ? Render::PipelineMode::Custom : Render::PipelineMode::Forward;
                if (j.contains("post_effect") && j["post_effect"].is_string())
                {
                    Render::PostEffect effect;
                    if (Render::postEffectFromName(j["post_effect"].get<std::string>(), effect)) settings.post_effect = effect;
                }
            }

            // ---- environment --------------------------------------------
            if (document.contains("environment") && document["environment"].is_object())
            {
                const Json &j = document["environment"];
                ECS::Entity env_entity = world.createEntity("Environment");
                world.addComponent<ECS::TransformComponent>(env_entity);
                ECS::EnvironmentComponent env;
                const std::string hdr = s(j, "hdr");
                if (!hdr.empty())
                {
                    const std::string full = resolve(root, hdr);
                    env.hdr_equirect_path = fileExists(full) ? full : "";
                }
                env.env_intensity = f(j, "intensity", 1.0f);
                env.env_rotation_degrees = f(j, "rotation", 0.0f);
                env.show_skybox = b(j, "skybox", true);
                env.use_ibl = b(j, "ibl", true);
                env.ibl_dirty = true;
                world.addComponent<ECS::EnvironmentComponent>(env_entity, env);
                ++result.entities;
            }

            // ---- physics settings ---------------------------------------
            if (document.contains("physics") && document["physics"].is_object())
            {
                const Json &j = document["physics"];
                Physics::PhysicsSettings &ps = engine.physics().settings();
                ps.gravity = vec3(j, "gravity", ps.gravity);
                ps.fixed_timestep = f(j, "fixed_timestep", ps.fixed_timestep);
                ps.solver_iterations = (uint32_t)std::max(1.0f, f(j, "iterations", (float)ps.solver_iterations));
                ps.enabled = b(j, "enabled", true);
                ps.debug_draw = b(j, "debug_draw", false);
                ps.sleeping = b(j, "sleeping", true);
            }

            // ---- materials ----------------------------------------------
            // Both inline material arrays and standalone *.material.json
            // files are supported; the packer ships the latter so artists can
            // iterate on shaders/materials independently of the scene.
            std::vector<Json> material_defs;
            if (document.contains("materials") && document["materials"].is_array())
                for (const Json &j : document["materials"]) material_defs.push_back(j);
            if (document.contains("material_files") && document["material_files"].is_array())
            {
                for (const Json &path_value : document["material_files"])
                {
                    if (!path_value.is_string()) continue;
                    const std::string full = resolve(root, path_value.get<std::string>());
                    std::string text;
                    if (!ResourceIO::readFileText(full, text)) continue;
                    try
                    {
                        Json file = Json::parse(text);
                        if (file.is_array()) for (const Json &j : file) material_defs.push_back(j);
                        else if (file.contains("materials") && file["materials"].is_array())
                            for (const Json &j : file["materials"]) material_defs.push_back(j);
                        else material_defs.push_back(file);
                    }
                    catch (const std::exception &ex)
                    {
                        GEN_LOG_WARN("[Scene] material file %s: %s", full.c_str(), ex.what());
                    }
                }
            }
            {
                for (const Json &j : material_defs)
                {
                    if (!j.is_object()) continue;
                    Render::MaterialPtr material = render.scene().createMaterial(s(j, "name", "material"));
                    material->base_color = color(j, "base_color", Math::Color(1, 1, 1, 1));
                    material->metallic = f(j, "metallic", 0.0f);
                    material->roughness = f(j, "roughness", 0.5f);
                    material->emissive = color(j, "emissive", Math::Color(0, 0, 0, 1));
                    material->emissive_strength = f(j, "emissive_strength", 1.0f);
                    material->normal_scale = f(j, "normal_scale", 1.0f);
                    material->occlusion_strength = f(j, "occlusion_strength", 1.0f);
                    material->alpha_cutoff = f(j, "alpha_cutoff", 0.5f);
                    material->uv_scale = vec2(j, "uv_scale", Math::Vec2(1.0f, 1.0f));
                    material->uv_offset = vec2(j, "uv_offset", Math::Vec2(0.0f, 0.0f));
                    material->shading = shadingFrom(s(j, "shading", "default"));
                    material->blend = blendFrom(s(j, "blend", "opaque"));
                    material->cull = cullFrom(s(j, "cull", "back"));
                    material->double_sided = b(j, "double_sided", false);
                    material->depth_test = b(j, "depth_test", true);
                    material->depth_write = b(j, "depth_write", true);
                    if (j.contains("textures") && j["textures"].is_object())
                    {
                        for (auto it = j["textures"].begin(); it != j["textures"].end(); ++it)
                        {
                            Render::TextureSlot slot;
                            if (!Render::textureSlotFromName(it.key(), slot) || !it.value().is_string()) continue;
                            const std::string full = resolve(root, it.value().get<std::string>());
                            if (!material->setTexture(slot, full))
                                GEN_LOG_WARN("[Scene] texture '%s' missing for material '%s'", full.c_str(),
                                             material->name.c_str());
                        }
                    }
                    material->touch();
                    ++result.materials;
                }
            }

            // ---- entities -----------------------------------------------
            std::unordered_map<std::string, ECS::Entity> by_name;
            std::vector<std::pair<std::string, ECS::Entity>> parent_links;
            if (document.contains("entities") && document["entities"].is_array())
            {
                for (const Json &j : document["entities"])
                {
                    if (!j.is_object()) continue;
                    ECS::Entity entity = buildEntity(engine, j, root, by_name, result);
                    const std::string parent = s(j, "parent");
                    if (!parent.empty()) parent_links.push_back({parent, entity});
                    if (j.contains("hinge_other"))
                    {
                        if (ECS::HingeJointComponent *hinge = world.getComponent<ECS::HingeJointComponent>(entity))
                            hinge->other = (ECS::EntityId)j["hinge_other"].get<int64_t>();
                    }
                }
            }
            for (const auto &link : parent_links)
            {
                auto it = by_name.find(link.first);
                if (it != by_name.end()) world.setParent(link.second, it->second);
            }

            result.ok = true;
            GEN_LOG_INFO("[Scene] '%s': %zu entities, %zu materials, %zu scripts, %zu physics bodies",
                         debug_name.c_str(), result.entities, result.materials, result.scripts, result.physics_bodies);
            return result;
        }

        LoadResult loadFromFile(Engine &engine, const std::string &scene_path, const std::string &asset_root)
        {
            LoadResult result;
            std::string text;
            if (!ResourceIO::readFileText(scene_path, text))
            {
                result.error = "cannot read scene: " + scene_path;
                GEN_LOG_ERROR("[Scene] %s", result.error.c_str());
                return result;
            }
            return loadFromText(engine, text, asset_root, ResourceIO::pathFilename(scene_path));
        }

    } // namespace Scene
} // namespace Genesis
