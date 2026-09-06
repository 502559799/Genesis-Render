/*
 * @Descripttion: 渲染系统实现
 */
#include "render_layer/render_system.hpp"
#include "render_layer/ibl_baker.hpp"

#include "foundation_layer/log.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Genesis
{
    namespace render
    {
        using namespace rhi;

        //-----------------------------------------------------------
        // 透视投影 (列主序, 手写以精确控制两种后端的 clip space)
        //   OpenGL: z ∈ [-1,1], y 向上
        //   Vulkan: z ∈ [0,1],  y 向下 (翻转 [1][1])
        //-----------------------------------------------------------
        static void perspectiveMatrix(float *out, float fovYRad, float aspect, float zNear, float zFar, bool vulkan)
        {
            memset(out, 0, sizeof(float) * 16);
            float f = 1.0f / tanf(fovYRad * 0.5f);
            out[0] = f / aspect;
            out[5] = vulkan ? -f : f;
            if (vulkan)
            {
                out[10] = zFar / (zNear - zFar);
                out[11] = -1.0f;
                out[14] = zNear * zFar / (zNear - zFar);
            }
            else
            {
                out[10] = (zFar + zNear) / (zNear - zFar);
                out[11] = -1.0f;
                out[14] = 2.0f * zNear * zFar / (zNear - zFar);
            }
        }

        static void mat4Mul(const float *a, const float *b, float *out)
        {
            // 列主序 a * b
            for (int c = 0; c < 4; ++c)
            {
                for (int r = 0; r < 4; ++r)
                {
                    float sum = 0;
                    for (int k = 0; k < 4; ++k)
                        sum += a[k * 4 + r] * b[c * 4 + k];
                    out[c * 4 + r] = sum;
                }
            }
        }

        // 法线矩阵: inverse(transpose(model)) — 对 4x4 求逆再转置
        static bool mat4Inverse(const float *m, float *out)
        {
            // 伴随矩阵法 (GLU 经典实现)
            float inv[16], det;
            inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
            inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
            inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
            inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
            inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
            inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
            inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
            inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
            inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
            inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
            inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
            inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
            inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
            inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
            inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
            inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

            det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
            if (det == 0)
                return false;
            det = 1.0f / det;
            for (int i = 0; i < 16; ++i)
                out[i] = inv[i] * det;
            return true;
        }

        static void mat4Transpose(const float *m, float *out)
        {
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    out[c * 4 + r] = m[r * 4 + c];
        }

        //-----------------------------------------------------------
        RenderSystem::RenderSystem() = default;
        RenderSystem::~RenderSystem() = default;

        bool RenderSystem::init(RHIDevice *device, resource::AssetManager *assets, const char *shaderDir)
        {
            m_device = device;
            m_assets = assets;
            m_shader_lib = std::make_unique<ShaderLibrary>(device, shaderDir);
            m_ibl_baker = std::make_unique<IBLBaker>(device, m_shader_lib.get());

            createDefaultResources();

            //---------------- 描述符布局 ----------------//
            {
                DescriptorSetLayoutCreateInfo info{};
                info.m_binding_count = 1;
                info.m_bindings[0] = {0, DescriptorType::UniformBuffer, ShaderStageFlags::All, "FrameUBO"};
                info.m_name = "frame_layout";
                m_frame_layout = m_device->createDescriptorSetLayout(info);
            }
            {
                DescriptorSetLayoutCreateInfo info{};
                info.m_binding_count = 9;
                info.m_bindings[0] = {0, DescriptorType::UniformBuffer, ShaderStageFlags::Fragment, "MaterialUBO"};
                info.m_bindings[1] = {1, DescriptorType::CombinedImageSampler, ShaderStageFlags::Fragment, "uAlbedoMap"};
                info.m_bindings[2] = {2, DescriptorType::CombinedImageSampler, ShaderStageFlags::Fragment, "uNormalMap"};
                info.m_bindings[3] = {3, DescriptorType::CombinedImageSampler, ShaderStageFlags::Fragment, "uMetallicRoughnessMap"};
                info.m_bindings[4] = {4, DescriptorType::CombinedImageSampler, ShaderStageFlags::Fragment, "uAOMap"};
                info.m_bindings[5] = {5, DescriptorType::CombinedImageSampler, ShaderStageFlags::Fragment, "uEmissiveMap"};
                info.m_bindings[6] = {6, DescriptorType::CombinedImageSampler, ShaderStageFlags::Fragment, "uIrradianceMap"};
                info.m_bindings[7] = {7, DescriptorType::CombinedImageSampler, ShaderStageFlags::Fragment, "uPrefilterMap"};
                info.m_bindings[8] = {8, DescriptorType::CombinedImageSampler, ShaderStageFlags::Fragment, "uBrdfLUT"};
                info.m_name = "material_layout";
                m_material_layout = m_device->createDescriptorSetLayout(info);
            }
            {
                DescriptorSetLayoutCreateInfo info{};
                info.m_binding_count = 1;
                info.m_bindings[0] = {0, DescriptorType::UniformBuffer, ShaderStageFlags::Vertex, "ObjectUBO"};
                info.m_name = "object_layout";
                m_object_layout = m_device->createDescriptorSetLayout(info);
            }
            {
                DescriptorSetLayoutCreateInfo info{};
                info.m_binding_count = 1;
                info.m_bindings[0] = {0, DescriptorType::CombinedImageSampler, ShaderStageFlags::Fragment, "uEnvMap"};
                info.m_name = "skybox_layout";
                m_skybox_layout = m_device->createDescriptorSetLayout(info);
            }

            //---------------- frame UBO ----------------//
            {
                BufferCreateInfo info{};
                info.m_usage = BufferUsage::Uniform;
                info.m_size = sizeof(FrameUBOData);
                info.m_name = "frame_ubo";
                m_frame_ubo = m_device->createBuffer(info);
                m_owned_buffers.push_back(m_frame_ubo);

                DescriptorSetCreateInfo set_info{};
                set_info.m_layout = m_frame_layout;
                set_info.m_write_count = 1;
                set_info.m_writes[0].m_binding = 0;
                set_info.m_writes[0].m_buffer = m_frame_ubo;
                set_info.m_writes[0].m_buffer_size = sizeof(FrameUBOData);
                m_frame_set = m_device->createDescriptorSet(set_info);
                m_owned_sets.push_back(m_frame_set);
            }

            //---------------- PBR 管线 ----------------//
            {
                ShaderPair shaders = m_shader_lib->load("pbr");
                if (!shaders.m_vs.isValid())
                {
                    GenesisPrint("[Render] failed to load pbr shaders\n");
                    return false;
                }

                PipelineCreateInfo info{};
                info.m_vs = shaders.m_vs;
                info.m_fs = shaders.m_fs;
                info.m_vertex_binding.m_stride = resource::kVertexStride;
                info.m_vertex_binding.m_attribute_count = 4;
                info.m_vertex_binding.m_attributes[0] = {0, VertexFormat::Float3, 0};
                info.m_vertex_binding.m_attributes[1] = {1, VertexFormat::Float3, 12};
                info.m_vertex_binding.m_attributes[2] = {2, VertexFormat::Float2, 24};
                info.m_vertex_binding.m_attributes[3] = {3, VertexFormat::Float4, 32};
                info.m_cull_mode = CullMode::Back;
                info.m_front_face = FrontFace::CounterClockwise;
                info.m_depth_test = true;
                info.m_depth_write = true;
                info.m_depth_compare = CompareOp::LessOrEqual;
                info.m_render_target_layout = m_device->getSwapchainLayout();
                info.m_set_layouts[0] = m_frame_layout;
                info.m_set_layouts[1] = m_material_layout;
                info.m_set_layouts[2] = m_object_layout;
                info.m_set_layout_count = 3;
                info.m_name = "pbr_pipeline";
                m_pbr_pipeline = m_device->createPipeline(info);
                if (!m_pbr_pipeline.isValid())
                    return false;
                m_device->destroyShader(shaders.m_vs);
                m_device->destroyShader(shaders.m_fs);
            }

            //---------------- 天空盒 ----------------//
            {
                ShaderPair shaders = m_shader_lib->load("skybox");
                if (!shaders.m_vs.isValid())
                {
                    GenesisPrint("[Render] failed to load skybox shaders\n");
                    return false;
                }

                PipelineCreateInfo info{};
                info.m_vs = shaders.m_vs;
                info.m_fs = shaders.m_fs;
                info.m_vertex_binding.m_stride = sizeof(float) * 3;
                info.m_vertex_binding.m_attribute_count = 1;
                info.m_vertex_binding.m_attributes[0] = {0, VertexFormat::Float3, 0};
                info.m_cull_mode = CullMode::None;
                info.m_depth_test = true;
                info.m_depth_write = false;
                info.m_depth_compare = CompareOp::LessOrEqual;
                info.m_render_target_layout = m_device->getSwapchainLayout();
                info.m_set_layouts[0] = m_frame_layout;
                info.m_set_layouts[1] = m_skybox_layout;
                info.m_set_layout_count = 2;
                info.m_name = "skybox_pipeline";
                m_skybox_pipeline = m_device->createPipeline(info);
                if (!m_skybox_pipeline.isValid())
                    return false;
                m_device->destroyShader(shaders.m_vs);
                m_device->destroyShader(shaders.m_fs);

                // 立方体顶点 (单位立方体 36 顶点, 位置即方向)
                static const float p = 1.0f;
                static const float cube_vertices[] = {
                    -p, -p, p, p, -p, p, p, p, p, -p, -p, p, p, p, p, -p, p, p, // +Z
                    p, -p, -p, -p, -p, -p, -p, p, -p, p, -p, -p, -p, p, -p, p, p, -p, // -Z
                    p, -p, p, p, -p, -p, p, p, -p, p, -p, p, p, p, -p, p, p, p, // +X
                    -p, -p, -p, -p, -p, p, -p, p, p, -p, -p, -p, -p, p, p, -p, p, -p, // -X
                    -p, p, p, p, p, p, p, p, -p, -p, p, p, p, p, -p, -p, p, -p, // +Y
                    -p, -p, -p, p, -p, -p, p, -p, p, -p, -p, -p, p, -p, p, -p, -p, p, // -Y
                };
                static const uint32_t cube_indices[] = {
                    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
                    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35};

                BufferCreateInfo vbo_info{};
                vbo_info.m_usage = BufferUsage::Vertex;
                vbo_info.m_size = sizeof(cube_vertices);
                vbo_info.m_data = cube_vertices;
                m_skybox_vbo = m_device->createBuffer(vbo_info);
                m_owned_buffers.push_back(m_skybox_vbo);
                m_skybox_index_count = 36;

                BufferCreateInfo ibo_info{};
                ibo_info.m_usage = BufferUsage::Index;
                ibo_info.m_size = sizeof(cube_indices);
                ibo_info.m_data = cube_indices;
                m_skybox_ibo = m_device->createBuffer(ibo_info);
                m_owned_buffers.push_back(m_skybox_ibo);
            }

            return true;
        }

        void RenderSystem::createDefaultResources()
        {
            // 默认贴图
            uint8_t white[] = {255, 255, 255, 255};
            uint8_t black[] = {0, 0, 0, 255};
            uint8_t normal[] = {128, 128, 255, 255};

            TextureCreateInfo info{};
            info.m_width = 1;
            info.m_height = 1;
            info.m_format = Format::RGBA8_UNorm;

            info.m_data = white;
            m_white_tex = m_device->createTexture(info);
            info.m_data = black;
            m_black_tex = m_device->createTexture(info);
            info.m_data = normal;
            m_normal_tex = m_device->createTexture(info);
            m_owned_textures.insert(m_owned_textures.end(), {m_white_tex, m_black_tex, m_normal_tex});

            // 采样器
            SamplerCreateInfo samp{};
            samp.m_min_filter = Filter::Linear;
            samp.m_mag_filter = Filter::Linear;
            samp.m_mipmap_filter = Filter::Linear;
            samp.m_address_u = AddressMode::Repeat;
            samp.m_address_v = AddressMode::Repeat;
            m_default_sampler = m_device->createSampler(samp);

            samp.m_address_u = AddressMode::ClampToEdge;
            samp.m_address_v = AddressMode::ClampToEdge;
            samp.m_address_w = AddressMode::ClampToEdge;
            m_cube_sampler = m_device->createSampler(samp);

            samp.m_mipmap_filter = Filter::Nearest;
            m_lut_sampler = m_device->createSampler(samp);
        }

        TextureHandle RenderSystem::uploadTexture2D(const resource::TextureAsset *asset, bool mipmaps)
        {
            if (!asset)
                return {};

            TextureCreateInfo info{};
            info.m_format = asset->m_format;
            info.m_width = asset->m_width;
            info.m_height = asset->m_height;
            if (mipmaps)
            {
                uint32_t max_dim = asset->m_width > asset->m_height ? asset->m_width : asset->m_height;
                uint32_t levels = 1;
                while (max_dim > 1)
                {
                    max_dim >>= 1;
                    ++levels;
                }
                info.m_mip_levels = levels;
            }
            info.m_data = asset->m_pixels.data();
            TextureHandle tex = m_device->createTexture(info);
            if (mipmaps && tex.isValid())
                m_device->generateMipmaps(tex);
            m_owned_textures.push_back(tex);
            return tex;
        }

        //-----------------------------------------------------------
        // 环境光照
        //-----------------------------------------------------------
        bool RenderSystem::setEnvironmentHDR(const char *path)
        {
            const resource::TextureAsset *hdr = m_assets->loadTexture(path);
            if (!hdr || !hdr->m_is_hdr)
            {
                GenesisPrint("[Render] environment HDR load failed: %s, fallback to procedural\n", path);
                setEnvironmentProcedural();
                return false;
            }

            IBLTextures ibl{};
            if (!m_ibl_baker->bake(hdr, ibl))
                return false;

            m_env_cube = ibl.m_env_cube;
            m_irradiance_cube = ibl.m_irradiance_cube;
            m_prefilter_cube = ibl.m_prefilter_cube;
            m_brdf_lut = ibl.m_brdf_lut;
            m_owned_textures.insert(m_owned_textures.end(), {m_env_cube, m_irradiance_cube, m_prefilter_cube, m_brdf_lut});

            updateFrameSetIBL();
            return true;
        }

        void RenderSystem::setEnvironmentProcedural()
        {
            // 程序化渐变天空 (地平线暖色 -> 天顶蓝色) 32x64 equirect RGBA16F
            const uint32_t w = 64, h = 32;
            std::vector<uint16_t> pixels((size_t)w * h * 4);
            for (uint32_t y = 0; y < h; ++y)
            {
                float v = (float)y / (float)(h - 1); // 0=底 1=顶
                float r = 0.35f + 0.45f * (1.0f - v);
                float g = 0.45f + 0.30f * (1.0f - v);
                float b = 0.95f - 0.35f * (1.0f - v);
                // 太阳方向亮斑
                for (uint32_t x = 0; x < w; ++x)
                {
                    float u = (float)x / (float)(w - 1);
                    float sun = expf(-30.0f * ((u - 0.25f) * (u - 0.25f) + (v - 0.55f) * (v - 0.55f)));
                    size_t idx = ((size_t)y * w + x) * 4;
                    pixels[idx + 0] = floatToHalf(r + 8.0f * sun);
                    pixels[idx + 1] = floatToHalf(g + 6.0f * sun);
                    pixels[idx + 2] = floatToHalf(b + 3.0f * sun);
                    pixels[idx + 3] = floatToHalf(1.0f);
                }
            }

            resource::TextureAsset asset;
            asset.m_width = w;
            asset.m_height = h;
            asset.m_is_hdr = true;
            asset.m_format = Format::RGBA16_Float;
            asset.m_pixels.assign((uint8_t *)pixels.data(), (uint8_t *)pixels.data() + pixels.size() * 2);

            IBLTextures ibl{};
            if (!m_ibl_baker->bake(&asset, ibl))
            {
                GenesisPrint("[Render] procedural IBL bake failed\n");
                return;
            }

            m_env_cube = ibl.m_env_cube;
            m_irradiance_cube = ibl.m_irradiance_cube;
            m_prefilter_cube = ibl.m_prefilter_cube;
            m_brdf_lut = ibl.m_brdf_lut;
            m_owned_textures.insert(m_owned_textures.end(), {m_env_cube, m_irradiance_cube, m_prefilter_cube, m_brdf_lut});

            updateFrameSetIBL();
        }

        void RenderSystem::updateFrameSetIBL()
        {
            // 天空盒采样集
            if (m_skybox_set.isValid())
            {
                m_device->destroyDescriptorSet(m_skybox_set);
                m_owned_sets.erase(std::remove(m_owned_sets.begin(), m_owned_sets.end(), m_skybox_set), m_owned_sets.end());
            }
            DescriptorSetCreateInfo set_info{};
            set_info.m_layout = m_skybox_layout;
            set_info.m_write_count = 1;
            set_info.m_writes[0].m_binding = 0;
            set_info.m_writes[0].m_texture = m_env_cube;
            set_info.m_writes[0].m_sampler = m_cube_sampler;
            m_skybox_set = m_device->createDescriptorSet(set_info);
            m_owned_sets.push_back(m_skybox_set);
        }

        //-----------------------------------------------------------
        // 模型上传
        //-----------------------------------------------------------
        uint32_t RenderSystem::uploadModel(uint32_t modelAssetId)
        {
            const resource::ModelAsset *model = m_assets->getModel(modelAssetId);
            if (!model)
                return 0xffffffff;

            GpuModel gpu_model;

            // 材质
            for (const auto &mat_data : model->m_materials)
            {
                GpuMaterial gpu_mat{};
                memcpy(gpu_mat.m_data.m_base_color, mat_data.m_base_color, sizeof(float) * 4);
                gpu_mat.m_data.m_factors[0] = mat_data.m_metallic;
                gpu_mat.m_data.m_factors[1] = mat_data.m_roughness;
                gpu_mat.m_data.m_factors[2] = 1.0f;
                memcpy(gpu_mat.m_data.m_emissive, mat_data.m_emissive, sizeof(float) * 3);

                TextureHandle albedo = m_white_tex;
                TextureHandle normal = m_normal_tex;
                TextureHandle mr = m_white_tex;
                TextureHandle ao = m_white_tex;
                TextureHandle emissive = m_black_tex;

                if (!mat_data.m_albedo_tex.empty())
                {
                    const resource::TextureAsset *asset = m_assets->loadTexture(mat_data.m_albedo_tex, true);
                    if (asset)
                    {
                        albedo = uploadTexture2D(asset, true);
                        gpu_mat.m_data.m_tex_flags[0] = 1;
                    }
                }
                if (!mat_data.m_normal_tex.empty())
                {
                    const resource::TextureAsset *asset = m_assets->loadTexture(mat_data.m_normal_tex);
                    if (asset)
                    {
                        normal = uploadTexture2D(asset, true);
                        gpu_mat.m_data.m_tex_flags[1] = 1;
                    }
                }
                if (!mat_data.m_metallic_roughness_tex.empty())
                {
                    const resource::TextureAsset *asset = m_assets->loadTexture(mat_data.m_metallic_roughness_tex);
                    if (asset)
                    {
                        mr = uploadTexture2D(asset, true);
                        gpu_mat.m_data.m_tex_flags[2] = 1;
                    }
                }
                if (!mat_data.m_ao_tex.empty())
                {
                    const resource::TextureAsset *asset = m_assets->loadTexture(mat_data.m_ao_tex);
                    if (asset)
                    {
                        ao = uploadTexture2D(asset, true);
                        gpu_mat.m_data.m_tex_flags[3] = 1;
                    }
                }

                BufferCreateInfo ubo_info{};
                ubo_info.m_usage = BufferUsage::Uniform;
                ubo_info.m_size = sizeof(MaterialUBOData);
                ubo_info.m_data = &gpu_mat.m_data;
                gpu_mat.m_ubo = m_device->createBuffer(ubo_info);
                m_owned_buffers.push_back(gpu_mat.m_ubo);

                DescriptorSetCreateInfo set_info{};
                set_info.m_layout = m_material_layout;
                set_info.m_write_count = 9;
                set_info.m_writes[0].m_binding = 0;
                set_info.m_writes[0].m_buffer = gpu_mat.m_ubo;
                set_info.m_writes[0].m_buffer_size = sizeof(MaterialUBOData);
                set_info.m_writes[1].m_binding = 1;
                set_info.m_writes[1].m_texture = albedo;
                set_info.m_writes[1].m_sampler = m_default_sampler;
                set_info.m_writes[2].m_binding = 2;
                set_info.m_writes[2].m_texture = normal;
                set_info.m_writes[2].m_sampler = m_default_sampler;
                set_info.m_writes[3].m_binding = 3;
                set_info.m_writes[3].m_texture = mr;
                set_info.m_writes[3].m_sampler = m_default_sampler;
                set_info.m_writes[4].m_binding = 4;
                set_info.m_writes[4].m_texture = ao;
                set_info.m_writes[4].m_sampler = m_default_sampler;
                set_info.m_writes[5].m_binding = 5;
                set_info.m_writes[5].m_texture = emissive;
                set_info.m_writes[5].m_sampler = m_default_sampler;
                set_info.m_writes[6].m_binding = 6;
                set_info.m_writes[6].m_texture = m_irradiance_cube.isValid() ? m_irradiance_cube : m_black_tex;
                set_info.m_writes[6].m_sampler = m_cube_sampler;
                set_info.m_writes[7].m_binding = 7;
                set_info.m_writes[7].m_texture = m_prefilter_cube.isValid() ? m_prefilter_cube : m_black_tex;
                set_info.m_writes[7].m_sampler = m_cube_sampler;
                set_info.m_writes[8].m_binding = 8;
                set_info.m_writes[8].m_texture = m_brdf_lut.isValid() ? m_brdf_lut : m_white_tex;
                set_info.m_writes[8].m_sampler = m_lut_sampler;
                gpu_mat.m_set = m_device->createDescriptorSet(set_info);
                m_owned_sets.push_back(gpu_mat.m_set);

                gpu_model.m_materials.push_back(gpu_mat);
            }

            // 网格
            for (const auto &mesh : model->m_meshes)
            {
                GpuSubMesh gpu_mesh{};
                gpu_mesh.m_index_count = (uint32_t)mesh.m_indices.size();
                gpu_mesh.m_material_index = mesh.m_material_index;

                BufferCreateInfo vbo_info{};
                vbo_info.m_usage = BufferUsage::Vertex;
                vbo_info.m_size = mesh.m_vertices.size() * sizeof(resource::Vertex);
                vbo_info.m_data = mesh.m_vertices.data();
                gpu_mesh.m_vbo = m_device->createBuffer(vbo_info);
                m_owned_buffers.push_back(gpu_mesh.m_vbo);

                BufferCreateInfo ibo_info{};
                ibo_info.m_usage = BufferUsage::Index;
                ibo_info.m_size = mesh.m_indices.size() * sizeof(uint32_t);
                ibo_info.m_data = mesh.m_indices.data();
                gpu_mesh.m_ibo = m_device->createBuffer(ibo_info);
                m_owned_buffers.push_back(gpu_mesh.m_ibo);

                gpu_model.m_meshes.push_back(gpu_mesh);
            }

            m_models.push_back(std::move(gpu_model));
            GenesisPrint("[Render] model uploaded to GPU (meshes=%zu materials=%zu)\n",
                         m_models.back().m_meshes.size(), m_models.back().m_materials.size());
            return (uint32_t)m_models.size() - 1;
        }

        //-----------------------------------------------------------
        RenderSystem::ObjectResources &RenderSystem::getObjectResources(uint32_t entity_id)
        {
            auto it = m_object_resources.find(entity_id);
            if (it != m_object_resources.end())
                return it->second;

            ObjectResources res{};
            BufferCreateInfo ubo_info{};
            ubo_info.m_usage = BufferUsage::Uniform;
            ubo_info.m_size = sizeof(ObjectUBOData);
            res.m_ubo = m_device->createBuffer(ubo_info);
            m_owned_buffers.push_back(res.m_ubo);

            DescriptorSetCreateInfo set_info{};
            set_info.m_layout = m_object_layout;
            set_info.m_write_count = 1;
            set_info.m_writes[0].m_binding = 0;
            set_info.m_writes[0].m_buffer = res.m_ubo;
            set_info.m_writes[0].m_buffer_size = sizeof(ObjectUBOData);
            res.m_set = m_device->createDescriptorSet(set_info);
            m_owned_sets.push_back(res.m_set);

            return m_object_resources.emplace(entity_id, res).first->second;
        }

        //-----------------------------------------------------------
        GpuMaterial *RenderSystem::getMaterial(uint32_t gpuModelId, uint32_t materialIndex)
        {
            if (gpuModelId >= m_models.size())
                return nullptr;
            GpuModel &model = m_models[gpuModelId];
            if (materialIndex >= model.m_materials.size())
                return nullptr;
            return &model.m_materials[materialIndex];
        }

        //-----------------------------------------------------------
        // 帧渲染
        //-----------------------------------------------------------
        void RenderSystem::render(ecs::World &world, ecs::Entity cameraEntity, const RenderSettings &settings, float time)
        {
            using namespace ecs;

            //---------------- 相机 ----------------//
            TransformComponent *cam_transform = world.getComponent<TransformComponent>(cameraEntity);
            CameraComponent *cam = world.getComponent<CameraComponent>(cameraEntity);
            if (!cam_transform || !cam)
                return;

            float *cam_world = (float *)cam_transform->m_world.raw;
            float cam_pos[3] = {cam_world[12], cam_world[13], cam_world[14]};
            // 相机朝向: -Z (列主序第三列取反)
            float cam_fwd[3] = {-cam_world[8], -cam_world[9], -cam_world[10]};
            float cam_up[3] = {cam_world[4], cam_world[5], cam_world[6]};
            float cam_center[3] = {cam_pos[0] + cam_fwd[0], cam_pos[1] + cam_fwd[1], cam_pos[2] + cam_fwd[2]};

            mat4s view = glms_lookat(*(vec3s *)cam_pos, *(vec3s *)cam_center, *(vec3s *)cam_up);

            float aspect = (float)m_device->getWidth() / (float)m_device->getHeight();
            float proj[16];
            perspectiveMatrix(proj, glm_rad(cam->m_fov_degrees), aspect, cam->m_near, cam->m_far, m_device->getBackend() == Backend::Vulkan);

            //---------------- 平行光 ----------------//
            float light_dir[3] = {0.3f, -1.0f, 0.4f};
            float light_color[3] = {1, 1, 1};
            float light_intensity = 3.0f;
            auto &light_pool = world.pool<DirectionalLightComponent>();
            if (light_pool.size() > 0)
            {
                DirectionalLightComponent &light = light_pool.m_dense[0];
                light_dir[0] = light.m_direction.raw[0];
                light_dir[1] = light.m_direction.raw[1];
                light_dir[2] = light.m_direction.raw[2];
                light_color[0] = light.m_color.raw[0];
                light_color[1] = light.m_color.raw[1];
                light_color[2] = light.m_color.raw[2];
                light_intensity = light.m_intensity;
            }

            //---------------- Frame UBO ----------------//
            FrameUBOData frame_data{};
            memcpy(frame_data.m_view, view.raw, sizeof(float) * 16);
            memcpy(frame_data.m_proj, proj, sizeof(float) * 16);
            frame_data.m_cam_pos_time[0] = cam_pos[0];
            frame_data.m_cam_pos_time[1] = cam_pos[1];
            frame_data.m_cam_pos_time[2] = cam_pos[2];
            frame_data.m_cam_pos_time[3] = time;
            frame_data.m_light_dir_intensity[0] = light_dir[0];
            frame_data.m_light_dir_intensity[1] = light_dir[1];
            frame_data.m_light_dir_intensity[2] = light_dir[2];
            frame_data.m_light_dir_intensity[3] = light_intensity;
            frame_data.m_light_color_exposure[0] = light_color[0];
            frame_data.m_light_color_exposure[1] = light_color[1];
            frame_data.m_light_color_exposure[2] = light_color[2];
            frame_data.m_light_color_exposure[3] = settings.m_exposure;
            frame_data.m_flags[0] = settings.m_lighting_model;
            frame_data.m_flags[1] = settings.m_enable_ibl ? 1 : 0;
            frame_data.m_flags[2] = settings.m_enable_skybox ? 1 : 0;
            frame_data.m_flags[3] = settings.m_debug_channel;
            m_device->updateBuffer(m_frame_ubo, &frame_data, sizeof(frame_data));

            //---------------- 主 pass ----------------//
            float clear_color[4] = {0.02f, 0.02f, 0.03f, 1.0f};
            m_device->beginRenderPass({}, clear_color, 1.0f, true);

            // PBR 物件
            m_device->bindPipeline(m_pbr_pipeline);
            m_device->bindDescriptorSet(0, m_frame_set);

            auto &renderer_pool = world.pool<MeshRendererComponent>();
            for (size_t i = 0; i < renderer_pool.m_dense.size(); ++i)
            {
                MeshRendererComponent &mr = renderer_pool.m_dense[i];
                if (!mr.m_visible || mr.m_model_id >= m_models.size())
                    continue;

                TransformComponent *tc = world.getComponent<TransformComponent>(renderer_pool.m_entities[i]);
                if (!tc)
                    continue;

                GpuModel &model = m_models[mr.m_model_id];

                ObjectUBOData object_data{};
                memcpy(object_data.m_model, tc->m_world.raw, sizeof(float) * 16);
                float inv[16], trans[16];
                if (mat4Inverse(object_data.m_model, inv))
                {
                    mat4Transpose(inv, trans);
                    memcpy(object_data.m_normal, trans, sizeof(float) * 16);
                }
                else
                {
                    memcpy(object_data.m_normal, object_data.m_model, sizeof(float) * 16);
                }

                // 每实体 object 资源
                ObjectResources &obj_res = getObjectResources(renderer_pool.m_entities[i]);
                m_device->updateBuffer(obj_res.m_ubo, &object_data, sizeof(object_data));

                for (GpuSubMesh &sub : model.m_meshes)
                {
                    uint32_t mat_idx = sub.m_material_index < model.m_materials.size() ? sub.m_material_index : 0;
                    m_device->bindDescriptorSet(1, model.m_materials[mat_idx].m_set);
                    m_device->bindDescriptorSet(2, obj_res.m_set);
                    m_device->bindVertexBuffer(sub.m_vbo);
                    m_device->bindIndexBuffer(sub.m_ibo);
                    m_device->drawIndexed(sub.m_index_count, 0, 0);
                }
            }

            // 天空盒
            if (settings.m_enable_skybox && m_skybox_set.isValid())
            {
                m_device->bindPipeline(m_skybox_pipeline);
                m_device->bindDescriptorSet(0, m_frame_set);
                m_device->bindDescriptorSet(1, m_skybox_set);
                m_device->bindVertexBuffer(m_skybox_vbo);
                m_device->bindIndexBuffer(m_skybox_ibo);
                m_device->drawIndexed(m_skybox_index_count, 0, 0);
            }

            m_device->endRenderPass();
        }

        //-----------------------------------------------------------
        void RenderSystem::shutdown()
        {
            for (auto set : m_owned_sets)
                m_device->destroyDescriptorSet(set);
            m_owned_sets.clear();
            for (auto buf : m_owned_buffers)
                m_device->destroyBuffer(buf);
            m_owned_buffers.clear();
            for (auto tex : m_owned_textures)
                m_device->destroyTexture(tex);
            m_owned_textures.clear();

            m_device->destroyPipeline(m_pbr_pipeline);
            m_device->destroyPipeline(m_skybox_pipeline);

            m_device->destroySampler(m_default_sampler);
            m_device->destroySampler(m_cube_sampler);
            m_device->destroySampler(m_lut_sampler);

            m_ibl_baker->destroy();

            m_device->destroyDescriptorSetLayout(m_frame_layout);
            m_device->destroyDescriptorSetLayout(m_material_layout);
            m_device->destroyDescriptorSetLayout(m_object_layout);
            m_device->destroyDescriptorSetLayout(m_skybox_layout);
        }

    } // namespace render
} // namespace Genesis
