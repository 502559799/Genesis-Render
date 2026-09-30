/*
 * Genesis Render - helpers shared by the GPU test files.
 *
 * Image convention: row 0 == top of the image, RGBA8, straight out of
 * RenderSystem::renderAndCapture().
 */
#ifndef __GENESIS_TEST_GPU_UTILS_HPP__
#define __GENESIS_TEST_GPU_UTILS_HPP__

#include "engine/engine.hpp"
#include "foundation_layer/genesis_log.hpp"
#include "resource/procedural_mesh.hpp"
#include "resource/resource_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef GENESIS_ASSET_DIR
#define GENESIS_ASSET_DIR "assets"
#endif

namespace gpu_test
{
    using namespace Genesis;

    // Set by the runner before the GPU tests execute.
    extern RHI::BackendType g_backend;
    extern bool g_available;

    inline std::string assetPath(const std::string &relative)
    {
        return ResourceIO::joinPath(GENESIS_ASSET_DIR, relative);
    }

    struct Image
    {
        std::vector<uint8_t> rgba;
        uint32_t width = 0;
        uint32_t height = 0;

        bool valid() const { return !rgba.empty() && width && height; }
        void at(uint32_t x, uint32_t y, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &a) const
        {
            size_t i = ((size_t)y * width + x) * 4;
            r = rgba[i]; g = rgba[i + 1]; b = rgba[i + 2]; a = rgba[i + 3];
        }
        float luminance(uint32_t x, uint32_t y) const
        {
            uint8_t r, g, b, a;
            at(x, y, r, g, b, a);
            return (0.2126f * r + 0.7152f * g + 0.0722f * b) / 255.0f;
        }
        float averageLuminance() const
        {
            if (!valid()) return 0.0f;
            double sum = 0.0;
            size_t n = 0;
            for (size_t i = 0; i + 3 < rgba.size(); i += 4)
            {
                sum += (0.2126 * rgba[i] + 0.7152 * rgba[i + 1] + 0.0722 * rgba[i + 2]) / 255.0;
                ++n;
            }
            return (float)(sum / (double)n);
        }
        float averageChannel(size_t channel) const
        {
            if (!valid()) return 0.0f;
            double sum = 0.0;
            size_t n = 0;
            for (size_t i = channel; i < rgba.size(); i += 4) { sum += rgba[i]; ++n; }
            return (float)(sum / (double)n) / 255.0f;
        }
        size_t countAbove(float threshold) const
        {
            size_t c = 0;
            for (uint32_t y = 0; y < height; ++y)
                for (uint32_t x = 0; x < width; ++x)
                    if (luminance(x, y) > threshold) ++c;
            return c;
        }
        // Maximum per-channel absolute difference (0..1).
        float maxAbsDifference(const Image &other) const
        {
            if (!valid() || !other.valid() || width != other.width || height != other.height) return 1.0f;
            int worst = 0;
            for (size_t i = 0; i < rgba.size(); ++i)
                worst = std::max(worst, std::abs((int)rgba[i] - (int)other.rgba[i]));
            return (float)worst / 255.0f;
        }
        // Mean absolute difference against another image of the same size.
        float meanAbsDifference(const Image &other) const
        {
            if (!valid() || !other.valid()) return 1.0f;
            if (width != other.width || height != other.height) return 1.0f;
            double sum = 0.0;
            for (size_t i = 0; i < rgba.size(); ++i)
                sum += std::fabs((double)rgba[i] - (double)other.rgba[i]);
            return (float)(sum / (double)rgba.size()) / 255.0f;
        }
    };

    //------------------------------------------------------------------
    // A tiny engine wrapper configured for headless verification.
    //------------------------------------------------------------------
    struct TestEngine
    {
        Engine engine;
        bool ok = false;

        bool init(uint32_t width = 256, uint32_t height = 256, bool with_ui = false, bool with_script = false)
        {
            EngineConfig cfg;
            cfg.app_name = "genesis-tests";
            cfg.width = width;
            cfg.height = height;
            cfg.backend = g_backend;
            cfg.headless = true;
            cfg.enable_ui = with_ui;
            cfg.enable_script = with_script;
            cfg.vsync = false;
            cfg.max_frames = 0;
            // Enable the overlay (free command list + SDF text) with the
            // vendored font so those paths are exercised on both backends.
            cfg.asset_dir = GENESIS_ASSET_DIR;
            if (ResourceIO::fileExists(assetPath("fonts/DejaVuSans.ttf"))) cfg.sdf_font_path = assetPath("fonts/DejaVuSans.ttf");
            ok = engine.initialize(cfg);
            if (!ok) GEN_LOG_ERROR("[test] engine init failed for backend %s", RHI::backendTypeName(g_backend));
            return ok;
        }

        bool capture(Image &out, float dt = 1.0f / 60.0f)
        {
            if (!ok) return false;
            (void)dt;
            return engine.captureFrame(out.rgba, out.width, out.height);
        }

        void addMesh(std::shared_ptr<ModelData> model, const Math::Vec3 &pos, float scale, const std::string &name)
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

        void addCamera(const Math::Vec3 &pos, const Math::Vec3 &target, float fov = 50.0f)
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

        void addDirectionalLight(const Math::Vec3 &direction, const Math::Color &color, float intensity)
        {
            ECS::World &world = engine.world();
            ECS::Entity e = world.createEntity("light");
            ECS::TransformComponent t;
            t.rotation = Math::Quat::lookRotation(direction, Math::Vec3(0, 1, 0));
            world.addComponent<ECS::TransformComponent>(e, t);
            ECS::LightComponent l;
            l.type = ECS::LightType::Directional;
            l.color = color;
            l.intensity = intensity;
            world.addComponent<ECS::LightComponent>(e, l);
        }
    };

} // namespace gpu_test

#endif /* __GENESIS_TEST_GPU_UTILS_HPP__ */
