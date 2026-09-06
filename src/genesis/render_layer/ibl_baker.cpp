/*
 * @Descripttion: IBL 烘焙器实现
 */
#include "render_layer/ibl_baker.hpp"

#include "foundation_layer/log.hpp"

#include <cstring>

namespace Genesis
{
    namespace render
    {
        using namespace rhi;

        static const uint32_t kEnvSize = 128;
        static const uint32_t kIrradianceSize = 32;
        static const uint32_t kPrefilterSize = 128;
        static const uint32_t kPrefilterMips = 5;
        static const uint32_t kBrdfLutSize = 256;

        // 立方体 6 面基底 (right/up/forward), 方向 = normalize(forward + right*x + up*y), x,y ∈ [-1,1] (NDC)
        // 由 lookAt(eye=0, target=f, up) 90° 透视推导 (与 LearnOpenGL 的 captureViews 一致)
        static const float kFaceBasis[6][3][4] = {
            {{0, 0, -1, 0}, {0, -1, 0, 0}, {1, 0, 0, 0}},   // +X
            {{0, 0, 1, 0}, {0, -1, 0, 0}, {-1, 0, 0, 0}},   // -X
            {{1, 0, 0, 0}, {0, 0, 1, 0}, {0, 1, 0, 0}},     // +Y
            {{1, 0, 0, 0}, {0, 0, -1, 0}, {0, -1, 0, 0}},   // -Y
            {{1, 0, 0, 0}, {0, -1, 0, 0}, {0, 0, 1, 0}},    // +Z
            {{-1, 0, 0, 0}, {0, -1, 0, 0}, {0, 0, -1, 0}},  // -Z
        };

        IBLBaker::IBLBaker(RHIDevice *device, ShaderLibrary *shaderLib)
            : m_device(device), m_shader_lib(shaderLib)
        {
        }

        bool IBLBaker::createPipelines()
        {
            // bake layout: binding0 UBO, binding1 sampler
            DescriptorSetLayoutCreateInfo layout_info{};
            layout_info.m_binding_count = 2;
            layout_info.m_bindings[0] = {0, DescriptorType::UniformBuffer, ShaderStageFlags::Vertex | ShaderStageFlags::Fragment, "BakeUBO"};
            layout_info.m_bindings[1] = {1, DescriptorType::CombinedImageSampler, ShaderStageFlags::Fragment, "uSource"};
            layout_info.m_name = "bake_layout";
            m_bake_layout = m_device->createDescriptorSetLayout(layout_info);

            RenderTargetLayout rt_layout{};
            rt_layout.m_color_count = 1;
            rt_layout.m_color_formats[0] = Format::RGBA16_Float;
            rt_layout.m_depth_format = Format::Undefined;

            struct PipelineDef
            {
                const char *name;
                PipelineHandle *out;
            };
            PipelineDef defs[] = {
                {"equirect_to_cube", &m_equirect_pipeline},
                {"irradiance", &m_irradiance_pipeline},
                {"prefilter", &m_prefilter_pipeline},
                {"brdf_lut", &m_brdf_pipeline},
            };

            for (auto &def : defs)
            {
                ShaderPair shaders = m_shader_lib->load(def.name);
                if (!shaders.m_vs.isValid() || !shaders.m_fs.isValid())
                    return false;
                m_shaders.push_back(shaders.m_vs);
                m_shaders.push_back(shaders.m_fs);

                PipelineCreateInfo info{};
                info.m_vs = shaders.m_vs;
                info.m_fs = shaders.m_fs;
                info.m_vertex_binding.m_stride = 0; // 全屏三角形, 无顶点缓冲
                info.m_vertex_binding.m_attribute_count = 0;
                info.m_cull_mode = CullMode::None;
                info.m_depth_test = false;
                info.m_depth_write = false;
                info.m_render_target_layout = rt_layout;
                info.m_set_layouts[0] = m_bake_layout;
                info.m_set_layout_count = 1;
                info.m_name = def.name;
                *def.out = m_device->createPipeline(info);
                if (!def.out->isValid())
                    return false;
            }

            SamplerCreateInfo sampler_info{};
            sampler_info.m_min_filter = Filter::Linear;
            sampler_info.m_mag_filter = Filter::Linear;
            sampler_info.m_mipmap_filter = Filter::Linear;
            sampler_info.m_address_u = AddressMode::ClampToEdge;
            sampler_info.m_address_v = AddressMode::ClampToEdge;
            sampler_info.m_address_w = AddressMode::ClampToEdge;
            m_bake_sampler = m_device->createSampler(sampler_info);

            return true;
        }

        void IBLBaker::bakeCubemap(TextureHandle target, PipelineHandle pipeline, TextureHandle source, SamplerHandle sampler, bool sourceIsCube, float roughness, uint32_t mipLevel)
        {
            (void)sourceIsCube;
            TextureInfo ti = m_device->getTextureInfo(target);
            uint32_t size = ti.m_width >> mipLevel;
            size = size ? size : 1;

            m_device->beginCommands();

            for (uint32_t face = 0; face < 6; ++face)
            {
                // 渲染目标 (当前面 + mip)
                RenderTargetCreateInfo rt_info{};
                rt_info.m_color_count = 1;
                rt_info.m_colors[0] = {target, mipLevel, face};
                rt_info.m_has_depth = false;
                RenderTargetHandle rt = m_device->createRenderTarget(rt_info);
                m_temp_targets.push_back(rt);

                // 面基底 UBO
                BakeUBOData ubo{};
                memcpy(ubo.m_right, kFaceBasis[face][0], sizeof(float) * 4);
                memcpy(ubo.m_up, kFaceBasis[face][1], sizeof(float) * 4);
                memcpy(ubo.m_forward, kFaceBasis[face][2], sizeof(float) * 4);
                ubo.m_params[0] = roughness;

                BufferCreateInfo buf_info{};
                buf_info.m_usage = BufferUsage::Uniform;
                buf_info.m_size = sizeof(BakeUBOData);
                buf_info.m_data = &ubo;
                BufferHandle ubo_buffer = m_device->createBuffer(buf_info);
                m_temp_buffers.push_back(ubo_buffer);

                DescriptorSetCreateInfo set_info{};
                set_info.m_layout = m_bake_layout;
                set_info.m_write_count = 2;
                set_info.m_writes[0].m_binding = 0;
                set_info.m_writes[0].m_buffer = ubo_buffer;
                set_info.m_writes[0].m_buffer_size = sizeof(BakeUBOData);
                set_info.m_writes[1].m_binding = 1;
                set_info.m_writes[1].m_texture = source;
                set_info.m_writes[1].m_sampler = sampler;
                DescriptorSetHandle set = m_device->createDescriptorSet(set_info);
                m_temp_sets.push_back(set);

                float clear[4] = {0, 0, 0, 1};
                m_device->beginRenderPass(rt, clear, 1.0f, true);
                m_device->bindPipeline(pipeline);
                m_device->bindDescriptorSet(0, set);
                m_device->draw(3, 0);
                m_device->endRenderPass();
            }

            m_device->endCommands();
        }

        void IBLBaker::bakeBrdfLut(TextureHandle target)
        {
            RenderTargetCreateInfo rt_info{};
            rt_info.m_color_count = 1;
            rt_info.m_colors[0] = {target, 0, 0};
            rt_info.m_has_depth = false;
            RenderTargetHandle rt = m_device->createRenderTarget(rt_info);
            m_temp_targets.push_back(rt);

            BakeUBOData ubo{};
            BufferCreateInfo buf_info{};
            buf_info.m_usage = BufferUsage::Uniform;
            buf_info.m_size = sizeof(BakeUBOData);
            buf_info.m_data = &ubo;
            BufferHandle ubo_buffer = m_device->createBuffer(buf_info);
            m_temp_buffers.push_back(ubo_buffer);

            DescriptorSetCreateInfo set_info{};
            set_info.m_layout = m_bake_layout;
            set_info.m_write_count = 1;
            set_info.m_writes[0].m_binding = 0;
            set_info.m_writes[0].m_buffer = ubo_buffer;
            set_info.m_writes[0].m_buffer_size = sizeof(BakeUBOData);
            DescriptorSetHandle set = m_device->createDescriptorSet(set_info);
            m_temp_sets.push_back(set);

            float clear[4] = {0, 0, 0, 1};
            m_device->beginCommands();
            m_device->beginRenderPass(rt, clear, 1.0f, true);
            m_device->bindPipeline(m_brdf_pipeline);
            m_device->bindDescriptorSet(0, set);
            m_device->draw(3, 0);
            m_device->endRenderPass();
            m_device->endCommands();
        }

        bool IBLBaker::bake(const resource::TextureAsset *equirect, IBLTextures &out)
        {
            if (!createPipelines())
            {
                GenesisPrint("[IBL] pipeline creation failed\n");
                return false;
            }

            // 1. equirect 纹理上传
            TextureCreateInfo eq_info{};
            eq_info.m_type = TextureType::Texture2D;
            eq_info.m_format = Format::RGBA16_Float;
            eq_info.m_width = equirect->m_width;
            eq_info.m_height = equirect->m_height;
            eq_info.m_mip_levels = 1;
            eq_info.m_data = equirect->m_pixels.data();
            eq_info.m_name = "equirect_hdr";
            TextureHandle eq_tex = m_device->createTexture(eq_info);

            // 2. 目标纹理
            TextureCreateInfo cube_info{};
            cube_info.m_type = TextureType::TextureCube;
            cube_info.m_format = Format::RGBA16_Float;
            cube_info.m_array_layers = 6;
            cube_info.m_render_target = true;

            cube_info.m_width = kEnvSize;
            cube_info.m_height = kEnvSize;
            cube_info.m_mip_levels = 1;
            cube_info.m_name = "env_cube";
            out.m_env_cube = m_device->createTexture(cube_info);

            cube_info.m_width = kIrradianceSize;
            cube_info.m_height = kIrradianceSize;
            cube_info.m_name = "irradiance_cube";
            out.m_irradiance_cube = m_device->createTexture(cube_info);

            cube_info.m_width = kPrefilterSize;
            cube_info.m_height = kPrefilterSize;
            cube_info.m_mip_levels = kPrefilterMips;
            cube_info.m_name = "prefilter_cube";
            out.m_prefilter_cube = m_device->createTexture(cube_info);

            TextureCreateInfo lut_info{};
            lut_info.m_type = TextureType::Texture2D;
            lut_info.m_format = Format::RG16_Float;
            lut_info.m_width = kBrdfLutSize;
            lut_info.m_height = kBrdfLutSize;
            lut_info.m_render_target = true;
            lut_info.m_name = "brdf_lut";
            out.m_brdf_lut = m_device->createTexture(lut_info);

            // 3. 烘焙
            GenesisPrint("[IBL] baking env cubemap...\n");
            bakeCubemap(out.m_env_cube, m_equirect_pipeline, eq_tex, m_bake_sampler, false, 0.0f, 0);

            GenesisPrint("[IBL] baking diffuse irradiance...\n");
            bakeCubemap(out.m_irradiance_cube, m_irradiance_pipeline, out.m_env_cube, m_bake_sampler, true, 0.0f, 0);

            GenesisPrint("[IBL] baking prefiltered env map...\n");
            for (uint32_t mip = 0; mip < kPrefilterMips; ++mip)
            {
                float roughness = (float)mip / (float)(kPrefilterMips - 1);
                bakeCubemap(out.m_prefilter_cube, m_prefilter_pipeline, out.m_env_cube, m_bake_sampler, true, roughness, mip);
            }

            GenesisPrint("[IBL] baking BRDF LUT...\n");
            bakeBrdfLut(out.m_brdf_lut);

            // 4. 清理中间资源
            for (auto rt : m_temp_targets)
                m_device->destroyRenderTarget(rt);
            m_temp_targets.clear();
            for (auto buf : m_temp_buffers)
                m_device->destroyBuffer(buf);
            m_temp_buffers.clear();
            for (auto set : m_temp_sets)
                m_device->destroyDescriptorSet(set);
            m_temp_sets.clear();
            m_device->destroyTexture(eq_tex);

            GenesisPrint("[IBL] bake complete\n");
            return true;
        }

        void IBLBaker::destroy()
        {
            for (auto s : m_shaders)
                m_device->destroyShader(s);
            m_shaders.clear();
            m_device->destroyPipeline(m_equirect_pipeline);
            m_device->destroyPipeline(m_irradiance_pipeline);
            m_device->destroyPipeline(m_prefilter_pipeline);
            m_device->destroyPipeline(m_brdf_pipeline);
            m_device->destroySampler(m_bake_sampler);
            m_device->destroyDescriptorSetLayout(m_bake_layout);
        }

    } // namespace render
} // namespace Genesis
