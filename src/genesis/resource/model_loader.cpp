/*
 * Genesis Render - Engine
 * model_loader.cpp : Assimp-backed importer for glTF/FBX/OBJ.
 */
#include "model_loader.hpp"
#include "foundation_layer/log.hpp"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/GltfMaterial.h>
#include <assimp/material.h>

#include <filesystem>

namespace fs = std::filesystem;
namespace Genesis
{
using Mesh = CPUModel::Mesh;
static Vec3 aiToVec3(const aiColor3D &c) { return {c.r, c.g, c.b}; }

static void grabTexture(const aiMaterial *mat, aiTextureType type, const aiScene *scene,
                        const std::string &base_dir, std::string &out_path,
                        CPUMaterial::Embedded &out_emb)
{
    if (mat->GetTextureCount(type) == 0) return;
    aiString ai_path;
    if (mat->GetTexture(type, 0, &ai_path) != aiReturn_SUCCESS) return;
    std::string p = ai_path.C_Str();
    // embedded?
    if (!p.empty() && p[0] == '*')
    {
        const aiTexture *tex = scene->mTextures ? scene->GetEmbeddedTexture(p.c_str()) : nullptr;
        if (tex)
        {
            if (tex->mHeight == 0 && tex->mWidth > 0)
            {
                // compressed payload (png/jpg), mWidth is the byte count
                out_emb.bytes.assign((uint8_t *)tex->pcData,
                                     (uint8_t *)tex->pcData + tex->mWidth);
            }
            else
            {
                // raw texels; aiTexel memory order is B,G,R,A
                out_emb.raw.resize((size_t)tex->mWidth * tex->mHeight * 4);
                for (unsigned i = 0; i < tex->mWidth * tex->mHeight; ++i)
                {
                    const aiTexel &t = tex->pcData[i];
                    out_emb.raw[i * 4 + 0] = t.r;
                    out_emb.raw[i * 4 + 1] = t.g;
                    out_emb.raw[i * 4 + 2] = t.b;
                    out_emb.raw[i * 4 + 3] = t.a;
                }
                out_emb.w = (int)tex->mWidth;
                out_emb.h = (int)tex->mHeight;
            }
            return;
        }
    }
    std::filesystem::path full = std::filesystem::path(base_dir) / p;
    if (std::filesystem::exists(full))
        out_path = full.string();
    else
        out_path = p;
}

bool ModelLoader::load(const std::string &path, CPUModel &out)
{
    Assimp::Importer importer;
    unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_ImproveCacheLocality |
        aiProcess_FindDegenerates |
        aiProcess_GenBoundingBoxes |
        aiProcess_PopulateArmatureData;
    const aiScene *scene = importer.ReadFile(path, flags);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        GenesisPrint("[Assimp] load failed %s: %s\n", path.c_str(), importer.GetErrorString());
        return false;
    }
    out.base_dir = fs::path(path).parent_path().string();
    out.meshes.resize(scene->mNumMeshes);

    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi)
    {
        const aiMesh *am = scene->mMeshes[mi];
        Mesh &mesh = out.meshes[mi];
        mesh.name = am->mName.C_Str();
        mesh.vertices.resize(am->mNumVertices);
        for (unsigned int v = 0; v < am->mNumVertices; ++v)
        {
            ModelVertex &mv = mesh.vertices[v];
            if (am->HasPositions())
                mv.position = {am->mVertices[v].x, am->mVertices[v].y, am->mVertices[v].z};
            if (am->HasNormals())
                mv.normal = {am->mNormals[v].x, am->mNormals[v].y, am->mNormals[v].z};
            else
                mv.normal = {0, 1, 0};
            if (am->HasTangentsAndBitangents())
                mv.tangent = {am->mTangents[v].x, am->mTangents[v].y, am->mTangents[v].z, 1};
            else
                mv.tangent = {1, 0, 0, 1};
            if (am->HasTextureCoords(0))
                mv.uv = {am->mTextureCoords[0][v].x, am->mTextureCoords[0][v].y};
            else
                mv.uv = {0, 0};
            out.bounds.addPoint(mv.position);
        }
        for (unsigned int f = 0; f < am->mNumFaces; ++f)
        {
            const aiFace &face = am->mFaces[f];
            if (face.mNumIndices != 3) continue;
            for (unsigned int k = 0; k < 3; ++k)
                mesh.indices.push_back(face.mIndices[k]);
        }
        SubMesh sm;
        sm.index_offset = 0;
        sm.index_count = (uint32_t)mesh.indices.size();
        sm.material_index = am->mMaterialIndex;
        sm.name = mesh.name;
        mesh.submeshes.push_back(sm);
    }

    out.materials.resize(scene->mNumMaterials);
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i)
    {
        const aiMaterial *am = scene->mMaterials[i];
        CPUMaterial &m = out.materials[i];
        aiString name;
        if (am->Get(AI_MATKEY_NAME, name) == aiReturn_SUCCESS) m.name = name.C_Str();

        aiColor4D base(1, 1, 1, 1);
        if (am->Get(AI_MATKEY_BASE_COLOR, base) == aiReturn_SUCCESS ||
            am->Get(AI_MATKEY_COLOR_DIFFUSE, base) == aiReturn_SUCCESS)
            m.base_color = {base.r, base.g, base.b, base.a};
        float metallic = 0.f, roughness = 1.f;
        am->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
        am->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
        m.metallic = metallic;
        m.roughness = roughness;
        aiColor3D em(0, 0, 0);
        if (am->Get(AI_MATKEY_COLOR_EMISSIVE, em) == aiReturn_SUCCESS)
            m.emissive = aiToVec3(em);

        grabTexture(am, aiTextureType_BASE_COLOR, scene, out.base_dir, m.albedo_path, m.albedo_emb);
        if (m.albedo_path.empty() && m.albedo_emb.bytes.empty() && m.albedo_emb.raw.empty())
            grabTexture(am, aiTextureType_DIFFUSE, scene, out.base_dir, m.albedo_path, m.albedo_emb);
        grabTexture(am, aiTextureType_NORMAL_CAMERA, scene, out.base_dir, m.normal_path, m.normal_emb);
        if (m.normal_path.empty() && m.normal_emb.bytes.empty() && m.normal_emb.raw.empty())
            grabTexture(am, aiTextureType_NORMALS, scene, out.base_dir, m.normal_path, m.normal_emb);
        // glTF metallic-roughness is the packed ORM source (g = roughness, b = metallic)
        grabTexture(am, aiTextureType_DIFFUSE_ROUGHNESS, scene, out.base_dir, m.orm_path, m.orm_emb);
        grabTexture(am, aiTextureType_EMISSION_COLOR, scene, out.base_dir, m.emissive_path, m.emissive_emb);
        if (m.emissive_path.empty() && m.emissive_emb.bytes.empty() && m.emissive_emb.raw.empty())
            grabTexture(am, aiTextureType_EMISSIVE, scene, out.base_dir, m.emissive_path, m.emissive_emb);
    }
    GenesisPrint("[Assimp] loaded %s : %u meshes, %u materials\n", path.c_str(),
                 scene->mNumMeshes, scene->mNumMaterials);
    return true;
}
}
