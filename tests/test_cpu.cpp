/*
 * Genesis Render - CPU side test suite
 *
 * Covers: math, ECS, procedural meshes, glTF/GLB import, FBX import, image IO,
 * the shader registry and the Lua scripting layer.  No GPU context needed.
 */
#include "test_framework.hpp"

#include "ecs/components.hpp"
#include "ecs/systems.hpp"
#include "foundation_layer/math/math.hpp"
#include "resource/procedural_mesh.hpp"
#include "resource/resource_types.hpp"
#include "script/script_system.hpp"

#include "genesis_shaders_generated.h"

#include <cmath>
#include <string>
#include <vector>

using namespace Genesis;

#ifndef GENESIS_ASSET_DIR
#define GENESIS_ASSET_DIR "assets"
#endif

namespace
{
    std::string assetPath(const std::string &relative)
    {
        return ResourceIO::joinPath(GENESIS_ASSET_DIR, relative);
    }
} // namespace

//======================================================================
// Math
//======================================================================
TEST(math, vec3_basics)
{
    Math::Vec3 a(1, 2, 3), b(4, 5, 6);
    Math::Vec3 c = a + b;
    CHECK_NEAR(c.x, 5.0, 1e-6);
    CHECK_NEAR(c.y, 7.0, 1e-6);
    CHECK_NEAR(c.z, 9.0, 1e-6);
    CHECK_NEAR(Math::dot(a, b), 32.0, 1e-6);
    Math::Vec3 cr = Math::cross(Math::Vec3(1, 0, 0), Math::Vec3(0, 1, 0));
    CHECK_NEAR(cr.z, 1.0, 1e-6);
    CHECK_NEAR(Math::length(Math::Vec3(3, 4, 0)), 5.0, 1e-6);
    CHECK_NEAR(Math::length(Math::normalize(Math::Vec3(0, 5, 0))), 1.0, 1e-6);
}

TEST(math, mat4_identity_and_multiply)
{
    Math::Mat4 id = Math::Mat4::identity();
    CHECK_NEAR(id.m[0], 1.0, 1e-6);
    CHECK_NEAR(id.m[5], 1.0, 1e-6);
    CHECK_NEAR(id.m[10], 1.0, 1e-6);
    CHECK_NEAR(id.m[15], 1.0, 1e-6);
    CHECK_NEAR(id.m[1], 0.0, 1e-6);

    Math::Mat4 t = Math::Mat4::translate(Math::Vec3(2, 3, 4));
    Math::Vec3 p = t.transformPoint(Math::Vec3(1, 1, 1));
    CHECK_NEAR(p.x, 3.0, 1e-6);
    CHECK_NEAR(p.y, 4.0, 1e-6);
    CHECK_NEAR(p.z, 5.0, 1e-6);

    Math::Mat4 product = t * t;
    CHECK_NEAR(product.m[12], 4.0, 1e-6);
}

TEST(math, mat4_inverse)
{
    Math::Mat4 m = Math::Mat4::compose(Math::Vec3(1, -2, 3),
                                       Math::Quat::fromAxisAngle(Math::Vec3(0.3f, 0.8f, -0.5f), 0.7f),
                                       Math::Vec3(2, 0.5f, 1.5f));
    Math::Mat4 inv = m.inversed();
    Math::Mat4 should_be_id = m * inv;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            CHECK_NEAR(should_be_id.at(i, j), i == j ? 1.0 : 0.0, 1e-4);
}

TEST(math, quat_rotation)
{
    Math::Quat q = Math::Quat::fromAxisAngle(Math::Vec3(0, 1, 0), Math::HALF_PI);
    Math::Vec3 v = q.rotate(Math::Vec3(0, 0, -1));
    // Rotating -Z by +90 degrees about +Y gives -X.
    CHECK_NEAR(v.x, -1.0, 1e-5);
    CHECK_NEAR(v.y, 0.0, 1e-5);
    CHECK_NEAR(v.z, 0.0, 1e-5);

    Math::Quat s = Math::Quat::slerp(Math::Quat::identity(), q, 0.5f);
    Math::Vec3 half = s.rotate(Math::Vec3(0, 0, -1));
    CHECK_NEAR(half.x, -std::sin(Math::PI * 0.25f), 1e-5);
}

TEST(math, look_rotation_and_euler_roundtrip)
{
    Math::Quat q = Math::Quat::lookRotation(Math::Vec3(0, 0, -1), Math::Vec3(0, 1, 0));
    Math::Mat4 m = Math::Mat4::fromQuat(q);
    CHECK_NEAR(m.m[0], 1.0, 1e-5);
    CHECK_NEAR(m.m[5], 1.0, 1e-5);
    CHECK_NEAR(m.m[10], 1.0, 1e-5);

    ECS::TransformComponent t;
    t.setEulerDegrees(20.0f, 35.0f, -10.0f);
    Math::Vec3 euler = t.eulerAnglesDegrees();
    CHECK_NEAR(euler.x, 20.0, 1e-3);
    CHECK_NEAR(euler.y, 35.0, 1e-3);
    CHECK_NEAR(euler.z, -10.0, 1e-3);
}

TEST(math, projection_and_frustum)
{
    const float near_plane = 1.0f, far_plane = 100.0f;
    Math::Mat4 persp = Math::Mat4::perspective(Math::toRadians(60.0f), 16.0f / 9.0f, near_plane, far_plane, false);
    // Defining property: the near plane maps to NDC z = -1 and the far plane to
    // +1 with the OpenGL convention.
    Math::Vec4 at_near = persp * Math::Vec4(0, 0, -near_plane, 1);
    Math::Vec4 at_far = persp * Math::Vec4(0, 0, -far_plane, 1);
    CHECK_NEAR(at_near.z / at_near.w, -1.0, 1e-4);
    CHECK_NEAR(at_far.z / at_far.w, 1.0, 1e-4);
    Math::Vec4 mid = persp * Math::Vec4(0, 0, -10.0f, 1);
    CHECK_MSG(mid.z / mid.w > -1.0f && mid.z / mid.w < 1.0f, "interior points stay inside [-1,1]");

    Math::Mat4 persp_vk = Math::Mat4::perspective(Math::toRadians(60.0f), 16.0f / 9.0f, near_plane, far_plane, true);
    Math::Vec4 vk_near = persp_vk * Math::Vec4(0, 0, -near_plane, 1);
    Math::Vec4 vk_far = persp_vk * Math::Vec4(0, 0, -far_plane, 1);
    CHECK_NEAR(vk_near.z / vk_near.w, 0.0, 1e-4);
    CHECK_NEAR(vk_far.z / vk_far.w, 1.0, 1e-4);

    Math::Mat4 view = Math::Mat4::lookAt(Math::Vec3(0, 0, 5), Math::Vec3(0, 0, 0), Math::Vec3(0, 1, 0));
    Math::Frustum f = Math::Frustum::fromViewProjection(persp * view);
    CHECK(f.intersectsSphere(Math::Vec3(0, 0, 0), 1.0f));
    CHECK(!f.intersectsSphere(Math::Vec3(0, 0, 50), 1.0f));
    CHECK(f.intersectsAABB(Math::AABB{Math::Vec3(-1, -1, -1), Math::Vec3(1, 1, 1), true}));
    CHECK(!f.intersectsAABB(Math::AABB{Math::Vec3(100, 100, 100), Math::Vec3(101, 101, 101), true}));
}

TEST(math, ray_intersection)
{
    Math::Ray ray{Math::Vec3(0, 0, 5), Math::Vec3(0, 0, -1)};
    float t = 0.0f;
    CHECK(ray.intersectSphere(Math::Vec3(0, 0, 0), 1.0f, t));
    CHECK_NEAR(t, 4.0, 1e-5);
    CHECK(ray.intersectAABB(Math::AABB{Math::Vec3(-1, -1, -1), Math::Vec3(1, 1, 1), true}, t));
    CHECK_NEAR(t, 4.0, 1e-5);
    Math::Ray miss{Math::Vec3(10, 10, 5), Math::Vec3(0, 0, -1)};
    CHECK(!miss.intersectSphere(Math::Vec3(0, 0, 0), 1.0f, t));
}

//======================================================================
// ECS
//======================================================================
TEST(ecs, entity_lifecycle_and_generation)
{
    ECS::World world("test");
    ECS::Entity a = world.createEntity("a");
    ECS::Entity b = world.createEntity("b");
    CHECK(a.valid() && b.valid());
    CHECK(world.isAlive(a));
    CHECK_EQ(world.entityCount(), (size_t)2);
    CHECK(world.entityName(a) == "a");

    world.destroyEntity(a);
    CHECK(!world.isAlive(a));
    CHECK_EQ(world.entityCount(), (size_t)1);

    // Ids are recycled but the generation changes, so stale handles die.
    ECS::Entity c = world.createEntity("c");
    CHECK_EQ(c.id, a.id);
    CHECK(c.generation != a.generation);
    CHECK(!world.isAlive(a));
    CHECK(world.isAlive(c));
    world.destroyEntity(b);
    world.destroyEntity(c);
    CHECK_EQ(world.entityCount(), (size_t)0);
}

TEST(ecs, components_sparse_set_integrity)
{
    ECS::World world;
    std::vector<ECS::Entity> entities;
    for (int i = 0; i < 64; ++i) entities.push_back(world.createEntity("e" + std::to_string(i)));

    for (size_t i = 0; i < entities.size(); ++i)
    {
        ECS::TransformComponent t;
        t.position = Math::Vec3((float)i, 0, 0);
        world.addComponent<ECS::TransformComponent>(entities[i], t);
        if (i % 2 == 0)
        {
            ECS::TagComponent tag;
            tag.tag = "even";
            world.addComponent<ECS::TagComponent>(entities[i], tag);
        }
    }

    auto *pool = world.pool<ECS::TransformComponent>();
    CHECK_EQ(pool->count(), (size_t)64);
    CHECK_EQ(world.pool<ECS::TagComponent>()->count(), (size_t)32);

    // Remove every third entity: the sparse set must stay consistent.
    for (size_t i = 0; i < entities.size(); i += 3) world.destroyEntity(entities[i]);

    size_t alive = 0;
    world.each<ECS::TransformComponent>([&](ECS::Entity e, ECS::TransformComponent &t)
                                        {
                                            CHECK(world.isAlive(e));
                                            CHECK_NEAR(t.position.x, (float)e.id, 1e-6);
                                            ++alive;
                                        });
    CHECK_EQ(alive, (size_t)42); // 64 - 22 removed (i = 0,3,...,63)

    // Components of destroyed entities must be gone from every pool.
    for (size_t i = 0; i < entities.size(); i += 3)
        CHECK(!world.hasComponent<ECS::TransformComponent>(entities[i]));

    size_t tagged = 0;
    world.each<ECS::TagComponent>([&](ECS::Entity, ECS::TagComponent &) { ++tagged; });
    CHECK_EQ(tagged, (size_t)21); // evens that survived: 22 even ids minus those removed
}

TEST(ecs, view_intersection_and_hierarchy)
{
    ECS::World world;
    ECS::Entity parent = world.createEntity("parent");
    ECS::TransformComponent pt;
    pt.position = Math::Vec3(10, 0, 0);
    world.addComponent<ECS::TransformComponent>(parent, pt);

    ECS::Entity child = world.createEntity("child");
    ECS::TransformComponent ct;
    ct.position = Math::Vec3(1, 2, 3);
    world.addComponent<ECS::TransformComponent>(child, ct);
    ECS::MeshComponent mesh;
    world.addComponent<ECS::MeshComponent>(child, mesh);

    world.setParent(child, parent);
    CHECK(world.parentOf(child) == parent);
    CHECK_EQ(world.childrenOf(parent).size(), (size_t)1);
    CHECK_EQ(world.depth(child), 1u);
    CHECK_EQ(world.depth(parent), 0u);

    ECS::TransformSystem system;
    system.update(world, 0.016f);

    ECS::TransformComponent *ct2 = world.getComponent<ECS::TransformComponent>(child);
    CHECK_NEAR(ct2->world_matrix.translation().x, 11.0, 1e-5);
    CHECK_NEAR(ct2->world_matrix.translation().y, 2.0, 1e-5);
    CHECK_NEAR(ct2->world_matrix.translation().z, 3.0, 1e-5);

    size_t pairs = 0;
    world.view<ECS::TransformComponent, ECS::MeshComponent>(
        [&](ECS::Entity e, ECS::TransformComponent &, ECS::MeshComponent &)
        {
            CHECK(e == child);
            ++pairs;
        });
    CHECK_EQ(pairs, (size_t)1);

    // Destroying the parent must destroy the children too.
    world.destroyEntity(parent);
    CHECK(!world.isAlive(child));
    CHECK_EQ(world.entityCount(), (size_t)0);
}

TEST(ecs, system_order_and_scheduling)
{
    ECS::World world;
    struct OrderRecorder : ECS::System
    {
        std::vector<std::string> *log;
        const char *id;
        int32_t ord;
        OrderRecorder(std::vector<std::string> *l, const char *i, int o) : log(l), id(i), ord(o) {}
        const char *name() const override { return id; }
        int32_t order() const override { return ord; }
        void update(ECS::World &, float) override { log->push_back(id); }
    };
    std::vector<std::string> order;
    world.scheduler().add(std::make_unique<OrderRecorder>(&order, "third", 300));
    world.scheduler().add(std::make_unique<OrderRecorder>(&order, "first", 100));
    world.scheduler().add(std::make_unique<OrderRecorder>(&order, "second", 200));
    world.update(0.016f);
    CHECK_EQ(order.size(), (size_t)3);
    CHECK(order[0] == "first");
    CHECK(order[1] == "second");
    CHECK(order[2] == "third");

    // Built-in orders: script(50) < camera(90) < transform(100) < render(200)
    Script::ScriptSystem script;
    ECS::CameraControllerSystem camera(nullptr);
    ECS::TransformSystem transform;
    CHECK(script.order() < camera.order());
    CHECK(camera.order() < transform.order());
    CHECK(transform.order() < 200);
}

//======================================================================
// Procedural meshes
//======================================================================
TEST(procedural, plane_box_sphere_torus)
{
    MeshData plane = Procedural::makePlane(4.0f, 6.0f, 1);
    CHECK_EQ(plane.vertices.size(), (size_t)4);
    CHECK_EQ(plane.indices.size(), (size_t)6);
    CHECK_NEAR(plane.bounds.maxPoint.x, 2.0, 1e-5);
    CHECK_NEAR(plane.bounds.maxPoint.z, 3.0, 1e-5);
    for (const auto &v : plane.vertices) CHECK_NEAR(v.normal[1], 1.0, 1e-5);

    MeshData box = Procedural::makeBox(2, 2, 2);
    CHECK_EQ(box.vertices.size(), (size_t)24);
    CHECK_EQ(box.indices.size(), (size_t)36);
    CHECK_NEAR(box.bounds.maxPoint.x, 1.0, 1e-5);
    CHECK_NEAR(box.bounds.minPoint.y, -1.0, 1e-5);
    for (const auto &v : box.vertices)
    {
        Math::Vec3 n(v.normal);
        CHECK_NEAR(Math::length(n), 1.0, 1e-4);
    }

    MeshData sphere = Procedural::makeSphere(1.0f, 16, 8);
    CHECK_EQ(sphere.vertices.size(), (size_t)(17 * 9));
    CHECK_NEAR(sphere.bounds.maxPoint.x, 1.0, 1e-3);
    CHECK_NEAR(sphere.bounds.minPoint.y, -1.0, 1e-3);
    // AABB::radius() is the centre-to-corner distance: sqrt(3) for a unit cube.
    CHECK_NEAR(sphere.bounds.radius(), std::sqrt(3.0), 1e-3);
    // Every sphere normal must be unit length and equal the normalised position.
    for (const auto &v : sphere.vertices)
    {
        Math::Vec3 n(v.normal), p(v.position);
        CHECK_NEAR(Math::length(n), 1.0, 1e-4);
        if (Math::length(p) > 1e-4f)
            CHECK_NEAR(Math::dot(n, Math::normalize(p)), 1.0, 1e-3);
    }

    MeshData torus = Procedural::makeTorus(1.0f, 0.3f, 24, 12);
    CHECK(torus.vertices.size() > 300);
    CHECK_NEAR(torus.bounds.maxPoint.x, 1.3f, 1e-3);
    CHECK_NEAR(torus.bounds.maxPoint.y, 0.3f, 1e-3);
    // Indices must all be in range.
    for (uint32_t i : torus.indices) CHECK_MSG(i < torus.vertices.size(), "torus index in range");
}

TEST(procedural, material_grid)
{
    ModelData grid = Procedural::makeMaterialGrid(4, 3, 1.0f, 0.4f);
    CHECK_EQ(grid.meshes.size(), (size_t)12);
    CHECK_EQ(grid.materials.size(), (size_t)12);
    CHECK_EQ(grid.nodes.size(), (size_t)12);
    CHECK_EQ(grid.root_nodes.size(), (size_t)12);
    CHECK_NEAR(grid.materials[0].metallic_factor, 0.0, 1e-6);
    CHECK_NEAR(grid.materials[3].metallic_factor, 1.0, 1e-6);
    auto placed = grid.flattenNodes();
    CHECK_EQ(placed.size(), (size_t)12);
}

//======================================================================
// glTF / GLB import
//======================================================================
TEST(gltf, sphere_json_and_glb_match)
{
    auto from_json = ResourceIO::loadModel(assetPath("models/genesis_sphere.gltf"));
    auto from_glb = ResourceIO::loadModel(assetPath("models/genesis_sphere.glb"));
    CHECK_MSG(from_json != nullptr, "genesis_sphere.gltf loaded");
    CHECK_MSG(from_glb != nullptr, "genesis_sphere.glb loaded");
    if (!from_json || !from_glb) return;

    CHECK_EQ(from_json->meshes.size(), (size_t)1);
    CHECK_EQ(from_json->meshes[0].vertices.size(), (size_t)1617);
    CHECK_EQ(from_json->meshes[0].indices.size(), (size_t)8928);
    CHECK_NEAR(from_json->bounds.maxPoint.x, 1.0, 1e-3);
    CHECK_NEAR(from_json->bounds.minPoint.y, -1.0, 1e-3);
    CHECK(from_json->importer == "gltf");
    CHECK(from_glb->importer == "glb");
    CHECK_EQ(from_glb->meshes[0].vertices.size(), from_json->meshes[0].vertices.size());
    CHECK_EQ(from_glb->meshes[0].indices.size(), from_json->meshes[0].indices.size());

    // Normals must be unit length after import.
    for (size_t i = 0; i < from_json->meshes[0].vertices.size(); i += 97)
    {
        Math::Vec3 n(from_json->meshes[0].vertices[i].normal);
        CHECK_NEAR(Math::length(n), 1.0, 1e-3);
    }
    // Material factors come from the generator.
    CHECK(!from_json->materials.empty());
    CHECK_NEAR(from_json->materials[0].roughness_factor, 0.35, 1e-4);
    CHECK_NEAR(from_json->materials[0].metallic_factor, 0.0, 1e-4);
}

TEST(gltf, damaged_helmet_full_pbr_material)
{
    auto model = ResourceIO::loadModel(assetPath("models/DamagedHelmet.glb"));
    CHECK_MSG(model != nullptr, "DamagedHelmet.glb loaded");
    if (!model) return;
    CHECK(model->meshes.size() >= 1);
    size_t triangles = 0;
    for (const auto &m : model->meshes) triangles += m.indices.size() / 3;
    CHECK_MSG(triangles > 10000, "helmet has a substantial triangle count");

    // The helmet ships every PBR texture slot; embedded images must decode.
    CHECK(!model->materials.empty());
    const MaterialData &m = model->materials[0];
    CHECK_MSG(m.base_color.valid, "base colour texture present");
    CHECK_MSG(m.metallic_roughness.valid, "metallic-roughness texture present");
    CHECK_MSG(m.normal.valid, "normal texture present");
    CHECK_MSG(m.emissive.valid, "emissive texture present");
    CHECK_MSG(m.occlusion.valid, "occlusion texture present");
    CHECK_MSG(m.base_color.embedded != nullptr, "embedded image decoded");
    if (m.base_color.embedded)
    {
        CHECK(m.base_color.embedded->width > 512);
        CHECK(m.base_color.embedded->channels == 4);
        CHECK(!m.base_color.embedded->pixels.empty());
    }
    CHECK(model->meshes[0].has_normals);
    CHECK(model->meshes[0].has_tangents);
}

TEST(gltf, multi_node_scene_hierarchy)
{
    auto model = ResourceIO::loadModel(assetPath("models/genesis_test_scene.gltf"));
    CHECK_MSG(model != nullptr, "genesis_test_scene.gltf loaded");
    if (!model) return;
    CHECK_EQ(model->nodes.size(), (size_t)3);
    CHECK_EQ(model->root_nodes.size(), (size_t)3);
    auto placed = model->flattenNodes();
    CHECK_EQ(placed.size(), (size_t)3);
    // At least one node carries a non identity transform.
    bool transformed = false;
    for (const auto &p : placed)
    {
        Math::Vec3 t = p.transform.translation();
        if (Math::length(t) > 1e-4f) transformed = true;
    }
    CHECK_MSG(transformed, "scene nodes carry translations");
}

TEST(gltf, textured_cube_external_images)
{
    auto model = ResourceIO::loadModel(assetPath("models/genesis_cube_textured.gltf"));
    CHECK_MSG(model != nullptr, "genesis_cube_textured.gltf loaded");
    if (!model) return;
    CHECK_EQ(model->meshes[0].vertices.size(), (size_t)24);
    CHECK_EQ(model->meshes[0].indices.size(), (size_t)36);
    CHECK(!model->materials.empty());
    const MaterialData &m = model->materials[0];
    CHECK_MSG(m.base_color.valid && !m.base_color.path.empty(), "external base colour URI resolved");
    CHECK_MSG(ResourceIO::fileExists(m.base_color.path), "base colour file exists on disk");
    CHECK_MSG(m.normal.valid, "normal texture bound");
    CHECK(m.base_color.srgb);
    CHECK(!m.metallic_roughness.srgb);
}

//======================================================================
// FBX import (native binary parser)
//======================================================================
TEST(fbx, binary_cube_geometry_and_material)
{
    auto model = ResourceIO::loadFbxModel(assetPath("models/genesis_test_cube.fbx"));
    CHECK_MSG(model != nullptr, "genesis_test_cube.fbx parsed");
    if (!model) return;
    CHECK(model->importer == "fbx");
    CHECK(!model->meshes.empty());

    size_t vertices = 0, indices = 0;
    for (const auto &m : model->meshes)
    {
        vertices += m.vertices.size();
        indices += m.indices.size();
    }
    CHECK_EQ(indices, (size_t)36); // 6 quads -> 12 triangles
    CHECK_MSG(vertices >= 8 && vertices <= 36, "cube vertices within [8,36] after dedup");

    // Unit cube centred at the origin.
    CHECK_NEAR(model->bounds.minPoint.x, -0.5, 1e-3);
    CHECK_NEAR(model->bounds.maxPoint.x, 0.5, 1e-3);
    CHECK_NEAR(model->bounds.minPoint.y, -0.5, 1e-3);
    CHECK_NEAR(model->bounds.maxPoint.y, 0.5, 1e-3);
    CHECK_NEAR(model->bounds.minPoint.z, -0.5, 1e-3);
    CHECK_NEAR(model->bounds.maxPoint.z, 0.5, 1e-3);

    CHECK(!model->materials.empty());
    CHECK(model->materials[0].name == "GenesisRed");
    CHECK_NEAR(model->materials[0].base_color_factor.r, 0.8, 1e-4);
    CHECK_NEAR(model->materials[0].base_color_factor.g, 0.2, 1e-4);
    CHECK_NEAR(model->materials[0].metallic_factor, 0.0, 1e-4);
    // Shininess 32 -> roughness = 1 - 32/128 = 0.75
    CHECK_NEAR(model->materials[0].roughness_factor, 0.75, 1e-4);

    // Normals must be unit length and axis aligned for a box.
    for (const auto &m : model->meshes)
        for (const auto &v : m.vertices)
        {
            Math::Vec3 n(v.normal);
            CHECK_NEAR(Math::length(n), 1.0, 1e-3);
        }
    CHECK(!model->nodes.empty());
}

TEST(fbx, binary_sphere_mixed_polygons)
{
    auto model = ResourceIO::loadFbxModel(assetPath("models/genesis_test_sphere.fbx"));
    CHECK_MSG(model != nullptr, "genesis_test_sphere.fbx parsed");
    if (!model) return;
    size_t vertices = 0, indices = 0;
    for (const auto &m : model->meshes)
    {
        vertices += m.vertices.size();
        indices += m.indices.size();
    }
    CHECK_MSG(vertices > 100, "sphere has more than 100 vertices");
    CHECK_EQ(indices % 3, (size_t)0);
    CHECK_NEAR(model->bounds.maxPoint.y, 0.5, 2e-2);
    CHECK_NEAR(model->bounds.minPoint.y, -0.5, 2e-2);
    // Sphere normals point away from the centre.
    for (const auto &m : model->meshes)
        for (size_t i = 0; i < m.vertices.size(); i += 17)
        {
            Math::Vec3 n(m.vertices[i].normal), p(m.vertices[i].position);
            if (Math::length(p) > 1e-3f)
                CHECK_MSG(Math::dot(Math::normalize(n), Math::normalize(p)) > 0.8, "sphere normal points outward");
        }
}

TEST(fbx, rejects_garbage_safely)
{
    // Not an FBX at all: must fail cleanly, never crash.
    std::string path = "/tmp/genesis_not_an_fbx.fbx";
    const char *junk = "this is definitely not a Kaydara FBX Binary file......";
    ResourceIO::writeFile(path, junk, std::strlen(junk));
    auto model = ResourceIO::loadFbxModel(path);
    CHECK_MSG(model == nullptr, "garbage FBX rejected");

    std::string truncated = assetPath("models/genesis_test_cube.fbx");
    std::vector<uint8_t> bytes;
    CHECK(ResourceIO::readFile(truncated, bytes));
    bytes.resize(bytes.size() / 3);
    std::string cut = "/tmp/genesis_truncated.fbx";
    ResourceIO::writeFile(cut, bytes.data(), bytes.size());
    auto cut_model = ResourceIO::loadFbxModel(cut);
    // Either it fails or it yields a partial model - both are acceptable, a
    // crash is not.
    CHECK_MSG(true, "truncated FBX handled without crashing");
    (void)cut_model;
}

TEST(resource, format_dispatch)
{
    CHECK(ResourceIO::isSupportedModelFile("a/b/c.gltf"));
    CHECK(ResourceIO::isSupportedModelFile("x.GLB"));
    CHECK(ResourceIO::isSupportedModelFile("y.fbx"));
    CHECK(ResourceIO::isSupportedModelFile("z.obj"));
    CHECK(!ResourceIO::isSupportedModelFile("w.png"));
    CHECK(ResourceIO::modelFileKind("A/B/C.GlTf") == "gltf");
    CHECK(ResourceIO::pathExtension("/tmp/Archive.TAR.GZ") == "gz");
    CHECK(ResourceIO::pathDirectory("/a/b/c.txt") == "/a/b");
    CHECK(ResourceIO::pathFilename("/a/b/c.txt") == "c.txt");
    CHECK(ResourceIO::joinPath("/a", "b") == "/a/b");
    CHECK(ResourceIO::joinPath("/a/", "b") == "/a/b");
    CHECK(ResourceIO::loadModel("/does/not/exist.glb") == nullptr);
}

//======================================================================
// Image IO
//======================================================================
TEST(image, png_loading_known_pixels)
{
    auto checker = ResourceIO::loadImage(assetPath("textures/checker_albedo.png"));
    CHECK_MSG(checker != nullptr, "checker_albedo.png decoded");
    if (!checker) return;
    CHECK_EQ(checker->width, 512u);
    CHECK_EQ(checker->height, 512u);
    CHECK_EQ(checker->channels, 4u);
    // 8x8 checker on 512px => 64px cells; (0,0) is the white cell.
    Math::Color top_left = checker->sampleNearest(0.0f, 0.0f);
    CHECK_NEAR(top_left.r, 1.0, 1e-3);
    CHECK_NEAR(top_left.g, 1.0, 1e-3);
    Math::Color second_cell = checker->sampleNearest(96.0f / 511.0f, 32.0f / 511.0f);
    CHECK_NEAR(second_cell.r, 128.0 / 255.0, 2e-3);

    auto uv = ResourceIO::loadImage(assetPath("textures/uv_gradient.png"));
    CHECK_MSG(uv != nullptr, "uv_gradient.png decoded");
    if (uv)
    {
        Math::Color c00 = uv->sampleNearest(0.0f, 0.0f);
        Math::Color c11 = uv->sampleNearest(1.0f, 1.0f);
        CHECK_NEAR(c00.r, 0.0, 8e-3);
        CHECK_NEAR(c00.g, 0.0, 8e-3);
        CHECK_NEAR(c11.r, 1.0, 8e-3);
        CHECK_NEAR(c11.g, 1.0, 8e-3);
    }

    auto mr = ResourceIO::loadImage(assetPath("textures/checker_metallic_roughness.png"));
    CHECK_MSG(mr != nullptr, "checker_metallic_roughness.png decoded");
    if (mr)
    {
        // glTF convention: G = roughness, B = metallic
        Math::Color white_cell = mr->sampleNearest(0.0f, 0.0f);
        CHECK_NEAR(white_cell.g, 230.0 / 255.0, 2e-3);
        CHECK_NEAR(white_cell.b, 0.0, 2e-3);
    }

    auto normal = ResourceIO::loadImage(assetPath("textures/panel_normal.png"));
    CHECK_MSG(normal != nullptr, "panel_normal.png decoded");
    if (normal)
    {
        // Flat areas must encode (0.5, 0.5, 1.0) i.e. (128,128,255).
        bool found_flat = false;
        for (uint32_t y = 20; y < 40 && !found_flat; ++y)
            for (uint32_t x = 20; x < 40; ++x)
            {
                size_t idx = ((size_t)y * normal->width + x) * normal->channels;
                if (std::abs((int)normal->pixels[idx] - 128) <= 1 &&
                    std::abs((int)normal->pixels[idx + 1] - 128) <= 1 &&
                    normal->pixels[idx + 2] > 250)
                    found_flat = true;
            }
        CHECK_MSG(found_flat, "normal map has flat (128,128,255) regions");
    }
}

TEST(image, hdr_equirect_loading)
{
    auto hdr = ResourceIO::loadImage(assetPath("textures/small_equirect.hdr"));
    CHECK_MSG(hdr != nullptr, "small_equirect.hdr decoded");
    if (!hdr) return;
    CHECK(hdr->is_hdr);
    CHECK_EQ(hdr->width, 256u);
    CHECK_EQ(hdr->height, 128u);
    CHECK_EQ(hdr->hdr.size(), (size_t)256 * 128 * 4);

    float max_value = 0.0f, sum = 0.0f;
    for (size_t i = 0; i < hdr->hdr.size(); i += 4)
    {
        float l = 0.2126f * hdr->hdr[i] + 0.7152f * hdr->hdr[i + 1] + 0.0722f * hdr->hdr[i + 2];
        max_value = std::max(max_value, l);
        sum += l;
    }
    float mean = sum / (float)(hdr->hdr.size() / 4);
    CHECK_MSG(max_value > 50.0f, "HDR map contains a bright sun");
    CHECK_MSG(mean > 0.05f && mean < 20.0f, "HDR mean luminance is sane");
}

TEST(image, png_and_tga_roundtrip)
{
    const uint32_t w = 16, h = 8;
    std::vector<uint8_t> rgba(w * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
        {
            size_t i = (y * w + x) * 4;
            rgba[i + 0] = (uint8_t)(x * 16);
            rgba[i + 1] = (uint8_t)(y * 32);
            rgba[i + 2] = 128;
            rgba[i + 3] = 255;
        }
    std::string png = "/tmp/genesis_roundtrip.png";
    std::string tga = "/tmp/genesis_roundtrip.tga";
    CHECK(ResourceIO::writeImagePNG(png, w, h, rgba.data()));
    CHECK(ResourceIO::writeImageTGA(tga, w, h, rgba.data()));

    auto back_png = ResourceIO::loadImage(png);
    CHECK_MSG(back_png != nullptr, "written PNG decodes");
    if (back_png)
    {
        CHECK_EQ(back_png->width, w);
        CHECK_EQ(back_png->height, h);
        for (size_t i = 0; i < rgba.size(); ++i)
            CHECK_MSG(back_png->pixels[i] == rgba[i], "PNG roundtrip is byte exact");
    }
    auto back_tga = ResourceIO::loadImage(tga);
    CHECK_MSG(back_tga != nullptr, "written TGA decodes");
    if (back_tga)
    {
        CHECK_EQ(back_tga->width, w);
        CHECK_EQ(back_tga->height, h);
        for (size_t i = 0; i < rgba.size(); ++i)
            CHECK_MSG(back_tga->pixels[i] == rgba[i], "TGA roundtrip is byte exact");
    }
}

//======================================================================
// Shader registry
//======================================================================
TEST(shaders, registry_has_both_variants)
{
    size_t count = Shaders::shaderCount();
    CHECK_MSG(count >= 13, "at least 13 shaders baked into the binary");

    const char *required[] = {
        "common/fullscreen.vert",
        "scene/pbr_mesh.vert",
        "scene/pbr_mesh.frag",
        "scene/skybox.frag",
        "scene/unlit_mesh.vert",
        "scene/unlit_mesh.frag",
        "scene/debug_line.vert",
        "scene/debug_line.frag",
        "ibl/equirect_to_cube.frag",
        "ibl/irradiance.frag",
        "ibl/prefilter.frag",
        "ibl/brdf_lut.frag",
        "post/tonemap.frag",
    };
    for (const char *name : required)
    {
        const Shaders::ShaderBinary *b = Shaders::findShader(name);
        CHECK_MSG(b != nullptr, std::string("shader present: ") + name);
        if (!b) continue;
        CHECK_MSG(b->glsl && b->glsl_size > 0, std::string("GLSL variant: ") + name);
        CHECK_MSG(b->spirv && b->spirv_words > 0, std::string("SPIR-V variant: ") + name);
        if (b->spirv && b->spirv_words > 0)
        {
            // SPIR-V magic number 0x07230203
            CHECK_MSG(b->spirv[0] == 0x07230203u, std::string("SPIR-V magic: ") + name);
        }
        if (b->glsl)
        {
            std::string src(b->glsl, b->glsl_size);
            CHECK_MSG(src.find("#version 410") != std::string::npos, std::string("GLSL 410 dialect: ") + name);
            CHECK_MSG(src.find("#define GENESIS_GLSL 1") != std::string::npos,
                      std::string("GL dialect define active: ") + name);
            // The Vulkan define must NOT be active in the GL variant; the raw
            // `#ifdef GENESIS_VK` branch text is still present (the GL driver
            // preprocesses it away), so only the define itself is checked.
            CHECK_MSG(src.find("#define GENESIS_VK 1") == std::string::npos,
                      std::string("Vulkan dialect define absent: ") + name);
            CHECK_MSG(src.find("GS_UBO(gs_set, gs_binding, gs_block) layout(std140) uniform gs_block") != std::string::npos ||
                          src.find("layout(std140) uniform") != std::string::npos,
                      std::string("GL uniform-block dialect present: ") + name);
        }
    }
    CHECK(Shaders::findShader("does/not/exist.frag") == nullptr);
}

//======================================================================
// Lua scripting
//======================================================================
TEST(script, lua_vm_and_entity_api)
{
    ECS::World world;
    Script::ScriptSystem script;
    CHECK(script.initialize(&world, nullptr, nullptr));
    CHECK(script.state() != nullptr);

    ECS::Entity e = world.createEntity("lua_target");
    ECS::TransformComponent t;
    t.position = Math::Vec3(1, 2, 3);
    world.addComponent<ECS::TransformComponent>(e, t);

    CHECK(script.executeString("genesis.log_info('hello from lua')"));
    // Global assignment at the top level of a plain chunk must work too.
    CHECK(script.executeString("genesis_probe_value = 41 + 1"));

    std::string code =
        "local id = genesis.entity_find('lua_target')\n"
        "genesis.set_position(id, 7.5, 8.5, 9.5)\n"
        "genesis.set_scale(id, 2.0)\n"
        "return id\n";
    CHECK(script.executeString(code));

    ECS::TransformComponent *after = world.getComponent<ECS::TransformComponent>(e);
    CHECK_NEAR(after->position.x, 7.5, 1e-5);
    CHECK_NEAR(after->position.y, 8.5, 1e-5);
    CHECK_NEAR(after->position.z, 9.5, 1e-5);
    CHECK_NEAR(after->scale.x, 2.0, 1e-5);

    // Errors are captured, not thrown.
    uint32_t before = script.errorCount();
    CHECK(!script.executeString("this is not valid lua !!!"));
    CHECK(script.errorCount() > before);
    script.shutdown();
}

TEST(script, component_lifecycle_hooks)
{
    ECS::World world;
    Script::ScriptSystem script;
    CHECK(script.initialize(&world, nullptr, nullptr));

    ECS::Entity e = world.createEntity("animated");
    ECS::TransformComponent t;
    world.addComponent<ECS::TransformComponent>(e, t);

    ECS::ScriptComponent comp;
    comp.inline_code =
        "started = 0\n"
        "function start(id)\n"
        "  started = 1\n"
        "  genesis.log_info('start hook')\n"
        "end\n"
        "function tick(dt, id)\n"
        "  local x, y, z = genesis.get_position(id)\n"
        "  genesis.set_position(id, x + dt, y, z)\n"
        "end\n";
    world.addComponent<ECS::ScriptComponent>(e, comp);

    // First update loads the script, subsequent updates tick it.
    script.update(world, 0.1f);
    script.update(world, 0.1f);
    script.update(world, 0.1f);

    ECS::TransformComponent *after = world.getComponent<ECS::TransformComponent>(e);
    CHECK_MSG(after->position.x > 0.15f, "tick hook moved the entity along +X");
    CHECK_EQ(script.loadedScripts(), (size_t)1);
    CHECK_EQ(script.errorCount(), 0u);

    // Sandboxing: two scripts must not see each other's locals.
    ECS::Entity e2 = world.createEntity("second");
    world.addComponent<ECS::TransformComponent>(e2);
    ECS::ScriptComponent comp2;
    comp2.inline_code = "function tick(dt, id) if started ~= nil then genesis.log_error('sandbox leak') end end";
    world.addComponent<ECS::ScriptComponent>(e2, comp2);
    script.update(world, 0.016f);
    CHECK_EQ(script.errorCount(), 0u);
    script.shutdown();
}

TEST(script, api_surface_is_complete)
{
    Script::ScriptSystem script;
    CHECK(script.initialize(nullptr, nullptr, nullptr));
    const char *api_check =
        "local required = {\n"
        "  'log_info','log_warn','log_error','elapsed','frame','set_time_scale',\n"
        "  'entity_create','entity_destroy','entity_find','entity_count','entity_name',\n"
        "  'get_position','set_position','translate','get_rotation','set_rotation','rotate',\n"
        "  'get_scale','set_scale','set_parent','light_set',\n"
        "  'input_key','input_key_pressed','input_mouse','input_mouse_pos','input_mouse_delta','input_wheel',\n"
        "  'render_set_exposure','render_set_gamma','render_set_tonemap','render_set_shading',\n"
        "  'render_set_wireframe','render_set_skybox','render_set_env_intensity','render_set_env_rotation',\n"
        "  'render_set_ambient','render_set_ibl','render_stats','render_draw_line','render_draw_box',\n"
        "  'render_bake_ibl','load_model','model_info'\n"
        "}\n"
        "local missing = {}\n"
        "for _, name in ipairs(required) do\n"
        "  if type(genesis[name]) ~= 'function' then table.insert(missing, name) end\n"
        "end\n"
        "if #missing > 0 then error('missing API: ' .. table.concat(missing, ',')) end\n"
        "if type(genesis.version) ~= 'string' then error('missing genesis.version') end\n";
    CHECK(script.executeString(api_check));
    CHECK_EQ(script.errorCount(), 0u);
    script.shutdown();
}

//======================================================================
// Render UBO layout (guards against GLSL/C++ drift)
//======================================================================
TEST(render_ubo, struct_sizes_match_glsl)
{
    CHECK_EQ(sizeof(Render::PerFrameUBO), (size_t)384);
    CHECK_EQ(sizeof(Render::LightsUBO), (size_t)544);
    CHECK_EQ(sizeof(Render::EnvironmentUBO), (size_t)64);
    CHECK_EQ(sizeof(Render::MaterialUBO), (size_t)96);
    CHECK_EQ(sizeof(Render::PerDrawUBO), (size_t)144);
    CHECK_EQ(sizeof(Render::IblParamsUBO), (size_t)32);
    CHECK_EQ(sizeof(Render::PostParamsUBO), (size_t)32);
    CHECK_EQ(sizeof(Render::GpuLight), (size_t)64);
    CHECK_EQ(Render::kMaxLights, 8u);
}
