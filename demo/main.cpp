/*
 * Genesis Render - demo application entry point.
 *
 * Command line:
 *   --backend gl|vulkan|metal  rendering backend (default gl)
 *   --scene NAME               showcase | lua | commandbuffer | empty
 *   --script FILE.lua          attach an extra Lua script (start/tick hooks)
 *   --lab NAME                 open a feature lab: material | commandbuffer |
 *                              custom | text | inspector
 *   --headless                 hidden window, no presentation (CI friendly)
 *   --frames N                 exit after N frames
 *   --screenshot PATH.png      capture a frame to disk and exit
 *   --showcase DIR             render a labelled set of proof images (headless)
 *   --width / --height         window size (default 1280x720)
 *   --validation               enable backend validation (Vulkan layers,
 *                              GL error checks, Metal API validation)
 *   --no-ui / --no-script      skip ImGui / Lua
 *   --asset-dir PATH           asset root (default: GENESIS_ASSET_DIR)
 *   --selftest                 drive every scene / panel / interaction path
 *                              for a few frames and exit (non-zero on errors)
 */
#include "demo_app.hpp"

#include "foundation_layer/genesis_log.hpp"
#include "resource/procedural_mesh.hpp"
#include "resource/resource_types.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace Genesis;
using Demo::DemoOptions;

namespace
{
    void printUsage()
    {
        std::printf(
            "usage: genesis_demo [--backend gl|vulkan|metal] [--scene showcase|lua|commandbuffer|empty]\n"
            "                    [--script file.lua] [--lab material|commandbuffer|custom|text|inspector]\n"
            "                    [--headless] [--frames N] [--screenshot out.png] [--showcase DIR]\n"
            "                    [--width W] [--height H] [--validation] [--no-ui] [--no-script]\n"
            "                    [--asset-dir DIR] [--selftest]\n"
            "\n"
            "  scenes   showcase       C++ scene: models, material library, labels, monitor\n"
            "           lua            scene built entirely by assets/scripts/scene_builder.lua\n"
            "           commandbuffer  showcase + assets/scripts/commandbuffer_demo.lua\n"
            "           empty          camera, sun and sky only\n"
            "  --showcase DIR  headless: render labelled PNGs that each isolate one capability\n");
    }

    DemoOptions parseArgs(int argc, char **argv, bool &exit_now)
    {
        DemoOptions o;
        exit_now = false;
        for (int i = 1; i < argc; ++i)
        {
            std::string a = argv[i];
            auto next = [&](const char *fallback) -> std::string { return (i + 1 < argc) ? argv[++i] : fallback; };
            if (a == "--backend")
            {
                std::string v = next("gl");
                if (v == "vulkan" || v == "vk") o.backend = RHI::BackendType::Vulkan;
                else if (v == "metal" || v == "mtl") o.backend = RHI::BackendType::Metal;
                else o.backend = RHI::BackendType::OpenGL;
            }
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
            else if (a == "--script") o.script = next("");
            else if (a == "--lab") o.lab = next("");
            else if (a == "--selftest") o.selftest = true;
            else if (a == "--scene")
            {
                std::string v = next("showcase");
                if (!Demo::sceneFromName(v, o.scene)) GEN_LOG_WARN("unknown scene '%s' (showcase | lua | commandbuffer | empty)", v.c_str());
            }
            else if (a == "--help" || a == "-h")
            {
                printUsage();
                exit_now = true;
            }
            else GEN_LOG_WARN("unknown argument '%s' (see --help)", a.c_str());
        }
        if (!o.screenshot.empty() && o.frames == 0) o.frames = 3;
        return o;
    }

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
        const std::string be = options.backend == RHI::BackendType::Vulkan ? "vk" : options.backend == RHI::BackendType::Metal ? "mtl" : "gl";
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

} // namespace

//======================================================================
int main(int argc, char **argv)
{
    Logger::instance().setMinLevel(LogLevel::Info);
    Logger::instance().setColorOutput(true);

    bool exit_now = false;
    DemoOptions options = parseArgs(argc, argv, exit_now);
    if (exit_now) return 0;
    GEN_LOG_INFO("=== Genesis Render demo (%s backend) ===", RHI::backendTypeName(options.backend));

    if (!RHI::backendAvailable(options.backend))
    {
        GEN_LOG_ERROR("the %s backend is not compiled in for this platform", RHI::backendTypeName(options.backend));
        return 2;
    }

    if (!options.showcase_dir.empty()) return runShowcase(options);

    Demo::DemoApp app;
    app.options = options;
    if (!app.initialize())
    {
        GEN_LOG_ERROR("demo initialisation failed");
        return 1;
    }

    if (options.selftest)
    {
        int errors = app.selfTest();
        app.engine.shutdown();
        return errors == 0 ? 0 : 4;
    }

    if (!options.screenshot.empty())
    {
        // Warm up (script start, IBL bake, first frames) then capture offscreen.
        Platform::Timer timer;
        for (uint32_t i = 0; i + 1 < options.frames; ++i)
        {
            app.engine.tick();
            app.tick((float)timer.tick());
        }
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
    GEN_LOG_INFO("demo finished: %llu frames, %u warnings, %u errors", (unsigned long long)app.engine.frameCount(),
                 Logger::instance().warningCount(), Logger::instance().errorCount());
    return Logger::instance().errorCount() > 0 ? 3 : 0;
}
