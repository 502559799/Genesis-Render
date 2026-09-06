/*
 * @Descripttion: 资源管理: 模型加载 (assimp: fbx/gltf/obj 等) 与纹理加载 (stb_image)
 *               对齐 Piccolo asset_manager + render_resource_base 的职责
 * @version: v1.0
 */
#ifndef __GENESIS_ASSET_MANAGER_H__
#define __GENESIS_ASSET_MANAGER_H__

#include "rhi_layer/rhi_types.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace Genesis
{
    namespace resource
    {
        // 交错顶点格式: pos(3) normal(3) uv(2) tangent(4) = 48 bytes
        struct Vertex
        {
            float m_position[3];
            float m_normal[3];
            float m_uv[2];
            float m_tangent[4];
        };
        static const uint32_t kVertexStride = sizeof(Vertex);

        struct TextureAsset
        {
            uint32_t m_width = 0;
            uint32_t m_height = 0;
            bool m_is_hdr = false;
            std::vector<uint8_t> m_pixels; // RGBA8 或 RGBA16F(half)
            rhi::Format m_format = rhi::Format::RGBA8_UNorm;
        };

        struct MaterialData
        {
            std::string m_name;
            float m_base_color[4] = {1, 1, 1, 1};
            float m_metallic = 0.0f;
            float m_roughness = 0.5f;
            float m_emissive[3] = {0, 0, 0};

            std::string m_albedo_tex;
            std::string m_normal_tex;
            std::string m_metallic_roughness_tex;
            std::string m_ao_tex;
            std::string m_emissive_tex;
        };

        struct MeshData
        {
            std::vector<Vertex> m_vertices;
            std::vector<uint32_t> m_indices;
            uint32_t m_material_index = 0;
        };

        struct ModelAsset
        {
            std::string m_name;
            std::vector<MeshData> m_meshes;
            std::vector<MaterialData> m_materials;
            // 包围盒
            float m_aabb_min[3] = {0, 0, 0};
            float m_aabb_max[3] = {0, 0, 0};
        };

        class AssetManager
        {
        public:
            AssetManager() = default;
            ~AssetManager() = default;

            // 加载模型 (fbx/gltf/glb/obj/dae..., 依赖 assimp); 返回模型 id, 失败返回 0xffffffff
            uint32_t loadModel(const std::string &path);
            const ModelAsset *getModel(uint32_t id) const;

            // 加载纹理 (带缓存); HDR (.hdr) 输出 RGBA16F
            const TextureAsset *loadTexture(const std::string &path, bool srgb = false);

            // 由 CPU 侧 RGBA8 数据创建纹理资产 (用于程序化贴图)
            const TextureAsset *createTextureFromPixels(const std::string &name, uint32_t w, uint32_t h, const uint8_t *rgba);

            void clear();

        private:
            std::vector<ModelAsset> m_models;
            std::unordered_map<std::string, TextureAsset> m_textures;
        };

    } // namespace resource
} // namespace Genesis

#endif // __GENESIS_ASSET_MANAGER_H__
