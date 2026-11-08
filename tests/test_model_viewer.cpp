#include "test_framework.hpp"
#include "test_gpu_utils.hpp"
#include "../demo/model_viewer.hpp"
#include "ecs/systems.hpp"
#include "scene/scene_loader.hpp"
#include "external/json.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>

using namespace Genesis;
using namespace gpu_test;

namespace
{
    // Generated glTF fixture isolates topology and validation, without relying
    // on a user-specific model or committing another binary asset.
    struct GltfFixture
    {
        std::filesystem::path dir;
        nlohmann::json doc;
        GltfFixture()
        {
            dir = std::filesystem::temp_directory_path() / ("genesis_gltf_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(dir);
            float positions[] = {0,0,0, 1,0,0, 0,1,0, 1,1,0};
            uint16_t indices[] = {0,1,2,3};
            std::vector<uint8_t> bytes(sizeof(positions) + sizeof(indices));
            std::memcpy(bytes.data(), positions, sizeof(positions));
            std::memcpy(bytes.data() + sizeof(positions), indices, sizeof(indices));
            ResourceIO::writeFile((dir / "data.bin").string(), bytes.data(), bytes.size());
            doc = nlohmann::json::parse(R"({
                "asset":{"version":"2.0"},
                "buffers":[{"uri":"data.bin","byteLength":56}],
                "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":8}],
                "accessors":[{"bufferView":0,"componentType":5126,"type":"VEC3","count":4},
                             {"bufferView":1,"componentType":5123,"type":"SCALAR","count":4}],
                "materials":[{"alphaMode":"MASK","alphaCutoff":0.0}],
                "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0,"mode":5}]}],
                "nodes":[{"mesh":0,"translation":[2,3,4]}],"scenes":[{"nodes":[0]}],"scene":0
            })");
        }
        ~GltfFixture() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
        std::string write()
        {
            auto path = (dir / "mesh.gltf").string();
            auto json = doc.dump();
            ResourceIO::writeFile(path, json.data(), json.size());
            return path;
        }
    };
}

TEST(gltf, strip_fan_winding_validation_and_zero_cutoff)
{
    GltfFixture f;
    auto model = ResourceIO::loadModel(f.write());
    CHECK(model != nullptr);
    if (!model) return;
    CHECK(model->meshes[0].indices == std::vector<uint32_t>({0,1,2,2,1,3}));
    CHECK_NEAR(model->materials[0].alpha_cutoff, 0.0, 1e-6);
    for (const auto &v : model->meshes[0].vertices) CHECK_NEAR(v.normal[2], 1.0, 1e-6);
    CHECK_NEAR(model->bounds.minPoint.x, 2.0, 1e-6);
    f.doc["meshes"][0]["primitives"][0]["mode"] = 6;
    model = ResourceIO::loadModel(f.write());
    CHECK(model != nullptr);
    if (model) CHECK(model->meshes[0].indices == std::vector<uint32_t>({0,1,2,0,2,3}));
    f.doc["accessors"][0]["count"] = 1000;
    CHECK(ResourceIO::loadModel(f.write()) == nullptr);
}

TEST(resource, computed_normals_are_unbiased_and_scale_independent)
{
    for (float scale : {0.001f, 1.0f, 1000.0f})
    {
        MeshData mesh;
        mesh.vertices.resize(3);
        mesh.vertices[1].position[1] = scale;
        mesh.vertices[2].position[2] = scale;
        mesh.indices = {0,1,2};
        mesh.computeNormals();
        for (const auto &v : mesh.vertices)
        {
            CHECK_NEAR(v.normal[0], 1.0, 1e-5);
            CHECK_NEAR(v.normal[1], 0.0, 1e-5);
            CHECK_NEAR(v.normal[2], 0.0, 1e-5);
        }
    }
}

TEST(input, keyboard_capture_blocks_fly_and_drop_payload_is_copied)
{
    Platform::InputSystem input;
    ECS::World world;
    auto e = world.createEntity("Camera");
    world.addComponent<ECS::TransformComponent>(e);
    auto &ctrl = world.addComponent<ECS::CameraControllerComponent>(e);
    ctrl.mode = ECS::CameraControllerComponent::Mode::Fly;
    ECS::CameraControllerSystem system(&input);
    SDL_Event event{};
    event.type = SDL_KEYDOWN;
    event.key.keysym.scancode = SDL_SCANCODE_W;
    input.processEvent(event);
    input.ui_wants_keyboard = true;
    input.ui_wants_mouse = false;
    system.update(world, 1.0f);
    CHECK_NEAR(world.getComponent<ECS::TransformComponent>(e)->position.z, 0.0, 1e-6);
    input.ui_wants_keyboard = false;
    system.update(world, 1.0f);
    CHECK(world.getComponent<ECS::TransformComponent>(e)->position.z < 0.0f);
    event.type = SDL_DROPFILE;
    event.drop.file = SDL_strdup("/tmp/model with spaces.GLB");
    input.processEvent(event);
    SDL_free(event.drop.file);
    CHECK(input.droppedFiles().size() == 1);
    CHECK(input.droppedFiles()[0] == "/tmp/model with spaces.GLB");
    input.beginFrame();
    CHECK(input.droppedFiles().empty());
}

TEST(model_viewer, fit_uses_transformed_model_bounds)
{
    Math::AABB bounds;
    bounds.expand(Math::Vec3(20, -3, 5));
    bounds.expand(Math::Vec3(28, 1, 7));
    const auto t = Demo::ModelViewer::fittedTransform(bounds);
    const auto fit = bounds.transformed(Math::Mat4::compose(t.position, t.rotation, t.scale));
    CHECK_NEAR(fit.minPoint.y, 0.0, 1e-5);
    CHECK_NEAR(fit.center().x, 0.0, 1e-5);
    CHECK_NEAR(fit.center().z, 0.0, 1e-5);
    CHECK_NEAR(fit.size().x, 2.8, 1e-5);
    CHECK(Demo::ModelViewer::accepts("new car.GLB"));
    CHECK(!Demo::ModelViewer::accepts("image.png"));
}

TEST_GPU(model_viewer, replace_preserves_references_failure_and_cached_transforms)
{
    if (!g_available) return;
    TestEngine te;
    CHECK(te.init(320, 240));
    if (!te.ok) return;
    auto &engine = te.engine;
    auto loaded = Scene::loadFromFile(engine, assetPath("scenes/result_pbr.scene.json"), GENESIS_ASSET_DIR);
    CHECK(loaded.ok);
    const char *names[] = {"RefSphereChrome", "RefSphereGold", "RefSphereRed", "RefSphereCeramic"};
    std::vector<ECS::Entity> refs;
    for (auto name : names) { refs.push_back(engine.world().findEntity(name)); CHECK(refs.back().valid()); }
    const size_t base_entities = engine.world().entityCount();
    Demo::ModelViewer viewer;
    std::string error;
    CHECK(viewer.load(engine, assetPath("models/result.glb"), error));
    CHECK(engine.world().entityCount() == base_entities + 1);
    auto original = viewer.target();
    CHECK(!viewer.load(engine, "/no/such/model.glb", error));
    CHECK(viewer.target() == original && engine.world().isAlive(original));
    CHECK(!error.empty());
    GltfFixture bad;
    bad.doc["accessors"][0]["count"] = 1000;
    CHECK(!viewer.load(engine, bad.write(), error));
    CHECK(viewer.target() == original);
    auto *transform = engine.world().getComponent<ECS::TransformComponent>(original);
    transform->setPosition(Math::Vec3(0.5f, 0.8f, 0.1f));
    transform->setEulerDegrees(15, 125, 20);
    transform->setScale(Math::Vec3(2.0f));
    CHECK(viewer.load(engine, assetPath("models/result (1).glb"), error));
    CHECK(!engine.world().isAlive(original));
    CHECK(engine.world().entityCount() == base_entities + 1);
    Image warmup;
    CHECK(te.capture(warmup)); // initialize lazy IBL/default textures before counting
    const auto meshes = engine.render().scene().meshCount();
    const auto materials = engine.render().scene().materialCount();
    const auto textures = engine.render().scene().textures().count();
    for (int i = 0; i < 6; ++i)
    {
        CHECK(viewer.load(engine, assetPath(i % 2 ? "models/result (1).glb" : "models/result.glb"), error));
        Image image;
        CHECK(te.capture(image));
        CHECK(image.countAbove(0.1f) > 100);
    }
    CHECK(engine.render().scene().meshCount() == meshes);
    CHECK(engine.render().scene().materialCount() == materials);
    CHECK(engine.render().scene().textures().count() == textures);
    CHECK(viewer.load(engine, assetPath("models/result.glb"), error));
    transform = engine.world().getComponent<ECS::TransformComponent>(viewer.target());
    CHECK_NEAR(transform->position.x, 0.5, 1e-6);
    CHECK_NEAR(transform->scale.x, 2.0, 1e-6);
    CHECK_NEAR(transform->eulerAnglesDegrees().y, 125.0, 1e-4);
    viewer.resetTransform(engine);
    viewer.focus(engine, engine.world().findEntity("MainCamera"));
    for (auto ref : refs) CHECK(engine.world().isAlive(ref));
    if (const char *path = std::getenv("GENESIS_TEST_GLB"))
    {
        CHECK(viewer.load(engine, path, error));
        CHECK(engine.world().entityCount() == base_entities + 1);
        Image image;
        CHECK(te.capture(image));
        CHECK(image.countAbove(0.1f) > 100);
    }
    viewer.detach(engine);
    CHECK(engine.world().entityCount() == base_entities);
}

TEST_GPU(texture_cache, separates_color_space_and_retains_embedded_sources)
{
    if (!g_available) return;
    TestEngine te;
    CHECK(te.init(64, 64));
    if (!te.ok) return;
    auto &cache = te.engine.render().scene().textures();
    auto image = std::make_shared<ImageData>();
    image->width = image->height = 1;
    image->channels = 4;
    image->pixels = {128,128,128,255};
    std::weak_ptr<ImageData> weak = image;
    TextureRef ref;
    ref.valid = true;
    ref.embedded = image;
    auto color = cache.acquire(ref, true);
    auto data = cache.acquire(ref, false);
    CHECK(color.texture != data.texture);
    CHECK(color.srgb && !data.srgb);
    CHECK(cache.acquire(ref, true).texture == color.texture);
    CHECK(cache.acquire(ref, false).texture == data.texture);
    ref.embedded.reset();
    image.reset();
    CHECK(!weak.expired());
    auto file_color = cache.acquirePath(assetPath("textures/checker_albedo.png"), true);
    auto file_data = cache.acquirePath(assetPath("textures/checker_albedo.png"), false);
    CHECK(file_color.texture != file_data.texture);
}
