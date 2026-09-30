/*
 * Genesis Render - test runner
 *
 * Usage:
 *   genesis_tests [--filter SUBSTR] [--verbose] [--no-gpu]
 *                 [--backend gl|vulkan] [--all-backends] [--no-window]
 *
 * CPU-only groups (math, ecs, procedural, gltf, fbx, image, shaders, script,
 * render_ubo) always run.  GPU groups run once per available backend
 * (OpenGL, Vulkan, Metal); with --all-backends (default) the suite also
 * compares the renders of the same scene on every pair of backends.
 */
#include "test_framework.hpp"

#include "engine/engine.hpp"
#include "foundation_layer/genesis_log.hpp"
#include "renderer/render_internal.hpp"
#include "resource/procedural_mesh.hpp"

#include <cstring>
#include <string>
#include <vector>

using namespace Genesis;

namespace gpu_test
{
    RHI::BackendType g_backend = RHI::BackendType::OpenGL;
    bool g_available = false;
} // namespace gpu_test

namespace
{
    //------------------------------------------------------------------
    // Cross backend comparison (not a registered TEST because it needs to
    // run both backends inside one process).
    //------------------------------------------------------------------
    struct Capture
    {
        std::vector<uint8_t> rgba;
        uint32_t width = 0, height = 0;
        bool ok = false;
    };

    Capture renderReferenceScene(RHI::BackendType backend)
    {
        Capture out;
        Engine engine;
        EngineConfig cfg;
        cfg.app_name = "genesis-cross-backend";
        cfg.width = cfg.height = 160;
        cfg.backend = backend;
        cfg.headless = true;
        cfg.enable_ui = false;
        cfg.enable_script = false;
        cfg.vsync = false;
        if (!engine.initialize(cfg))
        {
            GEN_LOG_ERROR("[cross-backend] %s engine init failed", RHI::backendTypeName(backend));
            return out;
        }

        ECS::World &world = engine.world();

        // Camera
        ECS::Entity camera = world.createEntity("camera");
        {
            ECS::TransformComponent t;
            t.position = Math::Vec3(0, 0.35f, 3.4f);
            t.rotation = Math::Quat::lookRotation(Math::Vec3(0, 0, 0) - t.position, Math::Vec3(0, 1, 0));
            world.addComponent<ECS::TransformComponent>(camera, t);
            ECS::CameraComponent cam;
            cam.fov_degrees = 45.0f;
            cam.near_plane = 0.05f;
            cam.far_plane = 100.0f;
            world.addComponent<ECS::CameraComponent>(camera, cam);
        }
        // Sun
        ECS::Entity sun = world.createEntity("sun");
        {
            ECS::TransformComponent t;
            t.rotation = Math::Quat::lookRotation(Math::normalize(Math::Vec3(-0.45f, -0.55f, -0.7f)), Math::Vec3(0, 1, 0));
            world.addComponent<ECS::TransformComponent>(sun, t);
            ECS::LightComponent l;
            l.type = ECS::LightType::Directional;
            l.color = Math::Color(1.0f, 0.97f, 0.92f);
            l.intensity = 7.0f;
            world.addComponent<ECS::LightComponent>(sun, l);
        }

        // Three spheres: dielectric rough, dielectric smooth, metal.
        struct Spec { float x; float metallic; float roughness; Math::Color color; const char *name; };
        const Spec specs[3] = {
            {-1.15f, 0.0f, 0.85f, Math::Color(0.85f, 0.25f, 0.2f), "rough_dielectric"},
            {0.0f, 0.0f, 0.1f, Math::Color(0.2f, 0.7f, 0.9f), "smooth_dielectric"},
            {1.15f, 1.0f, 0.25f, Math::Color(1.0f, 0.85f, 0.5f), "metal"},
        };
        for (const Spec &spec : specs)
        {
            MaterialData mat;
            mat.name = spec.name;
            mat.base_color_factor = spec.color;
            mat.metallic_factor = spec.metallic;
            mat.roughness_factor = spec.roughness;
            auto model = Procedural::wrapMesh(Procedural::makeSphere(0.5f, 48, 24), mat, spec.name);

            ECS::Entity e = world.createEntity(spec.name);
            ECS::TransformComponent t;
            t.position = Math::Vec3(spec.x, 0, 0);
            world.addComponent<ECS::TransformComponent>(e, t);
            ECS::MeshComponent m;
            m.model = model;
            m.pick_id = e.id + 1;
            world.addComponent<ECS::MeshComponent>(e, m);

            std::vector<uint32_t> gpu = engine.render().scene().uploadModel(*model, spec.name);
            Render::RenderSystem::EntityMeshBinding binding;
            binding.entity = e;
            binding.mesh_indices = gpu;
            engine.render().bindEntityMeshes(binding);
        }

        Render::RenderSettings &s = engine.render().settings();
        s.show_skybox = false;
        s.use_ibl = false;
        s.ambient_color = Math::Color(0.02f, 0.02f, 0.03f);
        s.ambient_intensity = 1.0f;
        s.tonemap_mode = Render::TonemapMode::Aces;
        s.exposure = 1.1f;
        s.gamma = 2.2f;
        s.shading_mode = Render::ShadingMode::CookTorrance;
        s.frustum_culling = true;

        // Run a couple of frames so transforms and the UBO ring settle.
        engine.tick();
        engine.tick();
        out.ok = engine.captureFrame(out.rgba, out.width, out.height);
        engine.shutdown();
        return out;
    }

    // Renders the reference scene on every available backend and compares
    // each pair pixel by pixel: the strongest guarantee that OpenGL, Vulkan
    // and Metal really implement the same renderer.
    bool compareBackends(const std::vector<RHI::BackendType> &backends)
    {
        GEN_LOG_INFO("=== cross backend comparison (%zu backends) ===", backends.size());
        std::vector<Capture> captures;
        for (RHI::BackendType b : backends)
        {
            captures.push_back(renderReferenceScene(b));
            if (!captures.back().ok)
            {
                GEN_LOG_ERROR("[cross-backend] %s capture failed", RHI::backendTypeName(b));
                return false;
            }
            std::string path = std::string("/tmp/genesis_cross_backend_") + RHI::backendTypeName(b) + ".png";
            ResourceIO::writeImagePNG(path, captures.back().width, captures.back().height, captures.back().rgba.data());
        }
        bool all_ok = true;
        for (size_t i = 0; i < captures.size(); ++i)
        {
            for (size_t j = i + 1; j < captures.size(); ++j)
            {
                const Capture &a = captures[i], &b = captures[j];
                if (a.width != b.width || a.height != b.height)
                {
                    GEN_LOG_ERROR("[cross-backend] size mismatch %ux%u vs %ux%u", a.width, a.height, b.width, b.height);
                    all_ok = false;
                    continue;
                }
                double sum = 0.0, worst = 0.0;
                size_t differing = 0;
                for (size_t k = 0; k < a.rgba.size(); ++k)
                {
                    double d = std::fabs((double)a.rgba[k] - (double)b.rgba[k]);
                    sum += d;
                    worst = std::max(worst, d);
                    if (d > 4.0) ++differing;
                }
                const double mean = sum / (double)a.rgba.size();
                const double ratio = (double)differing / (double)a.rgba.size();
                // Rasterisers round differently, so allow a small tolerance; a
                // structural mismatch (orientation, culling, missing geometry,
                // wrong lighting) is orders of magnitude larger.
                const bool ok = (mean < 3.0) && (ratio < 0.05);
                GEN_LOG_INFO("[cross-backend] %s vs %s: mean diff %.3f/255, worst %.0f, >4 off %.2f%% -> %s",
                             RHI::backendTypeName(backends[i]), RHI::backendTypeName(backends[j]), mean, worst, ratio * 100.0,
                             ok ? "MATCH" : "MISMATCH");
                all_ok = all_ok && ok;
            }
        }
        GEN_LOG_INFO("[cross-backend] %s (images in /tmp/genesis_cross_backend_<backend>.png)", all_ok ? "MATCH" : "MISMATCH");
        return all_ok;
    }
} // namespace

//======================================================================
int main(int argc, char **argv)
{
    bool verbose = false;
    bool run_gpu = true;
    bool all_backends = true;
    std::string filter;
    RHI::BackendType forced_backend = RHI::BackendType::OpenGL;
    bool force_backend = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--verbose" || a == "-v") verbose = true;
        else if (a == "--no-gpu" || a == "--no-window") run_gpu = false;
        else if (a == "--all-backends") all_backends = true;
        else if (a == "--single-backend") all_backends = false;
        else if (a == "--backend" && i + 1 < argc)
        {
            ++i;
            std::string be = argv[i];
            if (be == "vulkan" || be == "vk") forced_backend = RHI::BackendType::Vulkan;
            else if (be == "metal") forced_backend = RHI::BackendType::Metal;
            else forced_backend = RHI::BackendType::OpenGL;
            force_backend = true;
            all_backends = false;
        }
        else if (a == "--filter" && i + 1 < argc) filter = argv[++i];
        else if (a == "--help" || a == "-h")
        {
            std::printf("usage: genesis_tests [--verbose] [--no-gpu] [--backend gl|vulkan|metal]\n"
                        "                     [--all-backends|--single-backend] [--filter SUBSTR]\n");
            return 0;
        }
    }

    Logger::instance().setMinLevel(verbose ? LogLevel::Debug : LogLevel::Info);
    Logger::instance().setColorOutput(true);
    GenesisTest::Registry::instance().setVerbose(verbose);

    std::printf("Genesis Render test suite (%s)\n", GENESIS_ENGINE_VERSION);
    std::printf("  shaders baked : %zu\n", Shaders::shaderCount());
    std::printf("  backends built: OpenGL=%d Vulkan=%d Metal=%d\n", (int)RHI::backendAvailable(RHI::BackendType::OpenGL),
                (int)RHI::backendAvailable(RHI::BackendType::Vulkan), (int)RHI::backendAvailable(RHI::BackendType::Metal));
    std::printf("----------------------------------------------------------\n");

    std::vector<RHI::BackendType> backends;
    if (run_gpu)
    {
        if (force_backend) backends.push_back(forced_backend);
        else if (all_backends)
        {
            for (RHI::BackendType b : {RHI::BackendType::OpenGL, RHI::BackendType::Vulkan, RHI::BackendType::Metal})
                if (RHI::backendAvailable(b)) backends.push_back(b);
        }
        else
        {
            backends.push_back(RHI::backendAvailable(RHI::BackendType::OpenGL) ? RHI::BackendType::OpenGL
                                                                               : RHI::BackendType::Vulkan);
        }
    }

    int total_run = 0, total_failed_tests = 0;
    auto runGroup = [&](bool gpu, RHI::BackendType backend)
    {
        gpu_test::g_backend = backend;
        gpu_test::g_available = gpu && RHI::backendAvailable(backend);
        std::string current_group;
        for (const auto &test : GenesisTest::Registry::instance().tests())
        {
            if (test.requires_gpu != gpu) continue;
            if (!filter.empty() &&
                test.group.find(filter) == std::string::npos &&
                test.name.find(filter) == std::string::npos)
                continue;
            if (test.requires_gpu && !gpu_test::g_available) continue;

            if (test.group != current_group)
            {
                current_group = test.group;
                std::printf("\n[%s]%s\n", current_group.c_str(),
                            gpu ? (std::string("  (backend: ") + RHI::backendTypeName(backend) + ")").c_str() : "");
            }
            int before = GenesisTest::Registry::instance().failures();
            GenesisTest::Registry::instance().setCurrentTest(test.group + "." + test.name);
            std::printf("  - %-46s", test.name.c_str());
            std::fflush(stdout);
            test.fn();
            int after = GenesisTest::Registry::instance().failures();
            ++total_run;
            if (after > before)
            {
                std::printf(" FAIL (%d)\n", after - before);
                ++total_failed_tests;
            }
            else
            {
                std::printf(" ok\n");
            }
        }
    };

    runGroup(false, RHI::BackendType::OpenGL);
    for (RHI::BackendType backend : backends) runGroup(true, backend);

    bool cross_ok = true;
    if (run_gpu && all_backends && backends.size() >= 2 && filter.empty())
        cross_ok = compareBackends(backends);

    int failures = GenesisTest::Registry::instance().failures();
    int checks = GenesisTest::Registry::instance().checks();
    std::printf("\n----------------------------------------------------------\n");
    std::printf("tests run      : %d\n", total_run);
    std::printf("tests failed   : %d\n", total_failed_tests);
    std::printf("checks         : %d\n", checks);
    std::printf("check failures : %d\n", failures);
    std::printf("cross backend  : %s\n", (!run_gpu || backends.size() < 2) ? "skipped" : (cross_ok ? "MATCH" : "MISMATCH"));
    std::printf("log errors     : %u\n", Logger::instance().errorCount());
    std::printf("RESULT: %s\n", (failures == 0 && total_failed_tests == 0 && cross_ok) ? "PASS" : "FAIL");
    return (failures == 0 && total_failed_tests == 0 && cross_ok) ? 0 : 1;
}
