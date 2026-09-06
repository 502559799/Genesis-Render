/*
 * Genesis Render - Engine
 * model.hpp : CPU model structures + GPU mesh / PBR material resources.
 */
#pragma once

#include "math/math.hpp"
#include "rhi/rhi.hpp"

#include <string>
#include <vector>
#include <memory>

namespace Genesis
{
    // GPU vertex layout, packed to exactly 48 bytes.
    // location 0 position (0), 1 normal (12), 3 uv (24), 2 tangent (32).
    struct ModelVertex
    {
        Vec3 position;   // 12, offset 0
        Vec3 normal;     // 12, offset 12
        Vec2 uv;         // 8,  offset 24
        Vec4 tangent;    // 16, offset 32 (xyz tangent, w bitangent handedness)
    };
    static_assert(sizeof(ModelVertex) == 48, "ModelVertex expected 48 bytes");

    struct SubMesh
    {
        uint32_t index_offset = 0;
        uint32_t index_count = 0;
        uint32_t material_index = 0;
        std::string name;
    };

    struct GPUMesh
    {
        BufferHandle vbo{};
        BufferHandle ibo{};
        uint32_t index_count = 0;
        uint32_t vertex_count = 0;
        IndexType index_type = IndexType::Uint32;
        std::vector<SubMesh> submeshes;
        std::string name;
    };

    // std140 layout (aligned to 16): must match MaterialBlock in the shaders.
    struct alignas(16) MaterialUBO
    {
        Vec4 base_color_factor{1, 1, 1, 1};   // 16
        Vec4 metallic_roughness{0.0f, 1.0f, 0.0f, 0.0f}; // x metallic, y roughness, z aoStrength
        Vec4 emissive_factor{0, 0, 0, 0};     // xyz emissive, w emissive strength
        // x: use albedo map, y: normal map, z: orm map, w: emissive map
        Vec4 texture_flags{0, 0, 0, 0};
        Vec4 uv_transform{1, 1, 0, 0};        // xy scale, zw offset
        Vec4 pad[3];
    };
    static_assert(sizeof(MaterialUBO) >= 80, "MaterialUBO size");

    class IRHIDevice;

    struct PBRMaterial
    {
        std::string name;
        MaterialUBO ubo{};
        TextureHandle albedo_tex{};
        TextureHandle normal_tex{};
        TextureHandle orm_tex{};
        TextureHandle emissive_tex{};
        bool has_albedo = false, has_normal = false, has_orm = false, has_emissive = false;
        BufferHandle ubo_buffer{};
        DescriptorSetHandle set{};
        bool dirty = true;
        // UI / scripting
        float metallic() const { return ubo.metallic_roughness.x; }
        float roughness() const { return ubo.metallic_roughness.y; }
        void setMetallic(float v)
        {
            ubo.metallic_roughness.x = v;
            dirty = true;
        }
        void setRoughness(float v)
        {
            ubo.metallic_roughness.y = v;
            dirty = true;
        }
        void setBaseColor(const Vec3 &c)
        {
            ubo.base_color_factor = Vec4(c, ubo.base_color_factor.w);
            dirty = true;
        }
    };

    struct ModelAsset
    {
        std::string name;
        std::string path;
        std::vector<GPUMesh> meshes;
        std::vector<std::shared_ptr<PBRMaterial>> materials;
        AABB bounds;
    };

    // CPU-side result of an importer before RHI upload.
    struct CPUMaterial
    {
        std::string name;
        Vec4 base_color{1, 1, 1, 1};
        float metallic = 0.0f;
        float roughness = 1.0f;
        Vec3 emissive{0, 0, 0};
        float emissive_strength = 1.0f;
        std::string albedo_path;
        std::string normal_path;
        std::string orm_path;
        std::string emissive_path;
        // embedded texture blobs (decoded by stb later): keyed slot -> memory
        struct Embedded
        {
            std::vector<uint8_t> bytes; // encoded png/jpg payload
            std::vector<uint8_t> raw;   // raw texel payload (rgba8 / rgba32f)
            int w = 0, h = 0;
            bool hdr = false;
        };
        Embedded albedo_emb, normal_emb, orm_emb, emissive_emb;
    };

    struct CPUModel
    {
        struct Mesh
        {
            std::vector<ModelVertex> vertices;
            std::vector<uint32_t> indices;
            std::vector<SubMesh> submeshes;
            std::string name;
        };
        std::vector<Mesh> meshes;
        std::vector<CPUMaterial> materials;
        std::string base_dir;
        AABB bounds;
    };

    // Procedural meshes.
    namespace procgen
    {
        CPUModel makePlane(float size = 10.0f, int divisions = 1);
        CPUModel makeCube(float size = 1.0f);
        CPUModel makeSphere(float radius = 1.0f, int slices = 48, int stacks = 32);
        // RGBA equirectangular environment (linear HDR floats), used as IBL fallback.
        std::vector<float> makeProceduralEnvironment(uint32_t w, uint32_t h);
    }
}
