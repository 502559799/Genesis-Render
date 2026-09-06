/*
 * @Descripttion: 资源管理实现: assimp 模型解析 + stb 纹理解码
 */
#include "resource_layer/asset_manager.hpp"

#include "foundation_layer/log.hpp"
#include "rhi_layer/rhi_types.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

#include <cstring>
#include <filesystem>

namespace Genesis
{
    namespace resource
    {
        static std::string directoryOf(const std::string &path)
        {
            std::filesystem::path p(path);
            return p.parent_path().string();
        }

        //-----------------------------------------------------------
        // 纹理
        //-----------------------------------------------------------
        static bool endsWith(const std::string &s, const char *suffix)
        {
            size_t n = strlen(suffix);
            return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
        }

        const TextureAsset *AssetManager::loadTexture(const std::string &path, bool srgb)
        {
            std::string key = path + (srgb ? "#srgb" : "");
            auto it = m_textures.find(key);
            if (it != m_textures.end())
                return &it->second;

            TextureAsset asset;

            if (endsWith(path, ".hdr"))
            {
                int w = 0, h = 0, channels = 0;
                float *data = stbi_loadf(path.c_str(), &w, &h, &channels, 4);
                if (!data)
                {
                    GenesisPrint("[Asset] failed to load HDR texture: %s\n", path.c_str());
                    return nullptr;
                }
                asset.m_width = (uint32_t)w;
                asset.m_height = (uint32_t)h;
                asset.m_is_hdr = true;
                asset.m_format = rhi::Format::RGBA16_Float;
                asset.m_pixels.resize((size_t)w * h * 8);
                // float -> half
                uint16_t *dst = (uint16_t *)asset.m_pixels.data();
                for (size_t i = 0; i < (size_t)w * h * 4; ++i)
                    dst[i] = rhi::floatToHalf(data[i]);
                stbi_image_free(data);
            }
            else
            {
                int w = 0, h = 0, channels = 0;
                // 垂直翻转: 与 assimp aiProcess_FlipUVs 配套
                stbi_set_flip_vertically_on_load(1);
                uint8_t *data = stbi_load(path.c_str(), &w, &h, &channels, 4);
                stbi_set_flip_vertically_on_load(0);
                if (!data)
                {
                    GenesisPrint("[Asset] failed to load texture: %s\n", path.c_str());
                    return nullptr;
                }
                asset.m_width = (uint32_t)w;
                asset.m_height = (uint32_t)h;
                asset.m_is_hdr = false;
                asset.m_format = srgb ? rhi::Format::RGBA8_sRGB : rhi::Format::RGBA8_UNorm;
                asset.m_pixels.assign(data, data + (size_t)w * h * 4);
                stbi_image_free(data);
            }

            GenesisPrint("[Asset] texture loaded: %s (%ux%u%s)\n", path.c_str(), asset.m_width, asset.m_height, asset.m_is_hdr ? " HDR" : "");
            auto result = m_textures.emplace(key, std::move(asset));
            return &result.first->second;
        }

        const TextureAsset *AssetManager::createTextureFromPixels(const std::string &name, uint32_t w, uint32_t h, const uint8_t *rgba)
        {
            TextureAsset asset;
            asset.m_width = w;
            asset.m_height = h;
            asset.m_is_hdr = false;
            asset.m_format = rhi::Format::RGBA8_UNorm;
            asset.m_pixels.assign(rgba, rgba + (size_t)w * h * 4);
            auto result = m_textures.emplace(name, std::move(asset));
            return &result.first->second;
        }

        //-----------------------------------------------------------
        // 模型
        //-----------------------------------------------------------
        static std::string resolveTexturePath(const aiString &tex_path, const std::string &model_dir)
        {
            std::string p = tex_path.C_Str();
            if (p.empty())
                return p;
            std::filesystem::path full = std::filesystem::path(model_dir) / p;
            return full.lexically_normal().string();
        }

        uint32_t AssetManager::loadModel(const std::string &path)
        {
            Assimp::Importer importer;
            unsigned flags = aiProcess_Triangulate | aiProcess_CalcTangentSpace |
                             aiProcess_GenSmoothNormals | aiProcess_FlipUVs |
                             aiProcess_JoinIdenticalVertices | aiProcess_ImproveCacheLocality;

            const aiScene *scene = importer.ReadFile(path, flags);
            if (!scene || !scene->mRootNode)
            {
                GenesisPrint("[Asset] assimp failed to load model %s: %s\n", path.c_str(), importer.GetErrorString());
                return 0xffffffff;
            }

            ModelAsset model;
            model.m_name = path;
            std::string dir = directoryOf(path);

            // 材质
            for (uint32_t i = 0; i < scene->mNumMaterials; ++i)
            {
                aiMaterial *mat = scene->mMaterials[i];
                MaterialData data;

                aiString name;
                if (mat->Get(AI_MATKEY_NAME, name) == AI_SUCCESS)
                    data.m_name = name.C_Str();

                aiColor4D base_color;
                if (mat->Get(AI_MATKEY_BASE_COLOR, base_color) == AI_SUCCESS)
                {
                    data.m_base_color[0] = base_color.r;
                    data.m_base_color[1] = base_color.g;
                    data.m_base_color[2] = base_color.b;
                    data.m_base_color[3] = base_color.a;
                }
                else
                {
                    aiColor4D diffuse;
                    if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS)
                    {
                        data.m_base_color[0] = diffuse.r;
                        data.m_base_color[1] = diffuse.g;
                        data.m_base_color[2] = diffuse.b;
                        data.m_base_color[3] = diffuse.a;
                    }
                }

                float metallic = 0.0f;
                if (mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS)
                    data.m_metallic = metallic;
                float roughness = 0.5f;
                if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS)
                    data.m_roughness = roughness;

                aiColor3D emissive;
                if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS)
                {
                    data.m_emissive[0] = emissive.r;
                    data.m_emissive[1] = emissive.g;
                    data.m_emissive[2] = emissive.b;
                }

                aiString tex_path;
                // glTF PBR 贴图
                if (mat->GetTexture(aiTextureType_BASE_COLOR, 0, &tex_path) == AI_SUCCESS)
                    data.m_albedo_tex = resolveTexturePath(tex_path, dir);
                else if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &tex_path) == AI_SUCCESS)
                    data.m_albedo_tex = resolveTexturePath(tex_path, dir);

                if (mat->GetTexture(aiTextureType_NORMALS, 0, &tex_path) == AI_SUCCESS)
                    data.m_normal_tex = resolveTexturePath(tex_path, dir);
                else if (mat->GetTexture(aiTextureType_HEIGHT, 0, &tex_path) == AI_SUCCESS)
                    data.m_normal_tex = resolveTexturePath(tex_path, dir);

                if (mat->GetTexture(aiTextureType_METALNESS, 0, &tex_path) == AI_SUCCESS)
                    data.m_metallic_roughness_tex = resolveTexturePath(tex_path, dir);
                else if (mat->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &tex_path) == AI_SUCCESS)
                    data.m_metallic_roughness_tex = resolveTexturePath(tex_path, dir);
                else if (mat->GetTexture(aiTextureType_UNKNOWN, 0, &tex_path) == AI_SUCCESS)
                    data.m_metallic_roughness_tex = resolveTexturePath(tex_path, dir); // assimp 5.2: glTF metallicRoughness 落在 UNKNOWN

                if (mat->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &tex_path) == AI_SUCCESS)
                    data.m_ao_tex = resolveTexturePath(tex_path, dir);
                else if (mat->GetTexture(aiTextureType_LIGHTMAP, 0, &tex_path) == AI_SUCCESS)
                    data.m_ao_tex = resolveTexturePath(tex_path, dir);

                if (mat->GetTexture(aiTextureType_EMISSIVE, 0, &tex_path) == AI_SUCCESS)
                    data.m_emissive_tex = resolveTexturePath(tex_path, dir);

                model.m_materials.push_back(std::move(data));
            }

            if (model.m_materials.empty())
                model.m_materials.emplace_back(); // 默认材质

            // 网格
            bool first_vertex = true;
            for (uint32_t i = 0; i < scene->mNumMeshes; ++i)
            {
                aiMesh *mesh = scene->mMeshes[i];
                if (!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
                    continue;

                MeshData mesh_data;
                mesh_data.m_material_index = mesh->mMaterialIndex < model.m_materials.size() ? mesh->mMaterialIndex : 0;
                mesh_data.m_vertices.resize(mesh->mNumVertices);

                for (uint32_t v = 0; v < mesh->mNumVertices; ++v)
                {
                    Vertex &vert = mesh_data.m_vertices[v];
                    vert.m_position[0] = mesh->mVertices[v].x;
                    vert.m_position[1] = mesh->mVertices[v].y;
                    vert.m_position[2] = mesh->mVertices[v].z;

                    if (mesh->HasNormals())
                    {
                        vert.m_normal[0] = mesh->mNormals[v].x;
                        vert.m_normal[1] = mesh->mNormals[v].y;
                        vert.m_normal[2] = mesh->mNormals[v].z;
                    }
                    if (mesh->HasTextureCoords(0))
                    {
                        vert.m_uv[0] = mesh->mTextureCoords[0][v].x;
                        vert.m_uv[1] = mesh->mTextureCoords[0][v].y;
                    }
                    if (mesh->HasTangentsAndBitangents())
                    {
                        // 手性: sign(dot(cross(n, t), b))
                        float cx = mesh->mNormals[v].y * mesh->mTangents[v].z - mesh->mNormals[v].z * mesh->mTangents[v].y;
                        float cy = mesh->mNormals[v].z * mesh->mTangents[v].x - mesh->mNormals[v].x * mesh->mTangents[v].z;
                        float cz = mesh->mNormals[v].x * mesh->mTangents[v].y - mesh->mNormals[v].y * mesh->mTangents[v].x;
                        float dot = cx * mesh->mBitangents[v].x + cy * mesh->mBitangents[v].y + cz * mesh->mBitangents[v].z;
                        vert.m_tangent[0] = mesh->mTangents[v].x;
                        vert.m_tangent[1] = mesh->mTangents[v].y;
                        vert.m_tangent[2] = mesh->mTangents[v].z;
                        vert.m_tangent[3] = dot >= 0.0f ? 1.0f : -1.0f;
                    }

                    // AABB
                    for (int k = 0; k < 3; ++k)
                    {
                        if (first_vertex || vert.m_position[k] < model.m_aabb_min[k])
                            model.m_aabb_min[k] = vert.m_position[k];
                        if (first_vertex || vert.m_position[k] > model.m_aabb_max[k])
                            model.m_aabb_max[k] = vert.m_position[k];
                    }
                    first_vertex = false;
                }

                for (uint32_t f = 0; f < mesh->mNumFaces; ++f)
                {
                    aiFace &face = mesh->mFaces[f];
                    for (uint32_t idx = 0; idx < face.mNumIndices; ++idx)
                        mesh_data.m_indices.push_back(face.mIndices[idx]);
                }

                model.m_meshes.push_back(std::move(mesh_data));
            }

            GenesisPrint("[Asset] model loaded: %s (meshes=%zu materials=%zu)\n",
                         path.c_str(), model.m_meshes.size(), model.m_materials.size());

            m_models.push_back(std::move(model));
            return (uint32_t)m_models.size() - 1;
        }

        const ModelAsset *AssetManager::getModel(uint32_t id) const
        {
            if (id >= m_models.size())
                return nullptr;
            return &m_models[id];
        }

        void AssetManager::clear()
        {
            m_models.clear();
            m_textures.clear();
        }

    } // namespace resource
} // namespace Genesis
