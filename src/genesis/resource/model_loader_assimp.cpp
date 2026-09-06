/*
 * Genesis Render - Assimp model loader (FBX binary/ascii, OBJ, DAE, ...).
 * Shares the same CPU data structures as the cgltf path, so FBX content goes
 * through the exact same renderer/material pipeline as glTF.
 */
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "resource/model_loader.hpp"
#include "foundation_layer/log.hpp"

#include <filesystem>
#include <fstream>

namespace Genesis {
namespace fs = std::filesystem;

static std::string parentDir(const std::string& p) { return fs::path(p).parent_path().string(); }

static std::string texturePath(const aiScene* scene, const aiMaterial* mtl, aiTextureType type,
                               ModelAsset& asset, int index = 0) {
    if (mtl->GetTextureCount(type) == 0) return "";
    aiString path;
    if (mtl->GetTexture(type, index, &path) != aiReturn_SUCCESS) return "";
    std::string p = path.C_Str();
    if (auto* tex = scene->GetEmbeddedTexture(p.c_str())) {
        std::string key = "ai_" + std::to_string(asset.embeddedImages.size());
        if (tex->mHeight == 0 && tex->mWidth > 0 && tex->pcData) {
            const uint8_t* d = (const uint8_t*)tex->pcData;
            asset.embeddedImages[key].assign(d, d + tex->mWidth);
            return "embedded://" + key;
        }
    }
    fs::path full = fs::path(asset.baseDir) / p;
    if (fs::exists(full)) return full.lexically_normal().string();
    fs::path nameOnly = fs::path(p).filename();
    fs::path alt = fs::path(asset.baseDir) / nameOnly;
    if (fs::exists(alt)) return alt.string();
    return "";
}

static void readMaterial(const aiScene* scene, const aiMaterial* m, ModelAsset& asset) {
    MaterialDesc md;
    aiString name;
    if (m->Get(AI_MATKEY_NAME, name) == aiReturn_SUCCESS) md.name = name.C_Str();
    aiColor4D base;
    if (m->Get(AI_MATKEY_BASE_COLOR, base) == aiReturn_SUCCESS)
        md.baseColorFactor = Vec4{base.r, base.g, base.b, base.a};
    else if (m->Get(AI_MATKEY_COLOR_DIFFUSE, base) == aiReturn_SUCCESS)
        md.baseColorFactor = Vec4{base.r, base.g, base.b, base.a};
    float metallic = 0, rough = 1;
    m->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
    m->Get(AI_MATKEY_ROUGHNESS_FACTOR, rough);
    md.metallicFactor = metallic;
    md.roughnessFactor = rough;
    md.baseColorTex = texturePath(scene, m, aiTextureType_BASE_COLOR, asset);
    if (md.baseColorTex.empty()) md.baseColorTex = texturePath(scene, m, aiTextureType_DIFFUSE, asset);
    md.normalTex = texturePath(scene, m, aiTextureType_NORMALS, asset);
    if (md.normalTex.empty()) md.normalTex = texturePath(scene, m, aiTextureType_HEIGHT, asset);
    // glTF style metallicRoughness usually arrives as UNKNOWN / DIFFUSE_ROUGHNESS
    md.metallicRoughnessTex = texturePath(scene, m, aiTextureType_DIFFUSE_ROUGHNESS, asset);
    if (md.metallicRoughnessTex.empty())
        md.metallicRoughnessTex = texturePath(scene, m, aiTextureType_UNKNOWN, asset);
    md.occlusionTex = texturePath(scene, m, aiTextureType_LIGHTMAP, asset);
    md.emissiveTex = texturePath(scene, m, aiTextureType_EMISSIVE, asset);
    aiColor4D em;
    if (m->Get(AI_MATKEY_COLOR_EMISSIVE, em) == aiReturn_SUCCESS)
        md.emissiveFactor = Vec3{em.r, em.g, em.b};
    md.hasTextures = !md.baseColorTex.empty() || !md.metallicRoughnessTex.empty();
    asset.materials.push_back(md);
}

static Mat4 aiMatrix(const aiMatrix4x4& m) {
    Mat4 r(0.0f);
    // ai is row-major; transpose into our column-major layout
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) r.at(c, row) = m[row][c];
    return r;
}

static uint32_t nodeRecurse(const aiScene* scene, const aiNode* node, ModelAsset& asset) {
    MeshNode mn;
    mn.name = node->mName.C_Str();
    mn.local = aiMatrix(node->mTransformation);
    for (unsigned i = 0; i < node->mNumMeshes; ++i) {
        unsigned mi = node->mMeshes[i];
        if (mi < asset.meshes.size()) mn.meshIndices.push_back(mi);
    }
    asset.nodes.push_back(mn);
    uint32_t idx = (uint32_t)asset.nodes.size() - 1;
    for (unsigned i = 0; i < node->mNumChildren; ++i)
        asset.nodes[idx].children.push_back(nodeRecurse(scene, node->mChildren[i], asset));
    return idx;
}

std::shared_ptr<ModelAsset> loadAssimpModel(const std::string& path, std::string& err) {
    Assimp::Importer importer;
    unsigned flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                     aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices |
                     aiProcess_ImproveCacheLocality | aiProcess_PreTransformVertices;
    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene) {
        err = std::string("assimp: ") + importer.GetErrorString();
        return nullptr;
    }
    auto asset = std::make_shared<ModelAsset>();
    asset->name = fs::path(path).filename().string();
    asset->baseDir = parentDir(path);

    for (unsigned i = 0; i < scene->mNumMaterials; ++i)
        readMaterial(scene, scene->mMaterials[i], *asset);

    asset->meshes.resize(scene->mNumMeshes);
    for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* m = scene->mMeshes[mi];
        MeshData& out = asset->meshes[mi];
        out.vertices.resize(m->mNumVertices);
        for (unsigned v = 0; v < m->mNumVertices; ++v) {
            Vertex& vx = out.vertices[v];
            vx.position = Vec3{m->mVertices[v].x, m->mVertices[v].y, m->mVertices[v].z};
            if (m->HasNormals()) vx.normal = Vec3{m->mNormals[v].x, m->mNormals[v].y, m->mNormals[v].z};
            if (m->mTangents != nullptr)
                vx.tangent = Vec4{m->mTangents[v].x, m->mTangents[v].y, m->mTangents[v].z, 1};
            if (m->HasTextureCoords(0))
                vx.uv = Vec2{m->mTextureCoords[0][v].x, 1.0f - m->mTextureCoords[0][v].y};
        }
        uint32_t base = 0;
        for (unsigned f = 0; f < m->mNumFaces; ++f) {
            const aiFace& face = m->mFaces[f];
            for (unsigned k = 0; k < face.mNumIndices; ++k)
                out.indices.push_back(face.mIndices[k] + base);
        }
        SubMesh sm;
        sm.indexCount = (uint32_t)out.indices.size();
        sm.materialIndex = (int32_t)m->mMaterialIndex;
        out.submeshes.push_back(sm);
    }
    asset->roots.push_back(nodeRecurse(scene, scene->mRootNode, *asset));
    GenesisPrint("[assimp] loaded %s: %u meshes, %zu nodes, %zu materials\n",
                 asset->name.c_str(), scene->mNumMeshes, asset->nodes.size(), asset->materials.size());
    return asset;
}

} // namespace Genesis
