/*
 * Genesis Render 2.0 - demo application
 *
 * Exercises every capability that was added to the engine:
 *
 *   - ECS world with Transform / Mesh / Camera / Light / Environment /
 *     Script / CameraController components and an ordered system scheduler
 *   - multi format model import: glTF (genesis_sphere.gltf), GLB
 *     (DamagedHelmet.glb), binary FBX (genesis_test_cube.fbx /
 *     genesis_test_sphere.fbx) and a procedural material grid
 *   - PBR rendering with direct lights (Cook-Torrance GGX *and* Blinn-Phong,
 *     both with Fresnel / specular / diffuse / metallic terms) and indirect
 *     light (cubemap -> irradiance -> prefiltered specular -> BRDF LUT)
 *   - real time ImGui UI (viewport stats, render settings, hierarchy,
 *     lights, environment, Lua console, log)
 *   - Lua scripting driving the scene every frame
 *   - OpenGL and Vulkan backends, selected with --backend
 *
 * Command line:
 *   --backend gl|vulkan     rendering backend (default gl)
 *   --headless              hidden window, no presentation (CI friendly)
 *   --frames N              exit after N frames
 *   --screenshot PATH.png   capture a frame to disk and exit
 *   --showcase DIR          render a labelled set of proof images (headless)
 *   --width / --height      window size (default 1280x720)
 *   --validation            enable Vulkan validation layers
 *   --no-ui                 skip ImGui
 *   --no-script             skip Lua
 *   --asset-dir PATH        asset root (default: compile time GENESIS_ASSET_DIR)
 */
#include "engine/engine.hpp"
#include "foundation_layer/genesis_log.hpp"
#include "resource/procedural_mesh.hpp"
#include "resource/resource_types.hpp"

#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Genesis;

namespace
{
#ifndef GENESIS_ASSET_DIR
#define GENESIS_ASSET_DIR "assets"
#endif

    struct DemoOptions
    {
        RHI::BackendType backend = RHI::BackendType::OpenGL;
        bool headless = false;
        bool validation = false;
        bool enable_ui = true;
        bool enable_script = true;
        uint32_t frames = 0;
        uint32_t width = 1280;
        uint32_t height = 720;
        std::string screenshot;
        std::string showcase_dir;   // --showcase DIR : render a labelled proof-image set
        std::string asset_dir = GENESIS_ASSET_DIR;
    };

    DemoOptions parseArgs(int argc, char **argv)
    {
        DemoOptions o;
        for (int i = 1; i < argc; ++i)
        {
            std::string a = argv[i];
            auto next = [&](const char *fallback) -> std::string
            { return (i + 1 < argc) ? argv[++i] : fallback; };

            if (a == "--backend") { std::string v = next("gl");
                if (v == "vulkan" || v == "vk") o.backend = RHI::BackendType::Vulkan;
                else if (v == "metal") o.backend = RHI::BackendType::Metal;
                else o.backend = RHI::BackendType::OpenGL; }
            else if (a == "--headless") o.headless = true;
            else if (a == "--validation") o.validation = true;
            else if (a == "--no-ui") o.enable_ui = false;
            else if (a == "--no-script") o.enable_script = false;
            else if (a == "--frames") o.frames = (uint32_t)std::atoi(next("0").c_str());
            else if (a == "--width") o.width = (uint32_t)std::atoi(next("1280").c_str());
            else if (a == "--height") o.height = (uint32_t)std::atoi(next("720").c_str());
            else if (a == "--screenshot") o.screenshot = next("");
            else if (a == "--showcase") o.showcase_dir = next("docs/images");
            else if (a == "--asset-dir") o.asset_dir = next(GENESIS_ASSET_DIR);
            else if (a == "--help" || a == "-h")
            {
                std::printf("usage: genesis_demo [--backend gl|vulkan] [--headless] [--frames N]\n"
                            "                    [--screenshot out.png] [--showcase DIR] [--width W] [--height H]\n"
                            "                    [--validation] [--no-ui] [--no-script] [--asset-dir DIR]\n"
                            "\n"
                            "  --showcase DIR  headless: render a set of labelled PNGs that each\n"
                            "                  isolate one capability (PBR material grid, BRDF models,\n"
                            "                  IBL on/off, tone mapping, wireframe, imported models).\n");
            }
        }
        if (!o.screenshot.empty() && o.frames == 0) o.frames = 2;
        return o;
    }

    //======================================================================
    // Demo application
    //======================================================================
    class DemoApp
    {
    public:
        Engine engine;
        DemoOptions options;

        ECS::Entity camera_entity;
        ECS::Entity sun_entity;
        ECS::Entity point_a_entity;
        ECS::Entity point_b_entity;
        ECS::Entity environment_entity;
        std::vector<ECS::Entity> model_entities;

        int selected_entity = -1;
        float sun_intensity = 4.0f;
        float sun_elevation = 45.0f;
        float sun_azimuth = 35.0f;
        Math::Color sun_color = Math::Color(1.0f, 0.96f, 0.90f);
        char console_buffer[512] = "";
        bool animate_lights = true;
        float elapsed = 0.0f;

        // Live material editor (material index stored from buildScene).
        int editable_material_index = -1;
        float material_base_color[3] = {0.2f, 0.6f, 1.0f};
        float material_metallic = 0.3f;
        float material_roughness = 0.35f;
        // Overlay / CommandBuffer demo toggles.
        bool overlay_demo = true;
        int post_effect = 0; // 0 none, 1 invert, 2 scanline
        bool show_3d_text = true;

        std::string asset(const std::string &relative) const
        {
            return ResourceIO::joinPath(options.asset_dir, relative);
        }

        //--------------------------------------------------------------
        bool initialize()
        {
            EngineConfig cfg;
            cfg.app_name = "Genesis Render 2.0 - PBR / ECS / GL+Vulkan demo";
            cfg.asset_dir = options.asset_dir;
            cfg.width = options.width;
            cfg.height = options.height;
            cfg.backend = options.backend;
            cfg.headless = options.headless;
            cfg.enable_validation = options.validation;
            cfg.enable_ui = options.enable_ui;
            cfg.enable_script = options.enable_script;
            cfg.max_frames = options.frames;
            cfg.vsync = options.headless ? false : true;
            if (!engine.initialize(cfg)) return false;

            buildScene();
            engine.setUiDrawCallback([this](Engine &e) { drawUI(e); });
            return true;
        }

        //--------------------------------------------------------------
        void buildScene()
        {
            ECS::World &world = engine.world();

            // ---- camera -------------------------------------------------
            camera_entity = world.createEntity("MainCamera");
            {
                ECS::TransformComponent t;
                t.position = Math::Vec3(3.2f, 2.4f, 6.0f);
                world.addComponent<ECS::TransformComponent>(camera_entity, t);

                ECS::CameraComponent cam;
                cam.fov_degrees = 55.0f;
                cam.near_plane = 0.05f;
                cam.far_plane = 300.0f;
                cam.clear_color = Math::Color(0.02f, 0.025f, 0.035f, 1.0f);
                cam.enable_tonemap = true;
                world.addComponent<ECS::CameraComponent>(camera_entity, cam);

                ECS::CameraControllerComponent ctrl;
                ctrl.mode = ECS::CameraControllerComponent::Mode::Orbit;
                ctrl.orbit_target = Math::Vec3(0.0f, 1.0f, 0.0f);
                ctrl.orbit_distance = 7.5f;
                ctrl.orbit_yaw_degrees = 25.0f;
                ctrl.orbit_pitch_degrees = 18.0f;
                world.addComponent<ECS::CameraControllerComponent>(camera_entity, ctrl);
            }

            // ---- sun (directional light) --------------------------------
            sun_entity = world.createEntity("Sun");
            {
                world.addComponent<ECS::TransformComponent>(sun_entity);
                ECS::LightComponent light;
                light.type = ECS::LightType::Directional;
                light.color = sun_color;
                light.intensity = sun_intensity;
                world.addComponent<ECS::LightComponent>(sun_entity, light);
                updateSunDirection();
            }

            // ---- two animated point lights ------------------------------
            point_a_entity = world.createEntity("PointLightWarm");
            {
                ECS::TransformComponent t;
                t.position = Math::Vec3(2.5f, 1.6f, 1.5f);
                world.addComponent<ECS::TransformComponent>(point_a_entity, t);
                ECS::LightComponent light;
                light.type = ECS::LightType::Point;
                light.color = Math::Color(1.0f, 0.55f, 0.25f);
                light.intensity = 12.0f;
                light.range = 12.0f;
                world.addComponent<ECS::LightComponent>(point_a_entity, light);
            }
            point_b_entity = world.createEntity("PointLightCool");
            {
                ECS::TransformComponent t;
                t.position = Math::Vec3(-2.5f, 1.4f, -1.0f);
                world.addComponent<ECS::TransformComponent>(point_b_entity, t);
                ECS::LightComponent light;
                light.type = ECS::LightType::Point;
                light.color = Math::Color(0.35f, 0.65f, 1.0f);
                light.intensity = 10.0f;
                light.range = 12.0f;
                world.addComponent<ECS::LightComponent>(point_b_entity, light);
            }

            // ---- environment (IBL + skybox) ------------------------------
            environment_entity = world.createEntity("Environment");
            {
                ECS::EnvironmentComponent env;
                std::string hdr = asset("textures/sky_equirect.hdr");
                env.hdr_equirect_path = ResourceIO::fileExists(hdr) ? hdr : "";
                env.cubemap_size = 256;
                env.irradiance_size = 64;
                env.prefiltered_size = 256;
                env.brdf_lut_size = 256;
                env.env_intensity = 1.0f;
                env.show_skybox = true;
                env.use_ibl = true;
                env.ibl_dirty = true;
                world.addComponent<ECS::EnvironmentComponent>(environment_entity, env);
            }

            // ---- ground plane (checker + normal + AO) --------------------
            {
                MeshData plane = Procedural::makePlane(24.0f, 24.0f, 1);
                MaterialData mat;
                mat.name = "ground";
                mat.base_color_factor = Math::Color(1, 1, 1, 1);
                mat.metallic_factor = 1.0f;
                mat.roughness_factor = 1.0f;
                mat.normal_scale = 0.8f;
                mat.base_color = textureRef(asset("textures/checker_albedo.png"), true);
                mat.metallic_roughness = textureRef(asset("textures/checker_metallic_roughness.png"), false);
                mat.normal = textureRef(asset("textures/panel_normal.png"), false);
                mat.occlusion = textureRef(asset("textures/tile_ao.png"), false);
                mat.emissive = textureRef(asset("textures/grid_emissive.png"), true);
                auto model = Procedural::wrapMesh(plane, mat, "ground");
                spawnModel(model, "Ground", Math::Vec3(0, 0, 0), 1.0f);
            }

            // ---- imported models ----------------------------------------
            spawnFromFile(asset("models/DamagedHelmet.glb"), "DamagedHelmet", Math::Vec3(0.0f, 1.35f, 0.0f), 1.0f);
            spawnFromFile(asset("models/genesis_sphere.glb"), "GenesisSphere", Math::Vec3(-2.6f, 0.9f, 0.8f), 0.85f);
            spawnFromFile(asset("models/genesis_torus.gltf"), "GenesisTorus", Math::Vec3(2.6f, 1.2f, 0.6f), 0.8f);
            spawnFromFile(asset("models/genesis_cube_textured.gltf"), "TexturedCube", Math::Vec3(0.3f, 0.9f, 2.8f), 0.6f);
            spawnFromFile(asset("models/Duck.glb"), "Duck", Math::Vec3(-1.4f, 0.6f, 2.6f), 0.012f);
            // FBX (native parser: binary FBX + zlib compressed arrays)
            spawnFromFile(asset("models/genesis_test_cube.fbx"), "FbxCube", Math::Vec3(-3.2f, 0.6f, -2.0f), 1.0f);
            spawnFromFile(asset("models/genesis_test_sphere.fbx"), "FbxSphere", Math::Vec3(3.2f, 0.9f, -2.2f), 1.0f);

            // ---- procedural PBR material grid ---------------------------
            {
                ModelData grid = Procedural::makeMaterialGrid(6, 4, 1.15f, 0.42f);
                spawnModel(std::make_shared<ModelData>(grid), "MaterialGrid", Math::Vec3(0.0f, 1.6f, -5.5f), 1.0f);
            }

            // ---- live editable material sphere (real-time material UI) --
            {
                MeshData sphere = Procedural::makeSphere(0.5f, 32, 20);
                MaterialData mat;
                mat.name = "editable";
                mat.base_color_factor = Math::Color(material_base_color[0], material_base_color[1], material_base_color[2], 1.0f);
                mat.metallic_factor = material_metallic;
                mat.roughness_factor = material_roughness;
                auto model = Procedural::wrapMesh(sphere, mat, "editable_sphere");
                ECS::Entity e = world.createEntity("EditableSphere");
                ECS::TransformComponent t;
                t.position = Math::Vec3(0.0f, 0.75f, 0.0f);
                world.addComponent<ECS::TransformComponent>(e, t);
                ECS::MeshComponent mesh;
                mesh.model = model;
                mesh.pick_id = e.id + 1;
                world.addComponent<ECS::MeshComponent>(e, mesh);
                std::vector<uint32_t> gpu = engine.render().scene().uploadModel(*model, "editable_sphere");
                Render::RenderSystem::EntityMeshBinding binding;
                binding.entity = e;
                binding.mesh_indices = gpu;
                engine.render().bindEntityMeshes(binding);
                // The sphere uses one material; its index is the last uploaded.
                editable_material_index = (int)engine.render().scene().materialCount() - 1;
                model_entities.push_back(e);
            }

            // ---- Lua driven entity ---------------------------------------
            if (engine.hasScript())
            {
                ECS::Entity scripted = world.createEntity("LuaAnimator");
                world.addComponent<ECS::TransformComponent>(scripted);
                ECS::ScriptComponent script;
                std::string path = asset("scripts/demo_anim.lua");
                if (ResourceIO::fileExists(path)) script.script_path = path;
                else script.inline_code =
                    "function start(id)\n"
                    "  genesis.log_info('LuaAnimator online (inline fallback)')\n"
                    "end\n"
                    "function tick(dt, id)\n"
                    "  local e = genesis.entity_find('GenesisTorus')\n"
                    "  if e then genesis.rotate(e, 0.0, 40.0 * dt, 12.0 * dt) end\n"
                    "end\n";
                script.enabled = true;
                world.addComponent<ECS::ScriptComponent>(scripted, script);

                // Full capability demo: adds its own lights/meshes, records
                // free command-buffer draws, SDF text and a post effect.
                ECS::Entity full = world.createEntity("LuaFullDemo");
                world.addComponent<ECS::TransformComponent>(full);
                ECS::ScriptComponent full_script;
                std::string full_path = asset("scripts/full_demo.lua");
                if (ResourceIO::fileExists(full_path)) full_script.script_path = full_path;
                else full_script.inline_code = "function start(id) genesis.log_info('full demo fallback') end\n";
                world.addComponent<ECS::ScriptComponent>(full, full_script);
            }

            GEN_LOG_INFO("[Demo] scene built: %zu entities, %zu models",
                         world.entityCount(), model_entities.size());
        }

        TextureRef textureRef(const std::string &path, bool srgb)
        {
            TextureRef ref;
            if (ResourceIO::fileExists(path))
            {
                ref.path = path;
                ref.srgb = srgb;
                ref.valid = true;
            }
            else
            {
                GEN_LOG_WARN("[Demo] missing texture '%s' (run scripts/gen_assets.py)", path.c_str());
            }
            return ref;
        }

        void spawnFromFile(const std::string &path, const std::string &name, const Math::Vec3 &position, float scale)
        {
            if (!ResourceIO::fileExists(path))
            {
                GEN_LOG_WARN("[Demo] model not found: %s", path.c_str());
                return;
            }
            ECS::Entity e = engine.spawnModel(path, position, scale, name);
            if (e.valid()) model_entities.push_back(e);
        }

        void spawnModel(std::shared_ptr<ModelData> model, const std::string &name, const Math::Vec3 &position, float scale)
        {
            ECS::World &world = engine.world();
            ECS::Entity e = world.createEntity(name);
            ECS::TransformComponent t;
            t.position = position;
            t.scale = Math::Vec3(scale);
            world.addComponent<ECS::TransformComponent>(e, t);

            ECS::MeshComponent mesh;
            mesh.model = model;
            mesh.pick_id = e.id + 1;
            world.addComponent<ECS::MeshComponent>(e, mesh);

            std::vector<uint32_t> gpu = engine.render().scene().uploadModel(*model, name);
            Render::RenderSystem::EntityMeshBinding binding;
            binding.entity = e;
            binding.mesh_indices = gpu;
            engine.render().bindEntityMeshes(binding);
            model_entities.push_back(e);
        }

        void updateSunDirection()
        {
            ECS::TransformComponent *t = engine.world().getComponent<ECS::TransformComponent>(sun_entity);
            if (!t) return;
            float el = Math::toRadians(sun_elevation);
            float az = Math::toRadians(sun_azimuth);
            // The light shines along -Z of its transform, so aim the -Z axis at
            // the direction pointing from the sun towards the origin.
            Math::Vec3 dir(std::cos(el) * std::sin(az), -std::sin(el), std::cos(el) * std::cos(az));
            t->rotation = Math::Quat::lookRotation(dir, Math::Vec3(0, 1, 0));
            t->local_dirty = true;

            ECS::LightComponent *light = engine.world().getComponent<ECS::LightComponent>(sun_entity);
            if (light)
            {
                light->intensity = sun_intensity;
                light->color = sun_color;
            }
        }

        //--------------------------------------------------------------
        void tick(float dt)
        {
            elapsed += dt;
            updateSunDirection();

            if (animate_lights)
            {
                ECS::World &world = engine.world();
                if (ECS::TransformComponent *t = world.getComponent<ECS::TransformComponent>(point_a_entity))
                {
                    t->position = Math::Vec3(std::cos(elapsed * 0.7f) * 3.0f, 1.6f + std::sin(elapsed * 1.3f) * 0.4f,
                                             std::sin(elapsed * 0.7f) * 3.0f);
                    t->local_dirty = true;
                }
                if (ECS::TransformComponent *t = world.getComponent<ECS::TransformComponent>(point_b_entity))
                {
                    t->position = Math::Vec3(std::cos(elapsed * 0.5f + 2.4f) * 3.4f, 1.3f,
                                             std::sin(elapsed * 0.5f + 2.4f) * 3.4f);
                    t->local_dirty = true;
                }
            }

            // Light gizmos (exercises the RHI push constant path)
            engine.render().clearDebugLines();
            engine.world().each<ECS::LightComponent>([this](ECS::Entity e, ECS::LightComponent &light)
            {
                ECS::TransformComponent *t = engine.world().getComponent<ECS::TransformComponent>(e);
                if (!t || light.type == ECS::LightType::Directional) return;
                Math::Color c = light.color;
                engine.render().drawSphere(t->position, 0.09f, c, 16);
                engine.render().drawLine(t->position, t->position + Math::Vec3(0, 0.35f, 0), c);
            });

            // ---- live material updates (real-time material editor) ----
            if (editable_material_index >= 0)
            {
                engine.render().scene().setMaterialBaseColor(editable_material_index,
                    Math::Color(material_base_color[0], material_base_color[1], material_base_color[2], 1.0f));
                engine.render().scene().setMaterialPBR(editable_material_index, material_metallic, material_roughness);
            }

            // ---- CommandBuffer: free draws + SDF text (organise step) ----
            if (overlay_demo)
            {
                auto &ov = engine.render().overlay();
                ov.clearCommands();
                ov.postEffect((Render::PostEffect)(uint32_t)post_effect);

                // 2D free draws (screen space): a translucent HUD strip.
                float sw = (float)engine.render().settings().hdr_width;
                float sh = (float)engine.render().settings().hdr_height;
                ov.fill2D(8.0f, sh - 40.0f, 250.0f, 22.0f, 0.05f, 0.06f, 0.09f, 0.6f);
                ov.line2D(12.0f, sh - 40.0f, 240.0f, sh - 18.0f + std::sin(elapsed * 2.0f) * 6.0f,
                          0.2f, 0.8f, 1.0f, 0.9f);

                // 3D free draw: a small floating quad in world space.
                ov.quad3D(-1.4f, 2.2f, 0.0f, 0.4f, 0.02f, 0.9f, 0.4f, 0.1f, 0.9f);

                // SDF 2D text (HUD, after tone mapping).
                ov.text().drawText2D(20.0f, 20.0f, 26.0f, "Genesis Render",
                    Math::Color(0.1f, 0.1f, 0.1f, 1.0f), sw, sh);
                ov.text().drawText2D(20.0f, 52.0f, 16.0f,
                    std::string("backend: ") + RHI::backendTypeName(engine.device()->backend()) +
                    "   SDF text / CommandBuffer / programmable post",
                    Math::Color(0.3f, 0.5f, 0.8f, 1.0f), sw, sh);

                // SDF 3D text label above the editable sphere.
                if (show_3d_text)
                {
                    Math::Mat4 label = Math::Mat4::identity();
                    label.m[3] = 0.0f; label.m[7] = 1.65f; label.m[11] = 0.0f;
                    ov.text().drawText3D(label, "LIVE MATERIAL", Math::Color(1.0f, 0.85f, 0.2f, 1.0f), 0.18f);
                    label.m[3] = -1.4f; label.m[7] = 2.45f; label.m[11] = 0.0f;
                    ov.text().drawText3D(label, "3D SDF TEXT", Math::Color(0.4f, 1.0f, 0.7f, 1.0f), 0.15f);
                }
            }
            else
            {
                engine.render().overlay().clearCommands();
                engine.render().overlay().postEffect(Render::PostEffect::None);
            }
        }

        //--------------------------------------------------------------
        void drawUI(Engine &e)
        {
            Render::RenderSettings &settings = e.render().settings();
            const Render::RenderStats &stats = e.render().stats();

            ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(330, 250), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Genesis Render 2.0"))
            {
                ImGui::Text("backend   : %s", RHI::backendTypeName(e.device()->backend()));
                ImGui::Text("device    : %s", e.device()->info().device_name.c_str());
                ImGui::Text("api       : %s", e.device()->info().api_version.c_str());
                ImGui::Separator();
                ImGui::Text("fps       : %.1f", e.fps());
                ImGui::Text("frame     : %.2f ms", e.frameTimeMs());
                ImGui::Text("scene/present: %.2f / %.2f ms", stats.scene_ms, stats.present_ms);
                ImGui::Text("draw calls: %u", stats.draw_calls);
                ImGui::Text("triangles : %u", stats.triangles);
                ImGui::Text("items     : %u opaque, %u blended, %u culled",
                            stats.opaque_items, stats.transparent_items, stats.culled_items);
                ImGui::Text("resources : %zu meshes, %zu materials", e.render().scene().meshCount(),
                            e.render().scene().materialCount());
                ImGui::Text("lights    : %u", stats.active_lights);
                ImGui::Text("IBL       : %s", e.render().iblReady() ? "baked" : "procedural fallback");
                ImGui::Separator();
                ImGui::Text("entities  : %zu", e.world().entityCount());
                if (ImGui::Button("Save screenshot (png)"))
                {
                    std::string path = "screenshot_" + std::string(RHI::backendTypeName(e.device()->backend())) + ".png";
                    if (e.saveScreenshot(path)) e.ui().pushLog("saved " + path);
                }
                ImGui::SameLine();
                if (ImGui::Button("Quit")) e.requestQuit();
            }
            ImGui::End();

            // ---- render settings -----------------------------------------
            ImGui::SetNextWindowPos(ImVec2(8, 270), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(330, 400), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Render / PBR"))
            {
                const char *shading_names[] = {"Cook-Torrance (GGX)", "Blinn-Phong"};
                int shading = (int)settings.shading_mode;
                if (ImGui::RadioButton("Cook-Torrance (GGX)", &shading, 0) | ImGui::RadioButton("Blinn-Phong", &shading, 1))
                    settings.shading_mode = (Render::ShadingMode)shading;
                ImGui::Separator();

                const char *tonemap_names[] = {"None (clamp)", "Reinhard", "ACES filmic"};
                int tonemap = (int)settings.tonemap_mode;
                if (ImGui::Combo("Tone mapping", &tonemap, tonemap_names, 3))
                    settings.tonemap_mode = (Render::TonemapMode)tonemap;
                ImGui::SliderFloat("Exposure", &settings.exposure, 0.05f, 4.0f, "%.2f");
                ImGui::SliderFloat("Gamma", &settings.gamma, 1.0f, 3.0f, "%.2f");
                ImGui::SliderFloat("Saturation", &settings.saturation, 0.0f, 2.0f, "%.2f");
                ImGui::SliderFloat("Vignette", &settings.vignette, 0.0f, 1.0f, "%.2f");
                ImGui::Separator();

                ImGui::Checkbox("Wireframe", &settings.wireframe);
                ImGui::Checkbox("Skybox", &settings.show_skybox);
                ImGui::Checkbox("Frustum culling", &settings.frustum_culling);
                ImGui::Checkbox("Image based lighting", &settings.use_ibl);
                ImGui::SliderFloat("Environment intensity", &settings.env_intensity, 0.0f, 4.0f, "%.2f");
                ImGui::SliderFloat("Environment rotation", &settings.env_rotation_degrees, 0.0f, 360.0f, "%.1f deg");
                ImGui::ColorEdit3("Ambient", &settings.ambient_color.r);
                ImGui::SliderFloat("Ambient intensity", &settings.ambient_intensity, 0.0f, 2.0f, "%.2f");
                ImGui::Separator();

                ImGui::Checkbox("Animate point lights", &animate_lights);
                ImGui::SliderFloat("Sun intensity", &sun_intensity, 0.0f, 20.0f, "%.2f");
                ImGui::SliderFloat("Sun elevation", &sun_elevation, -10.0f, 90.0f, "%.1f deg");
                ImGui::SliderFloat("Sun azimuth", &sun_azimuth, 0.0f, 360.0f, "%.1f deg");
                ImGui::ColorEdit3("Sun colour", &sun_color.r);

                if (ImGui::Button("Re-bake IBL from HDR"))
                {
                    std::string hdr = asset("textures/sky_equirect.hdr");
                    bool ok = ResourceIO::fileExists(hdr) ? e.render().bakeIBL(hdr, {}) : e.render().bakeProceduralIBL({});
                    e.ui().pushLog(ok ? "IBL re-baked" : "IBL bake FAILED");
                }
            }
            ImGui::End();

            // ---- hierarchy ------------------------------------------------
            ImGui::SetNextWindowPos(ImVec2(350, 8), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(300, 320), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Scene hierarchy (ECS)"))
            {
                ImGui::Text("%zu entities, %zu component pools", e.world().entityCount(), e.world().poolCount());
                ImGui::Separator();
                int index = 0;
                for (ECS::Entity entity : e.world().entities())
                {
                    char label[160];
                    std::snprintf(label, sizeof(label), "%s##%u", e.world().entityName(entity).c_str(), entity.id);
                    if (ImGui::Selectable(label, selected_entity == (int)entity.id))
                        selected_entity = (int)entity.id;
                    ++index;
                }
                ImGui::Separator();
                if (selected_entity >= 0)
                {
                    ECS::Entity sel = e.world().entityFromId((ECS::EntityId)selected_entity);
                    if (e.world().isAlive(sel))
                    {
                        ECS::TransformComponent *t = e.world().getComponent<ECS::TransformComponent>(sel);
                        if (t)
                        {
                            ImGui::Text("transform");
                            ImGui::DragFloat3("position", &t->position.x, 0.05f);
                            Math::Vec3 euler = t->eulerAnglesDegrees();
                            if (ImGui::DragFloat3("rotation (deg)", &euler.x, 0.5f))
                                t->setEulerDegrees(euler.x, euler.y, euler.z);
                            ImGui::DragFloat3("scale", &t->scale.x, 0.02f);
                            t->local_dirty = true;
                        }
                        if (ECS::LightComponent *light = e.world().getComponent<ECS::LightComponent>(sel))
                        {
                            ImGui::Text("light: %s", ECS::lightTypeName(light->type));
                            ImGui::SliderFloat("intensity", &light->intensity, 0.0f, 40.0f);
                            ImGui::ColorEdit3("colour", &light->color.r);
                        }
                        if (ECS::MeshComponent *mesh = e.world().getComponent<ECS::MeshComponent>(sel))
                        {
                            ImGui::Text("mesh: %s", mesh->model ? mesh->model->name.c_str() : "-");
                            ImGui::Text("importer: %s", mesh->model ? mesh->model->importer.c_str() : "-");
                            ImGui::Checkbox("visible", &mesh->visible);
                        }
                    }
                    else
                    {
                        selected_entity = -1;
                    }
                }
            }
            ImGui::End();

            // ---- scripting -------------------------------------------------
            if (e.hasScript())
            {
                ImGui::SetNextWindowPos(ImVec2(350, 340), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(300, 210), ImGuiCond_FirstUseEver);
                if (ImGui::Begin("Lua scripting"))
                {
                    Script::ScriptSystem &script = e.script();
                    ImGui::Text("%s", LUA_RELEASE);
                    ImGui::Text("loaded scripts : %zu", script.loadedScripts());
                    ImGui::Text("tick           : %.3f ms", script.lastTickMs());
                    ImGui::Text("errors         : %u", script.errorCount());
                    ImGui::Separator();
                    ImGui::InputText("##console", console_buffer, IM_ARRAYSIZE(console_buffer));
                    ImGui::SameLine();
                    if (ImGui::Button("Run"))
                    {
                        if (console_buffer[0])
                        {
                            script.executeString(console_buffer);
                            e.ui().pushLog(std::string("> ") + console_buffer);
                            console_buffer[0] = '\0';
                        }
                    }
                    ImGui::TextWrapped("Try: genesis.log_info('hello')  or  "
                                       "genesis.render_set_exposure(1.6)");
                    if (!script.errors().empty())
                    {
                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "last error:");
                        ImGui::TextWrapped("%s", script.errors().back().c_str());
                    }
                }
                ImGui::End();
            }

            // ---- material editor + CommandBuffer / text ------------------
            ImGui::SetNextWindowPos(ImVec2(660, 250), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(330, 300), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Material / Overlay / SDF Text"))
            {
                ImGui::Text("live editable sphere (PBR):");
                if (ImGui::ColorEdit3("base color", material_base_color))
                    engine.render().scene().setMaterialBaseColor(editable_material_index,
                        Math::Color(material_base_color[0], material_base_color[1], material_base_color[2], 1.0f));
                ImGui::SliderFloat("metallic", &material_metallic, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("roughness", &material_roughness, 0.0f, 1.0f, "%.2f");
                if (ImGui::Button("apply PBR"))
                    engine.render().scene().setMaterialPBR(editable_material_index, material_metallic, material_roughness);
                ImGui::Separator();

                const char *fx_names[] = {"None", "Invert", "Scanline"};
                ImGui::Combo("programmable post", &post_effect, fx_names, 3);
                ImGui::Checkbox("CommandBuffer / SDF text overlay", &overlay_demo);
                ImGui::SameLine();
                ImGui::Checkbox("3D text", &show_3d_text);
                ImGui::Separator();
                ImGui::TextWrapped("Record free 2D fills/lines and 3D quads (organise) "
                                   "and they are submitted next frame (execute).");
                ImGui::TextWrapped("SDF text: 2D HUD above + 3D world labels.");
            }
            ImGui::End();

            // ---- log -------------------------------------------------------
            ImGui::SetNextWindowPos(ImVec2(1000, 8), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(360, 240), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Log"))
            {
                auto history = Logger::instance().history();
                size_t start = history.size() > 60 ? history.size() - 60 : 0;
                for (size_t i = start; i < history.size(); ++i)
                {
                    ImVec4 color(0.85f, 0.85f, 0.85f, 1.0f);
                    if (history[i].level == LogLevel::Warn) color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
                    else if (history[i].level >= LogLevel::Error) color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                    ImGui::TextColored(color, "%s", history[i].text.c_str());
                }
            }
            ImGui::End();

            // ---- help -------------------------------------------------------
            ImGui::SetNextWindowPos(ImVec2(660, 260), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(360, 150), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Controls"))
            {
                ImGui::BulletText("drag left mouse  : orbit camera");
                ImGui::BulletText("mouse wheel      : zoom");
                ImGui::BulletText("WASD / space/ctrl: fly (switch mode in Lua)");
                ImGui::BulletText("ESC              : quit");
                ImGui::BulletText("F1               : toggle UI");
                ImGui::BulletText("F2               : toggle Blinn-Phong / Cook-Torrance");
                ImGui::BulletText("F3               : toggle wireframe");
                ImGui::BulletText("F4               : toggle IBL");
            }
            ImGui::End();
        }

        //--------------------------------------------------------------
        void handleShortcuts()
        {
            Platform::InputSystem &in = engine.input();
            if (in.ui_wants_keyboard) return;
            Render::RenderSettings &s = engine.render().settings();
            if (in.keyPressed(SDL_SCANCODE_F1)) engine.ui().ui_visible = !engine.ui().ui_visible;
            if (in.keyPressed(SDL_SCANCODE_F2))
                s.shading_mode = (s.shading_mode == Render::ShadingMode::CookTorrance) ? Render::ShadingMode::BlinnPhong
                                                                                       : Render::ShadingMode::CookTorrance;
            if (in.keyPressed(SDL_SCANCODE_F3)) s.wireframe = !s.wireframe;
            if (in.keyPressed(SDL_SCANCODE_F4)) s.use_ibl = !s.use_ibl;
        }
    };

} // namespace


    //======================================================================
    // --showcase: a reproducible set of proof images, one per capability.
    //
    // Each shot renders offscreen (RenderSystem::renderAndCapture) and is
    // written to docs/images/<backend>_<name>.png by the engine's own PNG
    // writer, so the artefacts are produced by the code under test.
    //======================================================================
    struct ShowcaseRig
    {
        Engine engine;
        bool ok = false;

        bool init(const DemoOptions &options, uint32_t w, uint32_t h)
        {
            EngineConfig cfg;
            cfg.app_name = "genesis-showcase";
            cfg.asset_dir = options.asset_dir;
            cfg.width = w;
            cfg.height = h;
            cfg.backend = options.backend;
            cfg.headless = true;
            cfg.enable_ui = false;
            cfg.enable_script = false;
            cfg.vsync = false;
            cfg.max_frames = 0;
            ok = engine.initialize(cfg);
            return ok;
        }

        void addModel(const std::shared_ptr<ModelData> &model, const Math::Vec3 &pos, float scale,
                      const std::string &name)
        {
            ECS::World &world = engine.world();
            ECS::Entity e = world.createEntity(name);
            ECS::TransformComponent t;
            t.position = pos;
            t.scale = Math::Vec3(scale);
            world.addComponent<ECS::TransformComponent>(e, t);
            ECS::MeshComponent m;
            m.model = model;
            m.pick_id = e.id + 1;
            world.addComponent<ECS::MeshComponent>(e, m);
            std::vector<uint32_t> gpu = engine.render().scene().uploadModel(*model, name);
            Render::RenderSystem::EntityMeshBinding binding;
            binding.entity = e;
            binding.mesh_indices = gpu;
            engine.render().bindEntityMeshes(binding);
        }

        void addCamera(const Math::Vec3 &pos, const Math::Vec3 &target, float fov)
        {
            ECS::World &world = engine.world();
            ECS::Entity e = world.createEntity("camera");
            ECS::TransformComponent t;
            t.position = pos;
            t.rotation = Math::Quat::lookRotation(target - pos, Math::Vec3(0, 1, 0));
            world.addComponent<ECS::TransformComponent>(e, t);
            ECS::CameraComponent cam;
            cam.fov_degrees = fov;
            cam.near_plane = 0.05f;
            cam.far_plane = 200.0f;
            world.addComponent<ECS::CameraComponent>(e, cam);
        }

        void addDirectional(const Math::Vec3 &dir, float intensity, const Math::Color &color)
        {
            ECS::World &world = engine.world();
            ECS::Entity e = world.createEntity("sun");
            ECS::TransformComponent t;
            t.rotation = Math::Quat::lookRotation(dir, Math::Vec3(0, 1, 0));
            world.addComponent<ECS::TransformComponent>(e, t);
            ECS::LightComponent l;
            l.type = ECS::LightType::Directional;
            l.color = color;
            l.intensity = intensity;
            world.addComponent<ECS::LightComponent>(e, l);
        }

        void addPoint(const Math::Vec3 &pos, float intensity, const Math::Color &color)
        {
            ECS::World &world = engine.world();
            ECS::Entity e = world.createEntity("point");
            ECS::TransformComponent t;
            t.position = pos;
            world.addComponent<ECS::TransformComponent>(e, t);
            ECS::LightComponent l;
            l.type = ECS::LightType::Point;
            l.color = color;
            l.intensity = intensity;
            l.range = 20.0f;
            world.addComponent<ECS::LightComponent>(e, l);
        }

        bool shoot(const std::string &path)
        {
            for (int i = 0; i < 2; ++i) engine.tick(); // settle transforms / IBL
            if (!engine.saveScreenshot(path))
            {
                GEN_LOG_ERROR("[showcase] failed to write %s", path.c_str());
                return false;
            }
            GEN_LOG_INFO("[showcase] wrote %s", path.c_str());
            return true;
        }
    };

    // Standard "material grid" scene: 6 columns of increasing metallic x
    // 4 rows of decreasing roughness, one directional light.  This is the
    // classic PBR proof image and it is fully procedural (no downloaded asset).
    void buildMaterialGridScene(ShowcaseRig &rig, const DemoOptions &options, bool with_ibl)
    {
        ModelData grid = Procedural::makeMaterialGrid(6, 4, 1.15f, 0.45f);
        rig.addModel(std::make_shared<ModelData>(grid), Math::Vec3(0, 0, 0), 1.0f, "material_grid");
        rig.addCamera(Math::Vec3(0, 0.4f, 7.2f), Math::Vec3(0, 0, 0), 42.0f);
        rig.addDirectional(Math::normalize(Math::Vec3(-0.45f, -0.55f, -0.7f)), 3.2f, Math::Color(1, 0.97f, 0.92f));

        Render::RenderSettings &s = rig.engine.render().settings();
        s.show_skybox = false;
        s.ambient_color = Math::Color(0.02f, 0.02f, 0.03f);
        s.ambient_intensity = 1.0f;
        s.tonemap_mode = Render::TonemapMode::Aces;
        s.exposure = 1.15f;
        s.gamma = 2.2f;
        s.use_ibl = with_ibl;
        s.frustum_culling = true;
        if (with_ibl)
        {
            Render::RenderSystem::IblBakeSettings cfg;
            cfg.cubemap_size = 128;
            cfg.irradiance_size = 32;
            cfg.prefiltered_size = 128;
            cfg.brdf_lut_size = 128;
            cfg.prefilter_samples = 128;
            std::string hdr = ResourceIO::joinPath(options.asset_dir, "textures/sky_equirect.hdr");
            bool baked = ResourceIO::fileExists(hdr) ? rig.engine.render().bakeIBL(hdr, cfg)
                                                     : rig.engine.render().bakeProceduralIBL(cfg);
            if (!baked) GEN_LOG_ERROR("[showcase] IBL bake failed");
        }
    }

    int runShowcase(const DemoOptions &options)
    {
        const std::string &dir = options.showcase_dir;
        const std::string be = (options.backend == RHI::BackendType::Vulkan) ? "vk" : "gl";
        auto path = [&](const std::string &name) { return ResourceIO::joinPath(dir, be + "_" + name + ".png"); };
        int failures = 0;

        //--------------------------------------------------------------
        // 1-4: PBR material grid under the two direct-light BRDF models,
        //      with and without image based lighting.
        //--------------------------------------------------------------
        {
            struct Variant { const char *name; Render::ShadingMode shading; bool ibl; };
            const Variant variants[] = {
                {"pbr_grid_cook_torrance_direct", Render::ShadingMode::CookTorrance, false},
                {"pbr_grid_blinn_phong_direct", Render::ShadingMode::BlinnPhong, false},
                {"pbr_grid_cook_torrance_ibl", Render::ShadingMode::CookTorrance, true},
                {"pbr_grid_blinn_phong_ibl", Render::ShadingMode::BlinnPhong, true},
            };
            for (const Variant &v : variants)
            {
                ShowcaseRig rig;
                if (!rig.init(options, 512, 384)) { ++failures; continue; }
                buildMaterialGridScene(rig, options, v.ibl);
                rig.engine.render().settings().shading_mode = v.shading;
                if (!rig.shoot(path(v.name))) ++failures;
            }
        }

        //--------------------------------------------------------------
        // 5-7: tone mapping operators on the same scene
        //--------------------------------------------------------------
        {
            struct Tone { const char *name; Render::TonemapMode mode; float exposure; };
            const Tone tones[] = {
                {"tonemap_none", Render::TonemapMode::None, 1.0f},
                {"tonemap_reinhard", Render::TonemapMode::Reinhard, 1.6f},
                {"tonemap_aces", Render::TonemapMode::Aces, 1.15f},
            };
            for (const Tone &t : tones)
            {
                ShowcaseRig rig;
                if (!rig.init(options, 512, 384)) { ++failures; continue; }
                buildMaterialGridScene(rig, options, true);
                rig.engine.render().settings().tonemap_mode = t.mode;
                rig.engine.render().settings().exposure = t.exposure;
                if (!rig.shoot(path(t.name))) ++failures;
            }
        }

        //--------------------------------------------------------------
        // 8: skybox + HDR environment (procedural gradient fallback or the
        //    baked cubemap, whichever the asset dir provides)
        //--------------------------------------------------------------
        {
            ShowcaseRig rig;
            if (rig.init(options, 512, 384))
            {
                buildMaterialGridScene(rig, options, true);
                Render::RenderSettings &s = rig.engine.render().settings();
                s.show_skybox = true;
                s.sky_mode = Render::SkyMode::Cubemap;
                s.env_intensity = 0.55f;
                if (!rig.shoot(path("skybox_ibl_env"))) ++failures;
            }
            else ++failures;
        }

        //--------------------------------------------------------------
        // 9: wireframe + debug gizmos (RHI push constant path)
        //--------------------------------------------------------------
        {
            ShowcaseRig rig;
            if (rig.init(options, 512, 384))
            {
                buildMaterialGridScene(rig, options, false);
                rig.engine.render().settings().wireframe = false;
                rig.engine.render().drawBox(Math::AABB{Math::Vec3(-3.6f, -2.0f, -0.6f), Math::Vec3(3.6f, 2.0f, 0.6f), true},
                                            Math::Color(1.0f, 0.85f, 0.2f));
                rig.engine.render().drawSphere(Math::Vec3(0, 0, 0), 2.6f, Math::Color(0.3f, 1.0f, 0.6f), 28);
                if (!rig.shoot(path("debug_gizmos"))) ++failures;
            }
            else ++failures;
        }

        //--------------------------------------------------------------
        // 10-12: imported models (glTF / GLB / FBX) close-ups
        //--------------------------------------------------------------
        {
            struct ModelShot { const char *file; const char *name; float scale; Math::Vec3 cam; };
            const ModelShot shots[] = {
                {"models/DamagedHelmet.glb", "model_glb_damaged_helmet", 1.0f, Math::Vec3(0, 0.1f, 3.2f)},
                {"models/genesis_sphere.glb", "model_glb_sphere", 1.0f, Math::Vec3(0, 0.2f, 3.4f)},
                {"models/genesis_test_cube.fbx", "model_fbx_cube", 1.6f, Math::Vec3(1.4f, 1.1f, 2.6f)},
                {"models/genesis_test_sphere.fbx", "model_fbx_sphere", 1.6f, Math::Vec3(1.2f, 0.9f, 2.6f)},
                {"models/genesis_torus.gltf", "model_gltf_torus", 1.0f, Math::Vec3(0, 1.4f, 3.6f)},
                {"models/genesis_cube_textured.gltf", "model_gltf_textured_cube", 1.0f, Math::Vec3(1.6f, 1.4f, 3.0f)},
            };
            for (const ModelShot &shot : shots)
            {
                std::string full = ResourceIO::joinPath(options.asset_dir, shot.file);
                if (!ResourceIO::fileExists(full))
                {
                    GEN_LOG_WARN("[showcase] missing asset %s - skipped", full.c_str());
                    continue;
                }
                ShowcaseRig rig;
                if (!rig.init(options, 384, 384)) { ++failures; continue; }
                std::shared_ptr<ModelData> model = ResourceIO::loadModel(full);
                if (!model) { ++failures; continue; }
                rig.addModel(model, Math::Vec3(0, 0, 0), shot.scale, shot.name);
                rig.addCamera(shot.cam, Math::Vec3(0, 0, 0), 45.0f);
                rig.addDirectional(Math::normalize(Math::Vec3(-0.5f, -0.45f, -0.75f)), 3.5f, Math::Color(1, 0.98f, 0.94f));
                rig.addPoint(Math::Vec3(2.2f, 1.6f, 2.0f), 14.0f, Math::Color(0.45f, 0.7f, 1.0f));
                Render::RenderSettings &s = rig.engine.render().settings();
                s.show_skybox = true;
                s.sky_mode = Render::SkyMode::Procedural;
                s.env_intensity = 0.25f;
                s.use_ibl = false;
                s.ambient_color = Math::Color(0.03f, 0.035f, 0.05f);
                s.ambient_intensity = 1.0f;
                s.tonemap_mode = Render::TonemapMode::Aces;
                s.exposure = 1.25f;
                s.gamma = 2.2f;
                if (!rig.shoot(path(shot.name))) ++failures;
            }
        }

        GEN_LOG_INFO("[showcase] done: %d failure(s), images in %s/", failures, dir.c_str());
        return failures == 0 ? 0 : 1;
    }

//======================================================================
int main(int argc, char **argv)
{
    Logger::instance().setMinLevel(LogLevel::Info);
    Logger::instance().setColorOutput(true);

    DemoOptions options = parseArgs(argc, argv);
    GEN_LOG_INFO("=== Genesis Render 2.0 demo (%s backend) ===", RHI::backendTypeName(options.backend));

    if (!RHI::backendAvailable(options.backend))
    {
        GEN_LOG_ERROR("the %s backend is not compiled in; rebuild with -DGENESIS_BACKEND_%s=ON",
                      RHI::backendTypeName(options.backend),
                      options.backend == RHI::BackendType::Vulkan ? "VULKAN" : "OPENGL");
        return 2;
    }

    DemoApp app;
    app.options = options;
    if (!app.initialize())
    {
        GEN_LOG_ERROR("demo initialisation failed");
        return 1;
    }

    if (!options.showcase_dir.empty())
    {
        int rc = runShowcase(options);
        app.engine.shutdown();
        return rc;
    }

    if (!options.screenshot.empty())
    {
        // Warm up (IBL bake + first frame) then capture offscreen.
        for (int i = 0; i < 2; ++i) app.engine.tick();
        if (app.engine.saveScreenshot(options.screenshot))
            GEN_LOG_INFO("screenshot written to %s", options.screenshot.c_str());
        else
            GEN_LOG_ERROR("screenshot capture failed");
    }
    else
    {
        Platform::Timer timer;
        while (app.engine.tick())
        {
            float dt = (float)timer.tick();
            app.handleShortcuts();
            app.tick(dt);
        }
    }

    app.engine.shutdown();
    GEN_LOG_INFO("demo finished: %llu frames, %u warnings, %u errors",
                 (unsigned long long)app.engine.frameCount(),
                 Logger::instance().warningCount(), Logger::instance().errorCount());
    return Logger::instance().errorCount() > 0 ? 3 : 0;
}
