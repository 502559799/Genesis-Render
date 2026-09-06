/*
 * @Descripttion: 渲染系统: 前向 PBR 渲染器
 *               职责对齐 Piccolo RenderSystem+RenderPipeline 的精简前向路径:
 *               场景 ECS 提取 -> per-frame/per-material/per-object 资源 -> PBR pass -> skybox
 * @version: v1.0
 */
#ifndef __GENESIS_RENDER_SYSTEM_H__
#define __GENESIS_RENDER_SYSTEM_H__

#include "ecs_layer/components.hpp"
#include "render_layer/shader_library.hpp"
#include "resource_layer/asset_manager.hpp"
#include "rhi_layer/rhi_device.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Genesis
{
    namespace render
    {
        //-----------------------------------------------------------
        // UBO 布局 (std140, 与 GLSL 严格一致)
        //-----------------------------------------------------------
        struct FrameUBOData
        {
            float m_view[16];
            float m_proj[16];
            float m_cam_pos_time[4];
            float m_light_dir_intensity[4];
            float m_light_color_exposure[4];
            int32_t m_flags[4]; // x: 光照模型(0=BlinnPhong 1=PBR) y: IBL开关 z: 天空盒开关
        };

        struct MaterialUBOData
        {
            float m_base_color[4];
            float m_factors[4];   // x: metallic y: roughness z: ao
            float m_emissive[4];
            int32_t m_tex_flags[4]; // x: hasAlbedo y: hasNormal z: hasMR w: hasAO
        };

        struct ObjectUBOData
        {
            float m_model[16];
            float m_normal[16];
        };

        struct RenderSettings
        {
            int m_lighting_model = 1; // 0: Blinn-Phong 1: PBR (Cook-Torrance)
            bool m_enable_ibl = true;
            bool m_enable_skybox = true;
            float m_exposure = 1.0f;
            int m_debug_channel = 0; // 调试用: 0 关闭
        };

        //-----------------------------------------------------------
        // GPU 侧模型
        //-----------------------------------------------------------
        struct GpuSubMesh
        {
            rhi::BufferHandle m_vbo;
            rhi::BufferHandle m_ibo;
            uint32_t m_index_count = 0;
            uint32_t m_material_index = 0;
        };

        struct GpuMaterial
        {
            MaterialUBOData m_data{};
            rhi::BufferHandle m_ubo;
            rhi::DescriptorSetHandle m_set;
        };

        struct GpuModel
        {
            std::vector<GpuSubMesh> m_meshes;
            std::vector<GpuMaterial> m_materials;
        };

        class IBLBaker;

        //-----------------------------------------------------------
        class RenderSystem
        {
        public:
            RenderSystem();  // 在 .cpp 定义 (unique_ptr 前向声明需要)
            ~RenderSystem();

            bool init(rhi::RHIDevice *device, resource::AssetManager *assets, const char *shaderDir);
            void shutdown();

            // 环境光照: 加载 equirect HDR 并烘焙 IBL (cubemap/irradiance/prefilter/brdfLUT)
            bool setEnvironmentHDR(const char *path);
            // 程序化渐变环境 (无 HDR 文件时使用)
            void setEnvironmentProcedural();

            // 上传模型到 GPU, 返回 gpu model id
            uint32_t uploadModel(uint32_t modelAssetId);

            // 渲染一帧场景 (交换链 pass)
            void render(ecs::World &world, ecs::Entity cameraEntity, const RenderSettings &settings, float time);

            rhi::RHIDevice *device() const { return m_device; }

            // 材质覆盖 (UI 调节)
            GpuMaterial *getMaterial(uint32_t gpuModelId, uint32_t materialIndex);

        private:
            void createDefaultResources();
            rhi::TextureHandle uploadTexture2D(const resource::TextureAsset *asset, bool mipmaps);
            void updateFrameSetIBL();

        private:
            rhi::RHIDevice *m_device = nullptr;
            resource::AssetManager *m_assets = nullptr;
            std::unique_ptr<ShaderLibrary> m_shader_lib;

            rhi::DescriptorSetLayoutHandle m_frame_layout;
            rhi::DescriptorSetLayoutHandle m_material_layout;
            rhi::DescriptorSetLayoutHandle m_object_layout;
            rhi::DescriptorSetLayoutHandle m_skybox_layout;

            rhi::PipelineHandle m_pbr_pipeline;
            rhi::PipelineHandle m_skybox_pipeline;

            rhi::BufferHandle m_frame_ubo;
            rhi::DescriptorSetHandle m_frame_set;

            rhi::TextureHandle m_white_tex;
            rhi::TextureHandle m_black_tex;
            rhi::TextureHandle m_normal_tex;
            rhi::SamplerHandle m_default_sampler;
            rhi::SamplerHandle m_cube_sampler;
            rhi::SamplerHandle m_lut_sampler;

            std::unique_ptr<IBLBaker> m_ibl_baker;
            rhi::TextureHandle m_env_cube;
            rhi::TextureHandle m_irradiance_cube;
            rhi::TextureHandle m_prefilter_cube;
            rhi::TextureHandle m_brdf_lut;
            rhi::DescriptorSetHandle m_skybox_set;

            rhi::BufferHandle m_skybox_vbo;
            rhi::BufferHandle m_skybox_ibo;
            uint32_t m_skybox_index_count = 0;

            std::vector<GpuModel> m_models;
            std::vector<rhi::TextureHandle> m_owned_textures;
            std::vector<rhi::BufferHandle> m_owned_buffers;
            std::vector<rhi::DescriptorSetHandle> m_owned_sets;

            // 每实体 object UBO (模型共享网格, 变换必须逐实体)
            struct ObjectResources
            {
                rhi::BufferHandle m_ubo;
                rhi::DescriptorSetHandle m_set;
            };
            std::unordered_map<uint32_t, ObjectResources> m_object_resources;
            ObjectResources &getObjectResources(uint32_t entity_id);
        };

    } // namespace render
} // namespace Genesis

#endif // __GENESIS_RENDER_SYSTEM_H__
