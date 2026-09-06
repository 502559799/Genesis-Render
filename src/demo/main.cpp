/*
 * Genesis Render - demo / test application.
 *
 * Windowed : editor with hierarchy/inspector/render panels.
 * Headless : renders N frames with optional scenario switches and writes
 *            PNG/PPM screenshots for CI verification (both RHI backends).
 */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#include "application_layer/window.hpp"
#include "rhi/rhi.hpp"
#include "render/shader_library.hpp"
#include "render/pbr_renderer.hpp"
#include "render/primitives.hpp"
#include "resource/asset_manager.hpp"
#include "script/lua_engine.hpp"
#include "ui/editor_ui.hpp"
#include "foundation_layer/log.hpp"

#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Genesis;

namespace {

struct Options {
    RHIBackend backend = RHIBackend::OpenGL;
    bool headless = false;
    uint32_t width = 960, height = 540;
    int frames = 0; // 0 = windowed loop
    std::string screenshot;
    std::string model;
    std::string scenario = "basic";
    std::string script = "rotate.lua";
    bool noEnv = false;
};

Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--vulkan") o.backend = RHIBackend::Vulkan;
        else if (a == "--opengl") o.backend = RHIBackend::OpenGL;
        else if (a == "--headless") o.headless = true;
        else if (a == "--width") o.width = std::stoi(next());
        else if (a == "--height") o.height = std::stoi(next());
        else if (a == "--frames") o.frames = std::stoi(next());
        else if (a == "--screenshot") o.screenshot = next();
        else if (a == "--model") o.model = next();
        else if (a == "--scenario") o.scenario = next();
        else if (a == "--script") o.script = next();
        else if (a == "--no-env") o.noEnv = true;
    }
    if (o.headless && o.frames == 0) o.frames = 60;
    return o;
}

// Create an ECS entity per model node.
Entity instantiateModel(Scene& scene, GpuModel* model, const std::string& name,
                        Entity parent = kInvalidEntity) {
    if (!model || model->source->nodes.empty()) return kInvalidEntity;
    std::vector<Entity> nodes(model->source->nodes.size());
    // first pass: entities + local TRS
    for (uint32_t i = 0; i < nodes.size(); ++i) {
        const MeshNode& n = model->source->nodes[i];
        Entity e = scene.createEntity(n.name.empty() ? name : n.name, parent);
        TRS trs = decompose(n.local);
        auto* t = scene.registry.get<TransformComponent>(e);
        t->position = trs.position; t->euler = trs.euler; t->scale = trs.scale;
        if (!n.meshIndices.empty()) {
            auto& mc = scene.registry.add<MeshComponent>(e);
            mc.model = model; mc.node = (int32_t)i;
        }
        nodes[i] = e;
    }
    // second pass: parenting
    for (uint32_t i = 0; i < nodes.size(); ++i) {
        for (uint32_t c : model->source->nodes[i].children)
            scene.reparent(nodes[c], nodes[i]);
    }
    return nodes.empty() ? kInvalidEntity : nodes[model->source->roots.empty() ? 0 : model->source->roots[0]];
}

void applyScenario(Scene& scene, const std::string& s) {
    if (s == "basic") return;
    if (s == "nodirect") scene.settings.directLightEnabled = false;
    else if (s == "noibl") { scene.settings.iblEnabled = false; scene.settings.skyboxEnabled = false; }
    else if (s == "blinn") scene.settings.lightingModel = 1;
    else if (s == "dark") {
        scene.settings.directLightEnabled = false;
        scene.settings.iblEnabled = false;
        scene.settings.skyboxEnabled = false;
    }
}

struct PixelStats { double mean=0; float mx=0; int nonBlack=0; };
PixelStats analyze(const std::vector<uint8_t>& px) {
    PixelStats st;
    for (size_t i = 0; i + 3 < px.size(); i += 4) {
        int v = px[i] + px[i+1] + px[i+2];
        st.mean += v / 3.0;
        st.mx = std::max(st.mx, (float)v / 3.0f);
        if (v > 24) st.nonBlack++;
    }
    st.mean /= (px.size() / 4.0);
    return st;
}

} // namespace

int main(int argc, char** argv) {
    Options opt = parseArgs(argc, argv);
    std::string assetRoot = findAssetRoot(argv[0]);
    GenesisPrint("Genesis Render | backend=%s headless=%d assets=%s\n",
                 opt.backend == RHIBackend::Vulkan ? "Vulkan" : "OpenGL",
                 (int)opt.headless, assetRoot.c_str());

    Window window;
    WindowConfig wc;
    wc.width = opt.width; wc.height = opt.height; wc.headless = opt.headless;
    wc.backend = opt.backend; wc.title = "Genesis Render";
    window.init(wc);

    DeviceInitInfo di;
    di.backend = opt.backend; di.headless = opt.headless;
    di.width = window.width(); di.height = window.height();
    IDevice* device = createDevice(opt.backend);
    if (!device->init(di)) { GenesisPrint("device init failed\n"); return 2; }
    GenesisPrint("GPU: %s (%s)\n", device->info().device.c_str(),
                 device->info().version.c_str());

    ShaderLibrary shaders(device, assetRoot);
    AssetManager assets(device);
    PBRRenderer renderer;
    renderer.init(device, &shaders, window.width(), window.height());
    if (!opt.noEnv) renderer.generateProceduralEnvironment();

    Scene scene;
    scene.name = "GenesisDemo";

    // camera
    Entity cam = scene.createEntity("MainCamera");
    auto& cc = scene.registry.add<CameraComponent>(cam);
    cc.isMain = true; cc.orbit = true; cc.distance = 4.2f; cc.target = {0, 0.2f, 0};
    cc.yaw = 0.6f; cc.pitch = 0.18f;
    scene.mainCamera = cam;

    // ground plane (procedural geometry, dielectric rough)
    {
        MeshData plane; buildPlane(plane, 30.0f);
        MaterialDesc mat;
        mat.metallicFactor = 0.0f; mat.roughnessFactor = 0.92f;
        mat.baseColorFactor = Vec4{0.35f, 0.36f, 0.40f, 1.0f};
        GpuModel* g = assets.createMeshModel("ground", plane, mat);
        Entity e = instantiateModel(scene, g, "Ground");
        auto* t = scene.registry.get<TransformComponent>(e);
        t->position = Vec3{0, -1.0f, 0};
    }
    // metallic sphere
    {
        MeshData sph; buildSphere(sph, 32, 48);
        MaterialDesc mat;
        mat.metallicFactor = 1.0f; mat.roughnessFactor = 0.18f;
        mat.baseColorFactor = Vec4{0.95f, 0.7f, 0.25f, 1.0f};
        GpuModel* g = assets.createMeshModel("gold_sphere", sph, mat);
        Entity e = instantiateModel(scene, g, "GoldSphere");
        auto* t = scene.registry.get<TransformComponent>(e);
        t->position = Vec3{2.1f, -0.55f, 0.8f}; t->scale = Vec3{0.45f};
    }
    // rough plastic sphere
    {
        MeshData sph; buildSphere(sph, 24, 32);
        MaterialDesc mat;
        mat.metallicFactor = 0.0f; mat.roughnessFactor = 0.6f;
        mat.baseColorFactor = Vec4{0.2f, 0.45f, 0.9f, 1.0f};
        GpuModel* g = assets.createMeshModel("blue_sphere", sph, mat);
        Entity e = instantiateModel(scene, g, "BlueSphere");
        auto* t = scene.registry.get<TransformComponent>(e);
        t->position = Vec3{-1.7f, -0.45f, 0.8f}; t->scale = Vec3(0.55f);
    }
    // a point light entity
    {
        Entity e = scene.createEntity("PointLight");
        auto* t = scene.registry.get<TransformComponent>(e);
        t->position = Vec3{2.0f, 2.5f, 2.0f};
        auto& l = scene.registry.add<LightComponent>(e);
        l.type = LightType::Point; l.color = Vec3{1, 0.85f, 0.65f};
        l.intensity = 25.0f; l.range = 12.0f;
    }

    // imported model
    std::string modelPath = opt.model.empty()
        ? (assetRoot + "/models/DamagedHelmet.glb")
        : (opt.model[0] == '/' ? opt.model : assetRoot + "/" + opt.model);
    if (GpuModel* helmet = assets.loadModel(modelPath)) {
        Entity root = instantiateModel(scene, helmet, "Helmet");
        auto* t = scene.registry.get<TransformComponent>(root);
        t->position = Vec3{-0.35f, -1.0f, -0.2f};
        t->scale = Vec3(1.0f);
        // script on the helmet root
        auto& sc = scene.registry.add<ScriptComponent>(root);
        sc.path = assetRoot + "/scripts/" + opt.script;
    } else {
        GenesisPrint("warning: could not load %s\n", modelPath.c_str());
    }

    applyScenario(scene, opt.scenario);

    LuaEngine lua;
    EditorUI ui;
    if (!opt.headless) {
        ui.init(device, &window);
        window.register_os_message_callback(
            [](void* ev, void* ud) { ((EditorUI*)ud)->processSdlEvent(ev); }, &ui);
        ui.onImportModel = [&](const std::string& rel) {
            std::string p = rel[0] == '/' ? rel : assetRoot + "/" + rel;
            if (GpuModel* m = assets.loadModel(p)) instantiateModel(scene, m, m->source->name);
        };
        ui.onResetCamera = [&] {
            cc.yaw = 0.6f; cc.pitch = 0.18f; cc.distance = 4.2f; cc.target = {0,0.2f,0};
        };
    }

    uint64_t t0 = SDL_GetPerformanceCounter();
    int frame = 0;
    bool running = true;
    while (running) {
        uint64_t now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - t0) / SDL_GetPerformanceFrequency());
        t0 = now;
        dt = std::min(dt, 0.05f);

        window.pollEvents();
        if (window.shouldClose()) running = false;
        uint32_t rw = 0, rh = 0;
        if (window.consumeResize(rw, rh)) renderer.onResize(rw, rh);

        // camera orbit
        if (!ui.wantCaptureMouse()) {
            const auto& in = window.input();
            if (in.mouse_buttons[1]) { cc.yaw -= in.mouse_dx * 0.005f; cc.pitch += in.mouse_dy * 0.005f;
                                      cc.pitch = clampf(cc.pitch, -1.45f, 1.45f); }
            cc.distance = clampf(cc.distance - in.scroll * 0.2f, 1.0f, 20.0f);
        }

        lua.setContext(&scene, &window);
        lua.update(dt);
        scene.update(dt);
        assets.pollLoaded();

        if (!opt.headless) {
            ui.newFrame(&window);
            ui.renderEditor(scene, assets, renderer);
        }

        renderer.renderFrame(scene, [&] { ui.renderDrawData(); });
        frame++;

        if (opt.headless && frame >= opt.frames) running = false;
        if (opt.headless && opt.frames - frame == 2 && !opt.screenshot.empty()) {
            device->waitIdle();
        }
    }

    // final screenshot / stats
    if (opt.headless && getenv("GEN_DUMP_ATLAS")) {
        auto a=getenv("DUMP_IRR")?renderer.dumpIrrAtlas():renderer.dumpEnvAtlas();
        stbi_write_png("/tmp/atlas.png", a.size()/(128*4)>400?192:6*128, 32, 4, a.data(), (a.size()/(128*4)>400?192:768)*4);
        auto ir=renderer.dumpEquirect();
        stbi_write_png("/tmp/equirect.png",1024,512,4,ir.data(),1024*4);
    }
    if (opt.headless && getenv("GEN_DUMP_ENV")) {
        for (int f=0;f<6;f++){auto ff=renderer.dumpEnvFace(f);
          stbi_write_png((std::string("/tmp/vk_env")+std::to_string(f)+".png").c_str(),256,256,4,ff.data(),256*4);}
    }
    if (opt.headless) {
        device->waitIdle();
        std::vector<uint8_t> px = getenv("GEN_DUMP_SCENE")
                ? renderer.dumpSceneColor()
                : device->readTextureRGBA8(device->screenColorTexture());
        auto st = analyze(px);
        if (!opt.screenshot.empty()) {
            if (opt.screenshot.size() >= 4 && opt.screenshot.substr(opt.screenshot.size()-4) == ".png")
                stbi_write_png(opt.screenshot.c_str(), window.width(), window.height(), 4,
                               px.data(), window.width() * 4);
            else {
                FILE* f = fopen(opt.screenshot.c_str(), "wb");
                fprintf(f, "P6\n%u %u\n255\n", window.width(), window.height());
                fwrite(px.data(), 1, px.size(), f);
                fclose(f);
            }
            GenesisPrint("screenshot: %s\n", opt.screenshot.c_str());
        }
        GenesisPrint("SELFTEST stats: mean=%.2f max=%.0f nonBlack=%d/%zu scenario=%s\n",
                     st.mean, st.mx, st.nonBlack, px.size() / 4, opt.scenario.c_str());
    }

    if (!opt.headless) ui.shutdown();
    device->shutdown();
    window.shutdown();
    return 0;
}
