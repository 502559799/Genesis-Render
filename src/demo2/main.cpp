/*
 * @Descripttion: Genesis PBR Demo
 *               验证全部新引擎能力:
 *               - 多后端渲染 (Vulkan / OpenGL)
 *               - ECS 架构 (entity/component/system)
 *               - 多格式模型导入 (glTF / FBX via assimp)
 *               - PBR 直接光 (BlinnPhong / Cook-Torrance) + 间接光 IBL
 *               - Lua 脚本系统
 *               - ImGui 实时交互界面
 *
 * 用法:
 *   demo2_pbr [--backend vulkan|opengl] [--assets <dir>] [--shaders <dir>]
 *             [--headless] [--frames N] [--screenshot <path>] [--selftest]
 * @version: v1.0
 */
#include <cstdio>
#include <cstring>
#include <string>

#include "application_layer/input.hpp"
#include "application_layer/window.hpp"
#include "ecs_layer/components.hpp"
#include "render_layer/render_system.hpp"
#include "resource_layer/asset_manager.hpp"
#include "rhi_layer/rhi_device.hpp"
#include "script_layer/script_system.hpp"
#include "ui_layer/ui_service.hpp"

#include "external/imgui/imgui.h"

#include <SDL2/SDL.h>

using namespace Genesis;

// UI 全局指针: 供 SDL 事件回调转发
static ui::UIService *g_ui_service = nullptr;

//-----------------------------------------------------------
// 参数
//-----------------------------------------------------------
struct DemoArgs
{
    rhi::Backend m_backend = rhi::Backend::Vulkan;
    std::string m_assets_dir = "../assets";
    std::string m_shader_dir = "./shaders";
    bool m_headless = false;
    int m_frames = 0; // 0 = 无限
    std::string m_screenshot;
    bool m_selftest = false;
    int m_lighting_model = 1; // 0 blinnphong 1 pbr
    bool m_ibl = true;
    bool m_skybox = true;
    float m_exposure = 1.0f;
    int m_debug_channel = 0;
};

static bool parseArgs(int argc, char **argv, DemoArgs &args)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        auto next = [&](const char *def) -> std::string {
            return (i + 1 < argc) ? argv[++i] : def;
        };
        if (arg == "--backend")
        {
            std::string b = next("vulkan");
            args.m_backend = b == "opengl" || b == "gl" ? rhi::Backend::OpenGL : rhi::Backend::Vulkan;
        }
        else if (arg == "--assets")
            args.m_assets_dir = next(args.m_assets_dir.c_str());
        else if (arg == "--shaders")
            args.m_shader_dir = next(args.m_shader_dir.c_str());
        else if (arg == "--headless")
            args.m_headless = true;
        else if (arg == "--frames")
            args.m_frames = atoi(next("120").c_str());
        else if (arg == "--screenshot")
            args.m_screenshot = next("screenshot.png");
        else if (arg == "--selftest")
            args.m_selftest = true;
        else if (arg == "--lighting")
        {
            std::string m = next("pbr");
            args.m_lighting_model = m == "blinnphong" ? 0 : 1;
        }
        else if (arg == "--no-ibl")
            args.m_ibl = false;
        else if (arg == "--no-skybox")
            args.m_skybox = false;
        else if (arg == "--exposure")
            args.m_exposure = (float)atof(next("1.0").c_str());
        else if (arg == "--debug-channel")
            args.m_debug_channel = atoi(next("0").c_str());
        else if (arg == "--help")
        {
            printf("usage: demo2_pbr [--backend vulkan|opengl] [--assets dir] [--shaders dir] [--headless] [--frames N] [--screenshot path] [--selftest]\n");
            return false;
        }
    }
    return true;
}

//-----------------------------------------------------------
// 自检: 纯逻辑功能验证 (无需 GPU)
//-----------------------------------------------------------
static int g_selftest_failures = 0;

#define SELFTEST_CHECK(cond, name)                                              \
    do                                                                          \
    {                                                                           \
        if (cond)                                                               \
            printf("[SELFTEST] PASS: %s\n", name);                              \
        else                                                                    \
        {                                                                       \
            printf("[SELFTEST] FAIL: %s\n", name);                              \
            ++g_selftest_failures;                                              \
        }                                                                       \
    } while (0)

static int runSelftest(const DemoArgs &args)
{
    printf("[SELFTEST] === Genesis engine self test ===\n");

    //---------------- 1. ECS 基础 ----------------//
    {
        ecs::World world;
        ecs::Entity e1 = world.createEntity();
        ecs::Entity e2 = world.createEntity();
        SELFTEST_CHECK(e1 != e2 && world.isAlive(e1), "ecs create entities");

        auto &tc = world.addComponent<ecs::TransformComponent>(e1);
        tc.m_position = {{1.0f, 2.0f, 3.0f}};
        SELFTEST_CHECK(world.hasComponent<ecs::TransformComponent>(e1), "ecs add component");
        SELFTEST_CHECK(!world.hasComponent<ecs::TransformComponent>(e2), "ecs component isolation");

        world.addComponent<ecs::NameComponent>(e1).setName("alpha");
        world.addComponent<ecs::NameComponent>(e2).setName("beta");
        int count = 0;
        world.each<ecs::NameComponent, ecs::TransformComponent>([&](ecs::Entity e, ecs::NameComponent &, ecs::TransformComponent &) {
            ++count;
        });
        SELFTEST_CHECK(count == 1, "ecs multi-component view");

        world.removeComponent<ecs::TransformComponent>(e1);
        SELFTEST_CHECK(!world.hasComponent<ecs::TransformComponent>(e1), "ecs remove component");

        world.destroyEntity(e1);
        SELFTEST_CHECK(!world.isAlive(e1), "ecs destroy entity");
        ecs::Entity e3 = world.createEntity();
        SELFTEST_CHECK(e3 == e1, "ecs entity recycling");
    }

    //---------------- 2. Transform 层级 ----------------//
    {
        ecs::World world;
        world.addSystem(std::make_shared<ecs::TransformSystem>());
        ecs::Entity parent = world.createEntity();
        ecs::Entity child = world.createEntity();
        auto &pt = world.addComponent<ecs::TransformComponent>(parent);
        pt.m_position = {{10.0f, 0.0f, 0.0f}};
        auto &ct = world.addComponent<ecs::TransformComponent>(child);
        ct.m_position = {{0.0f, 5.0f, 0.0f}};
        ct.m_parent = parent;
        world.update(0.016f);

        float *w = (float *)ct.m_world.raw;
        SELFTEST_CHECK(fabsf(w[12] - 10.0f) < 1e-4f && fabsf(w[13] - 5.0f) < 1e-4f,
                       "transform hierarchy (parent+child)");
    }

    //---------------- 3. 模型加载 (glTF + FBX) ----------------//
    {
        resource::AssetManager assets;
        uint32_t gltf_id = assets.loadModel(args.m_assets_dir + "/models/sphere_gold.gltf");
        const resource::ModelAsset *gltf = assets.getModel(gltf_id);
        SELFTEST_CHECK(gltf != nullptr, "load gltf model");
        if (gltf)
        {
            SELFTEST_CHECK(gltf->m_meshes.size() == 1, "gltf mesh count == 1");
            SELFTEST_CHECK(gltf->m_meshes[0].m_vertices.size() == 2145, "gltf vertex count == 2145");
            SELFTEST_CHECK(gltf->m_meshes[0].m_indices.size() == 12288, "gltf index count == 12288");
            SELFTEST_CHECK(gltf->m_materials.size() >= 1 && !gltf->m_materials[0].m_albedo_tex.empty() &&
                               !gltf->m_materials[0].m_metallic_roughness_tex.empty(),
                           "gltf material textures resolved");
        }

        uint32_t fbx_id = assets.loadModel(args.m_assets_dir + "/models/sphere_gold.fbx");
        const resource::ModelAsset *fbx = assets.getModel(fbx_id);
        SELFTEST_CHECK(fbx != nullptr, "load fbx model");
        if (fbx)
        {
            SELFTEST_CHECK(fbx->m_meshes.size() >= 1, "fbx mesh count >= 1");
            SELFTEST_CHECK(fbx->m_meshes[0].m_vertices.size() > 1000, "fbx vertices parsed");
            SELFTEST_CHECK(fbx->m_meshes[0].m_indices.size() > 3000, "fbx indices parsed");
        }
    }

    //---------------- 4. 纹理加载 ----------------//
    {
        resource::AssetManager assets;
        const resource::TextureAsset *png = assets.loadTexture(args.m_assets_dir + "/textures/gold_albedo.png");
        SELFTEST_CHECK(png != nullptr && png->m_width == 256 && png->m_height == 256, "load png texture 256x256");

        const resource::TextureAsset *hdr = assets.loadTexture(args.m_assets_dir + "/env/sky.hdr");
        SELFTEST_CHECK(hdr != nullptr && hdr->m_is_hdr && hdr->m_width == 512, "load hdr equirect 512x256");
        if (hdr)
        {
            // 天顶区域(第 0 行)应为蓝色系: B > R
            uint16_t *px = (uint16_t *)hdr->m_pixels.data();
            float r = rhi::halfToFloat(px[0]);
            float b = rhi::halfToFloat(px[2]);
            SELFTEST_CHECK(b > r, "hdr sky content (zenith is blue)");
        }
    }

    //---------------- 5. 脚本系统 ----------------//
    {
        ecs::World world;
        world.addSystem(std::make_shared<ecs::TransformSystem>());
        script::ScriptSystem scripts;
        scripts.init(&world);

        SELFTEST_CHECK(scripts.runString("selftest_value = 40 + 2"), "lua runString");

        ecs::Entity e = world.createEntity();
        world.addComponent<ecs::NameComponent>(e).setName("orbiter");
        world.addComponent<ecs::TransformComponent>(e);
        auto &sc = world.addComponent<ecs::ScriptComponent>(e);
        sc.setPath((args.m_assets_dir + "/scripts/orbit.lua").c_str());

        // 首帧: 启动脚本
        scripts.onUpdate(world, 1.0f / 60.0f);
        SELFTEST_CHECK(sc.m_started, "lua script started (on_create)");
        SELFTEST_CHECK(scripts.loadedScriptCount() == 1, "script env registered");

        // 模拟 60 帧
        for (int i = 0; i < 60; ++i)
            scripts.onUpdate(world, 1.0f / 60.0f);
        world.update(0.016f);

        auto *tc = world.getComponent<ecs::TransformComponent>(e);
        float x = tc->m_position.raw[0];
        float z = tc->m_position.raw[2];
        float r = sqrtf(x * x + z * z);
        SELFTEST_CHECK(fabsf(r - 2.2f) < 0.05f, "lua drives transform (orbit radius 2.2)");

        scripts.reloadAll(world);
        SELFTEST_CHECK(scripts.loadedScriptCount() == 0, "script reload clears envs");
        scripts.onUpdate(world, 1.0f / 60.0f);
        SELFTEST_CHECK(scripts.loadedScriptCount() == 1, "script restarted after reload");

        scripts.shutdown();
    }

    //---------------- 6. 半精度转换 ----------------//
    {
        float values[] = {0.0f, 1.0f, -2.5f, 0.333f, 100.25f};
        bool ok = true;
        for (float v : values)
        {
            float rt = rhi::halfToFloat(rhi::floatToHalf(v));
            if (fabsf(rt - v) > fabsf(v) * 0.001f + 1e-6f)
                ok = false;
        }
        SELFTEST_CHECK(ok, "half float roundtrip");
    }

    printf("[SELFTEST] === %s (failures=%d) ===\n", g_selftest_failures == 0 ? "ALL PASS" : "FAILED", g_selftest_failures);
    return g_selftest_failures == 0 ? 0 : 1;
}

//-----------------------------------------------------------
// Demo 场景
//-----------------------------------------------------------
struct DemoContext
{
    Window m_window;
    rhi::RHIDevice *m_device = nullptr;
    resource::AssetManager m_assets;
    render::RenderSystem m_render;
    script::ScriptSystem m_scripts;
    ui::UIService m_ui;

    ecs::World m_world;
    ecs::Entity m_camera = ecs::kInvalidEntity;

    render::RenderSettings m_settings;
    uint32_t m_gold_gltf_gpu = 0xffffffff;
    uint32_t m_gold_fbx_gpu = 0xffffffff;
    uint32_t m_plastic_gpu = 0xffffffff;

    // 相机轨道
    float m_cam_yaw = -90.0f;
    float m_cam_pitch = -10.0f;
    float m_cam_dist = 6.0f;
};

static void setupScene(DemoContext &ctx, const DemoArgs &args)
{
    using namespace ecs;

    // 环境光照 (IBL 烘焙) — 必须在模型上传前
    if (!ctx.m_render.setEnvironmentHDR((args.m_assets_dir + "/env/sky.hdr").c_str()))
        GenesisPrint("[Demo] HDR env failed, procedural fallback already applied\n");

    // 相机
    ctx.m_camera = ctx.m_world.createEntity();
    ctx.m_world.addComponent<NameComponent>(ctx.m_camera).setName("main_camera");
    ctx.m_world.addComponent<TransformComponent>(ctx.m_camera).m_position = {{0.0f, 1.0f, 6.0f}};
    ctx.m_world.addComponent<CameraComponent>(ctx.m_camera);

    // 平行光
    Entity light = ctx.m_world.createEntity();
    ctx.m_world.addComponent<NameComponent>(light).setName("sun");
    auto &dl = ctx.m_world.addComponent<DirectionalLightComponent>(light);
    dl.m_direction = {{-0.4f, -0.75f, -0.5f}};
    dl.m_intensity = 3.5f;

    // 模型: gltf 金球
    uint32_t gold_gltf = ctx.m_assets.loadModel(args.m_assets_dir + "/models/sphere_gold.gltf");
    // 模型: fbx 金球
    uint32_t gold_fbx = ctx.m_assets.loadModel(args.m_assets_dir + "/models/sphere_gold.fbx");
    // 模型: gltf 红塑料球
    uint32_t plastic = ctx.m_assets.loadModel(args.m_assets_dir + "/models/sphere_plastic.gltf");

    ctx.m_gold_gltf_gpu = ctx.m_render.uploadModel(gold_gltf);
    ctx.m_gold_fbx_gpu = ctx.m_render.uploadModel(gold_fbx);
    ctx.m_plastic_gpu = ctx.m_render.uploadModel(plastic);

    // 实体: gltf 金球 (左)
    {
        Entity e = ctx.m_world.createEntity();
        ctx.m_world.addComponent<NameComponent>(e).setName("gold_gltf");
        auto &tc = ctx.m_world.addComponent<TransformComponent>(e);
        tc.m_position = {{-2.4f, 0.0f, 0.0f}};
        auto &mr = ctx.m_world.addComponent<MeshRendererComponent>(e);
        mr.m_model_id = ctx.m_gold_gltf_gpu;
    }

    // 实体: fbx 金球 (中)
    {
        Entity e = ctx.m_world.createEntity();
        ctx.m_world.addComponent<NameComponent>(e).setName("gold_fbx");
        auto &tc = ctx.m_world.addComponent<TransformComponent>(e);
        tc.m_position = {{0.0f, 0.0f, 0.0f}};
        auto &mr = ctx.m_world.addComponent<MeshRendererComponent>(e);
        mr.m_model_id = ctx.m_gold_fbx_gpu;
    }

    // 实体: 塑料球 + 公转脚本 (右)
    {
        Entity e = ctx.m_world.createEntity();
        ctx.m_world.addComponent<NameComponent>(e).setName("plastic_orbit");
        auto &tc = ctx.m_world.addComponent<TransformComponent>(e);
        tc.m_position = {{2.4f, 0.0f, 0.0f}};
        auto &mr = ctx.m_world.addComponent<MeshRendererComponent>(e);
        mr.m_model_id = ctx.m_plastic_gpu;
        auto &sc = ctx.m_world.addComponent<ScriptComponent>(e);
        sc.setPath((args.m_assets_dir + "/scripts/orbit.lua").c_str());
    }

    // 实体: 金属度/粗糙度渐变展示行 (5 个小球, 复用 gltf 金球网格)
    for (int i = 0; i < 5; ++i)
    {
        Entity e = ctx.m_world.createEntity();
        char name[32];
        snprintf(name, sizeof(name), "sample_%d", i);
        ctx.m_world.addComponent<NameComponent>(e).setName(name);
        auto &tc = ctx.m_world.addComponent<TransformComponent>(e);
        tc.m_position = {{(i - 2) * 1.2f, -2.0f, 1.5f}};
        tc.m_scale = {{0.4f, 0.4f, 0.4f}};
        auto &mr = ctx.m_world.addComponent<MeshRendererComponent>(e);
        mr.m_model_id = ctx.m_gold_gltf_gpu;
    }

    ctx.m_world.addSystem(std::make_shared<TransformSystem>());
}

static void updateCamera(DemoContext &ctx, float dt)
{
    using namespace ecs;
    Input *input = Input::instance();

    if (!ctx.m_ui.wantCaptureMouse() && input->isMouseButtonDown(3)) // 右键拖拽
    {
        float dx, dy;
        input->getMouseDelta(dx, dy);
        ctx.m_cam_yaw += dx * 0.15f;
        ctx.m_cam_pitch -= dy * 0.15f;
        if (ctx.m_cam_pitch > 89.0f)
            ctx.m_cam_pitch = 89.0f;
        if (ctx.m_cam_pitch < -89.0f)
            ctx.m_cam_pitch = -89.0f;
    }
    if (!ctx.m_ui.wantCaptureMouse())
    {
        ctx.m_cam_dist -= input->getWheelDelta() * 0.5f;
        if (ctx.m_cam_dist < 2.0f)
            ctx.m_cam_dist = 2.0f;
        if (ctx.m_cam_dist > 30.0f)
            ctx.m_cam_dist = 30.0f;
    }

    float yaw = ctx.m_cam_yaw * 3.14159265f / 180.0f;
    float pitch = ctx.m_cam_pitch * 3.14159265f / 180.0f;
    float x = ctx.m_cam_dist * cosf(pitch) * cosf(yaw);
    float y = ctx.m_cam_dist * sinf(pitch) + 0.5f;
    float z = ctx.m_cam_dist * cosf(pitch) * sinf(yaw);

    auto *tc = ctx.m_world.getComponent<TransformComponent>(ctx.m_camera);
    tc->m_position = {{x, y, z}};
    // 看向原点: 由 lookAt 的逆取得相机朝向四元数
    vec3s eye{{x, y, z}};
    vec3s center{{0, 0, 0}};
    vec3s up{{0, 1, 0}};
    mat4s view = glms_lookat(eye, center, up);
    versors q = glms_mat4_quat(glms_mat4_inv(view));
    tc->m_rotation.raw[0] = q.raw[0];
    tc->m_rotation.raw[1] = q.raw[1];
    tc->m_rotation.raw[2] = q.raw[2];
    tc->m_rotation.raw[3] = q.raw[3];
    tc->m_dirty = true;
    (void)dt;
}

static void buildUI(DemoContext &ctx, const DemoArgs &args)
{
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 420), ImGuiCond_FirstUseEver);
    ImGui::Begin("Genesis PBR Demo - Control Panel");

    ImGui::Text("Backend: %s", ctx.m_device->getBackend() == rhi::Backend::Vulkan ? "Vulkan" : "OpenGL");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Separator();

    // 光照模型
    ImGui::Text("Direct lighting model:");
    ImGui::RadioButton("PBR (Cook-Torrance GGX)", &ctx.m_settings.m_lighting_model, 1);
    ImGui::RadioButton("Blinn-Phong", &ctx.m_settings.m_lighting_model, 0);
    ImGui::Checkbox("Indirect IBL (irradiance+prefilter+LUT)", &ctx.m_settings.m_enable_ibl);
    ImGui::Checkbox("Skybox", &ctx.m_settings.m_enable_skybox);
    ImGui::SliderFloat("Exposure", &ctx.m_settings.m_exposure, 0.1f, 4.0f);
    ImGui::Separator();

    // 平行光
    auto &light_pool = ctx.m_world.pool<ecs::DirectionalLightComponent>();
    if (light_pool.size() > 0)
    {
        auto &light = light_pool.m_dense[0];
        ImGui::Text("Directional light:");
        ImGui::SliderFloat3("Direction", light.m_direction.raw, -1.0f, 1.0f);
        ImGui::ColorEdit3("Color", light.m_color.raw);
        ImGui::SliderFloat("Intensity", &light.m_intensity, 0.0f, 10.0f);
        ImGui::Separator();
    }

    // 材质覆盖 (gltf 金球)
    render::GpuMaterial *mat = ctx.m_render.getMaterial(ctx.m_gold_gltf_gpu, 0);
    if (mat)
    {
        ImGui::Text("Material override (gold_gltf):");
        bool dirty = false;
        dirty |= ImGui::SliderFloat("Metallic", &mat->m_data.m_factors[0], 0.0f, 1.0f);
        dirty |= ImGui::SliderFloat("Roughness", &mat->m_data.m_factors[1], 0.04f, 1.0f);
        dirty |= ImGui::ColorEdit4("BaseColor", mat->m_data.m_base_color);
        if (dirty)
            ctx.m_device->updateBuffer(mat->m_ubo, &mat->m_data, sizeof(render::MaterialUBOData));
        ImGui::Separator();
    }

    // 实体列表
    if (ImGui::CollapsingHeader("ECS Entities"))
    {
        auto &name_pool = ctx.m_world.pool<ecs::NameComponent>();
        for (size_t i = 0; i < name_pool.m_dense.size(); ++i)
        {
            ecs::Entity e = name_pool.m_entities[i];
            ImGui::BulletText("#%u %s%s", e, name_pool.m_dense[i].m_name,
                              ctx.m_world.hasComponent<ecs::ScriptComponent>(e) ? " [script]" : "");
        }
    }

    // 脚本
    if (ImGui::Button("Reload scripts"))
        ctx.m_scripts.reloadAll(ctx.m_world);
    ImGui::SameLine();
    ImGui::Text("scripts: %u", ctx.m_scripts.loadedScriptCount());

    ImGui::End();
}

//-----------------------------------------------------------
int main(int argc, char **argv)
{
    printf("=== Genesis Engine - PBR Multi-Backend Demo ===\n");

    DemoArgs args;
    if (!parseArgs(argc, argv, args))
        return 0;

    if (args.m_selftest)
        return runSelftest(args);

    DemoContext ctx;

    // 窗口
    WindowConfig win_cfg{};
    win_cfg.width = 1280;
    win_cfg.height = 720;
    win_cfg.title = args.m_backend == rhi::Backend::Vulkan ? "Genesis PBR Demo (Vulkan)" : "Genesis PBR Demo (OpenGL)";
    win_cfg.allocator = nullptr;
    win_cfg.graphics_api = args.m_backend == rhi::Backend::Vulkan ? WindowGraphicsApi::Vulkan : WindowGraphicsApi::OpenGL;
    win_cfg.hidden = args.m_headless;
    ctx.m_window.init(&win_cfg);

    // 输入
    Input::instance()->init(nullptr);

    // 渲染设备
    rhi::DeviceCreateInfo device_info{};
    device_info.m_backend = args.m_backend;
    device_info.m_window = ctx.m_window.getWindowHandle();
    device_info.m_width = ctx.m_window.getWidth();
    device_info.m_height = ctx.m_window.getHeight();
    ctx.m_device = rhi::createRHIDevice(args.m_backend);
    if (!ctx.m_device->init(device_info))
    {
        printf("[Demo] RHI device init failed\n");
        return 1;
    }
    printf("[Demo] RHI device ready: %s\n", args.m_backend == rhi::Backend::Vulkan ? "vulkan" : "opengl");

    // 渲染系统
    if (!ctx.m_render.init(ctx.m_device, &ctx.m_assets, args.m_shader_dir.c_str()))
    {
        printf("[Demo] render system init failed\n");
        return 1;
    }

    // 脚本系统
    ctx.m_scripts.init(&ctx.m_world);

    // 场景
    setupScene(ctx, args);

    // UI
    if (!args.m_headless)
    {
        ctx.m_ui.init(&ctx.m_window, ctx.m_device);
        g_ui_service = &ctx.m_ui;
        Input::instance()->m_event_callback = [](SDL_Event *e) {
            if (g_ui_service)
                g_ui_service->processEvent(e);
        };
    }

    printf("[Demo] scene ready: gltf_gold(gpu=%u) fbx_gold(gpu=%u) plastic(gpu=%u)\n",
           ctx.m_gold_gltf_gpu, ctx.m_gold_fbx_gpu, ctx.m_plastic_gpu);

    // CLI 渲染设置
    ctx.m_settings.m_lighting_model = args.m_lighting_model;
    ctx.m_settings.m_enable_ibl = args.m_ibl;
    ctx.m_settings.m_enable_skybox = args.m_skybox;
    ctx.m_settings.m_exposure = args.m_exposure;
    ctx.m_settings.m_debug_channel = args.m_debug_channel;

    // 主循环
    int frame = 0;
    uint64_t last_time = SDL_GetPerformanceCounter();
    uint64_t freq = SDL_GetPerformanceFrequency();
    float elapsed = 0.0f;
    bool running = true;

    while (running)
    {
        uint64_t now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last_time) / (float)freq;
        last_time = now;
        if (dt > 0.1f)
            dt = 0.1f;
        // 无头模式使用固定步长, 保证双后端截图逐帧一致可对比
        if (args.m_headless)
            dt = 1.0f / 60.0f;
        elapsed += dt;

        running = Input::instance()->pollEvents();

        // 逻辑更新: 脚本 -> transform
        ctx.m_scripts.onUpdate(ctx.m_world, dt);
        updateCamera(ctx, dt);
        ctx.m_world.update(dt);

        // 渲染
        ctx.m_device->beginFrame();
        ctx.m_render.render(ctx.m_world, ctx.m_camera, ctx.m_settings, elapsed);

        if (!args.m_headless)
        {
            ctx.m_ui.newFrame();
            buildUI(ctx, args);
            ctx.m_ui.render();
        }

        // 截图 (最后一帧)
        if (!args.m_screenshot.empty() && args.m_frames > 0 && frame == args.m_frames - 1)
            ctx.m_device->requestScreenCapture(args.m_screenshot.c_str());

        ctx.m_device->endFrame();

        ++frame;
        if (args.m_frames > 0 && frame >= args.m_frames)
            break;
    }

    printf("[Demo] rendered %d frames, shutting down\n", frame);

    // 清理
    ctx.m_device->waitIdle();
    ctx.m_scripts.shutdown();
    if (!args.m_headless)
        ctx.m_ui.shutdown();
    ctx.m_render.shutdown();
    ctx.m_device->shutdown();
    rhi::destroyRHIDevice(ctx.m_device);
    Input::instance()->shutdown();
    ctx.m_window.shutdown();

    printf("[Demo] shutdown complete\n");
    return 0;
}
