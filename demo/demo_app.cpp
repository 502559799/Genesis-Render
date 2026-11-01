/*
 * Genesis Render - demo application: scenes and per-frame logic.
 */
#include "demo_app.hpp"

#include "foundation_layer/genesis_log.hpp"
#include "renderer/render_system.hpp"
#include "resource/asset_package.hpp"
#include "resource/procedural_mesh.hpp"
#include "resource/resource_types.hpp"
#include "api/encoder_decoder_api.hpp"
#include "api/ray_tracing_api.hpp"
#include "scene/scene_loader.hpp"
#include "script/script_system.hpp"
#include "ui/ui_system.hpp"

#include "external/json.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

namespace Demo
{
    const char *sceneName(SceneKind k)
    {
        switch (k)
        {
        case SceneKind::Showcase: return "showcase";
        case SceneKind::LuaScene: return "lua";
        case SceneKind::LuaCommandBuffer: return "commandbuffer";
        case SceneKind::Physics: return "physics";
        case SceneKind::Game: return "game";
        case SceneKind::Gaussian: return "gaussian";
        case SceneKind::Package: return "package";
        case SceneKind::Animation: return "animation";
        case SceneKind::RayTracing: return "raytracing";
        case SceneKind::Media: return "media";
        case SceneKind::Empty: return "empty";
        default: return "?";
        }
    }

    bool sceneFromName(const std::string &name, SceneKind &out)
    {
        for (uint32_t i = 0; i < (uint32_t)SceneKind::Count; ++i)
            if (name == sceneName((SceneKind)i))
            {
                out = (SceneKind)i;
                return true;
            }
        if (name == "script" || name == "scene_builder") { out = SceneKind::LuaScene; return true; }
        if (name == "cb") { out = SceneKind::LuaCommandBuffer; return true; }
        if (name == "phys") { out = SceneKind::Physics; return true; }
        if (name == "minigame" || name == "cascade") { out = SceneKind::Game; return true; }
        if (name == "splat" || name == "gaussians") { out = SceneKind::Gaussian; return true; }
        if (name == "gpkg" || name == "assets") { out = SceneKind::Package; return true; }
        if (name == "anim" || name == "sprite") { out = SceneKind::Animation; return true; }
        if (name == "rt" || name == "raytrace") { out = SceneKind::RayTracing; return true; }
        if (name == "video" || name == "player") { out = SceneKind::Media; return true; }
        return false;
    }

    std::string DemoApp::asset(const std::string &relative) const
    {
        return ResourceIO::joinPath(options.asset_dir, relative);
    }

    //======================================================================
    bool DemoApp::initialize()
    {
        if (options.headless) SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
        EngineConfig cfg;
        cfg.app_name = "Genesis Render - OpenGL / Vulkan / Metal feature demo";
        cfg.asset_dir = options.asset_dir;
        cfg.width = options.width;
        cfg.height = options.height;
        cfg.backend = options.backend;
        cfg.headless = options.headless;
        cfg.enable_validation = options.validation;
        cfg.enable_ui = options.enable_ui;
        cfg.enable_script = options.enable_script;
        cfg.max_frames = options.frames;
        cfg.vsync = !options.headless;
        if (!options.record_dir.empty()) cfg.fixed_delta_time = 1.0f / std::max(1.0f, options.record_fps);
        if (!engine.initialize(cfg)) return false;

        // Textures offered by the material editor.
        if (DIR *dir = opendir(asset("textures").c_str()))
        {
            while (dirent *ent = readdir(dir))
            {
                std::string n = ent->d_name;
                std::string lower = n;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                if (lower.size() > 4 && (lower.rfind(".png") == lower.size() - 4 || lower.rfind(".jpg") == lower.size() - 4))
                    texture_files_.push_back(asset("textures/" + n));
            }
            closedir(dir);
            std::sort(texture_files_.begin(), texture_files_.end());
        }

        Render::RenderSystem &rs = engine.render();
        preview_hdr_ = rs.createRenderTexture("material_preview_hdr", 320, 320, true, true);
        preview_ldr_ = rs.createRenderTexture("material_preview", 320, 320, false, false);
        monitor_rt_ = rs.createRenderTexture("security_camera", 512, 288, true, true);
        const uint32_t w = rs.hdrTarget()->width(), h = rs.hdrTarget()->height();
        custom_scene_ = rs.createRenderTexture("custom_scene", w, h, true, true);
        custom_ldr_ = rs.createRenderTexture("custom_ldr", w, h, false, false);
        custom_fx_ = rs.createRenderTexture("custom_fx", w, h, false, false);
        preview_cb_ = std::make_shared<Render::CommandBuffer>("material_preview");
        monitor_cb_ = std::make_shared<Render::CommandBuffer>("security_camera");
        custom_cb_ = std::make_shared<Render::CommandBuffer>("custom_pipeline");
        hud_cb_ = std::make_shared<Render::CommandBuffer>("demo_hud");

        const char *kinds[] = {"sphere", "box", "torus"};
        const float sizes[] = {1.6f, 1.1f, 1.7f};
        for (int i = 0; i < 3; ++i) preview_meshes_.push_back(Reflect::createMeshAsset(kinds[i], sizes[i], 64));
        preview_meshes_.push_back(Reflect::loadMeshAsset(asset("models/DamagedHelmet.glb")));

        refreshPackageList();
        if (options.lab.empty() || options.lab == "material") show_material_ = true;
        else if (options.lab == "commandbuffer") show_cb_ = true;
        else if (options.lab == "custom") { show_cb_ = true; custom_pipeline_ = true; }
        else if (options.lab == "text") show_text_ = true;
        else if (options.lab == "inspector") show_inspector_ = true;
        else if (options.lab == "physics") show_physics_ = true;
        else if (options.lab == "gaussian" || options.lab == "splat") show_gaussian_ = true;
        else if (options.lab == "package") show_package_ = true;
        else if (options.lab == "game") show_game_ = true;
        else if (options.lab == "animation" || options.lab == "anim") show_animation_ = true;
        else if (options.lab == "raytracing" || options.lab == "rt") show_raytracing_ = true;
        else if (options.lab == "codec") show_codec_ = true;
        else if (options.lab == "apis") show_apis_ = true;
        else if (options.lab == "media" || options.lab == "player") show_media_ = true;

        if (options.scene == SceneKind::Physics) { show_physics_ = true; show_material_ = false; }
        if (options.scene == SceneKind::Gaussian) { show_gaussian_ = true; show_material_ = false; }
        if (options.scene == SceneKind::Game) { show_game_ = true; show_material_ = false; }
        if (options.scene == SceneKind::Package) { show_package_ = true; show_material_ = false; }
        if (options.scene == SceneKind::Animation) { show_animation_ = true; show_material_ = false; }
        if (options.scene == SceneKind::RayTracing) { show_raytracing_ = true; show_material_ = false; }
        if (options.scene == SceneKind::Media) { show_media_ = true; show_material_ = false; }

        loadScene(options.scene);
        if (!options.script.empty() && engine.hasScript()) attachScript(options.script, "ScriptHost");
        engine.setUiDrawCallback([this](Engine &e) { drawUI(e); });
        return true;
    }

    //======================================================================
    // Scenes
    //======================================================================
    void DemoApp::applyPendingScene()
    {
        if (pending_scene_ == SceneKind::Count) return;
        SceneKind k = pending_scene_;
        pending_scene_ = SceneKind::Count;
        // Let in-flight frames finish before entities / buffers disappear.
        engine.device()->waitIdle();
        loadScene(k);
    }

    void DemoApp::resetWorld()
    {
        ECS::World &world = engine.world();
        world.each<ECS::ScriptComponent>([this](ECS::Entity, ECS::ScriptComponent &c)
        {
            if (c.language == ECS::ScriptLanguage::Lua)
            {
                if (engine.hasScript()) engine.script().unloadComponentScript(c);
            }
            else if (engine.hasJS())
            {
                engine.js().unloadComponentScript(c);
            }
        });
        std::vector<ECS::Entity> all = world.entities();
        for (ECS::Entity e : all)
            if (world.isAlive(e)) world.destroyEntity(e);

        Render::RenderSystem &rs = engine.render();
        rs.clearCommandBuffers();
        rs.clearDebugLines();
        Render::RenderSettings &s = rs.settings();
        s.pipeline_mode = Render::PipelineMode::Forward;
        s.post_effect = Render::PostEffect::None;
        camera_ = sun_ = warm_ = cool_ = selected_ = monitor_screen_ = ECS::nullEntity();
        material_index_ = -1;
        custom_pipeline_ = options.lab == "custom";
        applied_custom_ = false;
        engine.physics().setPaused(false);
        engine.physics().settings().debug_draw = false;
        engine.physics().resetAccumulator();
        if (gaussian_cb_) gaussian_cb_->clear();
        if (rt_cb_) rt_cb_->clear();
        if (media_cb_) media_cb_->clear();
        if (media_player_) media_player_->close();
        if (engine.device() && media_texture_) engine.device()->destroyTexture(media_texture_);
        if (engine.device() && media_sampler_) engine.device()->destroySampler(media_sampler_);
        media_texture_ = nullptr;
        media_sampler_ = nullptr;
        media_texture_width_ = media_texture_height_ = 0;
        media_serial_ = 0;
        media_status_.clear();
        anim_sprite_ = anim_vertex_ = ECS::nullEntity();
        rt_scene_.clear();
        rt_status_.clear();
        package_scene_loaded_ = false;
    }

    void DemoApp::loadScene(SceneKind kind)
    {
        resetWorld();
        scene_ = kind;
        switch (kind)
        {
        case SceneKind::Showcase: buildShowcase(); break;
        case SceneKind::LuaCommandBuffer:
            buildShowcase(false); // the script brings its own security camera
            attachScript(asset("scripts/commandbuffer_demo.lua"), "CommandBufferDemo");
            monitor_enabled_ = false;
            break;
        case SceneKind::LuaScene:
            if (!attachScript(asset("scripts/scene_builder.lua"), "SceneBuilder").valid()) buildMinimalRig();
            break;
        case SceneKind::Physics: buildPhysicsScene(); break;
        case SceneKind::Gaussian: buildGaussianScene(); break;
        case SceneKind::Game: buildGameScene(); break;
        case SceneKind::Animation: buildAnimationScene(); break;
        case SceneKind::RayTracing: buildRayTracingScene(); break;
        case SceneKind::Media: buildMediaScene(); break;
        case SceneKind::Package:
            if (!engine.activePackage() && !options.package.empty())
            {
                std::string error;
                if (!loadPackage(options.package, &error))
                    GEN_LOG_ERROR("[Demo] cannot load package '%s': %s", options.package.c_str(), error.c_str());
            }
            buildFromActivePackage();
            break;
        case SceneKind::Empty:
        default: buildMinimalRig(); break;
        }
        registerLabBuffers();
        selected_ = engine.world().findEntity("DamagedHelmet");
        GEN_LOG_INFO("[Demo] scene '%s': %zu entities, %zu materials", sceneName(kind), engine.world().entityCount(),
                     engine.render().scene().materialCount());
    }

    void DemoApp::registerLabBuffers()
    {
        Render::RenderSystem &rs = engine.render();
        rs.addCommandBuffer(Render::CameraEvent::BeforeScene, preview_cb_);
        rs.addCommandBuffer(Render::CameraEvent::BeforeScene, monitor_cb_);
        rs.addCommandBuffer(Render::CameraEvent::Overlay, hud_cb_);
        if (gaussian_cb_) rs.addCommandBuffer(Render::CameraEvent::Overlay, gaussian_cb_);
        if (rt_cb_) rs.addCommandBuffer(Render::CameraEvent::Overlay, rt_cb_);
        if (media_cb_) rs.addCommandBuffer(Render::CameraEvent::Overlay, media_cb_);
        rs.addCustomCommandBuffer(custom_cb_);
    }

    ECS::Entity DemoApp::attachScript(const std::string &path, const std::string &entity_name)
    {
        if (!engine.hasScript()) return ECS::nullEntity();
        if (!ResourceIO::fileExists(path))
        {
            GEN_LOG_ERROR("[Demo] script not found: %s", path.c_str());
            return ECS::nullEntity();
        }
        ECS::World &world = engine.world();
        ECS::Entity e = world.createEntity(entity_name);
        world.addComponent<ECS::TransformComponent>(e);
        ECS::ScriptComponent sc;
        sc.script_path = path;
        if (ResourceIO::pathExtension(path) == "js" || ResourceIO::pathExtension(path) == "mjs")
            sc.language = ECS::ScriptLanguage::JavaScript;
        world.addComponent<ECS::ScriptComponent>(e, sc);
        return e;
    }

    ECS::Entity DemoApp::spawnFile(const std::string &path, const std::string &name, const Math::Vec3 &pos, float scale)
    {
        if (!ResourceIO::fileExists(path))
        {
            GEN_LOG_WARN("[Demo] model not found: %s", path.c_str());
            return ECS::nullEntity();
        }
        return engine.spawnModel(path, pos, scale, name);
    }

    ECS::Entity DemoApp::spawnMesh(const std::string &name, const Reflect::MeshAssetPtr &mesh, const Render::MaterialPtr &mat,
                                   const Math::Vec3 &pos, float scale)
    {
        ECS::World &world = engine.world();
        ECS::Entity e = world.createEntity(name);
        ECS::TransformComponent t;
        t.position = pos;
        t.scale = Math::Vec3(scale);
        world.addComponent<ECS::TransformComponent>(e, t);
        Reflect::attachMesh(e, mesh, mat);
        return e;
    }

    ECS::Entity DemoApp::addLabel(ECS::Entity e, const std::string &text, float height, const Math::Color &color)
    {
        if (!engine.world().isAlive(e)) return e;
        ECS::TextComponent t;
        t.text = text;
        t.size = 0.2f;
        t.color = color;
        t.offset = Math::Vec3(0.0f, height, 0.0f);
        t.outline = 0.18f;
        t.outline_color = Math::Color(0.0f, 0.0f, 0.0f, 0.85f);
        engine.world().addComponent<ECS::TextComponent>(e, t);
        return e;
    }

    void DemoApp::buildMinimalRig()
    {
        ECS::World &world = engine.world();
        camera_ = world.createEntity("MainCamera");
        {
            ECS::TransformComponent t;
            t.position = Math::Vec3(0.0f, 2.0f, 6.0f);
            world.addComponent<ECS::TransformComponent>(camera_, t);
            ECS::CameraComponent cam;
            cam.fov_degrees = 55.0f;
            cam.far_plane = 300.0f;
            world.addComponent<ECS::CameraComponent>(camera_, cam);
            ECS::CameraControllerComponent ctrl;
            ctrl.orbit_target = Math::Vec3(0.0f, 0.8f, 0.0f);
            ctrl.orbit_distance = 7.0f;
            ctrl.orbit_pitch_degrees = 18.0f;
            world.addComponent<ECS::CameraControllerComponent>(camera_, ctrl);
        }
        sun_ = world.createEntity("Sun");
        {
            world.addComponent<ECS::TransformComponent>(sun_);
            ECS::LightComponent l;
            l.type = ECS::LightType::Directional;
            l.intensity = 4.0f;
            l.color = Math::Color(1.0f, 0.96f, 0.9f);
            world.addComponent<ECS::LightComponent>(sun_, l);
        }
        ECS::Entity env = world.createEntity("Environment");
        {
            ECS::EnvironmentComponent c;
            std::string hdr = asset("textures/sky_equirect.hdr");
            c.hdr_equirect_path = ResourceIO::fileExists(hdr) ? hdr : "";
            world.addComponent<ECS::EnvironmentComponent>(env, c);
        }
    }

    void DemoApp::buildShowcase(bool with_monitor)
    {
        ECS::World &world = engine.world();
        Render::RenderScene &scene = engine.render().scene();
        buildMinimalRig();
        if (auto *ctrl = world.getComponent<ECS::CameraControllerComponent>(camera_))
        {
            ctrl->orbit_target = Math::Vec3(0.0f, 1.0f, 0.0f);
            ctrl->orbit_distance = 8.5f;
            ctrl->orbit_yaw_degrees = 20.0f;
            ctrl->orbit_pitch_degrees = 18.0f;
        }

        // ---- two animated point lights -----------------------------------
        auto point = [&](const char *name, const Math::Color &c, float intensity)
        {
            ECS::Entity e = world.createEntity(name);
            world.addComponent<ECS::TransformComponent>(e);
            ECS::LightComponent l;
            l.type = ECS::LightType::Point;
            l.color = c;
            l.intensity = intensity;
            l.range = 12.0f;
            world.addComponent<ECS::LightComponent>(e, l);
            return e;
        };
        warm_ = point("PointLightWarm", Math::Color(1.0f, 0.55f, 0.25f), 12.0f);
        cool_ = point("PointLightCool", Math::Color(0.35f, 0.65f, 1.0f), 10.0f);

        // ---- ground: fully textured PBR material ---------------------------
        {
            Render::MaterialPtr ground = scene.createMaterial("Ground (textured)");
            ground->metallic = 1.0f;
            ground->roughness = 1.0f;
            ground->normal_scale = 0.8f;
            ground->uv_scale = Math::Vec2(4.0f, 4.0f);
            ground->setTexture(Render::TextureSlot::BaseColor, asset("textures/checker_albedo.png"));
            ground->setTexture(Render::TextureSlot::MetallicRoughness, asset("textures/checker_metallic_roughness.png"));
            ground->setTexture(Render::TextureSlot::Normal, asset("textures/panel_normal.png"));
            ground->setTexture(Render::TextureSlot::Occlusion, asset("textures/tile_ao.png"));
            spawnMesh("Ground", Reflect::createMeshAsset("plane", 24.0f), ground, Math::Vec3(0.0f));
        }

        // ---- imported models (glTF / GLB / FBX) ----------------------------
        spawnFile(asset("models/DamagedHelmet.glb"), "DamagedHelmet", Math::Vec3(0.0f, 1.35f, 0.0f), 1.0f);
        addLabel(world.findEntity("DamagedHelmet"), "glTF / GLB", 1.2f, Math::Color(1.0f, 0.9f, 0.45f));
        spawnFile(asset("models/genesis_torus.gltf"), "GenesisTorus", Math::Vec3(2.8f, 1.2f, 0.4f), 0.8f);
        spawnFile(asset("models/genesis_cube_textured.gltf"), "TexturedCube", Math::Vec3(-2.8f, 0.6f, 0.4f), 0.6f);
        spawnFile(asset("models/genesis_test_cube.fbx"), "FbxCube", Math::Vec3(-3.6f, 0.6f, -2.4f), 1.0f);
        addLabel(world.findEntity("FbxCube"), "FBX", 1.0f, Math::Color(0.7f, 0.9f, 1.0f));
        spawnFile(asset("models/genesis_test_sphere.fbx"), "FbxSphere", Math::Vec3(3.6f, 0.9f, -2.4f), 1.0f);

        // ---- procedural PBR material grid ----------------------------------
        {
            ModelData grid = Procedural::makeMaterialGrid(6, 4, 1.15f, 0.42f);
            auto model = std::make_shared<ModelData>(grid);
            auto mesh = std::make_shared<Reflect::MeshAsset>();
            mesh->name = "material_grid";
            mesh->model = model;
            mesh->gpu_meshes = scene.uploadModel(*model, "MaterialGrid");
            spawnMesh("MaterialGrid", mesh, nullptr, Math::Vec3(0.0f, 1.6f, -5.5f));
        }

        // ---- material library row: editable in the Material Editor ---------
        {
            Reflect::MeshAssetPtr sphere = Reflect::createMeshAsset("sphere", 0.9f, 48);
            struct Preset { const char *name; Math::Color color; float metallic, roughness; };
            const Preset presets[] = {
                {"Gold", Math::Color(1.0f, 0.76f, 0.33f), 1.0f, 0.22f},
                {"Car paint", Math::Color(0.75f, 0.05f, 0.08f), 0.35f, 0.18f},
                {"Frosted glass", Math::Color(0.65f, 0.85f, 1.0f, 0.35f), 0.0f, 0.3f},
                {"Neon", Math::Color(0.02f, 0.02f, 0.02f), 0.0f, 0.6f},
                {"Checker", Math::Color(1.0f, 1.0f, 1.0f), 0.0f, 0.55f},
                {"Toon (unlit)", Math::Color(0.3f, 0.85f, 0.45f), 0.0f, 1.0f},
            };
            const int n = (int)(sizeof(presets) / sizeof(presets[0]));
            for (int i = 0; i < n; ++i)
            {
                Render::MaterialPtr m = scene.createMaterial(presets[i].name);
                m->base_color = presets[i].color;
                m->metallic = presets[i].metallic;
                m->roughness = presets[i].roughness;
                if (i == 2) m->blend = Render::MaterialBlend::Transparent;
                if (i == 3)
                {
                    m->emissive = Math::Color(0.1f, 0.9f, 1.0f);
                    m->emissive_strength = 2.5f;
                }
                if (i == 4)
                {
                    m->setTexture(Render::TextureSlot::BaseColor, asset("textures/uv_gradient.png"));
                    m->uv_scale = Math::Vec2(2.0f, 1.0f);
                }
                if (i == 5) m->shading = Render::MaterialShading::Unlit;
                ECS::Entity e = spawnMesh(std::string("Sphere ") + presets[i].name, sphere, m,
                                          Math::Vec3(-3.75f + i * 1.5f, 0.45f, 2.9f));
                addLabel(e, presets[i].name, 0.75f, Math::Color(1.0f, 1.0f, 1.0f));
                if (i == 0)
                {
                    material_index_ = m->index;
                    scroll_to_material_ = true;
                }
            }
        }

        // ---- security camera monitor (render texture on a mesh) -----------
        if (with_monitor)
        {
            monitor_mat_ = scene.createMaterial("Monitor screen (render texture)");
            monitor_mat_->shading = Render::MaterialShading::Unlit;
            monitor_mat_->double_sided = true;
            monitor_mat_->setRenderTexture(Render::TextureSlot::BaseColor, monitor_rt_);
            monitor_screen_ = spawnMesh("MonitorScreen", Reflect::createMeshAsset("plane", 1.0f), monitor_mat_,
                                        Math::Vec3(-5.4f, 1.9f, -0.6f));
            if (auto *t = world.getComponent<ECS::TransformComponent>(monitor_screen_))
            {
                t->scale = Math::Vec3(3.2f, 1.0f, 1.8f);
                t->setEulerDegrees(90.0f, 50.0f, 0.0f);
            }
            addLabel(monitor_screen_, "render texture", 1.25f, Math::Color(1.0f, 0.4f, 0.35f));
            Render::MaterialPtr frame = scene.createMaterial("Monitor frame");
            frame->base_color = Math::Color(0.05f, 0.05f, 0.06f);
            frame->metallic = 0.8f;
            frame->roughness = 0.35f;
            ECS::Entity back = spawnMesh("MonitorFrame", Reflect::createMeshAsset("box", 1.0f), frame, Math::Vec3(0.0f));
            world.setParent(back, monitor_screen_);
            if (auto *t = world.getComponent<ECS::TransformComponent>(back))
            {
                t->position = Math::Vec3(0.0f, -0.03f, 0.0f);
                t->scale = Math::Vec3(1.06f, 0.04f, 1.1f);
            }
            monitor_enabled_ = true;
        }

        // ---- scripts: legacy API animator + RTTI script --------------------
        if (engine.hasScript())
        {
            attachScript(asset("scripts/demo_anim.lua"), "LuaAnimator");
            attachScript(asset("scripts/full_demo.lua"), "LuaOrbiters");
        }
    }

    //======================================================================
    // Physics playground
    //======================================================================
    void DemoApp::buildPhysicsScene()
    {
        ECS::World &world = engine.world();
        Render::RenderScene &scene = engine.render().scene();
        buildMinimalRig();
        if (auto *ctrl = world.getComponent<ECS::CameraControllerComponent>(camera_))
        {
            ctrl->orbit_target = Math::Vec3(0.0f, 2.0f, 0.0f);
            ctrl->orbit_distance = 13.0f;
            ctrl->orbit_yaw_degrees = 28.0f;
            ctrl->orbit_pitch_degrees = 20.0f;
        }
        Render::RenderSettings &rs = engine.render().settings();
        rs.ambient_intensity = 1.2f;
        rs.ambient_color = Math::Color(0.055f, 0.06f, 0.075f);

        auto makeMaterial = [&](const std::string &name, const Math::Color &color, float metallic, float roughness)
        {
            Render::MaterialPtr m = scene.createMaterial(name);
            m->base_color = color;
            m->metallic = metallic;
            m->roughness = roughness;
            return m;
        };

        // ---- infinite ground -----------------------------------------
        Render::MaterialPtr ground = makeMaterial("Physics ground", Math::Color(0.16f, 0.18f, 0.22f, 1.0f), 0.08f, 0.85f);
        ECS::Entity ground_entity = spawnMesh("PhysicsGround", Reflect::createMeshAsset("plane", 30.0f), ground,
                                              Math::Vec3(0.0f));
        world.addComponent<ECS::RigidBodyComponent>(ground_entity, [] { ECS::RigidBodyComponent b; b.type = ECS::BodyType::Static; return b; }());
        world.addComponent<ECS::ColliderComponent>(ground_entity, [] { ECS::ColliderComponent c; c.shape = ECS::ColliderShape::Plane; return c; }());

        // ---- box pyramid ---------------------------------------------
        Render::MaterialPtr box_mat = makeMaterial("Physics crates", Math::Color(0.86f, 0.42f, 0.16f, 1.0f), 0.05f, 0.55f);
        Reflect::MeshAssetPtr box_mesh = Reflect::createMeshAsset("box", 0.9f, 4);
        const int levels = 4;
        for (int level = 0; level < levels; ++level)
        {
            const int count = levels + 1 - level;
            for (int i = 0; i < count; ++i)
            {
                const float x = (i - (count - 1) * 0.5f) * 0.94f;
                const float y = 0.46f + level * 0.92f;
                ECS::Entity e = spawnMesh("Crate_" + std::to_string(level) + "_" + std::to_string(i), box_mesh,
                                          box_mat, Math::Vec3(x, y, 0.0f));
                ECS::RigidBodyComponent rb;
                rb.type = ECS::BodyType::Dynamic;
                rb.mass = 1.4f;
                rb.restitution = 0.12f;
                rb.friction = 0.7f;
                world.addComponent<ECS::RigidBodyComponent>(e, rb);
                ECS::ColliderComponent col;
                col.shape = ECS::ColliderShape::Box;
                col.half_extents = Math::Vec3(0.45f);
                world.addComponent<ECS::ColliderComponent>(e, col);
                if (level == levels - 1 && i == 0) addLabel(e, "crates", 0.7f, Math::Color(1.0f, 0.8f, 0.4f));
            }
        }

        // ---- bouncy spheres -----------------------------------------
        Render::MaterialPtr ball_mat = makeMaterial("Physics balls", Math::Color(0.2f, 0.55f, 0.95f, 1.0f), 0.65f, 0.22f);
        Reflect::MeshAssetPtr ball_mesh = Reflect::createMeshAsset("sphere", 0.7f, 32);
        for (int i = 0; i < 8; ++i)
        {
            const float angle = i * 0.9f;
            ECS::Entity e = spawnMesh("Ball_" + std::to_string(i), ball_mesh, ball_mat,
                                      Math::Vec3(std::cos(angle) * 3.4f, 4.0f + i * 0.55f, std::sin(angle) * 3.4f));
            ECS::RigidBodyComponent rb;
            rb.type = ECS::BodyType::Dynamic;
            rb.mass = 0.6f;
            rb.restitution = 0.72f;
            rb.friction = 0.35f;
            world.addComponent<ECS::RigidBodyComponent>(e, rb);
            ECS::ColliderComponent col;
            col.shape = ECS::ColliderShape::Sphere;
            col.radius = 0.35f;
            world.addComponent<ECS::ColliderComponent>(e, col);
        }

        // ---- static ramp --------------------------------------------
        Render::MaterialPtr ramp_mat = makeMaterial("Physics ramp", Math::Color(0.32f, 0.62f, 0.48f, 1.0f), 0.2f, 0.45f);
        ECS::Entity ramp = spawnMesh("Ramp", Reflect::createMeshAsset("box", 1.0f), ramp_mat, Math::Vec3(0.0f, 0.9f, -5.2f));
        if (auto *t = world.getComponent<ECS::TransformComponent>(ramp))
        {
            t->scale = Math::Vec3(6.0f, 0.25f, 3.2f);
            t->setEulerDegrees(-16.0f, 0.0f, 0.0f);
        }
        ECS::RigidBodyComponent ramp_rb;
        ramp_rb.type = ECS::BodyType::Static;
        world.addComponent<ECS::RigidBodyComponent>(ramp, ramp_rb);
        ECS::ColliderComponent ramp_col;
        ramp_col.shape = ECS::ColliderShape::Box;
        ramp_col.half_extents = Math::Vec3(0.5f);
        world.addComponent<ECS::ColliderComponent>(ramp, ramp_col);

        // ---- hinge pendulum -----------------------------------------
        Render::MaterialPtr brass = makeMaterial("Physics brass", Math::Color(0.95f, 0.7f, 0.25f, 1.0f), 1.0f, 0.25f);
        ECS::Entity pendulum = spawnMesh("Pendulum", Reflect::createMeshAsset("box", 1.0f), brass, Math::Vec3(4.5f, 3.2f, 0.0f));
        if (auto *t = world.getComponent<ECS::TransformComponent>(pendulum))
            t->scale = Math::Vec3(0.32f, 2.2f, 0.32f);
        ECS::RigidBodyComponent prb;
        prb.type = ECS::BodyType::Dynamic;
        prb.mass = 2.5f;
        prb.restitution = 0.1f;
        prb.friction = 0.4f;
        world.addComponent<ECS::RigidBodyComponent>(pendulum, prb);
        ECS::ColliderComponent pcol;
        pcol.shape = ECS::ColliderShape::Box;
        pcol.half_extents = Math::Vec3(0.5f);
        world.addComponent<ECS::ColliderComponent>(pendulum, pcol);
        ECS::HingeJointComponent hinge;
        hinge.anchor = Math::Vec3(4.5f, 5.4f, 0.0f);
        hinge.axis = Math::Vec3(0.0f, 0.0f, 1.0f);
        world.addComponent<ECS::HingeJointComponent>(pendulum, hinge);
        addLabel(pendulum, "hinge", 1.5f, Math::Color(1.0f, 0.85f, 0.4f));

        // ---- capsule ------------------------------------------------
        Render::MaterialPtr capsule_mat = makeMaterial("Physics capsule", Math::Color(0.7f, 0.35f, 0.9f, 1.0f), 0.1f, 0.4f);
        ECS::Entity capsule = spawnMesh("Capsule", Reflect::createMeshAsset("sphere", 1.0f), capsule_mat, Math::Vec3(-4.2f, 4.5f, 0.0f));
        if (auto *t = world.getComponent<ECS::TransformComponent>(capsule))
            t->scale = Math::Vec3(0.75f, 1.8f, 0.75f);
        ECS::RigidBodyComponent crb;
        crb.type = ECS::BodyType::Dynamic;
        crb.mass = 1.2f;
        world.addComponent<ECS::RigidBodyComponent>(capsule, crb);
        ECS::ColliderComponent ccol;
        ccol.shape = ECS::ColliderShape::Capsule;
        ccol.radius = 0.38f;
        ccol.height = 2.4f;
        world.addComponent<ECS::ColliderComponent>(capsule, ccol);
        addLabel(capsule, "capsule", 1.4f, Math::Color(0.8f, 0.6f, 1.0f));

        // ---- trigger volume (no response, shown in the panel) --------
        Render::MaterialPtr trigger_mat = makeMaterial("Physics trigger", Math::Color(0.15f, 1.0f, 0.55f, 0.25f), 0.0f, 0.6f);
        trigger_mat->shading = Render::MaterialShading::Unlit;
        trigger_mat->blend = Render::MaterialBlend::Transparent;
        trigger_mat->depth_write = false;
        ECS::Entity trigger = spawnMesh("Trigger", Reflect::createMeshAsset("sphere", 2.0f), trigger_mat,
                                        Math::Vec3(-4.6f, 1.2f, 3.8f));
        ECS::RigidBodyComponent trb;
        trb.type = ECS::BodyType::Static;
        world.addComponent<ECS::RigidBodyComponent>(trigger, trb);
        ECS::ColliderComponent tcol;
        tcol.shape = ECS::ColliderShape::Sphere;
        tcol.radius = 1.0f;
        tcol.is_trigger = true;
        world.addComponent<ECS::ColliderComponent>(trigger, tcol);
        addLabel(trigger, "trigger", 1.3f, Math::Color(0.4f, 1.0f, 0.7f));

        Physics::PhysicsSettings &ps = engine.physics().settings();
        ps.enabled = true;
        ps.debug_draw = true;
        ps.solver_iterations = 10;
        ps.sleeping = true;
        engine.physics().setPaused(false);
        physics_debug_ = true;
        GEN_LOG_INFO("[Demo] physics playground: %zu bodies", engine.physics().bodyCount());
    }

    //======================================================================
    // Gaussian splat viewer
    //======================================================================
    void DemoApp::buildGaussianScene()
    {
        ECS::World &world = engine.world();
        Render::RenderSettings &rs = engine.render().settings();
        rs.show_skybox = false;
        rs.use_ibl = false;
        rs.ambient_intensity = 0.0f;
        rs.ambient_color = Math::Color(0.0f, 0.0f, 0.0f);
        rs.tonemap_mode = Render::TonemapMode::Aces;
        rs.exposure = 1.0f;
        rs.gamma = 2.2f;

        camera_ = world.createEntity("SplatCamera");
        ECS::TransformComponent t;
        t.position = Math::Vec3(0.0f, 1.6f, 6.0f);
        world.addComponent<ECS::TransformComponent>(camera_, t);
        ECS::CameraComponent cam;
        cam.fov_degrees = 45.0f;
        cam.far_plane = 500.0f;
        world.addComponent<ECS::CameraComponent>(camera_, cam);
        ECS::CameraControllerComponent ctrl;
        ctrl.orbit_target = Math::Vec3(0.0f, 1.2f, 0.0f);
        ctrl.orbit_distance = 6.0f;
        ctrl.orbit_pitch_degrees = 8.0f;
        ctrl.orbit_yaw_degrees = 0.0f;
        world.addComponent<ECS::CameraControllerComponent>(camera_, ctrl);

        sun_ = world.createEntity("SplatFill");
        world.addComponent<ECS::TransformComponent>(sun_);
        ECS::LightComponent light;
        light.type = ECS::LightType::Directional;
        light.intensity = 1.5f;
        world.addComponent<ECS::LightComponent>(sun_, light);

        if (gaussian_path_.empty()) gaussian_path_ = asset("splats/genesis_gaussians.ply");
        std::snprintf(gaussian_path_input_, sizeof(gaussian_path_input_), "%s", gaussian_path_.c_str());
        std::string error;
        if (!gaussian_cloud_.loadPly(gaussian_path_, &error))
            GEN_LOG_WARN("[Demo] gaussian splats unavailable: %s", error.c_str());

        if (gaussian_cloud_.loaded())
        {
            const Math::Vec3 center = (gaussian_cloud_.bounds().minPoint + gaussian_cloud_.bounds().maxPoint) * 0.5f;
            const float extent = Math::length(gaussian_cloud_.bounds().maxPoint - gaussian_cloud_.bounds().minPoint);
            if (auto *c = world.getComponent<ECS::CameraControllerComponent>(camera_))
            {
                c->orbit_target = center;
                c->orbit_distance = std::max(2.5f, extent * 1.25f);
            }
        }
        if (!gaussian_cb_) gaussian_cb_ = std::make_shared<Render::CommandBuffer>("gaussian_splats");
        gaussian_cb_->clear();
        GEN_LOG_INFO("[Demo] gaussian scene: %zu splats", gaussian_cloud_.count());
    }

    //======================================================================
    // Packages / mini-game
    //======================================================================
    void DemoApp::refreshPackageList()
    {
        package_files_.clear();
        if (DIR *dir = opendir(asset("packages").c_str()))
        {
            while (dirent *ent = readdir(dir))
            {
                std::string name = ent->d_name;
                if (name.size() > 5 && name.rfind(".gpkg") == name.size() - 5)
                    package_files_.push_back(asset("packages/" + name));
            }
            closedir(dir);
        }
        std::sort(package_files_.begin(), package_files_.end());
        if (package_files_.empty()) GEN_LOG_WARN("[Demo] no .gpkg packages found under %s", asset("packages").c_str());
    }

    bool DemoApp::loadPackage(const std::string &path, std::string *error)
    {
        engine.device()->waitIdle();
        std::string err;
        AssetPackagePtr package = engine.loadAssetPackage(path, &err);
        if (!package)
        {
            package_status_ = "load failed: " + err;
            if (error) *error = err;
            return false;
        }
        package_manifest_ = package->manifestJson();
        for (size_t i = 0; i < package_files_.size(); ++i)
            if (ResourceIO::pathFilename(package_files_[i]) == ResourceIO::pathFilename(path))
                package_index_ = (int)i;
        package_status_ = "loaded '" + package->name() + "' (" + std::to_string(package->entries().size()) + " files)";
        return true;
    }

    void DemoApp::buildFromActivePackage()
    {
        AssetPackagePtr package = engine.activePackage();
        if (!package)
        {
            package_status_ = "no package loaded";
            buildMinimalRig();
            return;
        }
        std::string scene_rel;
        try
        {
            nlohmann::json manifest = nlohmann::json::parse(package->manifestJson());
            if (manifest.contains("scene") && manifest["scene"].is_string())
                scene_rel = manifest["scene"].get<std::string>();
            else if (manifest.contains("entry_scene") && manifest["entry_scene"].is_string())
                scene_rel = manifest["entry_scene"].get<std::string>();
        }
        catch (const std::exception &)
        {
        }
        if (scene_rel.empty())
        {
            for (const PackageEntry &entry : package->entries())
                if (entry.path.rfind("scenes/", 0) == 0 &&
                    entry.path.size() > 11 && entry.path.rfind(".scene.json") == entry.path.size() - 11)
                {
                    scene_rel = entry.path;
                    break;
                }
        }
        if (scene_rel.empty())
        {
            package_status_ = "package has no scene entry";
            buildMinimalRig();
            return;
        }
        const std::string root = engine.packageRoot();
        const std::string scene_path = ResourceIO::joinPath(root, scene_rel);
        Scene::LoadResult result = Scene::loadFromFile(engine, scene_path, root);
        if (!result.ok)
        {
            package_status_ = "scene load failed: " + result.error;
            buildMinimalRig();
            return;
        }
        package_scene_loaded_ = true;
        package_status_ = "scene '" + scene_rel + "': " + std::to_string(result.entities) + " entities, " +
                          std::to_string(result.materials) + " materials, " + std::to_string(result.scripts) +
                          " scripts";
        camera_ = engine.world().findEntity("MainCamera");
        if (!camera_.valid()) camera_ = engine.world().findEntity("Camera");
        game_status_ = package_status_;
    }

    void DemoApp::buildGameScene()
    {
        buildMinimalRig(); // fallback rig if the package is missing
        // Remove the fallback rig pieces once the package scene is loaded.
        std::string path = options.package.empty() ? asset("packages/cascade.gpkg") : options.package;
        std::string error;
        if (!loadPackage(path, &error))
        {
            game_status_ = "package missing: " + path + " (run tools/genesis_pack.py)";
            GEN_LOG_ERROR("[Demo] %s", game_status_.c_str());
            buildPhysicsScene();
            return;
        }
        // Clear the fallback rig before adding the packaged scene.
        std::vector<ECS::Entity> all = engine.world().entities();
        for (ECS::Entity e : all)
            if (engine.world().isAlive(e)) engine.world().destroyEntity(e);
        buildFromActivePackage();
        game_status_ = "cascade mini-game loaded from " + ResourceIO::pathFilename(path);
    }

    //======================================================================
    // Animation scene (2D sprite sheet + 3D compute vertex animation)
    //======================================================================
    void DemoApp::buildAnimationScene()
    {
        ECS::World &world = engine.world();
        Render::RenderScene &scene = engine.render().scene();
        buildMinimalRig();
        if (auto *ctrl = world.getComponent<ECS::CameraControllerComponent>(camera_))
        {
            ctrl->orbit_target = Math::Vec3(0.0f, 1.2f, 0.0f);
            ctrl->orbit_distance = 8.0f;
            ctrl->orbit_yaw_degrees = 30.0f;
            ctrl->orbit_pitch_degrees = 18.0f;
        }
        Render::RenderSettings &settings = engine.render().settings();
        settings.ambient_intensity = 1.3f;
        settings.ambient_color = Math::Color(0.05f, 0.06f, 0.08f);

        Render::MaterialPtr anim_mat = scene.createMaterial("Animation surface");
        anim_mat->base_color = Math::Color(0.25f, 0.65f, 0.95f, 1.0f);
        anim_mat->metallic = 0.65f;
        anim_mat->roughness = 0.22f;
        anim_mat->emissive = Math::Color(0.05f, 0.2f, 0.35f);
        anim_mat->emissive_strength = 1.5f;

        // 3D compute-vertex-animated torus.
        anim_vertex_ = spawnMesh("VertexAnimatedTorus", Reflect::createMeshAsset("torus", 2.2f, 64), anim_mat,
                                 Math::Vec3(0.0f, 1.8f, 0.0f));
        if (auto *t = world.getComponent<ECS::TransformComponent>(anim_vertex_))
            t->setEulerDegrees(90.0f, 0.0f, 0.0f);
        ECS::VertexAnimationComponent va;
        va.amplitude = vertex_amplitude_;
        va.frequency = vertex_frequency_;
        va.speed = vertex_speed_;
        world.addComponent<ECS::VertexAnimationComponent>(anim_vertex_, va);
        addLabel(anim_vertex_, "compute vertex animation", 1.6f, Math::Color(0.6f, 0.9f, 1.0f));

        // 3D sprite animation projected to screen.
        anim_sprite_ = world.createEntity("SpriteAnimation3D");
        ECS::TransformComponent st;
        st.position = Math::Vec3(3.0f, 2.0f, 0.0f);
        world.addComponent<ECS::TransformComponent>(anim_sprite_, st);
        ECS::SpriteAnimationComponent sprite;
        sprite.texture = asset("textures/sprite_sheet.png");
        sprite.columns = 4;
        sprite.rows = 2;
        sprite.frame_count = 8;
        sprite.fps = sprite_fps_;
        sprite.size = 1.1f;
        sprite.screen_space = false;
        world.addComponent<ECS::SpriteAnimationComponent>(anim_sprite_, sprite);

        // HUD sprite: the same sheet in screen space.
        ECS::Entity hud_sprite = world.createEntity("SpriteAnimation2D");
        world.addComponent<ECS::TransformComponent>(hud_sprite);
        ECS::SpriteAnimationComponent hud;
        hud.texture = sprite.texture;
        hud.columns = 4;
        hud.rows = 2;
        hud.frame_count = 8;
        hud.fps = sprite_fps_;
        hud.size = 160.0f;
        hud.screen_position = Math::Vec3(36.0f, 500.0f, 0.0f);
        world.addComponent<ECS::SpriteAnimationComponent>(hud_sprite, hud);

        sprite_playing_ = true;
        vertex_animation_enabled_ = true;
        GEN_LOG_INFO("[Demo] animation scene: sprite sheets + compute vertex animation (%s)",
                     engine.compute().supportsHardwareCompute() ? "compute enabled" : "compute unavailable");
    }

    //======================================================================
    // Ray tracing scene
    //======================================================================
    void DemoApp::buildRayTracingScene()
    {
        ECS::World &world = engine.world();
        buildMinimalRig();
        if (auto *ctrl = world.getComponent<ECS::CameraControllerComponent>(camera_))
        {
            ctrl->orbit_target = Math::Vec3(0.0f, 1.1f, 0.0f);
            ctrl->orbit_distance = 8.0f;
            ctrl->orbit_yaw_degrees = 15.0f;
            ctrl->orbit_pitch_degrees = 12.0f;
        }
        Render::RenderSettings &settings = engine.render().settings();
        settings.show_skybox = false;
        settings.use_ibl = false;
        settings.ambient_intensity = 0.0f;
        settings.ambient_color = Math::Color(0.0f, 0.0f, 0.0f);

        rt_scene_.clear();
        const int count = 8;
        for (int i = 0; i < count; ++i)
        {
            API::RTSphere sphere;
            const float a = (float)i / (float)count * Math::TWO_PI;
            const float ring = 2.6f;
            sphere.center = Math::Vec3(std::cos(a) * ring, 0.7f + (float)(i % 3) * 0.45f,
                                       std::sin(a) * ring - 1.0f);
            sphere.radius = 0.5f + (float)(i % 3) * 0.12f;
            sphere.color = Math::Color(0.2f + 0.08f * (float)i, 0.45f + 0.05f * (float)(i % 4),
                                       0.85f - 0.07f * (float)i, 1.0f);
            sphere.metallic = (i % 3 == 0) ? 1.0f : 0.0f;
            rt_scene_.push_back(sphere);
        }
        engine.rayTracing().setScene(rt_scene_);
        rt_settings_.width = 320;
        rt_settings_.height = 180;
        rt_settings_.samples = 2;
        rt_auto_trace_ = true;
        rt_trace_requested_ = true;
        if (!rt_cb_) rt_cb_ = std::make_shared<Render::CommandBuffer>("ray_tracing_overlay");
        rt_cb_->clear();
        GEN_LOG_INFO("[Demo] ray tracing scene: %zu spheres, backend %s", rt_scene_.size(),
                     engine.rayTracing().backendName().c_str());
    }

    //======================================================================
    // Media playback scene (libavcodec + SDL audio, no subprocess)
    //======================================================================
    void DemoApp::buildMediaScene()
    {
        buildMinimalRig();
        Render::RenderSettings &settings = engine.render().settings();
        settings.show_skybox = false;
        settings.use_ibl = false;
        settings.ambient_intensity = 0.0f;
        settings.ambient_color = Math::Color(0.0f, 0.0f, 0.0f);
        if (!media_cb_) media_cb_ = std::make_shared<Render::CommandBuffer>("media_overlay");
        media_cb_->clear();
        if (media_path_[0] == '\0')
            std::snprintf(media_path_, sizeof(media_path_), "%s", asset("media/sample.mp4").c_str());
        media_player_ = engine.encoderDecoder().createPlayer();
        if (!media_player_)
        {
            media_status_ = "EncoderDecoder API unavailable";
            return;
        }
        std::string error;
        if (!media_player_->open(media_path_, true, &error))
        {
            media_status_ = "open failed: " + error;
            GEN_LOG_ERROR("[Demo] media open failed: %s", error.c_str());
            return;
        }
        media_player_->setVolume(media_volume_);
        media_player_->play();
        media_serial_ = 0;
        media_status_ = "playing " + std::string(media_path_);
        GEN_LOG_INFO("[Demo] media scene: %s", media_status_.c_str());
    }

    void DemoApp::updateMedia(float dt)
    {
        if (!media_player_ || scene_ != SceneKind::Media) return;
        media_player_->update(dt);
        const int width = media_player_->videoWidth();
        const int height = media_player_->videoHeight();
        const uint8_t *pixels = media_player_->videoPixels();
        if (pixels && width > 0 && height > 0 && media_player_->videoSerial() != media_serial_)
        {
            if (!media_texture_ || media_texture_width_ != width || media_texture_height_ != height)
            {
                if (media_texture_) engine.device()->destroyTexture(media_texture_);
                RHI::TextureDesc desc;
                desc.format = RHI::PixelFormat::RGBA8_UNORM;
                desc.width = (uint32_t)width;
                desc.height = (uint32_t)height;
                desc.usage = (uint32_t)RHI::TextureUsageFlagBits::Sampled |
                             (uint32_t)RHI::TextureUsageFlagBits::TransferDst;
                desc.name = "media_video";
                media_texture_ = engine.device()->createTexture(desc);
                media_texture_width_ = width;
                media_texture_height_ = height;
                if (!media_sampler_)
                {
                    RHI::SamplerDesc sd;
                    sd.min_filter = RHI::FilterMode::Linear;
                    sd.mag_filter = RHI::FilterMode::Linear;
                    sd.address_u = RHI::AddressMode::ClampToEdge;
                    sd.address_v = RHI::AddressMode::ClampToEdge;
                    sd.name = "media_sampler";
                    media_sampler_ = engine.device()->createSampler(sd);
                }
            }
            if (media_texture_)
                engine.device()->writeTexture(media_texture_, pixels, (uint64_t)width * height * 4, (uint32_t)width,
                                              (uint32_t)height);
            media_serial_ = media_player_->videoSerial();
        }

        media_cb_->clear();
        const float target_w = (float)(engine.render().hdrTarget() ? engine.render().hdrTarget()->width() : 1280u);
        const float target_h = (float)(engine.render().hdrTarget() ? engine.render().hdrTarget()->height() : 720u);
        media_cb_->drawRect(0, 0, target_w, target_h, Math::Color(0.01f, 0.01f, 0.015f, 1.0f));
        if (media_texture_ && width > 0 && height > 0)
        {
            const float aspect = (float)width / (float)height;
            float w = target_w * 0.86f;
            float h = w / aspect;
            if (h > target_h * 0.82f)
            {
                h = target_h * 0.82f;
                w = h * aspect;
            }
            const float x = (target_w - w) * 0.5f;
            const float y = (target_h - h) * 0.5f;
            media_cb_->drawSprite(media_texture_, media_sampler_, x, y, w, h, Math::Vec2(0, 1), Math::Vec2(1, 0),
                                  Math::Color(1, 1, 1, 1));
        }
        const float s = target_h / 720.0f;
        const Media::MediaInfo &info = media_player_->info();
        media_cb_->drawRect(16 * s, 16 * s, 760 * s, 64 * s, Math::Color(0, 0, 0, 0.62f));
        media_cb_->drawText("MediaPlayer (libavcodec, in-process)  |  " + info.video_codec + " " +
                                std::to_string(info.width) + "x" + std::to_string(info.height) + "  |  audio " +
                                info.audio_codec + " " + std::to_string(info.sample_rate) + " Hz  |  " +
                                std::to_string(media_player_->position()).substr(0, 5) + " / " +
                                std::to_string(media_player_->duration()).substr(0, 5) + " s",
                            30 * s, 24 * s, 18 * s, Math::Color(0.7f, 0.9f, 1.0f), Text::TextAlign::Left, 0.15f);
        media_cb_->drawText(media_player_->playing() ? "playing" : "paused",
                            30 * s, 50 * s, 15 * s,
                            media_player_->playing() ? Math::Color(0.4f, 1.0f, 0.6f) : Math::Color(1.0f, 0.6f, 0.3f),
                            Text::TextAlign::Left, 0.1f);
    }

    //======================================================================
    // Per frame
    //======================================================================
    void DemoApp::tick(float dt)
    {
        applyPendingScene();
        if (options.tour) tourStep(dt);
        time_ += dt;
        if (preview_rotate_) preview_yaw_ += 0.6f * dt;
        animate(dt);
        if (scene_ == SceneKind::Gaussian && gaussian_auto_orbit_)
            engine.world().each<ECS::CameraControllerComponent>(
                [dt](ECS::Entity, ECS::CameraControllerComponent &c) { c.orbit_yaw_degrees += dt * 9.0f; });
        // Scripts may switch the pipeline too: only write it when the demo toggle changes.
        Render::RenderSettings &s = engine.render().settings();
        if (custom_pipeline_ != applied_custom_)
        {
            s.pipeline_mode = custom_pipeline_ ? Render::PipelineMode::Custom : Render::PipelineMode::Forward;
            applied_custom_ = custom_pipeline_;
        }
        else if (custom_pipeline_ && s.pipeline_mode != Render::PipelineMode::Custom)
        {
            custom_pipeline_ = applied_custom_ = false;
        }
        recordMaterialPreview();
        recordMonitor();
        recordCustomPipeline();
        recordTextLab();
        recordPhysicsDebug();
        recordGaussian();
        recordRayTracing();
        updateMedia(dt);
        if (codec_capture_requested_)
        {
            codec_capture_requested_ = false;
            std::vector<uint8_t> rgba;
            uint32_t w = 0, h = 0;
            if (engine.captureFrame(rgba, w, h))
            {
                API::FrameImage frame;
                frame.width = w;
                frame.height = h;
                frame.rgba = std::move(rgba);
                std::vector<uint8_t> png;
                std::string error;
                if (engine.encoderDecoder().encodeFrame(frame, png, API::Codec::PNG, &error))
                {
                    ResourceIO::writeFile("/tmp/genesis_codec_ui_frame.png", png.data(), png.size());
                    codec_status_ = "captured frame -> /tmp/genesis_codec_ui_frame.png (" + std::to_string(w) +
                                    "x" + std::to_string(h) + ")";
                }
                else codec_status_ = "PNG encode failed: " + error;
            }
            else codec_status_ = "frame capture failed";
        }
    }

    void DemoApp::recordPhysicsDebug()
    {
        engine.physics().settings().debug_draw = physics_debug_;
        if (!physics_debug_) return;
        if (scene_ == SceneKind::Physics || scene_ == SceneKind::Game)
            engine.physics().drawDebug(engine.render());
    }

    void DemoApp::recordGaussian()
    {
        if (!gaussian_cb_) return;
        gaussian_cb_->clear();
        if (scene_ != SceneKind::Gaussian || !gaussian_cloud_.loaded()) return;
        gaussian_renderer_.record(engine.render(), *gaussian_cb_, gaussian_cloud_, gaussian_settings_);
        const auto &stats = gaussian_renderer_.stats();
        const float s = engine.render().hdrTarget() ? engine.render().hdrTarget()->height() / 720.0f : 1.0f;
        gaussian_cb_->drawRect(16.0f * s, 16.0f * s, 520.0f * s, 58.0f * s, Math::Color(0.01f, 0.01f, 0.02f, 0.65f));
        gaussian_cb_->drawText("3D Gaussian Splatting  |  " + std::to_string(stats.drawn) + " splats, depth sorted",
                               30.0f * s, 24.0f * s, 20.0f * s, Math::Color(0.75f, 0.9f, 1.0f), Text::TextAlign::Left, 0.15f);
        gaussian_cb_->drawText("CPU projection + sort -> RHI alpha quads (all three backends)",
                               30.0f * s, 48.0f * s, 15.0f * s, Math::Color(0.65f, 0.75f, 0.9f), Text::TextAlign::Left, 0.1f);
    }

    void DemoApp::recordRayTracing()
    {
        if (!rt_cb_) return;
        rt_cb_->clear();
        if (scene_ != SceneKind::RayTracing) return;
        API::RayTracingAPI &rt = engine.rayTracing();
        if (!rt.available())
        {
            rt_cb_->drawText("Ray tracing API unavailable", 24, 24, 24, Math::Color(1, 0.4, 0.4));
            return;
        }
        if (rt_auto_trace_ || rt_trace_requested_)
        {
            const Render::CameraState &camera = engine.render().camera();
            API::RTCamera rt_camera;
            rt_camera.eye = camera.position;
            rt_camera.forward = camera.forward;
            rt_camera.right = camera.right;
            rt_camera.up = camera.up;
            rt_camera.fov_degrees = camera.fov_degrees;
            const Render::RenderTexturePtr target = engine.render().hdrTarget();
            rt_camera.aspect = target ? (float)target->width() / (float)std::max(1u, target->height()) : 16.0f / 9.0f;
            rt.setScene(rt_scene_);
            const bool ok = rt.trace(rt_camera, rt_settings_);
            rt_last_ms_ = rt.stats().trace_ms;
            rt_status_ = ok ? (rt.stats().gpu ? "GPU compute trace" : "CPU fallback trace") : "trace failed";
            rt_trace_requested_ = false;
        }

        const float width = (float)(engine.render().hdrTarget() ? engine.render().hdrTarget()->width() : 1280);
        const float height = (float)(engine.render().hdrTarget() ? engine.render().hdrTarget()->height() : 720);
        rt_cb_->drawRect(0, 0, width, height, Math::Color(0.01f, 0.01f, 0.02f, 1.0f));
        if (rt.outputTexture() && !rt.pixels().empty())
        {
            const float src_aspect = (float)rt.stats().width / (float)std::max(1u, rt.stats().height);
            float w = width * 0.82f;
            float h = w / src_aspect;
            if (h > height * 0.82f)
            {
                h = height * 0.82f;
                w = h * src_aspect;
            }
            const float x = (width - w) * 0.5f;
            const float y = (height - h) * 0.5f;
            // The image is uploaded top-down: flip V in the UV rect.
            rt_cb_->drawSprite(rt.outputTexture(), rt.sampler(), x, y, w, h, Math::Vec2(0, 1),
                               Math::Vec2(1, 0), Math::Color(1, 1, 1, 1));
        }
        const float s = height / 720.0f;
        rt_cb_->drawRect(16 * s, 16 * s, 620 * s, 62 * s, Math::Color(0, 0, 0, 0.62f));
        rt_cb_->drawText("Ray Tracing API  |  " + rt.backendName() + "  |  " +
                             std::to_string(rt.stats().width) + "x" + std::to_string(rt.stats().height) +
                             "  " + std::to_string(rt.stats().samples) + " spp  |  " +
                             std::to_string(rt_last_ms_).substr(0, 5) + " ms",
                         30 * s, 24 * s, 19 * s, Math::Color(0.7f, 0.9f, 1.0f), Text::TextAlign::Left, 0.15f);
        rt_cb_->drawText("spheres " + std::to_string(rt_scene_.size()) +
                             "   shadows + one reflection bounce   (shader: rt/raytrace.comp)",
                         30 * s, 50 * s, 15 * s, Math::Color(0.75f, 0.8f, 0.9f), Text::TextAlign::Left, 0.1f);
    }

    void DemoApp::animate(float dt)
    {
        ECS::World &world = engine.world();
        if (ECS::TransformComponent *t = world.getComponent<ECS::TransformComponent>(sun_))
        {
            float el = Math::toRadians(sun_elevation_), az = Math::toRadians(sun_azimuth_);
            Math::Vec3 dir(std::cos(el) * std::sin(az), -std::sin(el), std::cos(el) * std::cos(az));
            t->rotation = Math::Quat::lookRotation(dir, Math::Vec3(0, 1, 0));
            t->local_dirty = true;
        }
        if (!animate_lights_) return;
        if (ECS::TransformComponent *t = world.getComponent<ECS::TransformComponent>(warm_))
            t->setPosition(Math::Vec3(std::cos(time_ * 0.7f) * 3.0f, 1.6f + std::sin(time_ * 1.3f) * 0.4f, std::sin(time_ * 0.7f) * 3.0f));
        if (ECS::TransformComponent *t = world.getComponent<ECS::TransformComponent>(cool_))
            t->setPosition(Math::Vec3(std::cos(time_ * 0.5f + 2.4f) * 3.4f, 1.3f, std::sin(time_ * 0.5f + 2.4f) * 3.4f));
        (void)dt;
    }

    // --tour: a fixed timeline that drives the same state the UI panels edit.
    //   0 - 5 s   showcase, material editor: live roughness / colour edits
    //   5 - 9 s   CommandBuffer lab: custom pipeline (edge), then split view
    //   9 - 12 s  SDF text lab: 2D size ramp + spinning 3D text
    //  12 - 16 s  scene built by Lua through RTTI
    void DemoApp::tourStep(float dt)
    {
        tour_time_ += dt;
        const float t = tour_time_;
        const int phase = t < 5.0f ? 0 : t < 9.0f ? 1 : t < 12.0f ? 2 : 3;
        if (phase != tour_phase_)
        {
            tour_phase_ = phase;
            show_render_ = show_inspector_ = show_console_ = show_log_ = show_help_ = false;
            show_engine_ = true;
            show_material_ = phase == 0;
            show_cb_ = phase == 1;
            show_text_ = phase == 2;
            custom_pipeline_ = phase == 1;
            custom_split_ = false;
            custom_pip_ = true;
            custom_effect_a_ = (int)Render::PostEffect::Edge;
            custom_effect_b_ = (int)Render::PostEffect::None;
            if (phase == 0)
            {
                if (Render::MaterialPtr gold = engine.render().scene().findMaterial("Gold"))
                {
                    material_index_ = gold->index;
                    scroll_to_material_ = true;
                }
            }
            if (phase == 3) pending_scene_ = SceneKind::LuaScene;
        }
        if (phase == 0)
        {
            if (Render::MaterialPtr m = selectedMaterial())
            {
                const float k = 0.5f + 0.5f * std::sin(t * 1.6f);
                m->roughness = 0.05f + 0.7f * k;
                m->base_color = Math::Color(1.0f, 0.76f - 0.4f * k, 0.33f + 0.4f * k, 1.0f);
                m->touch();
            }
        }
        if (phase == 1) custom_split_ = t > 7.0f;
        if (phase == 2) text2d_size_ = 36.0f + 18.0f * std::sin((t - 9.0f) * 2.0f);
        // Gentle sway (+-15 degrees) so the view stays on the front of the scene.
        const float sway = 15.0f * 0.8f * std::cos(t * 0.8f) * dt;
        engine.world().each<ECS::CameraControllerComponent>([sway](ECS::Entity, ECS::CameraControllerComponent &c)
                                                            { c.orbit_yaw_degrees += sway; });
    }

    // Material preview: a command buffer injected before the scene renders the
    // selected material on a preview mesh into its own render texture, then
    // tone maps it into an LDR texture shown by the UI.
    void DemoApp::recordMaterialPreview()
    {
        preview_cb_->clear();
        Render::MaterialPtr mat = selectedMaterial();
        if (!show_material_ || !mat || !engine.ui().ui_visible) return;
        Reflect::MeshAssetPtr mesh = preview_meshes_[std::min<size_t>(preview_mesh_, preview_meshes_.size() - 1)];
        if (!mesh) mesh = preview_meshes_[0];

        Render::CommandBuffer &cb = *preview_cb_;
        const float dist = preview_mesh_ == 3 ? 2.9f : 3.0f;
        cb.setRenderTarget(preview_hdr_);
        cb.clearRenderTarget(true, Math::Color(0.06f, 0.065f, 0.08f, 1.0f), true);
        cb.setCamera(Math::Vec3(0.0f, 0.5f, dist), Math::Vec3(0.0f), 38.0f, 0.05f, 50.0f);
        if (preview_sky_) cb.drawSkybox();
        Math::Mat4 model = Math::Mat4::fromQuat(Math::Quat::fromAxisAngle(Math::Vec3(0, 1, 0), preview_yaw_));
        for (uint32_t gm : mesh->gpu_meshes) cb.drawMesh(gm, model, mat);
        cb.blit(Render::RenderTargetId::of(preview_hdr_), Render::RenderTargetId::of(preview_ldr_));
    }

    // Security camera: renders the scene from an orbiting viewpoint into a
    // render texture that a material on the monitor mesh samples.
    void DemoApp::recordMonitor()
    {
        monitor_cb_->clear();
        if (!monitor_enabled_) return;
        Render::CommandBuffer &cb = *monitor_cb_;
        Math::Vec3 eye(std::cos(time_ * 0.35f) * 7.5f, 3.6f, std::sin(time_ * 0.35f) * 7.5f);
        cb.setRenderTarget(monitor_rt_);
        cb.clearRenderTarget(true, Math::Color(0, 0, 0, 1), true);
        cb.setCamera(eye, Math::Vec3(0.0f, 0.9f, 0.0f), 55.0f);
        cb.drawSkybox();
        cb.drawScene();
        cb.drawText3D("REC", Math::Vec3(0.0f, 3.0f, 0.0f), 0.5f, Math::Color(1.0f, 0.15f, 0.15f), true,
                      Text::TextAlign::Center, 0.2f);
    }

    // Programmable pipeline: with PipelineMode::Custom the built-in forward
    // pipeline is skipped and this command buffer builds the whole frame.
    void DemoApp::recordCustomPipeline()
    {
        custom_cb_->clear();
        if (!custom_pipeline_) return;
        Render::RenderSystem &rs = engine.render();
        const uint32_t w = rs.hdrTarget()->width(), h = rs.hdrTarget()->height();
        if (custom_scene_->width() != w || custom_scene_->height() != h)
        {
            custom_scene_->resize(w, h);
            custom_ldr_->resize(w, h);
            custom_fx_->resize(w, h);
        }
        using RT = Render::RenderTargetId;
        Render::CommandBuffer &cb = *custom_cb_;
        // 1. scene into an HDR texture (plus script-style gizmos)
        cb.setRenderTarget(custom_scene_);
        cb.clearRenderTarget(true, Math::Color(0, 0, 0, 1), true);
        cb.resetCamera();
        if (rs.settings().show_skybox) cb.drawSkybox();
        cb.drawScene();
        for (int i = 0; i < 16; ++i)
        {
            float a = time_ * 0.5f + i * Math::PI / 8.0f;
            cb.drawLine3D(Math::Vec3(0.0f, 0.03f, 0.0f), Math::Vec3(std::cos(a) * 4.0f, 0.03f, std::sin(a) * 4.0f),
                          Math::Color(0.2f, 0.9f, 1.0f, 0.7f), 0.025f);
        }
        // 2. tone map, 3. two chained effects, 4. present
        cb.blit(RT::of(custom_scene_), RT::of(custom_ldr_));
        cb.blit(RT::of(custom_ldr_), RT::of(custom_fx_), (Render::PostEffect)custom_effect_a_, Math::Vec4(1.0f, 2.5f, 0, 0),
                Math::Color(0.25f, 1.0f, 0.6f));
        cb.blit(RT::of(custom_fx_), RT::backbuffer(), (Render::PostEffect)custom_effect_b_, Math::Vec4(1.0f, 3.0f, 0, 0),
                Math::Color(1.0f, 0.85f, 0.6f));
        cb.setRenderTarget(RT::backbuffer());
        const float fw = (float)w, fh = (float)h;
        if (custom_split_)
        {
            // left half: untouched image (drawImage samples the full texture
            // into the given rectangle, so crop by viewport)
            cb.setViewport(0.0f, 0.0f, fw * 0.5f, fh);
            cb.drawImage(custom_ldr_, 0.0f, 0.0f, fw, fh);
            cb.resetViewport();
            cb.drawLine2D(Math::Vec2(fw * 0.5f, 0.0f), Math::Vec2(fw * 0.5f, fh), Math::Color(1, 1, 1, 0.9f), 2.0f);
        }
        if (custom_pip_)
        {
            const float pw = fw * 0.26f, ph = fh * 0.26f, x = fw * 0.5f - pw * 0.5f, y = fh * 0.08f;
            cb.drawRect(x - 4.0f, y - 4.0f, pw + 8.0f, ph + 8.0f, Math::Color(1, 1, 1, 0.9f));
            cb.drawImage(custom_ldr_, x, y, pw, ph);
            cb.drawText("scene before effects", x + 8.0f, y + 6.0f, 16.0f * fh / 720.0f, Math::Color(1, 1, 1),
                        Text::TextAlign::Left, 0.25f);
        }
        const float ts = fh / 720.0f;
        cb.drawRect(fw - 680.0f * ts, fh - 44.0f * ts, 670.0f * ts, 34.0f * ts, Math::Color(0, 0, 0, 0.6f));
        cb.drawText(std::string("custom pipeline: scene -> HDR RT -> tonemap -> ") +
                        Render::postEffectName((Render::PostEffect)custom_effect_a_) + " -> " +
                        Render::postEffectName((Render::PostEffect)custom_effect_b_) + " -> backbuffer",
                    fw - 20.0f * ts, fh - 38.0f * ts, 20.0f * ts, Math::Color(1, 1, 1), Text::TextAlign::Right, 0.25f);
    }

    void DemoApp::recordTextLab()
    {
        hud_cb_->clear();
        Render::RenderSystem &rs = engine.render();
        const float h = (float)rs.hdrTarget()->height();
        if (!show_text_) return;
        if (text2d_enabled_)
        {
            const float s = h / 720.0f;
            hud_cb_->drawText(text2d_, text2d_pos_[0] * s, text2d_pos_[1] * s, text2d_size_ * s, text2d_color_,
                              (Text::TextAlign)text2d_align_, text2d_outline_, text2d_outline_color_);
            // size ramp: the same SDF atlas at many sizes
            float y = text2d_pos_[1] * s + text2d_size_ * s * 1.3f;
            for (int i = 0; i < 5; ++i)
            {
                float size = (12.0f + i * 10.0f) * s;
                hud_cb_->drawText("SDF " + std::to_string((int)(12 + i * 10)) + "px", text2d_pos_[0] * s, y, size,
                                  Math::Color(0.95f, 0.95f, 1.0f), Text::TextAlign::Left, 0.15f,
                                  Math::Color(0.05f, 0.05f, 0.1f, 0.9f));
                y += size * 1.15f;
            }
        }
        if (text3d_enabled_)
        {
            Render::CommandBuffer &imm = rs.immediate(Render::CameraEvent::AfterTransparent);
            imm.setDepthTest3D(text3d_depth_);
            if (text3d_billboard_)
                imm.drawText3D(text3d_, text3d_pos_, text3d_size_, text3d_color_, true, Text::TextAlign::Center, text3d_outline_);
            else
            {
                float yaw = text3d_spin_ ? time_ * 0.8f : 0.0f;
                Math::Mat4 m = Math::Mat4::translate(text3d_pos_) *
                               Math::Mat4::fromQuat(Math::Quat::fromAxisAngle(Math::Vec3(0, 1, 0), yaw));
                imm.drawText3D(text3d_, m, text3d_size_, text3d_color_, Text::TextAlign::Center, text3d_outline_);
            }
        }
    }

    Render::MaterialPtr DemoApp::selectedMaterial()
    {
        Render::RenderScene &scene = engine.render().scene();
        if (material_index_ < 0 || (size_t)material_index_ >= scene.materialCount())
            material_index_ = scene.materialCount() ? 0 : -1;
        return scene.materialPtr(material_index_);
    }

    Render::MaterialPtr DemoApp::entityMaterial(ECS::Entity e)
    {
        Render::RenderSystem &rs = engine.render();
        ECS::MeshComponent *mc = engine.world().getComponent<ECS::MeshComponent>(e);
        const Render::RenderSystem::EntityMeshBinding *b = rs.binding(e);
        if (!mc || !b || b->mesh_indices.empty()) return nullptr;
        if (!mc->material_overrides.empty() && mc->material_overrides[0] >= 0)
            return rs.scene().materialPtr(mc->material_overrides[0]);
        Render::GpuMesh *gm = rs.scene().mesh(b->mesh_indices[0]);
        return gm ? rs.scene().materialPtr(gm->material_index) : nullptr;
    }

    //======================================================================
    void DemoApp::handleShortcuts()
    {
        Platform::InputSystem &in = engine.input();
        if (in.ui_wants_keyboard) return;
        Render::RenderSettings &s = engine.render().settings();
        if (in.keyPressed(SDL_SCANCODE_F1)) engine.ui().ui_visible = !engine.ui().ui_visible;
        if (in.keyPressed(SDL_SCANCODE_F2))
            s.shading_mode = s.shading_mode == Render::ShadingMode::CookTorrance ? Render::ShadingMode::BlinnPhong
                                                                                 : Render::ShadingMode::CookTorrance;
        if (in.keyPressed(SDL_SCANCODE_F3)) s.wireframe = !s.wireframe;
        if (in.keyPressed(SDL_SCANCODE_F4)) s.use_ibl = !s.use_ibl;
        if (in.keyPressed(SDL_SCANCODE_F5)) custom_pipeline_ = !custom_pipeline_;
        if (in.keyPressed(SDL_SCANCODE_F6))
            s.post_effect = (Render::PostEffect)(((uint32_t)s.post_effect + 1) % (uint32_t)Render::PostEffect::Count);
    }

} // namespace Demo
