/*
 * Genesis Render - demo application: scenes and per-frame logic.
 */
#include "demo_app.hpp"

#include "foundation_layer/genesis_log.hpp"
#include "renderer/render_system.hpp"
#include "resource/procedural_mesh.hpp"
#include "resource/resource_types.hpp"
#include "script/script_system.hpp"
#include "ui/ui_system.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <dirent.h>

namespace Demo
{
    const char *sceneName(SceneKind k)
    {
        switch (k)
        {
        case SceneKind::Showcase: return "showcase";
        case SceneKind::LuaScene: return "lua";
        case SceneKind::LuaCommandBuffer: return "commandbuffer";
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
        return false;
    }

    std::string DemoApp::asset(const std::string &relative) const
    {
        return ResourceIO::joinPath(options.asset_dir, relative);
    }

    //======================================================================
    bool DemoApp::initialize()
    {
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

        if (options.lab.empty() || options.lab == "material") show_material_ = true;
        else if (options.lab == "commandbuffer") show_cb_ = true;
        else if (options.lab == "custom") { show_cb_ = true; custom_pipeline_ = true; }
        else if (options.lab == "text") show_text_ = true;
        else if (options.lab == "inspector") show_inspector_ = true;

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
        if (engine.hasScript())
            world.each<ECS::ScriptComponent>([this](ECS::Entity, ECS::ScriptComponent &c) { engine.script().unloadComponentScript(c); });
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
    // Per frame
    //======================================================================
    void DemoApp::tick(float dt)
    {
        applyPendingScene();
        time_ += dt;
        animate(dt);
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

    // Material preview: a command buffer injected before the scene renders the
    // selected material on a preview mesh into its own render texture, then
    // tone maps it into an LDR texture shown by the UI.
    void DemoApp::recordMaterialPreview()
    {
        preview_cb_->clear();
        Render::MaterialPtr mat = selectedMaterial();
        if (!show_material_ || !mat || !engine.ui().ui_visible) return;
        if (preview_rotate_) preview_yaw_ += 0.6f * (float)engine.frameTimeMs() * 0.001f;
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
