/*
 * Genesis Render - PBR / ECS / multi-backend demo.
 *
 *   --backend opengl|vulkan   render backend (default: vulkan)
 *   --width N --height N      window / render size
 *   --frames N                exit automatically after N frames
 *   --screenshot path.png     write a PNG of the 3D scene at exit
 *   --selftest                headless validation run (assertions + PNGs)
 *   --model path.glb|fbx|obj  extra model to import
 *   --hdri path.hdr           equirect HDR (otherwise bundled procedural sky)
 *   --script path.lua         attach a Lua script to the first model
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <cmath>
#include <functional>

#include "foundation_layer/memory.hpp"
#include "foundation_layer/log.hpp"
#include "application_layer/window.hpp"
#include "rhi/rhi.hpp"
#include "resource/asset_manager.hpp"
#include "ecs/scene.hpp"
#include "render/renderer.hpp"
#include "script/script_system.hpp"
#include "ui/ui_service.hpp"

#include "external/imgui/imgui.h"
#include "external/imgui/imgui_impl_sdl.h"

#include <SDL2/SDL.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "external/stb_image_write.h"

using namespace Genesis;

namespace
{
    struct Args
    {
        RenderBackend backend = RenderBackend::Vulkan;
        uint32_t width = 1280, height = 800;
        int frames = -1;
        std::string screenshot;
        bool selftest = false;
        std::string model, hdri, script, select, addkind; int addn = 0;
    };

    std::string resolveAsset(const std::string &rel)
    {
        std::vector<std::string> roots;
        if (const char *env = getenv("GENESIS_ASSETS")) roots.emplace_back(env);
#ifdef GENESIS_ASSETS_DIR
        roots.emplace_back(GENESIS_ASSETS_DIR);
#endif
        roots.push_back("assets");
        roots.push_back("../assets");
        for (auto &r : roots)
        {
            std::string p = r + "/" + rel;
            FILE *f = fopen(p.c_str(), "rb");
            if (f) { fclose(f); return p; }
        }
        return rel;
    }

    bool savePNG(const std::string &path, const uint8_t *rgba, int w, int h)
    {
        return stbi_write_png(path.c_str(), w, h, 4, rgba, w * 4) != 0;
    }

    // Statistics over an RGBA8 screenshot, used by the self test.
    struct FrameStats
    {
        double mean = 0;
        double variance = 0;
        bool has_bright = false;
        size_t colored_pixels = 0;
        size_t total = 0;
    };
    FrameStats analyze(const std::vector<uint8_t> &px)
    {
        FrameStats s;
        s.total = px.size() / 4;
        double sum = 0;
        std::vector<double> lums;
        lums.reserve(s.total);
        for (size_t i = 0; i < s.total; ++i)
        {
            double r = px[i * 4] / 255.0, g = px[i * 4 + 1] / 255.0, b = px[i * 4 + 2] / 255;
            double l = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            lums.push_back(l);
            sum += l;
            if (l > 0.75) s.has_bright = true;
            if (std::max({r, g, b}) - std::min({r, g, b}) > 0.05) ++s.colored_pixels;
        }
        s.mean = sum / std::max<size_t>(1, s.total);
        for (double l : lums) s.variance += (l - s.mean) * (l - s.mean);
        s.variance /= std::max<size_t>(1, s.total);
        return s;
    }
}

int main(int argc, char **argv)
{
    Args args;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--backend")
        {
            std::string b = next();
            args.backend = (b == "opengl" || b == "gl") ? RenderBackend::OpenGL
                                                       : RenderBackend::Vulkan;
        }
        else if (a == "--width") args.width = atoi(next());
        else if (a == "--height") args.height = atoi(next());
        else if (a == "--frames") args.frames = atoi(next());
        else if (a == "--screenshot") args.screenshot = next();
        else if (a == "--selftest") args.selftest = true;
        else if (a == "--model") args.model = next();
        else if (a == "--hdri") args.hdri = next();
        else if (a == "--script") args.script = next();
        else if (a == "--select") args.select = next();
        else if (a == "--add") { args.addkind = next(); args.addn = atoi(next()); }
    }
    if (args.selftest && args.frames < 0) args.frames = 160;

    printf("=== Genesis Render PBR demo (%s) ===\n", backendName(args.backend));

    MemoryService::instance()->init(nullptr);

    WindowConfig wc{args.width, args.height,
                    args.selftest ? "Genesis self-test" : "Genesis Render - PBR",
                    args.backend, true, nullptr};
    Window window;
    window.init(&wc);

    uint32_t dw = 0, dh = 0;
    window.getDrawableSize(dw, dh);
    if (dw == 0) { dw = args.width; dh = args.height; }

    IRHIDevice *device = IRHIDevice::create(args.backend);
    if (!device->init((SDL_Window *)window.getWindowHandle(), dw, dh, false))
    {
        printf("FATAL: failed to initialize %s backend\n", backendName(args.backend));
        return 2;
    }

    AssetManager assets;
    assets.init(device);

    Renderer renderer;
    renderer.init(device, &assets);

    UIService ui;
    ui.init(device);

    Scene scene;
    auto scripts_owner = std::make_unique<ScriptSystem>();
    ScriptSystem *script_system = scripts_owner.get();
    scene.addSystem(std::move(scripts_owner));

    // ------------------------------------------------------------ scene build
    auto camera_e = scene.createEntity("MainCamera");
    auto &cam = scene.addComponent<CameraComponent>(camera_e, CameraComponent{});
    cam.distance = 6.5f; cam.target = Vec3(0, 0.1f, 0); cam.fov_y = 50.f; cam.pitch = 0.30f;

    auto sun_e = scene.createEntity("Sun");
    auto &sun = scene.addComponent<DirectionalLightComponent>(sun_e);
    sun.color = Vec3(1.0f, 0.95f, 0.88f);
    sun.intensity = 3.2f;
    {
        auto *t = scene.getComponent<TransformComponent>(sun_e);
        t->rotation = quatFromEuler(radians(-55.f), radians(-35.f), 0);
    }
    auto pl1 = scene.createEntity("PointLight-Red");
    scene.addComponent<PointLightComponent>(pl1, PointLightComponent{Vec3(1, 0.3f, 0.2f), 30.f, 25.f});
    scene.getComponent<TransformComponent>(pl1)->position = Vec3(-3.2f, 1.2f, 1.5f);
    auto pl2 = scene.createEntity("PointLight-Cyan");
    scene.addComponent<PointLightComponent>(pl2, PointLightComponent{Vec3(0.2f, 0.7f, 1.0f), 24.f, 25.f});
    scene.getComponent<TransformComponent>(pl2)->position = Vec3(3.2f, 1.4f, 1.2f);

    auto env_e = scene.createEntity("Environment");
    auto &env_comp = scene.addComponent<EnvironmentComponent>(env_e);

    // ground plane
    auto ground = scene.createEntity("Ground");
    {
        CPUModel cpu = procgen::makePlane(40.f);
        auto model = assets.uploadModel(cpu, "proc://plane");
        model->materials[0]->setRoughness(0.9f);
        model->materials[0]->setMetallic(0.0f);
        model->materials[0]->setBaseColor(Vec3(0.35f, 0.34f, 0.32f));
        assets.syncMaterial(*model->materials[0]);
        scene.getComponent<TransformComponent>(ground)->position = Vec3(0, -1.0f, 0);
        scene.addComponent<MeshComponent>(ground, MeshComponent{model});
    }
    // metallic + dielectric procedural spheres
    auto sphere_metal = scene.createEntity("ChromeSphere");
    {
        CPUModel cpu = procgen::makeSphere(0.85f, 48, 32);
        auto model = assets.uploadModel(cpu, "proc://sphere-metal");
        model->materials[0]->setMetallic(1.0f);
        model->materials[0]->setRoughness(0.08f);
        model->materials[0]->setBaseColor(Vec3(0.95f, 0.95f, 0.95f));
        scene.getComponent<TransformComponent>(sphere_metal)->position = Vec3(2.4f, -0.15f, 0.4f);
        scene.addComponent<MeshComponent>(sphere_metal, MeshComponent{model});
    }
    auto sphere_rough = scene.createEntity("RoughSphere");
    {
        CPUModel cpu = procgen::makeSphere(0.85f, 40, 28);
        auto model = assets.uploadModel(cpu, "proc://sphere-rough");
        model->materials[0]->setMetallic(0.0f);
        model->materials[0]->setRoughness(0.75f);
        model->materials[0]->setBaseColor(Vec3(0.75f, 0.25f, 0.2f));
        scene.getComponent<TransformComponent>(sphere_rough)->position = Vec3(-2.4f, -0.15f, -0.2f);
        scene.addComponent<MeshComponent>(sphere_rough, MeshComponent{model});
    }

    // FBX imported via Assimp (generated at build time)
    Entity fbx_e = scene.createEntity("FBX Cube (genesis_cube.fbx)");
    {
        std::string fbx = resolveAsset("models/genesis_cube.fbx");
        auto model = assets.loadModel(fbx);
        if (model)
        {
            scene.getComponent<TransformComponent>(fbx_e)->position = Vec3(-0.2f, 0.0f, 1.6f);
            scene.getComponent<TransformComponent>(fbx_e)->scale = Vec3(1.1f, 1.1f, 1.1f);
            scene.addComponent<MeshComponent>(fbx_e, MeshComponent{model});
        }
        else printf("WARN: FBX load failed\n");
    }

    // glTF 2.0 PBR sample model (embedded PBR textures)
    Entity helmet_e = scene.createEntity("DamagedHelmet.glb");
    {
        std::string glb = resolveAsset("models/DamagedHelmet.glb");
        auto model = assets.loadModel(glb);
        if (model)
        {
            auto *t = scene.getComponent<TransformComponent>(helmet_e);
            t->position = Vec3(0, -0.75f, 0);
            t->scale = Vec3(1.0f, 1.0f, 1.0f);
            scene.addComponent<MeshComponent>(helmet_e, MeshComponent{model});
        }
        else printf("WARN: glb load failed\n");
    }

    // Extra model passed on command line
    if (!args.model.empty())
    {
        auto model = assets.loadModel(args.model);
        if (model)
        {
            Entity e = scene.createEntity(args.model);
            scene.getComponent<TransformComponent>(e)->position = Vec3(0, 0, -3);
            scene.addComponent<MeshComponent>(e, MeshComponent{model});
        }
    }

    // Lua scripts
    const std::string rotate_lua = args.script.empty() ? resolveAsset("scripts/rotate.lua") : args.script;
    scene.addComponent<LuaScriptComponent>(helmet_e, LuaScriptComponent{rotate_lua, true});
    {
        std::string p = resolveAsset("scripts/light_pulse.lua");
        scene.addComponent<LuaScriptComponent>(pl1, LuaScriptComponent{p, true});
    }
    script_system->setKeyQuery([](int sc) { return SDL_GetKeyboardState(nullptr)[sc] != 0; });

    // ------------------------------------------------------------ env / settings
    TextureHandle equirect{};
    std::string hdr = args.hdri.empty() ? resolveAsset("env/venice_sunset_1k.hdr") : args.hdri;
    if (!hdr.empty()) equirect = assets.equirectFromHDR(hdr);
    if (!equirect.valid())
    {
        equirect = assets.proceduralEnvironment(1024, 512);
        env_comp.equirect_path = "procedural";
    }
    else env_comp.equirect_path = hdr;

    RenderSettings settings;
    settings.env_intensity = 1.0f;

    // ------------------------------------------------------------ main loop
    bool running = true;
    bool dragging = false;
    int frame = 0;
    float fps = 0, frame_ms = 0;
    auto t_prev = std::chrono::high_resolution_clock::now();

    std::vector<uint8_t> screenshot_px;
    std::string pending_shot;
    int test_step = 0;
    int failures = 0, passes = 0;
    auto check = [&](bool cond, const char *name) {
        if (cond) { ++passes; printf("[TEST] PASS %s\n", name); }
        else { ++failures; printf("[TEST] FAIL %s\n", name); }
    };

    EditorState editor;
    editor.device = device;
    editor.scene = &scene;
    editor.settings = &settings;
    editor.reloadScripts = [&] { script_system->reloadAll(); };
    if (!args.select.empty()) editor.selected = scene.findEntityByName(args.select);
    editor.addPrimitive = [&](const std::string &kind) {
        static int s_counter = 0;
        CPUModel cpu;
        std::string name;
        if (kind == "Sphere") { cpu = procgen::makeSphere(0.6f, 32, 24); name = "Sphere"; }
        else if (kind == "Plane") { cpu = procgen::makePlane(6.0f); name = "Plane"; }
        else { cpu = procgen::makeCube(1.0f); name = "Cube"; }
        auto model = assets.uploadModel(cpu, "proc://" + name + std::to_string(s_counter++));
        Entity e = scene.createEntity(name);
        auto *t = scene.getComponent<TransformComponent>(e);
        t->position = Vec3((s_counter % 5 - 2) * 1.8f, 0.4f, 2.5f + s_counter * 0.1f);
        scene.addComponent<MeshComponent>(e, MeshComponent{model});
        if (!kind.empty()) printf("Spawned %s entity\n", kind.c_str());
    };
    for (int i = 0; i < args.addn; ++i) editor.addPrimitive(args.addkind);

    while (running)
    {
        auto t_now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(t_now - t_prev).count();
        t_prev = t_now;
        frame_ms = dt * 1000.f;
        if (frame > 4) fps = fps == 0 ? 1.f / std::max(dt, 1e-4f) : fps * 0.95f + 0.05f / std::max(dt, 1e-4f);

        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_WINDOWEVENT &&
                (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                 ev.window.event == SDL_WINDOWEVENT_RESIZED))
            {
                uint32_t nw = 0, nh = 0;
                window.getDrawableSize(nw, nh);
                if (nw && nh) { dw = nw; dh = nh; device->onResize(nw, nh); }
            }
            if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT &&
                !ImGui::GetIO().WantCaptureMouse)
                dragging = true;
            if (ev.type == SDL_MOUSEBUTTONUP) dragging = false;
            if (ev.type == SDL_MOUSEMOTION && dragging)
            {
                cam.yaw += ev.motion.xrel * 0.005f;
                cam.pitch = std::clamp(cam.pitch + ev.motion.yrel * 0.005f, -1.45f, 1.45f);
            }
            if (ev.type == SDL_MOUSEWHEEL && !ImGui::GetIO().WantCaptureMouse)
                cam.distance = std::clamp(cam.distance * (1.0f - ev.wheel.y * 0.08f), 1.5f, 60.f);
        }
        if (window.shouldClose()) running = false;

        if (args.selftest)
        {
            cam.yaw = frame * 0.006f;
            cam.pitch = 0.25f - 0.08f * std::sin(frame * 0.02f);
        }

        // ------------------------------------------------------ logic tick
        scene.update(dt);
        if (auto *ec = scene.getComponent<EnvironmentComponent>(env_e))
        {
            settings.env_intensity = ec->intensity;
            settings.show_skybox = ec->show_skybox;
        }

        // ------------------------------------------------------ self-test steps
        bool want_capture = false;
        std::string capture_tag;
        if (args.selftest)
        {
            switch (frame)
            {
            case 40: settings.direct_lighting = true;  settings.image_based_lighting = true;
                     settings.direct_model = 0; want_capture = true; capture_tag = "pbr_ibl"; break;
            case 70: settings.direct_model = 1; want_capture = true; capture_tag = "blinnphong_ibl"; break;
            case 100: settings.direct_lighting = true; settings.image_based_lighting = false;
                      want_capture = true; capture_tag = "direct_only"; break;
            case 130: settings.direct_lighting = false; settings.image_based_lighting = true;
                      want_capture = true; capture_tag = "ibl_only"; break;
            }
        }

        // ------------------------------------------------------ GPU frame
        if (!device->beginFrame()) { SDL_Delay(2); continue; }
        if (frame == 0)
            renderer.bakeEnvironment(equirect);

        renderer.renderScene(scene, cam, settings, dw, dh, frame * dt);

        device->beginSwapchainPass((const float[4]){0.05f, 0.05f, 0.07f, 1.f}, 1.0f);
        renderer.drawBlit();

        // ------------------------------------------------------ UI
        ui.newFrame();
        editor.frame_ms = frame_ms; editor.fps = (int)fps;
        editor.lua_error = script_system->lastError();
        drawEditor(editor);
        ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(960.0f, 12.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Genesis PBR");
        ImGui::Text("FPS %.0f | entities %zu", fps, scene.entityCount());
        ImGui::Text("Drag to orbit | wheel to zoom");
        ImGui::Text("Formats: DamagedHelmet.glb (glTF2) + genesis_cube.fbx (FBX)");
        ImGui::End();
        ui.renderDrawData();

        device->endSwapchainPass();
        device->endFrame();

        if (want_capture)
        {
            // capture the just-presented frame's offscreen color target
            std::vector<uint8_t> px((size_t)dw * dh * 4);
            device->readTexturePixels(device->sceneColor(), dw, dh, px.data());
            savePNG(std::string("screenshot_") + backendName(args.backend) + "_" + capture_tag + ".png",
                    px.data(), (int)dw, (int)dh);
            FrameStats st = analyze(px);
            printf("[TEST] %-20s mean=%.3f var=%.4f colored=%zu bright=%d\n",
                   capture_tag.c_str(), st.mean, st.variance, st.colored_pixels, (int)st.has_bright);
            // Basic assertions: frame rendered, has variation, not black.
            check(st.mean > 0.02 && st.mean < 0.98, (capture_tag + ": non-degenerate brightness").c_str());
            check(st.variance > 0.002, (capture_tag + ": scene detail present").c_str());
            check(st.has_bright, (capture_tag + ": highlights present").c_str());
        }

        if (args.frames >= 0 && frame + 1 >= args.frames) running = false;
        ++frame;
    }

    if (args.selftest)
    {
        check(scene.entityCount() >= 10, "ECS entity count");
        check(renderer.environmentReady(), "IBL textures baked");
        check(script_system->lastError().empty(), "Lua scripts ran without error");
        auto *helmet = scene.getComponent<MeshComponent>(helmet_e);
        check(helmet && helmet->model && !helmet->model->meshes.empty(),
              "glTF 2.0 (DamagedHelmet.glb) imported");
        auto *fbx_mesh = scene.getComponent<MeshComponent>(fbx_e);
        check(fbx_mesh && fbx_mesh->model && !fbx_mesh->model->meshes.empty(),
              "FBX (genesis_cube.fbx) imported");
        check(passes > 0 && failures == 0, "all screenshot checks passed");
        printf("[TEST] %d passed, %d failed\n", passes, failures);
    }

    if (!args.screenshot.empty())
    {
        std::vector<uint8_t> px((size_t)dw * dh * 4);
        device->readTexturePixels(device->sceneColor(), dw, dh, px.data());
        savePNG(args.screenshot, px.data(), (int)dw, (int)dh);
        printf("screenshot -> %s\n", args.screenshot.c_str());
    }

    ui.shutdown();
    device->shutdown();
    window.shutdown();
    MemoryService::instance()->shutdown();

    if (args.selftest) return failures == 0 ? 0 : 1;
    return 0;
}
