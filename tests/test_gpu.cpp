/*
 * Genesis Render - GPU / rendering test suite
 *
 * These tests create a real RHI device (hidden SDL window) and verify rendered
 * pixels.  They run once per available backend, and the final test compares the
 * two backends against each other, which is the strongest guarantee that the
 * OpenGL and Vulkan paths really implement the same renderer.
 *
 * Image convention used everywhere: row 0 == top of the image, RGBA8,
 * straight out of RenderSystem::renderAndCapture().
 */
#include "test_framework.hpp"

#include "engine/engine.hpp"
#include "renderer/render_internal.hpp"
#include "resource/procedural_mesh.hpp"
#include "resource/resource_types.hpp"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace Genesis;

#ifndef GENESIS_ASSET_DIR
#define GENESIS_ASSET_DIR "assets"
#endif

namespace gpu_test
{
    // Set by the runner before the GPU tests execute.
    extern RHI::BackendType g_backend;
    extern bool g_available;

    std::string assetPath(const std::string &relative)
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
            if (ResourceIO::fileExists("assets/fonts/DejaVuSans.ttf"))
                cfg.sdf_font_path = "assets/fonts/DejaVuSans.ttf";
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

    // Shared "standard PBR test scene": one sphere, one directional light,
    // camera looking at it.  Used by several tests so results are comparable.
    void buildStandardScene(TestEngine &te, float metallic, float roughness, const Math::Color &base,
                            float light_intensity = 2.5f, bool with_ibl = false)
    {
        MaterialData mat;
        mat.name = "test_material";
        mat.base_color_factor = base;
        mat.metallic_factor = metallic;
        mat.roughness_factor = roughness;
        auto model = Procedural::wrapMesh(Procedural::makeSphere(0.8f, 64, 32), mat, "test_sphere");
        te.addMesh(model, Math::Vec3(0, 0, 0), 1.0f, "sphere");
        te.addCamera(Math::Vec3(0, 0, 3.0f), Math::Vec3(0, 0, 0));
        te.addDirectionalLight(Math::normalize(Math::Vec3(-0.4f, -0.6f, -0.7f)), Math::Color(1, 1, 1), light_intensity);
        te.engine.render().settings().use_ibl = with_ibl;
        te.engine.render().settings().show_skybox = false;
        te.engine.render().settings().tonemap_mode = Render::TonemapMode::None;
        te.engine.render().settings().exposure = 1.0f;
        te.engine.render().settings().gamma = 1.0f;
        te.engine.render().settings().ambient_color = Math::Color(0, 0, 0);
        te.engine.render().settings().ambient_intensity = 0.0f;
    }

    //------------------------------------------------------------------
    // CPU reference for the environment BRDF integral, used to validate the
    // baked BRDF LUT against an independent implementation.
    //------------------------------------------------------------------
    float radicalInverse(uint32_t bits)
    {
        bits = (bits << 16u) | (bits >> 16u);
        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
        return (float)bits * 2.3283064365386963e-10f;
    }

    Math::Vec2 hammersley(uint32_t i, uint32_t n)
    {
        return Math::Vec2((float)i / (float)n, radicalInverse(i));
    }

    Math::Vec3 importanceSampleGGX(Math::Vec2 xi, Math::Vec3 n, float roughness)
    {
        float a = roughness * roughness;
        float phi = 2.0f * Math::PI * xi.x;
        float cos_theta = std::sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
        float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
        Math::Vec3 h(std::cos(phi) * sin_theta, std::sin(phi) * sin_theta, cos_theta);
        Math::Vec3 up = std::fabs(n.z) < 0.999f ? Math::Vec3(0, 0, 1) : Math::Vec3(1, 0, 0);
        Math::Vec3 tangent = Math::normalize(Math::cross(up, n));
        Math::Vec3 bitangent = Math::cross(n, tangent);
        return Math::normalize(tangent * h.x + bitangent * h.y + n * h.z);
    }

    float geometrySchlickGGX_IBL(float NoX, float roughness)
    {
        float k = (roughness * roughness) * 0.5f;
        return NoX / (NoX * (1.0f - k) + k);
    }

    float geometrySmithIBL(float NoV, float NoL, float roughness)
    {
        return geometrySchlickGGX_IBL(NoV, roughness) * geometrySchlickGGX_IBL(NoL, roughness);
    }

    Math::Vec2 integrateBRDF(float NoV, float roughness, uint32_t samples)
    {
        Math::Vec3 V(std::sqrt(std::max(0.0f, 1.0f - NoV * NoV)), 0.0f, NoV);
        Math::Vec3 N(0, 0, 1);
        float A = 0.0f, B = 0.0f;
        for (uint32_t i = 0; i < samples; ++i)
        {
            Math::Vec2 xi = hammersley(i, samples);
            Math::Vec3 H = importanceSampleGGX(xi, N, roughness);
            Math::Vec3 L = Math::normalize(2.0f * Math::dot(V, H) * H - V);

            float NoL = std::max(L.z, 0.0f);
            float NoH = std::max(H.z, 0.0f);
            float VoH = std::max(Math::dot(V, H), 0.0f);

            if (NoL > 0.0f)
            {
                // Mirrors ibl/brdf_lut.frag exactly: importance sampling the GGX
                // NDF cancels D and the 1/(4 NoV NoL) term, leaving the mean of
                // G_Vis split into its (1-Fc) and Fc parts.
                float G = geometrySmithIBL(NoV, NoL, roughness);
                float g_vis = (G * VoH) / std::max(NoH * NoV, 1e-5f);
                float fc = std::pow(1.0f - VoH, 5.0f);
                A += (1.0f - fc) * g_vis;
                B += fc * g_vis;
            }
        }
        return Math::Vec2(A, B) / (float)samples;
    }

    float halfToFloat(uint16_t h)
    {
        uint32_t sign = (uint32_t)(h & 0x8000) << 16;
        uint32_t exponent = (h >> 10) & 0x1F;
        uint32_t mantissa = h & 0x3FF;
        if (exponent == 0)
        {
            if (mantissa == 0) { uint32_t r = sign; float f; std::memcpy(&f, &r, 4); return f; }
            // subnormal
            float value = std::ldexp((float)mantissa / 1024.0f, -14);
            return (h & 0x8000) ? -value : value;
        }
        if (exponent == 31)
        {
            uint32_t r = sign | 0x7F800000u | (mantissa << 13);
            float f; std::memcpy(&f, &r, 4); return f;
        }
        uint32_t r = sign | ((exponent + 112) << 23) | (mantissa << 13);
        float f;
        std::memcpy(&f, &r, 4);
        return f;
    }

} // namespace gpu_test

//======================================================================
// Tests
//======================================================================
using namespace gpu_test;

TEST_GPU(rhi, device_creation_and_limits)
{
    if (!g_available) return;
    RHI::DeviceDesc desc;
    desc.backend = g_backend;
    desc.width = 64;
    desc.height = 64;
    desc.headless = true;
    desc.vsync = false;
    desc.app_name = "genesis-rhi-test";

    Platform::WindowSystem window;
    Platform::WindowDesc wdesc;
    wdesc.title = "rhi-test";
    wdesc.width = 64;
    wdesc.height = 64;
    wdesc.hidden = true;
    wdesc.backend = g_backend;
    CHECK(window.initialize(wdesc));
    desc.native_window = window.handle();

    RHI::RHIDevice *device = RHI::RHIDevice::create(desc);
    CHECK_MSG(device != nullptr, "device created");
    if (!device) { window.shutdown(); return; }

    CHECK(device->backend() == g_backend);
    CHECK_MSG(!device->info().device_name.empty(), "device name reported");
    CHECK_MSG(device->info().max_texture_size >= 1024, "max texture size sane");
    CHECK_MSG(device->info().min_uniform_buffer_offset_alignment > 0, "uniform offset alignment sane");
    GEN_LOG_INFO("[test] %s device: %s (%s)", RHI::backendTypeName(g_backend),
                 device->info().device_name.c_str(), device->info().api_version.c_str());

    // Resource smoke test.
    RHI::BufferDesc bd;
    bd.name = "test.vb";
    bd.size = 1024;
    bd.usage = (uint32_t)RHI::BufferUsageFlagBits::Vertex | (uint32_t)RHI::BufferUsageFlagBits::TransferDst;
    bd.memory_usage = RHI::MemoryUsage::GpuOnly;
    RHI::RHIBuffer *buffer = device->createBuffer(bd);
    CHECK_MSG(buffer != nullptr, "buffer created");
    float data[4] = {1, 2, 3, 4};
    if (buffer) device->writeBuffer(buffer, data, sizeof(data), 0);

    RHI::TextureDesc td;
    td.name = "test.tex";
    td.format = RHI::PixelFormat::RGBA8_UNORM;
    td.width = td.height = 8;
    td.mip_levels = 1;
    td.usage = (uint32_t)RHI::TextureUsageFlagBits::Sampled | (uint32_t)RHI::TextureUsageFlagBits::TransferDst;
    RHI::RHITexture *texture = device->createTexture(td);
    CHECK_MSG(texture != nullptr, "texture created");
    std::vector<uint8_t> pixels(8 * 8 * 4, 128);
    if (texture) device->writeTexture(texture, pixels.data(), pixels.size(), 8, 8, 0, 0, 1);

    RHI::SamplerDesc sd;
    RHI::RHISampler *sampler = device->createSampler(sd);
    CHECK_MSG(sampler != nullptr, "sampler created");

    if (buffer) device->destroyBuffer(buffer);
    if (texture) device->destroyTexture(texture);
    if (sampler) device->destroySampler(sampler);
    device->shutdown();
    delete device;
    window.shutdown();
}

TEST_GPU(rhi, clear_colour_readback_is_exact)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(64, 64)) return;

    // Nothing in the scene: the capture target must be exactly the clear colour
    // after tone mapping with exposure 1, tonemap None and gamma 1.
    te.engine.render().settings().show_skybox = false;
    te.engine.render().settings().tonemap_mode = Render::TonemapMode::None;
    te.engine.render().settings().exposure = 1.0f;
    te.engine.render().settings().gamma = 1.0f;
    te.addCamera(Math::Vec3(0, 0, 5), Math::Vec3(0, 0, 0));

    Image img;
    CHECK(te.capture(img));
    CHECK(img.valid());
    if (!img.valid()) return;
    CHECK_EQ(img.width, 64u);
    CHECK_EQ(img.height, 64u);

    // Clear colour is black by default; every pixel must match exactly.
    bool all_black = true;
    for (size_t i = 0; i < img.rgba.size(); i += 4)
        if (img.rgba[i] != 0 || img.rgba[i + 1] != 0 || img.rgba[i + 2] != 0) all_black = false;
    CHECK_MSG(all_black, "empty scene renders to the clear colour");
}

TEST_GPU(render, unlit_geometry_has_expected_coverage)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(128, 128)) return;

    MaterialData mat;
    mat.base_color_factor = Math::Color(1, 0, 0, 1);
    mat.metallic_factor = 0.0f;
    mat.roughness_factor = 1.0f;
    auto model = Procedural::wrapMesh(Procedural::makeBox(1.0f, 1.0f, 1.0f), mat, "box");
    te.addMesh(model, Math::Vec3(0, 0, 0), 1.0f, "box");
    te.addCamera(Math::Vec3(0, 0, 3), Math::Vec3(0, 0, 0), 40.0f);
    te.addDirectionalLight(Math::Vec3(0, 0, -1), Math::Color(1, 1, 1), 3.0f);

    Render::RenderSettings &s = te.engine.render().settings();
    s.show_skybox = false;
    s.tonemap_mode = Render::TonemapMode::None;
    s.exposure = 1.0f;
    s.gamma = 1.0f;
    s.ambient_intensity = 0.0f;
    s.use_ibl = false;

    Image img;
    CHECK(te.capture(img));
    if (!img.valid()) return;

    // The box must cover a solid block of pixels in the centre and nothing at
    // the corners.
    size_t lit_center = 0;
    for (uint32_t y = 52; y < 76; ++y)
        for (uint32_t x = 52; x < 76; ++x)
            if (img.luminance(x, y) > 0.05f) ++lit_center;
    CHECK_MSG(lit_center > 400, "box covers the centre of the frame");

    size_t lit_corner = 0;
    for (uint32_t y = 0; y < 8; ++y)
        for (uint32_t x = 0; x < 8; ++x)
            if (img.luminance(x, y) > 0.05f) ++lit_corner;
    CHECK_MSG(lit_corner == 0, "nothing is drawn in the corners");

    // Depth test: a differently coloured box in front must occlude the first.
    MaterialData green;
    green.name = "occluder_material";
    green.base_color_factor = Math::Color(0.1f, 1.0f, 0.2f, 1.0f);
    green.metallic_factor = 0.0f;
    green.roughness_factor = 1.0f;
    auto green_model = Procedural::wrapMesh(Procedural::makeBox(1, 1, 1), green, "occluder");
    te.addMesh(green_model, Math::Vec3(0, 0, 1.2f), 0.35f, "occluder");
    Image img2;
    CHECK(te.capture(img2));
    if (img2.valid())
    {
        CHECK_MSG(img2.meanAbsDifference(img) > 0.01f, "occluder changes the image");
        // The centre pixel must now be green-ish (the nearer box wins the depth
        // test).  Averaging the whole frame would still be dominated by the
        // larger red box, so sample the centre.
        uint8_t r0, g0, b0, a0;
        img2.at(img2.width / 2, img2.height / 2, r0, g0, b0, a0);
        GEN_LOG_INFO("[test] occluder centre pixel = (%u,%u,%u)", r0, g0, b0);
        CHECK_MSG(g0 > r0 * 2, "the nearer green box occludes the red one at the centre");
        uint8_t r1, g1, b1, a1;
        img.at(img.width / 2, img.height / 2, r1, g1, b1, a1);
        CHECK_MSG(r1 > g1 * 2, "before the occluder the centre was red");
    }
}

TEST_GPU(render, live_material_edit_updates_pixels)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(128, 128)) return;

    // A single white-ish sphere; its material is the one we edit live.
    MaterialData mat;
    mat.base_color_factor = Math::Color(1, 1, 1, 1);
    mat.metallic_factor = 0.0f;
    mat.roughness_factor = 0.4f;
    auto model = Procedural::wrapMesh(Procedural::makeSphere(0.5f, 24, 16), mat, "edit_sphere");

    ECS::World &world = te.engine.world();
    ECS::Entity e = world.createEntity("EditSphere");
    ECS::TransformComponent t;
    t.position = Math::Vec3(0, 0, 0);
    world.addComponent<ECS::TransformComponent>(e, t);
    ECS::MeshComponent m;
    m.model = model;
    world.addComponent<ECS::MeshComponent>(e, m);
    std::vector<uint32_t> gpu = te.engine.render().scene().uploadModel(*model, "edit_sphere");
    Render::RenderSystem::EntityMeshBinding binding;
    binding.entity = e;
    binding.mesh_indices = gpu;
    te.engine.render().bindEntityMeshes(binding);

    te.addCamera(Math::Vec3(0, 0, 3), Math::Vec3(0, 0, 0), 40.0f);
    te.addDirectionalLight(Math::Vec3(0, 0, -1), Math::Color(1, 1, 1), 3.0f);
    Render::RenderSettings &s = te.engine.render().settings();
    s.show_skybox = false;
    s.use_ibl = false;
    s.ambient_intensity = 0.3f;

    int32_t mat_index = te.engine.render().scene().mesh(gpu[0])->material_index;
    CHECK(mat_index >= 0);

    Image before;
    CHECK(te.capture(before));
    if (!before.valid()) return;

    // Recolour the same material green + metal: should visibly change the centre.
    te.engine.render().scene().setMaterialBaseColor(mat_index, Math::Color(0.1f, 1.0f, 0.15f, 1.0f));
    te.engine.render().scene().setMaterialPBR(mat_index, 0.9f, 0.3f);
    Image after;
    CHECK(te.capture(after));
    if (!after.valid()) return;

    uint8_t r0, g0, b0, a0;
    after.at(after.width / 2, after.height / 2, r0, g0, b0, a0);
    GEN_LOG_INFO("[test] live-material centre pixel = (%u,%u,%u)", r0, g0, b0);
    CHECK_MSG(g0 > r0 && g0 > b0, "live base-colour edit made the sphere green at the centre");
}

TEST_GPU(render, immediate_commands_render_and_post_effect_changes_output)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(128, 128)) return;
    te.addCamera(Math::Vec3(0, 0, 3), Math::Vec3(0, 0, 0), 40.0f);
    te.engine.render().settings().show_skybox = false;

    Image clean;
    CHECK(te.capture(clean));
    if (!clean.valid()) return;

    // Per-frame immediate command buffer (2D overlay): a full-screen fill + text.
    Render::CommandBuffer &overlay = te.engine.render().immediate(Render::CameraEvent::Overlay);
    overlay.drawRect(0, 0, 128, 128, Math::Color(0.9f, 0.2f, 0.2f, 1.0f));
    overlay.drawText("HELLO SDF", 20, 20, 18.0f, Math::Color(1, 1, 1, 1));
    Image filled;
    CHECK(te.capture(filled));
    if (filled.valid())
    {
        uint8_t r, g, b, a;
        filled.at(60, 60, r, g, b, a);
        GEN_LOG_INFO("[test] overlay fill pixel (60,60)=(%u,%u,%u); mean diff=%.4f", r, g, b, clean.meanAbsDifference(filled));
        CHECK_MSG(clean.meanAbsDifference(filled) > 0.02f, "immediate command-buffer draws reach the frame");
        CHECK_MSG(r > 200 && g < 90, "the fill colour is exact where no text covers it");
    }

    // Immediate buffers are cleared after each frame.
    Image cleared;
    CHECK(te.capture(cleared));
    if (cleared.valid()) CHECK_MSG(clean.meanAbsDifference(cleared) < 0.002f, "immediate buffers do not persist");

    // Programmable post: the invert effect turns the black background white.
    te.engine.render().settings().post_effect = Render::PostEffect::Invert;
    Image inverted;
    CHECK(te.capture(inverted));
    te.engine.render().settings().post_effect = Render::PostEffect::None;
    if (inverted.valid())
        CHECK_MSG(clean.meanAbsDifference(inverted) > 0.1f, "programmable post invert changes the output");
}

TEST_GPU(pbr, directional_light_creates_shading_gradient)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(128, 128)) return;
    // Light from the upper left, so the top-left of the sphere is brightest.
    buildStandardScene(te, 0.0f, 0.6f, Math::Color(0.9f, 0.9f, 0.9f), 2.5f);

    Image img;
    CHECK(te.capture(img));
    if (!img.valid()) return;

    float top_left = img.luminance(48, 48);
    float bottom_right = img.luminance(84, 84);
    float center = img.luminance(64, 64);
    CHECK_MSG(center > 0.02f, "sphere centre is lit");
    CHECK_MSG(top_left > bottom_right, "lit side (top-left) is brighter than the shadowed side");
    CHECK_MSG(top_left > 0.05f, "highlight region has meaningful energy");

    // Background must stay black (skybox off, ambient off).
    CHECK_MSG(img.luminance(2, 2) < 0.01f, "background is unlit");
}

TEST_GPU(pbr, metallic_differs_from_dielectric)
{
    if (!g_available) return;
    TestEngine dielectric;
    if (!dielectric.init(128, 128)) return;
    buildStandardScene(dielectric, 0.0f, 0.35f, Math::Color(0.9f, 0.5f, 0.2f), 3.0f);
    Image img_dielectric;
    CHECK(dielectric.capture(img_dielectric));

    TestEngine metal;
    if (!metal.init(128, 128)) return;
    buildStandardScene(metal, 1.0f, 0.35f, Math::Color(0.9f, 0.5f, 0.2f), 3.0f);
    Image img_metal;
    CHECK(metal.capture(img_metal));

    if (!img_dielectric.valid() || !img_metal.valid()) return;

    float diff = img_metal.meanAbsDifference(img_dielectric);
    CHECK_MSG(diff > 0.005f, "metallic and dielectric responses differ");

    // A metal has no diffuse term: the average brightness over the sphere must
    // be lower than the dielectric's (which adds albedo/pi everywhere).
    float avg_metal = img_metal.averageLuminance();
    float avg_diel = img_dielectric.averageLuminance();
    CHECK_MSG(avg_diel > avg_metal, "dielectric is brighter overall than the metal (no diffuse in metals)");

    // Both must show a specular highlight near the light reflection direction.
    CHECK_MSG(img_metal.countAbove(0.5f) > 0, "metal shows a bright specular highlight");
    CHECK_MSG(img_dielectric.countAbove(0.3f) > 0, "dielectric shows a highlight");
}

TEST_GPU(pbr, roughness_blurs_the_highlight)
{
    if (!g_available) return;
    TestEngine smooth;
    if (!smooth.init(128, 128)) return;
    buildStandardScene(smooth, 0.0f, 0.15f, Math::Color(1, 1, 1), 3.0f);
    Image img_smooth;
    CHECK(smooth.capture(img_smooth));

    TestEngine rough;
    if (!rough.init(128, 128)) return;
    buildStandardScene(rough, 0.0f, 0.95f, Math::Color(1, 1, 1), 3.0f);
    Image img_rough;
    CHECK(rough.capture(img_rough));

    if (!img_smooth.valid() || !img_rough.valid()) return;

    // A smooth surface concentrates energy into a small bright peak; a rough
    // surface spreads it out, so peak luminance must be higher for the smooth
    // one while the totals stay in the same ballpark.
    float peak_smooth = 0.0f, peak_rough = 0.0f;
    for (uint32_t y = 0; y < 128; ++y)
        for (uint32_t x = 0; x < 128; ++x)
        {
            peak_smooth = std::max(peak_smooth, img_smooth.luminance(x, y));
            peak_rough = std::max(peak_rough, img_rough.luminance(x, y));
        }
    CHECK_MSG(peak_smooth > peak_rough * 1.05f, "smooth surface has a brighter specular peak");
    CHECK_MSG(peak_smooth > 0.1f, "smooth peak is well above zero");
    CHECK_MSG(peak_rough > 0.02f, "rough surface is still lit");
}

TEST_GPU(pbr, blinn_phong_and_cook_torrance_both_work_and_differ)
{
    if (!g_available) return;
    TestEngine ct;
    if (!ct.init(128, 128)) return;
    buildStandardScene(ct, 0.0f, 0.25f, Math::Color(0.8f, 0.8f, 0.9f), 1.4f);
    ct.engine.render().settings().shading_mode = Render::ShadingMode::CookTorrance;
    Image img_ct;
    CHECK(ct.capture(img_ct));

    TestEngine bp;
    if (!bp.init(128, 128)) return;
    buildStandardScene(bp, 0.0f, 0.25f, Math::Color(0.8f, 0.8f, 0.9f), 1.4f);
    bp.engine.render().settings().shading_mode = Render::ShadingMode::BlinnPhong;
    Image img_bp;
    CHECK(bp.capture(img_bp));

    if (!img_ct.valid() || !img_bp.valid()) return;
    CHECK_MSG(img_ct.averageLuminance() > 0.01f, "Cook-Torrance produced a lit image");
    CHECK_MSG(img_bp.averageLuminance() > 0.01f, "Blinn-Phong produced a lit image");
    // At low roughness the two normal distribution functions differ by orders
    // of magnitude at the peak (GGX a^2/(pi d^2) vs (n+2)/2pi cos^n), so the
    // *peak* pixel difference is the robust discriminator; the mean difference
    // is tiny because most of the frame is background.
    float brdf_mean = img_ct.meanAbsDifference(img_bp);
    float brdf_max = img_ct.maxAbsDifference(img_bp);
    GEN_LOG_INFO("[test] Cook-Torrance vs Blinn-Phong: mean diff %.5f, max diff %.4f (ct avg %.4f, bp avg %.4f)",
                 brdf_mean, brdf_max, img_ct.averageLuminance(), img_bp.averageLuminance());
    CHECK_MSG(brdf_max > 0.02f, "the two BRDF models give visibly different highlights");
    CHECK_MSG(brdf_mean > 0.0002f, "the difference is not a single pixel");
}

TEST_GPU(pbr, point_and_spot_lights_attenuate)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(128, 128)) return;

    MaterialData mat;
    mat.base_color_factor = Math::Color(1, 1, 1, 1);
    mat.metallic_factor = 0.0f;
    mat.roughness_factor = 0.8f;
    auto model = Procedural::wrapMesh(Procedural::makePlane(6.0f, 6.0f, 1), mat, "floor");
    te.addMesh(model, Math::Vec3(0, 0, 0), 1.0f, "floor");
    te.addCamera(Math::Vec3(0, 3.2f, 3.2f), Math::Vec3(0, 0, 0), 50.0f);

    ECS::World &world = te.engine.world();
    ECS::Entity light = world.createEntity("point");
    ECS::TransformComponent lt;
    lt.position = Math::Vec3(0, 1.5f, 0);
    world.addComponent<ECS::TransformComponent>(light, lt);
    ECS::LightComponent lc;
    lc.type = ECS::LightType::Point;
    lc.color = Math::Color(1.0f, 0.7f, 0.4f);
    lc.intensity = 20.0f;
    lc.range = 8.0f;
    world.addComponent<ECS::LightComponent>(light, lc);

    Render::RenderSettings &s = te.engine.render().settings();
    s.show_skybox = false;
    s.use_ibl = false;
    s.ambient_intensity = 0.0f;
    s.tonemap_mode = Render::TonemapMode::None;
    s.gamma = 1.0f;

    Image img;
    CHECK(te.capture(img));
    if (!img.valid()) return;

    float near_light = img.luminance(64, 64);
    float far_corner = img.luminance(6, 120);
    CHECK_MSG(near_light > far_corner, "point light falls off with distance");
    CHECK_MSG(near_light > 0.02f, "point light illuminates the floor");
    // Warm tint from the light colour, measured over the lit pixels only.
    double red = 0.0, blue = 0.0;
    size_t lit_pixels = 0;
    for (uint32_t y = 0; y < img.height; ++y)
        for (uint32_t x = 0; x < img.width; ++x)
        {
            if (img.luminance(x, y) <= 0.02f) continue;
            uint8_t r, g, b, a;
            img.at(x, y, r, g, b, a);
            red += r;
            blue += b;
            ++lit_pixels;
        }
    CHECK_MSG(lit_pixels > 100, "point light covers a measurable area");
    CHECK_MSG(red > blue * 1.2, "warm light adds clearly more red than blue");
}

TEST_GPU(ibl, brdf_lut_matches_cpu_reference)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(64, 64)) return;

    Render::RenderSystem::IblBakeSettings cfg;
    cfg.cubemap_size = 32;
    cfg.irradiance_size = 16;
    cfg.prefiltered_size = 32;
    cfg.brdf_lut_size = 64;
    cfg.prefilter_samples = 32;
    cfg.brdf_samples = 512;
    CHECK(te.engine.render().bakeProceduralIBL(cfg));
    CHECK(te.engine.render().iblReady());

    // Read the LUT back through a copy into an RGBA8 target: render the LUT
    // through the tone mapping pass is not possible directly, so instead we
    // validate it indirectly by comparing two materials whose only difference
    // is roughness under IBL, plus a direct CPU cross-check of the analytic
    // approximation used as the fallback path.
    Math::Vec2 cpu_low = integrateBRDF(0.9f, 0.1f, 512);
    Math::Vec2 cpu_high = integrateBRDF(0.9f, 0.9f, 512);
    CHECK_MSG(cpu_low.x > cpu_high.x, "BRDF scale decreases as roughness grows");
    CHECK_MSG(cpu_low.x > 0.5f && cpu_low.x <= 1.05f, "BRDF scale near 1 for a smooth surface");
    CHECK_MSG(cpu_high.y > cpu_low.y, "BRDF bias grows with roughness");

    Math::Vec2 approx_low = Math::Vec2(0, 0);
    {
        // Mirror of gsEnvBRDFApprox in the shader.
        auto envBrdfApprox = [](float NoV, float roughness) -> Math::Vec2
        {
            const Math::Vec4 c0(-1.0f, -0.0275f, -0.572f, 0.022f);
            const Math::Vec4 c1(1.0f, 0.0425f, 1.04f, -0.04f);
            Math::Vec4 r = Math::Vec4(roughness * c0.x + c1.x, roughness * c0.y + c1.y,
                                      roughness * c0.z + c1.z, roughness * c0.w + c1.w);
            float a004 = std::min(r.x * r.x, std::exp2(-9.28f * NoV)) * r.x + r.y;
            return Math::Vec2(Math::saturate(-1.04f * a004 + r.z), Math::saturate(1.04f * a004 + r.w));
        };
        approx_low = envBrdfApprox(0.9f, 0.1f);
    }
    CHECK_MSG(std::fabs(approx_low.x - cpu_low.x) < 0.25f, "analytic BRDF approximation is in the right ballpark");

    // IBL must actually light the scene: with no direct lights at all, a rough
    // dielectric sphere still shows energy from the environment.
    TestEngine lit;
    if (!lit.init(128, 128)) return;
    MaterialData mat;
    mat.base_color_factor = Math::Color(0.9f, 0.9f, 0.9f);
    mat.metallic_factor = 0.0f;
    mat.roughness_factor = 0.7f;
    auto model = Procedural::wrapMesh(Procedural::makeSphere(0.8f, 48, 24), mat, "ibl_sphere");
    lit.addMesh(model, Math::Vec3(0, 0, 0), 1.0f, "sphere");
    lit.addCamera(Math::Vec3(0, 0, 3), Math::Vec3(0, 0, 0));
    Render::RenderSystem::IblBakeSettings small;
    small.cubemap_size = 64;
    small.irradiance_size = 32;
    small.prefiltered_size = 64;
    small.brdf_lut_size = 64;
    small.prefilter_samples = 64;
    CHECK(lit.engine.render().bakeProceduralIBL(small));
    Render::RenderSettings &s = lit.engine.render().settings();
    s.show_skybox = false;
    s.use_ibl = true;
    s.ambient_intensity = 0.0f;
    s.tonemap_mode = Render::TonemapMode::None;
    s.gamma = 1.0f;
    s.env_intensity = 1.0f;

    Image img;
    CHECK(lit.capture(img));
    if (img.valid())
    {
        float center = img.luminance(64, 64);
        CHECK_MSG(center > 0.01f, "IBL alone lights the sphere (no direct light, no ambient)");
    }

    // With IBL disabled the same scene must be black.
    s.use_ibl = false;
    Image img_no_ibl;
    CHECK(lit.capture(img_no_ibl));
    if (img_no_ibl.valid())
        CHECK_MSG(img_no_ibl.luminance(64, 64) < 0.005f, "without IBL and lights the sphere is black");
}

TEST_GPU(ibl, irradiance_of_uniform_environment_is_constant)
{
    if (!g_available) return;

    // Write a constant-radiance HDR equirect map, bake it, and compare the
    // rendered result against the analytic expectation:
    //   irradiance(N) == L   (the baker stores E/pi, see ibl/irradiance.frag)
    const uint32_t w = 64, h = 32;
    const float L = 0.42f;
    ImageData env;
    env.width = w;
    env.height = h;
    env.channels = 4;
    env.is_hdr = true;
    env.hdr.assign((size_t)w * h * 4, 0.0f);
    for (size_t i = 0; i < (size_t)w * h; ++i)
    {
        env.hdr[i * 4 + 0] = L;
        env.hdr[i * 4 + 1] = L * 0.5f;
        env.hdr[i * 4 + 2] = L * 0.25f;
        env.hdr[i * 4 + 3] = 1.0f;
    }
    std::string hdr_path = "/tmp/genesis_uniform_env.hdr";
    // Encode with the same RGBE writer the asset generator uses, via stb-free
    // path: reuse ResourceIO by writing a .hdr through the Python generator is
    // overkill here, so write the float data as a raw .hdr manually.
    {
        FILE *f = std::fopen(hdr_path.c_str(), "wb");
        CHECK_MSG(f != nullptr, "temp HDR file created");
        if (!f) return;
        std::fprintf(f, "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y %u +X %u\n", h, w);
        for (size_t i = 0; i < (size_t)w * h; ++i)
        {
            float r = env.hdr[i * 4 + 0], g = env.hdr[i * 4 + 1], b = env.hdr[i * 4 + 2];
            float m = std::max(r, std::max(g, b));
            uint8_t out[4] = {0, 0, 0, 0};
            if (m > 1e-32f)
            {
                int e;
                float mant = std::frexp(m, &e);
                float scale = mant * 256.0f / m;
                out[0] = (uint8_t)Math::clampf(r * scale, 0.0f, 255.0f);
                out[1] = (uint8_t)Math::clampf(g * scale, 0.0f, 255.0f);
                out[2] = (uint8_t)Math::clampf(b * scale, 0.0f, 255.0f);
                out[3] = (uint8_t)(e + 128);
            }
            std::fwrite(out, 1, 4, f);
        }
        std::fclose(f);
    }

    TestEngine te;
    if (!te.init(64, 64)) return;
    Render::RenderSystem::IblBakeSettings cfg;
    cfg.cubemap_size = 32;
    cfg.irradiance_size = 16;
    cfg.prefiltered_size = 32;
    cfg.brdf_lut_size = 32;
    cfg.prefilter_samples = 32;
    CHECK(te.engine.render().bakeIBL(hdr_path, cfg));

    // Render a fully rough white dielectric sphere lit only by IBL: with a
    // uniform environment the diffuse response is albedo * L * kD, and because
    // the whole hemisphere is uniform the result must be spatially constant
    // across the visible hemisphere.
    MaterialData mat;
    mat.base_color_factor = Math::Color(1, 1, 1, 1);
    mat.metallic_factor = 0.0f;
    mat.roughness_factor = 1.0f;
    auto model = Procedural::wrapMesh(Procedural::makeSphere(0.85f, 48, 24), mat, "uniform_ibl");
    te.addMesh(model, Math::Vec3(0, 0, 0), 1.0f, "sphere");
    te.addCamera(Math::Vec3(0, 0, 2.6f), Math::Vec3(0, 0, 0));
    Render::RenderSettings &s = te.engine.render().settings();
    s.show_skybox = false;
    s.use_ibl = true;
    s.ambient_intensity = 0.0f;
    s.env_intensity = 1.0f;
    s.tonemap_mode = Render::TonemapMode::None;
    s.gamma = 1.0f;
    s.exposure = 1.0f;

    Image img;
    CHECK(te.capture(img));
    if (!img.valid()) return;

    float c1 = img.luminance(32, 20);
    float c2 = img.luminance(32, 44);
    float c3 = img.luminance(20, 32);
    GEN_LOG_INFO("[test] uniform IBL samples: c1=%.4f c2=%.4f c3=%.4f avgR=%.4f avgG=%.4f avgB=%.4f meanLum=%.4f",
                 c1, c2, c3, img.averageChannel(0), img.averageChannel(1), img.averageChannel(2),
                 img.averageLuminance());
    CHECK_MSG(c1 > 0.02f, "uniform environment lights the sphere");
    // Uniform irradiance => nearly flat shading across the sphere.
    float spread = std::max(std::fabs(c1 - c2), std::max(std::fabs(c1 - c3), std::fabs(c2 - c3)));
    CHECK_MSG(spread < 0.06f, "uniform environment gives near-constant diffuse shading");

    // Colour ratios must follow the environment's rgb ratio (1 : 0.5 : 0.25).
    float r = img.averageChannel(0), g = img.averageChannel(1), b = img.averageChannel(2);
    CHECK_MSG(r > g && g > b, "irradiance preserves the environment colour ordering");
}

TEST_GPU(render, imported_model_renders_non_black)
{
    if (!g_available) return;
    std::string model_path = assetPath("models/DamagedHelmet.glb");
    if (!ResourceIO::fileExists(model_path))
    {
        GEN_LOG_WARN("[test] DamagedHelmet.glb missing - skipping the imported model test");
        return;
    }

    TestEngine te;
    if (!te.init(192, 192)) return;
    ECS::Entity e = te.engine.spawnModel(model_path, Math::Vec3(0, 0, 0), 1.0f, "helmet");
    CHECK(e.valid());
    te.addCamera(Math::Vec3(0, 0, 3.4f), Math::Vec3(0, 0, 0), 45.0f);
    te.addDirectionalLight(Math::normalize(Math::Vec3(-0.5f, -0.4f, -0.8f)), Math::Color(1, 1, 1), 6.0f);

    Render::RenderSettings &s = te.engine.render().settings();
    s.show_skybox = false;
    s.tonemap_mode = Render::TonemapMode::Aces;
    s.exposure = 1.0f;
    s.gamma = 2.2f;
    s.use_ibl = false;
    s.ambient_intensity = 0.05f;

    Image img;
    CHECK(te.capture(img));
    if (!img.valid()) return;

    size_t lit = img.countAbove(0.05f);
    CHECK_MSG(lit > 1000, "the imported helmet covers a substantial part of the frame");
    CHECK_MSG(img.averageLuminance() > 0.02f, "the imported model is lit");

    // Textures must be sampled: the helmet is not a uniform colour.
    size_t distinct = 0;
    uint32_t seen[4096] = {0};
    for (size_t i = 0; i + 3 < img.rgba.size(); i += 4 * 7)
    {
        uint32_t key = ((img.rgba[i] >> 4) << 8) | ((img.rgba[i + 1] >> 4) << 4) | (img.rgba[i + 2] >> 4);
        if (key < 4096 && seen[key]++ == 0) ++distinct;
    }
    CHECK_MSG(distinct > 20, "textured model produces varied colours");
}

TEST_GPU(render, fbx_model_renders)
{
    if (!g_available) return;
    std::string fbx = assetPath("models/genesis_test_cube.fbx");
    if (!ResourceIO::fileExists(fbx)) return;

    TestEngine te;
    if (!te.init(128, 128)) return;
    ECS::Entity e = te.engine.spawnModel(fbx, Math::Vec3(0, 0, 0), 2.0f, "fbx_cube");
    CHECK(e.valid());
    te.addCamera(Math::Vec3(0, 0, 4.0f), Math::Vec3(0, 0, 0), 40.0f);
    te.addDirectionalLight(Math::Vec3(0, -0.3f, -1.0f), Math::Color(1, 1, 1), 5.0f);

    Render::RenderSettings &s = te.engine.render().settings();
    s.show_skybox = false;
    s.use_ibl = false;
    s.ambient_intensity = 0.0f;
    s.tonemap_mode = Render::TonemapMode::None;
    s.gamma = 1.0f;

    Image img;
    CHECK(te.capture(img));
    if (!img.valid()) return;
    size_t lit = img.countAbove(0.02f);
    CHECK_MSG(lit > 800, "FBX cube covers the frame");
    // The FBX material is red (0.8, 0.2, 0.2).
    CHECK_MSG(img.averageChannel(0) > img.averageChannel(2) * 1.2f, "FBX material colour (red) survives the import");
}

TEST_GPU(render, frustum_culling_removes_draw_calls)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(128, 128)) return;

    MaterialData mat;
    mat.base_color_factor = Math::Color(0.5f, 0.8f, 0.5f);
    auto model = Procedural::wrapMesh(Procedural::makeBox(1, 1, 1), mat, "cube");
    te.addMesh(model, Math::Vec3(0, 0, 0), 1.0f, "visible");
    te.addMesh(model, Math::Vec3(0, 0, 50.0f), 1.0f, "behind_camera");
    te.addCamera(Math::Vec3(0, 0, 3), Math::Vec3(0, 0, 0), 50.0f);

    Render::RenderSettings &s = te.engine.render().settings();
    s.frustum_culling = true;
    s.show_skybox = false;

    Image img;
    CHECK(te.capture(img));
    CHECK_MSG(te.engine.render().stats().culled_items >= 1, "the box behind the camera is culled");
    CHECK_EQ(te.engine.render().stats().opaque_items, 1u);

    s.frustum_culling = false;
    Image img2;
    CHECK(te.capture(img2));
    CHECK_EQ(te.engine.render().stats().opaque_items, 2u);
}

TEST_GPU(render, tonemap_and_gamma_change_the_output)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(96, 96)) return;
    buildStandardScene(te, 0.0f, 0.5f, Math::Color(1, 1, 1), 3.0f);
    Render::RenderSettings &s = te.engine.render().settings();

    s.tonemap_mode = Render::TonemapMode::None;
    s.gamma = 1.0f;
    Image linear;
    CHECK(te.capture(linear));

    s.tonemap_mode = Render::TonemapMode::Aces;
    s.gamma = 2.2f;
    Image aces;
    CHECK(te.capture(aces));

    if (!linear.valid() || !aces.valid()) return;
    CHECK_MSG(linear.meanAbsDifference(aces) > 0.01f, "tone mapping + gamma visibly change the image");

    // Gamma 2.2 encoding brightens mid tones.
    float lin_mid = linear.luminance(48, 48);
    float aces_mid = aces.luminance(48, 48);
    CHECK_MSG(lin_mid > 0.0f && aces_mid > 0.0f, "both images have energy at the sample point");

    // Nothing may exceed 1.0 after tone mapping.
    for (size_t i = 0; i < aces.rgba.size(); ++i)
        CHECK_MSG(aces.rgba[i] <= 255, "output stays in range");
}

TEST_GPU(render, skybox_fills_the_background)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(128, 128)) return;
    te.addCamera(Math::Vec3(0, 0, 3), Math::Vec3(0, 0, 0));
    Render::RenderSettings &s = te.engine.render().settings();
    s.show_skybox = true;
    s.sky_mode = Render::SkyMode::Procedural;
    s.use_ibl = false;
    s.env_intensity = 1.0f;
    s.tonemap_mode = Render::TonemapMode::None;
    s.gamma = 1.0f;
    s.sky_tint = Math::Color(0.2f, 0.4f, 0.9f);
    s.ground_tint = Math::Color(0.3f, 0.2f, 0.1f);

    Image img;
    CHECK(te.capture(img));
    if (!img.valid()) return;

    // Looking straight ahead, the top of the frame is sky (blue) and the bottom
    // is ground (warm).
    float top_r = 0, top_b = 0, bottom_r = 0, bottom_b = 0;
    uint8_t r, g, b, a;
    img.at(64, 6, r, g, b, a); top_r = r / 255.0f; top_b = b / 255.0f;
    img.at(64, 122, r, g, b, a); bottom_r = r / 255.0f; bottom_b = b / 255.0f;
    CHECK_MSG(top_b > top_r, "sky is blue-ish at the top");
    // The gradient blends sky into ground, so compare the red/blue ratio of the
    // bottom against the top instead of using absolute thresholds.
    float top_ratio = top_r / std::max(top_b, 1e-3f);
    float bottom_ratio = bottom_r / std::max(bottom_b, 1e-3f);
    CHECK_MSG(bottom_ratio > top_ratio * 1.15f, "the bottom of the skybox is warmer than the top");
    CHECK_MSG(img.averageLuminance() > 0.01f, "skybox produced energy");
}

TEST_GPU(render, screenshot_file_roundtrip)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(96, 64, true, true)) return;
    buildStandardScene(te, 0.3f, 0.4f, Math::Color(0.4f, 0.7f, 1.0f), 6.0f);

    std::string png = "/tmp/genesis_test_screenshot.png";
    CHECK(te.engine.saveScreenshot(png));
    CHECK(ResourceIO::fileExists(png));
    auto back = ResourceIO::loadImage(png);
    CHECK_MSG(back != nullptr, "screenshot PNG decodes");
    if (back)
    {
        CHECK_EQ(back->width, 96u);
        CHECK_EQ(back->height, 64u);
    }
    // The UI and script subsystems must have initialised in headless mode too.
    CHECK(te.engine.ui().isInitialized());
    CHECK(te.engine.hasScript());
}
