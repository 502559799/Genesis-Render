/*
 * @Descripttion: IBL 烘焙器: equirect HDR -> envCubemap / diffuseIrradiance / prefilteredEnv / BRDF LUT
 *               全部通过 RHI 离屏渲染完成, 双后端一致
 * @version: v1.0
 */
#ifndef __GENESIS_IBL_BAKER_H__
#define __GENESIS_IBL_BAKER_H__

#include "render_layer/shader_library.hpp"
#include "resource_layer/asset_manager.hpp"
#include "rhi_layer/rhi_device.hpp"

#include <memory>

namespace Genesis
{
    namespace render
    {
        struct IBLTextures
        {
            rhi::TextureHandle m_env_cube;        // 环境 cubemap (128)
            rhi::TextureHandle m_irradiance_cube; // 漫反射辐照度 (32)
            rhi::TextureHandle m_prefilter_cube;  // 镜面预滤波 (128, 5 mips)
            rhi::TextureHandle m_brdf_lut;        // BRDF 积分 LUT (256, RG16F)
        };

        // 烘焙用 UBO (std140)
        struct BakeUBOData
        {
            float m_right[4];
            float m_up[4];
            float m_forward[4];
            float m_params[4]; // x: roughness
        };

        class IBLBaker
        {
        public:
            IBLBaker(rhi::RHIDevice *device, ShaderLibrary *shaderLib);
            ~IBLBaker() = default;

            // 从 equirect HDR 纹理资产烘焙全部 IBL 贴图
            bool bake(const resource::TextureAsset *equirect, IBLTextures &out);
            void destroy();

        private:
            bool createPipelines();
            void bakeCubemap(rhi::TextureHandle target, rhi::PipelineHandle pipeline,
                             rhi::TextureHandle source, rhi::SamplerHandle sampler, bool sourceIsCube,
                             float roughness, uint32_t mipLevel);
            void bakeBrdfLut(rhi::TextureHandle target);

        private:
            rhi::RHIDevice *m_device;
            ShaderLibrary *m_shader_lib;

            rhi::DescriptorSetLayoutHandle m_bake_layout; // binding0: BakeUBO, binding1: source sampler
            rhi::PipelineHandle m_equirect_pipeline;
            rhi::PipelineHandle m_irradiance_pipeline;
            rhi::PipelineHandle m_prefilter_pipeline;
            rhi::PipelineHandle m_brdf_pipeline;

            rhi::SamplerHandle m_bake_sampler;

            std::vector<rhi::RenderTargetHandle> m_temp_targets;
            std::vector<rhi::BufferHandle> m_temp_buffers;
            std::vector<rhi::DescriptorSetHandle> m_temp_sets;
            std::vector<rhi::ShaderHandle> m_shaders;
        };

    } // namespace render
} // namespace Genesis

#endif // __GENESIS_IBL_BAKER_H__
