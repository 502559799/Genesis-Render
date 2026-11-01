/*
 * Genesis Render - tests for the v4 extensions:
 *   asset packages, JSON scene loading, V8 JavaScript, physics and gaussian
 *   splat parsing / projection.
 */
#include "test_framework.hpp"
#include "test_gpu_utils.hpp"

#include "ecs/components.hpp"
#include "physics/physics.hpp"
#include "renderer/gaussian_splat.hpp"
#include "resource/asset_package.hpp"
#include "resource/resource_types.hpp"
#include "animation/animation_system.hpp"
#include "api/compute_api.hpp"
#include "api/encoder_decoder_api.hpp"
#include "api/ray_tracing_api.hpp"
#include "api/rendering_api.hpp"
#include "media/media_player.hpp"
#include "scene/scene_loader.hpp"
#include "script/js_system.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace Genesis;

namespace
{
    ECS::Entity addBody(ECS::World &world, const std::string &name, const Math::Vec3 &position,
                        ECS::BodyType type, const ECS::ColliderComponent *collider)
    {
        ECS::Entity e = world.createEntity(name);
        ECS::TransformComponent t;
        t.position = position;
        world.addComponent<ECS::TransformComponent>(e, t);
        ECS::RigidBodyComponent rb;
        rb.type = type;
        rb.linear_damping = 0.0f;
        rb.angular_damping = 0.0f;
        rb.restitution = 0.0f;
        rb.friction = 0.8f;
        world.addComponent<ECS::RigidBodyComponent>(e, rb);
        if (collider) world.addComponent<ECS::ColliderComponent>(e, *collider);
        return e;
    }

    void simulate(Physics::PhysicsSystem &physics, ECS::World &world, float seconds)
    {
        const float dt = 1.0f / 60.0f;
        for (float t = 0.0f; t < seconds; t += dt) physics.update(world, dt);
    }
} // namespace

//======================================================================
// Asset package
//======================================================================
TEST(package, cascade_loads_and_validates)
{
    AssetPackage package;
    std::string error;
    CHECK(package.loadFromFile(gpu_test::assetPath("packages/cascade.gpkg"), &error));
    CHECK(package.loaded());
    CHECK_EQ(package.name(), "cascade");
    CHECK(package.entries().size() >= 20);
    CHECK(package.find("package.json") != nullptr);
    CHECK(package.find("scenes/cascade.scene.json") != nullptr);
    CHECK(package.find("materials/crate.material.json") != nullptr);
    CHECK(package.find("scripts/cascade.js") != nullptr);
    CHECK(package.find("scripts/cascade.lua") != nullptr);
    CHECK(package.find("config/game.json") != nullptr);
    CHECK(package.find("textures/checker_albedo.png") != nullptr);
    CHECK(package.find("shaders/generated/pbr_mesh.vert.spv") != nullptr);
    CHECK(package.find("shaders/generated/pbr_mesh.vert.metal") != nullptr);

    std::string config;
    CHECK(package.readText("config/game.json", config));
    CHECK(config.find("target_score") != std::string::npos);
    std::string shader;
    CHECK(package.readText("shaders/generated/pbr_mesh.metal", shader) == false); // not shipped under this name
    CHECK(package.readText("shaders/generated/pbr_mesh.vert.metal", shader));

    std::string extract_dir = "/tmp/genesis_package_test";
    CHECK(package.extractAll(extract_dir, &error));
    CHECK(ResourceIO::fileExists(ResourceIO::joinPath(extract_dir, "scripts/cascade.js")));
}

TEST(package, rejects_non_packages)
{
    AssetPackage package;
    std::string error;
    CHECK(!package.loadFromFile(gpu_test::assetPath("textures/checker_albedo.png"), &error));
    CHECK(!error.empty());
}

//======================================================================
// Physics
//======================================================================
TEST(physics, free_fall_and_gravity)
{
    ECS::World world;
    Physics::PhysicsSystem physics;
    CHECK(physics.initialize(&world));
    physics.settings().sleeping = false;
    ECS::Entity body = addBody(world, "falling", Math::Vec3(0, 10, 0), ECS::BodyType::Dynamic, nullptr);
    simulate(physics, world, 1.0f);
    const ECS::TransformComponent *t = world.getComponent<ECS::TransformComponent>(body);
    const ECS::RigidBodyComponent *rb = world.getComponent<ECS::RigidBodyComponent>(body);
    CHECK(t != nullptr);
    CHECK(t->position.y < 6.0f); // 10 - 0.5 g t^2 ~ 5.1
    CHECK(rb != nullptr && rb->linear_velocity.y < -8.0f);
}

TEST(physics, sphere_rests_on_plane)
{
    ECS::World world;
    Physics::PhysicsSystem physics;
    CHECK(physics.initialize(&world));
    physics.settings().sleeping = false;
    ECS::ColliderComponent plane;
    plane.shape = ECS::ColliderShape::Plane;
    ECS::Entity ground = addBody(world, "ground", Math::Vec3(0, 0, 0), ECS::BodyType::Static, &plane);
    (void)ground;
    ECS::ColliderComponent sphere;
    sphere.shape = ECS::ColliderShape::Sphere;
    sphere.radius = 0.5f;
    ECS::Entity ball = addBody(world, "ball", Math::Vec3(0, 2.0f, 0), ECS::BodyType::Dynamic, &sphere);
    simulate(physics, world, 2.0f);
    const ECS::TransformComponent *t = world.getComponent<ECS::TransformComponent>(ball);
    CHECK(t != nullptr);
    CHECK_NEAR(t->position.y, 0.5, 0.08);
    CHECK(std::isfinite(t->position.x) && std::isfinite(t->position.z));
    Physics::RayHit hit;
    CHECK(physics.rayCast(world, Math::Vec3(0, 5, 0), Math::Vec3(0, -1, 0), 10.0f, hit));
    CHECK(hit.entity == ball);
}

TEST(physics, box_stack_settles)
{
    ECS::World world;
    Physics::PhysicsSystem physics;
    CHECK(physics.initialize(&world));
    physics.settings().sleeping = false;
    ECS::ColliderComponent plane;
    plane.shape = ECS::ColliderShape::Plane;
    addBody(world, "ground", Math::Vec3(0, 0, 0), ECS::BodyType::Static, &plane);
    ECS::ColliderComponent box;
    box.shape = ECS::ColliderShape::Box;
    box.half_extents = Math::Vec3(0.5f);
    ECS::Entity bottom = addBody(world, "bottom", Math::Vec3(0, 0.52f, 0), ECS::BodyType::Dynamic, &box);
    ECS::Entity middle = addBody(world, "middle", Math::Vec3(0, 1.54f, 0), ECS::BodyType::Dynamic, &box);
    ECS::Entity top = addBody(world, "top", Math::Vec3(0, 2.56f, 0), ECS::BodyType::Dynamic, &box);
    simulate(physics, world, 2.5f);
    const ECS::TransformComponent *tb = world.getComponent<ECS::TransformComponent>(bottom);
    const ECS::TransformComponent *tt = world.getComponent<ECS::TransformComponent>(top);
    CHECK(tb != nullptr && tt != nullptr);
    CHECK_NEAR(tb->position.y, 0.5, 0.12);
    CHECK(tt->position.y > 2.2f);
    CHECK(physics.contactCount() > 0);
    (void)middle;
}

TEST(physics, hinge_keeps_anchor_distance)
{
    ECS::World world;
    Physics::PhysicsSystem physics;
    CHECK(physics.initialize(&world));
    physics.settings().gravity = Math::Vec3(0.0f);
    physics.settings().sleeping = false;
    ECS::ColliderComponent sphere;
    sphere.shape = ECS::ColliderShape::Sphere;
    sphere.radius = 0.2f;
    ECS::Entity body = addBody(world, "pendulum", Math::Vec3(1, 0, 0), ECS::BodyType::Dynamic, &sphere);
    ECS::HingeJointComponent hinge;
    hinge.anchor = Math::Vec3(0, 0, 0);
    hinge.axis = Math::Vec3(0, 0, 1);
    world.addComponent<ECS::HingeJointComponent>(body, hinge);
    if (ECS::RigidBodyComponent *rb = world.getComponent<ECS::RigidBodyComponent>(body))
        rb->linear_velocity = Math::Vec3(0, 4, 0);
    simulate(physics, world, 1.0f);
    const ECS::TransformComponent *t = world.getComponent<ECS::TransformComponent>(body);
    CHECK(t != nullptr);
    const float distance = Math::length(t->position);
    CHECK(distance > 0.85f && distance < 1.15f);
}

//======================================================================
// Gaussian splats
//======================================================================
TEST(gaussian, loads_3dgs_ply)
{
    Gaussian::GaussianSplatCloud cloud;
    std::string error;
    CHECK(cloud.loadPly(gpu_test::assetPath("splats/genesis_gaussians.ply"), &error));
    CHECK(cloud.count() >= 1000);
    CHECK(cloud.stats().has_sh);
    CHECK(cloud.stats().binary);
    CHECK(cloud.bounds().valid);
    CHECK(cloud.bounds().maxPoint.y > cloud.bounds().minPoint.y);
}

TEST(gaussian, rejects_missing_file)
{
    Gaussian::GaussianSplatCloud cloud;
    std::string error;
    CHECK(!cloud.loadPly(gpu_test::assetPath("splats/does_not_exist.ply"), &error));
    CHECK(!error.empty());
}

//======================================================================
// GPU: JavaScript through RTTI
//======================================================================
TEST_GPU(js, rtti_scene_construction)
{
    gpu_test::TestEngine te;
    if (!te.init(128, 128, false, true))
    {
        CHECK(false);
        return;
    }
    if (!te.engine.hasJS())
    {
        // V8 is optional; nothing to verify in this build.
        return;
    }
    ECS::World &world = te.engine.world();
    ECS::Entity host = world.createEntity("JSHost");
    world.addComponent<ECS::TransformComponent>(host);
    ECS::ScriptComponent script;
    script.language = ECS::ScriptLanguage::JavaScript;
    script.inline_code =
        "var started = false;\n"
        "function start(id) {\n"
        "  var m = renderer.createMaterial('js_test_material');\n"
        "  m.base_color = color(0.2, 0.8, 0.4, 1.0);\n"
        "  m.metallic = 0.5;\n"
        "  m.roughness = 0.25;\n"
        "  var e = world.create('JSSphere');\n"
        "  e.setMesh(renderer.createMesh('sphere', 1.0, 16), m);\n"
        "  e.Transform.position = vec3(0.0, 1.0, 0.0);\n"
        "  var t = e.add('Text');\n"
        "  t.text = 'from V8';\n"
        "  t.offset = vec3(0, 0.8, 0);\n"
        "  var rt = RenderTexture.new('js_rt', 32, 32, true, true);\n"
        "  var cb = CommandBuffer.new('js_overlay');\n"
        "  cb.drawRect(4, 4, 40, 20, color(1, 0, 0, 0.5));\n"
        "  renderer.addCommandBuffer('overlay', cb);\n"
        "  var info = reflect.describe('Material');\n"
        "  genesis.cmd_fill2d(2, 2, 8, 8, 0, 1, 0, 0.5);\n"
        "  var mats = genesis.material_count();\n"
        "  settings.exposure = 1.4;\n"
        "  started = info && info.properties.length > 0;\n"
        "}\n"
        "function tick(dt, id) {\n"
        "  var e = world.find('JSSphere');\n"
        "  if (e) e.Transform.position = vec3(0.0, 1.0 + Math.sin(elapsed()), 0.0);\n"
        "}\n";
    world.addComponent<ECS::ScriptComponent>(host, script);

    for (int i = 0; i < 3; ++i) te.engine.tick();
    CHECK(te.engine.js().errorCount() == 0);
    CHECK(te.engine.js().loadedScripts() == 1);
    CHECK(world.findEntity("JSSphere").valid());
    CHECK(te.engine.render().scene().findMaterial("js_test_material") != nullptr);
    CHECK_NEAR(te.engine.render().settings().exposure, 1.4, 1e-4);

    // Nothing crashed while executing the RTTI tick.
    CHECK(te.engine.js().lastTickMs() >= 0.0);
}

//======================================================================
// CPU: in-process MediaPlayer (libavformat/libavcodec + SDL audio)
//======================================================================
TEST(codec, media_player_decodes_video_and_audio)
{
    API::EncoderDecoderAPI codec;
    CHECK(codec.initialize());
    if (!codec.available()) return; // no FFmpeg build
    auto player = codec.createPlayer();
    CHECK(player != nullptr);
    if (!player) return;
    std::string error;
    // enable_audio=false keeps the test headless; the audio decoder still runs
    // and its PCM is queued for inspection.
    CHECK_MSG(player->open(gpu_test::assetPath("media/sample.mp4"), false, &error), error);
    CHECK(player->isOpen());
    const Media::MediaInfo &info = player->info();
    CHECK(info.has_video);
    CHECK(info.has_audio);
    CHECK_EQ(info.width, 320);
    CHECK_EQ(info.height, 180);
    CHECK(info.video_codec == "h264");
    CHECK(info.audio_codec == "aac");
    CHECK(info.duration > 2.0);

    player->play();
    uint64_t serial = player->videoSerial();
    for (int i = 0; i < 30; ++i) player->update(1.0f / 30.0f);
    CHECK(player->videoPixels() != nullptr);
    CHECK(player->videoSerial() != serial);
    CHECK(player->videoWidth() == 320 && player->videoHeight() == 180);
    CHECK(player->queuedAudioSamples() > 1000);

    CHECK(player->seek(1.0));
    for (int i = 0; i < 5; ++i) player->update(1.0f / 30.0f);
    CHECK(player->position() >= 1.0);

    std::vector<std::vector<uint8_t>> frames;
    int width = 0, height = 0;
    CHECK(player->decodeAllVideo(frames, width, height));
    CHECK(!frames.empty());
    CHECK(width == 320 && height == 180);
    player->close();
}

//======================================================================
// GPU: Compute API (storage buffers, dispatch, readback)
//======================================================================
TEST_GPU(compute, vertex_animation_kernel)
{
    gpu_test::TestEngine te;
    if (!te.init(64, 64, false, false)) { CHECK(false); return; }
    if (!te.engine.compute().supportsHardwareCompute()) return; // e.g. macOS GL 4.1

    API::ComputeAPI &compute = te.engine.compute();
    float input[22] = {0.0f};
    input[5] = 1.0f; // normal z
    float params[8] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f}; // time, amp, freq, speed, count

    API::ComputeAPI::Buffer base = compute.createBuffer(sizeof(input),
                                                        (uint32_t)RHI::BufferUsageFlagBits::Vertex,
                                                        RHI::MemoryUsage::CpuToGpu, input, sizeof(input));
    API::ComputeAPI::Buffer out = compute.createBuffer(sizeof(input),
                                                       (uint32_t)RHI::BufferUsageFlagBits::Vertex |
                                                           (uint32_t)RHI::BufferUsageFlagBits::TransferSrc,
                                                       RHI::MemoryUsage::GpuToCpu);
    API::ComputeAPI::Buffer uniforms = compute.createBuffer(sizeof(params),
                                                            (uint32_t)RHI::BufferUsageFlagBits::Uniform,
                                                            RHI::MemoryUsage::CpuToGpu, params, sizeof(params));
    CHECK(base.valid() && out.valid() && uniforms.valid());

    API::ComputeAPI::KernelDesc desc;
    desc.shader = "anim/vertex_anim.comp";
    desc.workgroup_x = 64;
    desc.bindings = {{0, "BaseVertexBuffer", true}, {1, "AnimatedVertexBuffer", false}, {2, "AnimParams", false}};
    std::string error;
    auto kernel = compute.createKernel(desc, &error);
    CHECK_MSG(kernel != nullptr, error);
    if (!kernel) return;
    std::vector<API::ComputeAPI::Buffer *> buffers = {&base, &out, &uniforms};
    CHECK_MSG(compute.bind(*kernel, buffers, &error), error);
    CHECK(compute.execute(*kernel, 1, 1, 1, &out, RHI::BufferAccess::HostRead));

    float result[22] = {0.0f};
    CHECK(compute.read(out, result, sizeof(result)));
    // wave = sin(0*1 + 1) * cos(0*0.7 + 0.6) = sin(1)*cos(0.6)
    CHECK_NEAR(result[1], std::sin(1.0f) * std::cos(0.6f), 0.01);
    CHECK(std::fabs(result[0]) > 0.01f); // secondary x ripple

    compute.destroyBuffer(base);
    compute.destroyBuffer(out);
    compute.destroyBuffer(uniforms);
}

//======================================================================
// GPU: animation components (2D sprite + 3D compute vertex animation)
//======================================================================
TEST_GPU(animation, sprite_and_vertex_animation_scene)
{
    gpu_test::TestEngine te;
    if (!te.init(192, 128, false, false)) { CHECK(false); return; }
    te.addCamera(Math::Vec3(0, 3, 7), Math::Vec3(0, 0, 0), 50.0f);
    te.engine.render().settings().show_skybox = false;
    te.engine.render().settings().use_ibl = false;

    MaterialData mat;
    mat.name = "anim_material";
    mat.base_color_factor = Math::Color(0.3f, 0.8f, 1.0f, 1.0f);
    auto model = Procedural::wrapMesh(Procedural::makePlane(6.0f, 6.0f, 24), mat, "anim_plane");
    te.addMesh(model, Math::Vec3(0, 0, 0), 1.0f, "AnimPlane");
    ECS::Entity plane = te.engine.world().findEntity("AnimPlane");
    CHECK(plane.valid());
    if (plane.valid())
    {
        ECS::VertexAnimationComponent anim;
        anim.amplitude = 0.35f;
        anim.frequency = 1.8f;
        anim.speed = 1.4f;
        te.engine.world().addComponent<ECS::VertexAnimationComponent>(plane, anim);
    }

    ECS::Entity sprite = te.engine.world().createEntity("Sprite");
    te.engine.world().addComponent<ECS::TransformComponent>(sprite);
    ECS::SpriteAnimationComponent sa;
    sa.texture = gpu_test::assetPath("textures/sprite_sheet.png");
    sa.columns = 4;
    sa.rows = 2;
    sa.frame_count = 8;
    sa.fps = 16.0f;
    sa.size = 96.0f;
    sa.screen_position = Math::Vec3(20.0f, 20.0f, 0.0f);
    te.engine.world().addComponent<ECS::SpriteAnimationComponent>(sprite, sa);

    for (int i = 0; i < 6; ++i) te.engine.tick();
    CHECK(te.engine.animation().sprites() == 1);
    if (te.engine.compute().supportsHardwareCompute())
    {
        CHECK(te.engine.animation().vertex_animations() >= 1);
        CHECK(te.engine.animation().dispatches() > 0);
    }
    gpu_test::Image image;
    CHECK(te.capture(image));
    CHECK(image.valid());
    CHECK(image.averageLuminance() > 0.01f);
}

//======================================================================
// GPU: Ray Tracing API (compute path on GL/Vulkan/Metal)
//======================================================================
TEST_GPU(raytracing, gpu_trace_to_texture)
{
    gpu_test::TestEngine te;
    if (!te.init(96, 64, false, false)) { CHECK(false); return; }
    API::RayTracingAPI &rt = te.engine.rayTracing();
    CHECK(rt.available());
    std::vector<API::RTSphere> spheres;
    for (int i = 0; i < 6; ++i)
    {
        API::RTSphere s;
        const float a = (float)i * 1.05f;
        s.center = Math::Vec3(std::cos(a) * 2.2f, 0.8f + (float)(i % 2) * 0.5f, std::sin(a) * 2.2f - 1.0f);
        s.radius = 0.55f;
        s.color = Math::Color(0.2f + 0.12f * i, 0.8f - 0.08f * i, 0.35f + 0.1f * i, 1.0f);
        s.metallic = (i % 3 == 0) ? 1.0f : 0.0f;
        spheres.push_back(s);
    }
    rt.setScene(spheres);
    API::RTCamera camera;
    camera.eye = Math::Vec3(0.0f, 2.2f, 6.5f);
    camera.forward = Math::normalize(Math::Vec3(0.0f, 0.1f, 0.0f) - camera.eye);
    camera.right = Math::normalize(Math::cross(camera.forward, Math::Vec3(0, 1, 0)));
    camera.up = Math::cross(camera.right, camera.forward);
    camera.aspect = 96.0f / 64.0f;
    API::RTSettings settings;
    settings.width = 96;
    settings.height = 64;
    settings.samples = 1;
    CHECK(rt.trace(camera, settings));
    CHECK(rt.outputTexture() != nullptr);
    CHECK(rt.pixels().size() == (size_t)96 * 64 * 4);
    size_t non_black = 0;
    for (size_t i = 0; i + 3 < rt.pixels().size(); i += 4)
        if (rt.pixels()[i] + rt.pixels()[i + 1] + rt.pixels()[i + 2] > 30) ++non_black;
    CHECK(non_black > 200);
    if (te.engine.compute().supportsHardwareCompute())
    {
        CHECK(rt.stats().gpu);
        CHECK(rt.backendName().find("CPU") == std::string::npos);
    }
}

//======================================================================
// GPU: four public API facades coexist and the old render path still works
//======================================================================
TEST_GPU(api, four_facades)
{
    gpu_test::TestEngine te;
    if (!te.init(64, 64, false, false)) { CHECK(false); return; }
    CHECK(te.engine.rendering().valid());
    CHECK(std::string(te.engine.rendering().backendName()) == RHI::backendTypeName(gpu_test::g_backend));
    CHECK(te.engine.rendering().renderer() == &te.engine.render());
    CHECK(te.engine.compute().available());
    CHECK(te.engine.rayTracing().available());
    CHECK(!te.engine.encoderDecoder().codecs().empty());
    CHECK(te.engine.encoderDecoder().codecs()[0].name == "raw");

    // Existing rendering still produces a valid frame through the same path.
    te.addCamera(Math::Vec3(0, 0, 4), Math::Vec3(0.0f));
    gpu_test::Image image;
    CHECK(te.capture(image));
    CHECK(image.valid());
}

//======================================================================
// CPU: Encoder & Decoder API (raw / PNG / H.264 if ffmpeg exists)
//======================================================================
TEST(codec, raw_and_png_roundtrip)
{
    API::EncoderDecoderAPI codec;
    CHECK(codec.initialize());
    std::vector<API::FrameImage> frames;
    for (int f = 0; f < 3; ++f)
    {
        API::FrameImage frame;
        frame.width = 32;
        frame.height = 24;
        frame.rgba.resize((size_t)frame.width * frame.height * 4);
        for (uint32_t y = 0; y < frame.height; ++y)
            for (uint32_t x = 0; x < frame.width; ++x)
            {
                const size_t i = ((size_t)y * frame.width + x) * 4;
                frame.rgba[i + 0] = (uint8_t)(x * 8 + f * 20);
                frame.rgba[i + 1] = (uint8_t)(y * 10);
                frame.rgba[i + 2] = (uint8_t)(f * 80);
                frame.rgba[i + 3] = 255;
            }
        frames.push_back(std::move(frame));
    }

    API::CodecOptions raw_options;
    raw_options.codec = API::Codec::Raw;
    CHECK(codec.encodeSequence(frames, "/tmp/genesis_codec_test.gseq", raw_options));
    std::vector<API::FrameImage> decoded;
    CHECK(codec.decodeSequence("/tmp/genesis_codec_test.gseq", decoded));
    CHECK(decoded.size() == frames.size());
    if (decoded.size() == frames.size())
        CHECK(decoded[1].rgba == frames[1].rgba);

    std::vector<uint8_t> png;
    std::string error;
    CHECK(codec.encodeFrame(frames[0], png, API::Codec::PNG, &error));
    API::FrameImage png_frame;
    CHECK(codec.decodeFrame(png.data(), png.size(), png_frame, &error));
    CHECK(png_frame.width == frames[0].width && png_frame.height == frames[0].height);
    CHECK(png_frame.rgba == frames[0].rgba);
}

TEST(codec, h264_hardware_or_software_roundtrip)
{
    API::EncoderDecoderAPI codec;
    CHECK(codec.initialize());
    if (!codec.available()) return; // ffmpeg is optional
    std::vector<API::FrameImage> frames;
    for (int f = 0; f < 5; ++f)
    {
        API::FrameImage frame;
        frame.width = 64;
        frame.height = 48;
        frame.rgba.resize((size_t)frame.width * frame.height * 4);
        for (size_t i = 0; i < frame.rgba.size(); i += 4)
        {
            const size_t p = i / 4;
            frame.rgba[i + 0] = (uint8_t)((p % frame.width) * 3 + f * 10);
            frame.rgba[i + 1] = (uint8_t)((p / frame.width) * 4);
            frame.rgba[i + 2] = (uint8_t)(f * 40);
            frame.rgba[i + 3] = 255;
        }
        frames.push_back(std::move(frame));
    }
    API::CodecOptions options;
    options.codec = API::Codec::H264;
    options.fps = 10;
    options.prefer_hardware = true;
    std::string error;
    if (!codec.encodeSequence(frames, "/tmp/genesis_codec_test.mp4", options, &error))
    {
        CHECK_MSG(false, error);
        return;
    }
    std::vector<API::FrameImage> decoded;
    CHECK(codec.decodeSequence("/tmp/genesis_codec_test.mp4", decoded, &error));
    CHECK(decoded.size() >= 4);
    if (!decoded.empty())
    {
        CHECK(decoded[0].width == 64 && decoded[0].height == 48);
        CHECK(!codec.lastBackend().empty());
    }
}

//======================================================================
// GPU: gaussian splats render through the shared batch path
//======================================================================
TEST_GPU(gaussian, splats_render_on_every_backend)
{
    gpu_test::TestEngine te;
    if (!te.init(192, 144, false, false))
    {
        CHECK(false);
        return;
    }
    te.addCamera(Math::Vec3(0, 2.0f, 6.0f), Math::Vec3(0, 1.8f, 0), 45.0f);
    te.engine.render().settings().show_skybox = false;
    te.engine.render().settings().use_ibl = false;
    te.engine.render().settings().ambient_intensity = 0.0f;
    te.engine.tick(); // sync the camera

    Gaussian::GaussianSplatCloud cloud;
    std::string error;
    CHECK(cloud.loadPly(gpu_test::assetPath("splats/genesis_gaussians.ply"), &error));
    if (!cloud.loaded()) return;

    Gaussian::GaussianSplatRenderer splats;
    Gaussian::SplatSettings settings;
    settings.size_scale = 1.0f;
    settings.opacity_scale = 0.6f;
    settings.max_splats = 4000;
    auto cb = std::make_shared<Render::CommandBuffer>("test_splats");
    splats.record(te.engine.render(), *cb, cloud, settings);
    CHECK(cb->size() > 1000);
    te.engine.render().addCommandBuffer(Render::CameraEvent::Overlay, cb);

    gpu_test::Image image;
    CHECK(te.capture(image));
    CHECK(image.valid());
    CHECK(image.countAbove(0.03f) > (size_t)image.width * image.height / 20);
    CHECK(image.averageLuminance() > 0.005f);
}

//======================================================================
// GPU: package -> JSON scene -> JS + physics, end to end
//======================================================================
TEST_GPU(package, cascade_scene_runs_from_package)
{
    gpu_test::TestEngine te;
    if (!te.init(160, 120, false, true))
    {
        CHECK(false);
        return;
    }
    if (!te.engine.hasJS()) return;
    std::string error;
    AssetPackagePtr package = te.engine.loadAssetPackage(gpu_test::assetPath("packages/cascade.gpkg"), &error);
    CHECK(package != nullptr);
    CHECK(te.engine.activePackage() != nullptr);
    const std::string root = te.engine.packageRoot();
    Scene::LoadResult result =
        Scene::loadFromFile(te.engine, ResourceIO::joinPath(root, "scenes/cascade.scene.json"), root);
    CHECK(result.ok);
    CHECK(result.entities >= 15);
    CHECK(result.materials >= 4);
    CHECK(result.scripts >= 1);
    CHECK(result.physics_bodies >= 10);

    for (int i = 0; i < 4; ++i) te.engine.tick();
    CHECK(te.engine.hasJS());
    CHECK(te.engine.js().loadedScripts() >= 1);
    CHECK(te.engine.js().errorCount() == 0);
    CHECK(te.engine.world().findEntity("Target_1").valid());
    CHECK(te.engine.physics().bodyCount() >= 10);

    // Drive the game's own JS entry point and verify the created ball is
    // simulated by the physics system.
    ECS::Entity game_script = te.engine.world().findEntity("GameScript");
    ECS::Entity target_entity = te.engine.world().findEntity("Target_1");
    CHECK(game_script.valid());
    if (game_script.valid())
    {
        CHECK(te.engine.js().executeInScript(game_script, "launch();", "=test"));
        te.engine.tick();
        ECS::Entity ball = ECS::nullEntity();
        for (ECS::Entity e : te.engine.world().entities())
            if (te.engine.world().entityName(e).rfind("Ball_", 0) == 0) ball = e;
        CHECK(ball.valid());
        if (ball.valid())
        {
            const ECS::TransformComponent *before = te.engine.world().getComponent<ECS::TransformComponent>(ball);
            const float start_z = before ? before->position.z : 0.0f;
            Math::Vec3 target_before(0.0f);
            if (const ECS::TransformComponent *t = te.engine.world().getComponent<ECS::TransformComponent>(target_entity))
                target_before = t->position;
            for (int i = 0; i < 120; ++i) te.engine.physics().update(te.engine.world(), 1.0f / 60.0f);
            const ECS::TransformComponent *after = te.engine.world().getComponent<ECS::TransformComponent>(ball);
            CHECK(after != nullptr);
            if (after) CHECK(after->position.z < start_z - 2.0f); // launched towards the crates
            const ECS::TransformComponent *target_after =
                te.engine.world().getComponent<ECS::TransformComponent>(target_entity);
            CHECK(target_after != nullptr);
            if (target_after)
                CHECK(Math::length(target_after->position - target_before) > 0.05f); // the ball hit the stack
        }
        CHECK(te.engine.js().errorCount() == 0);
    }
}
