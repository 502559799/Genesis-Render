/*
 * Genesis Render - CPU-side geometry/material data produced by model loaders
 * (cgltf for glTF/glb, Assimp for FBX/OBJ/...) and consumed by AssetManager
 * to build GPU resources.
 */
#pragma once

#include "core/math.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace Genesis {

// Interleaved engine vertex - shared attribute layout for every backend.
struct Vertex {
    Vec3 position;   // loc 0
    Vec3 normal;     // loc 1
    Vec4 tangent;    // loc 2 (xyz + handedness)
    Vec2 uv;         // loc 3
};

struct SubMesh {
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    int32_t materialIndex = -1;
};

// One mesh = one interleaved vertex buffer + index buffer + sub-meshes.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SubMesh> submeshes;
};

struct MaterialDesc {
    std::string name;
    Vec4 baseColorFactor{1, 1, 1, 1};
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    Vec3 emissiveFactor{0, 0, 0};
    bool hasTextures = false;
    // resolved absolute paths (empty = use factor / default texture)
    std::string baseColorTex;
    std::string normalTex;
    std::string metallicRoughnessTex;
    std::string occlusionTex;
    std::string emissiveTex;
    float normalScale = 1.0f;
    bool doubleSided = false;
};

struct MeshNode {
    std::string name;
    Mat4 local{1.0f};
    std::vector<uint32_t> meshIndices;
    std::vector<uint32_t> children;
};

struct ModelAsset {
    std::string name;
    std::string baseDir;
    std::vector<MeshData> meshes;
    std::vector<MaterialDesc> materials;
    std::vector<MeshNode> nodes;
    std::vector<uint32_t> roots;

    // merged index space produced by mergeMeshes()
    bool merged = false;
    MeshData combined;
    struct CombinedSub {
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;
        int32_t materialIndex = -1;
        uint32_t node = 0;
    };
    std::vector<CombinedSub> combinedSubs;
    // node draws into combined space
    struct NodeDraw {
        uint32_t combinedOffset = 0;
        uint32_t combinedCount = 0;
    };
    std::vector<NodeDraw> nodeDraws;
    // embedded textures (glb): "embedded://key" -> encoded image bytes
    std::unordered_map<std::string, std::vector<uint8_t>> embeddedImages;
};

// Merge every mesh into a single vertex/index buffer and record per-node,
// per-submesh draw ranges. Keeps GPU binding simple in the renderer.
void mergeMeshes(ModelAsset& asset);

// Generate tangents for any submesh lacking them.
void generateTangents(MeshData& mesh);

} // namespace Genesis
