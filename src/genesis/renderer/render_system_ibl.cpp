/*
 * Genesis Render - image based lighting baker
 *
 * Pipeline (all raster based, so it runs identically on the OpenGL 4.1 and
 * Vulkan backends - no compute shaders required):
 *
 *   equirect HDR (.hdr)
 *        |  ibl/equirect_to_cube.frag    (6 draws, one per face)
 *        v
 *   environment cubemap  + full mip chain (for prefilter LOD sampling)
 *        |                                   |
 *        | ibl/irradiance.frag               | ibl/prefilter.frag
 *        v                                   v
 *   irradiance cubemap               prefiltered specular cubemap
 *                                            |
 *                    ibl/brdf_lut.frag  -->  BRDF integration LUT (2D)
 *
 * The three results are exactly the textures consumed by the split-sum
 * approximation in scene/pbr_mesh.frag.
 */
#include "renderer/render_internal.hpp"
#include "renderer/render_system.hpp"
#include "foundation_layer/genesis_log.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Genesis
{
    namespace Render
    {
        namespace
        {
            //--------------------------------------------------------------
            // IEEE-754 half float conversion (needed because the environment
            // maps are stored as RGBA16F, which is filterable in core OpenGL
            // 4.1 while RGBA32F is not).
            //--------------------------------------------------------------
            uint16_t halfFromFloat(float value)
            {
                uint32_t f;
                std::memcpy(&f, &value, sizeof(f));
                uint32_t sign = (f >> 16) & 0x8000u;
                int32_t exponent = (int32_t)((f >> 23) & 0xFF) - 127 + 15;
                uint32_t mantissa = f & 0x7FFFFFu;

                if (((f >> 23) & 0xFF) == 0xFF)
                    return (uint16_t)(sign | 0x7C00u | (mantissa ? 0x200u : 0u)); // inf / nan
                if (exponent <= 0)
                {
                    if (exponent < -10) return (uint16_t)sign;
                    mantissa |= 0x800000u;
                    uint32_t shift = (uint32_t)(14 - exponent);
                    uint16_t half_mantissa = (uint16_t)(mantissa >> shift);
                    if ((mantissa >> (shift - 1)) & 1u) ++half_mantissa;
                    return (uint16_t)(sign | half_mantissa);
                }
                if (exponent >= 31) return (uint16_t)(sign | 0x7C00u); // clamp to inf
                return (uint16_t)(sign | ((uint32_t)exponent << 10) | (mantissa >> 13));
            }

            void floatRGBAToHalf(const float *src, size_t pixel_count, std::vector<uint16_t> &dst)
            {
                dst.resize(pixel_count * 4);
                for (size_t i = 0; i < pixel_count; ++i)
                {
                    dst[i * 4 + 0] = halfFromFloat(src[i * 4 + 0]);
                    dst[i * 4 + 1] = halfFromFloat(src[i * 4 + 1]);
                    dst[i * 4 + 2] = halfFromFloat(src[i * 4 + 2]);
                    dst[i * 4 + 3] = halfFromFloat(src[i * 4 + 3]);
                }
            }

            //--------------------------------------------------------------
            void transitionToShaderRead(RHI::RHICommandBuffer *cmd, RHI::RHITexture *texture)
            {
                RHI::TextureBarrier barrier{};
                barrier.texture = texture;
                barrier.old_layout = texture->currentLayout();
                barrier.new_layout = RHI::TextureLayout::ShaderReadOnlyOptimal;
                barrier.base_mip = 0;
                barrier.mip_count = ~0u;
                barrier.base_layer = 0;
                barrier.layer_count = ~0u;
                cmd->pipelineBarrier(&barrier, 1);
            }

            void transitionMipToTransferDst(RHI::RHICommandBuffer *cmd, RHI::RHITexture *texture, uint32_t mip)
            {
                RHI::TextureBarrier barrier{};
                barrier.texture = texture;
                barrier.old_layout = texture->currentLayout();
                barrier.new_layout = RHI::TextureLayout::TransferDstOptimal;
                barrier.base_mip = mip;
                barrier.mip_count = 1;
                barrier.base_layer = 0;
                barrier.layer_count = ~0u;
                cmd->pipelineBarrier(&barrier, 1);
            }

            //--------------------------------------------------------------
            // Procedural sky used when no HDR map is supplied.  Matches the
            // fallback in scene/skybox.frag so the two agree visually.
            //--------------------------------------------------------------
            void generateProceduralEquirect(ImageData &out, uint32_t width, uint32_t height)
            {
                out.width = width;
                out.height = height;
                out.channels = 4;
                out.is_hdr = true;
                out.name = "procedural_sky";
                out.hdr.resize((size_t)width * height * 4);

                const Math::Vec3 sky_top(0.10f, 0.28f, 0.75f);
                const Math::Vec3 sky_horizon(0.75f, 0.80f, 0.90f);
                const Math::Vec3 ground(0.06f, 0.055f, 0.05f);
                const Math::Vec3 sun_dir = Math::normalize(Math::Vec3(0.4f, 0.6f, 0.3f));

                for (uint32_t y = 0; y < height; ++y)
                {
                    float v = ((float)y + 0.5f) / (float)height;
                    for (uint32_t x = 0; x < width; ++x)
                    {
                        float u = ((float)x + 0.5f) / (float)width;
                        // Same convention as gsEquirectUVToDirection in the shaders.
                        float phi = (u - 0.5f) * Math::TWO_PI;
                        float theta = v * Math::PI;
                        Math::Vec3 dir(std::cos(phi) * std::sin(theta), std::cos(theta), std::sin(phi) * std::sin(theta));

                        Math::Vec3 color;
                        if (dir.y >= 0.0f)
                        {
                            float t = std::pow(dir.y, 0.55f);
                            color = Math::lerp(sky_horizon, sky_top, t);
                        }
                        else
                        {
                            color = Math::lerp(sky_horizon, ground, Math::clampf(-dir.y * 4.0f, 0.0f, 1.0f));
                        }
                        float cos_angle = Math::clampf(Math::dot(dir, sun_dir), 0.0f, 1.0f);
                        color += Math::Vec3(1.0f, 0.92f, 0.75f) * std::pow(cos_angle, 900.0f) * 260.0f;
                        color += Math::Vec3(0.35f, 0.30f, 0.22f) * std::pow(cos_angle, 12.0f) * 0.45f;

                        size_t idx = ((size_t)y * width + x) * 4;
                        out.hdr[idx + 0] = color.x;
                        out.hdr[idx + 1] = color.y;
                        out.hdr[idx + 2] = color.z;
                        out.hdr[idx + 3] = 1.0f;
                    }
                }
            }
        } // namespace

        //==================================================================
        RHI::RHITexture *RenderSystem::createCubeTexture(uint32_t size, uint32_t mips, const std::string &name,
                                                         RHI::PixelFormat &format_inout)
        {
            RHI::PixelFormat candidates[3] = {format_inout, RHI::PixelFormat::RGBA32_SFLOAT, RHI::PixelFormat::RGBA8_UNORM};
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                RHI::PixelFormat format = candidates[attempt];
                if (attempt > 0 && format == candidates[attempt - 1]) continue;

                RHI::TextureDesc desc;
                desc.type = RHI::TextureType::TexCube;
                desc.format = format;
                desc.width = size;
                desc.height = size;
                desc.array_layers = 1;
                desc.mip_levels = mips;
                desc.generate_mipmaps = mips > 1;
                desc.usage = (uint32_t)RHI::TextureUsageFlagBits::RenderTarget |
                             (uint32_t)RHI::TextureUsageFlagBits::Sampled |
                             (uint32_t)RHI::TextureUsageFlagBits::TransferSrc |
                             (uint32_t)RHI::TextureUsageFlagBits::TransferDst;
                desc.name = name;
                RHI::RHITexture *texture = device_->createTexture(desc);
                if (!texture) continue;

                // Validate that the driver can actually render into it.
                RHI::RHIFramebuffer *probe = faceFramebuffer(texture, 0, 0, format, name + ".probe");
                if (probe)
                {
                    format_inout = format;
                    return texture;
                }
                GEN_LOG_WARN("[IBL] cannot render into %s with pixel format %u - trying another", name.c_str(), (unsigned)format);
                device_->destroyTexture(texture);
            }
            return nullptr;
        }

        //==================================================================
        bool RenderSystem::bakeIBLFromImage(const ImageData &image, const IblBakeSettings &cfg)
        {
            if (!initialized_) return false;
            if (image.width == 0 || image.height == 0) return false;

            releaseIBL();
            if (!createIBLPipelines(RHI::PixelFormat::RGBA16_SFLOAT, RHI::PixelFormat::RG16_SFLOAT)) return false;

            RHI::PixelFormat cube_format = RHI::PixelFormat::RGBA16_SFLOAT;

            //--------------------------------------------------------------
            // 1. equirectangular source texture
            //--------------------------------------------------------------
            {
                RHI::TextureDesc desc;
                desc.type = RHI::TextureType::Tex2D;
                desc.format = RHI::PixelFormat::RGBA16_SFLOAT;
                desc.width = image.width;
                desc.height = image.height;
                desc.mip_levels = 1;
                desc.usage = (uint32_t)RHI::TextureUsageFlagBits::Sampled |
                             (uint32_t)RHI::TextureUsageFlagBits::TransferDst;
                desc.name = "ibl.equirect";
                env_equirect_ = device_->createTexture(desc);
                if (!env_equirect_)
                {
                    GEN_LOG_ERROR("[IBL] failed to create the equirect texture");
                    return false;
                }
                std::vector<uint16_t> half_data;
                floatRGBAToHalf(image.hdr.data(), (size_t)image.width * image.height, half_data);
                device_->writeTexture(env_equirect_, half_data.data(), half_data.size() * sizeof(uint16_t),
                                      image.width, image.height, 0, 0, 1);
            }

            //--------------------------------------------------------------
            // 2. environment cubemap (with mip chain for prefilter LODs)
            //--------------------------------------------------------------
            const uint32_t cube_size = std::max(8u, cfg.cubemap_size);
            const uint32_t cube_mips = detail::mipLevelsFor(cube_size);
            env_cubemap_ = createCubeTexture(cube_size, cube_mips, "ibl.env_cube", cube_format);
            if (!env_cubemap_)
            {
                GEN_LOG_ERROR("[IBL] failed to create the environment cubemap");
                releaseIBL();
                return false;
            }

            const uint32_t irradiance_size = std::max(4u, cfg.irradiance_size);
            RHI::PixelFormat irradiance_format = cube_format;
            irradiance_cubemap_ = createCubeTexture(irradiance_size, 1, "ibl.irradiance", irradiance_format);

            const uint32_t prefilter_size = std::max(8u, cfg.prefiltered_size);
            const uint32_t prefilter_mips = detail::mipLevelsFor(prefilter_size);
            RHI::PixelFormat prefilter_format = cube_format;
            prefiltered_cubemap_ = createCubeTexture(prefilter_size, prefilter_mips, "ibl.prefiltered", prefilter_format);
            prefilter_mip_levels_ = prefilter_mips;

            const uint32_t lut_size = std::max(8u, cfg.brdf_lut_size);
            RHI::PixelFormat lut_format = RHI::PixelFormat::RG16_SFLOAT;
            {
                RHI::TextureDesc desc;
                desc.type = RHI::TextureType::Tex2D;
                desc.format = lut_format;
                desc.width = lut_size;
                desc.height = lut_size;
                desc.mip_levels = 1;
                desc.usage = (uint32_t)RHI::TextureUsageFlagBits::RenderTarget |
                             (uint32_t)RHI::TextureUsageFlagBits::Sampled |
                             (uint32_t)RHI::TextureUsageFlagBits::TransferDst;
                desc.name = "ibl.brdf_lut";
                brdf_lut_ = device_->createTexture(desc);
                RHI::RHIFramebuffer *probe = brdf_lut_ ? faceFramebuffer(brdf_lut_, 0, 0, lut_format, "ibl.brdf_lut.probe") : nullptr;
                if (!probe && brdf_lut_)
                {
                    // RG16F is not renderable everywhere; fall back to RGBA16F.
                    device_->destroyTexture(brdf_lut_);
                    brdf_lut_ = nullptr;
                    lut_format = RHI::PixelFormat::RGBA16_SFLOAT;
                    desc.format = lut_format;
                    desc.name = "ibl.brdf_lut.rgba16f";
                    brdf_lut_ = device_->createTexture(desc);
                    probe = brdf_lut_ ? faceFramebuffer(brdf_lut_, 0, 0, lut_format, "ibl.brdf_lut.probe2") : nullptr;
                }
                if (!brdf_lut_ || !probe)
                {
                    GEN_LOG_ERROR("[IBL] failed to create the BRDF LUT target");
                    releaseIBL();
                    return false;
                }
            }
            if (!irradiance_cubemap_ || !prefiltered_cubemap_)
            {
                GEN_LOG_ERROR("[IBL] failed to create the convolution targets");
                releaseIBL();
                return false;
            }

            // The bake pipelines were created for RGBA16F; if the driver forced
            // a different format we must recreate them with a matching pass.
            if (cube_format != RHI::PixelFormat::RGBA16_SFLOAT || lut_format != RHI::PixelFormat::RG16_SFLOAT)
            {
                for (RHI::RHIPipeline **p : {&ibl_equirect_pipeline_, &ibl_irradiance_pipeline_,
                                             &ibl_prefilter_pipeline_, &ibl_brdf_pipeline_})
                {
                    if (*p) { device_->destroyPipeline(*p); *p = nullptr; }
                }
                if (!createIBLPipelines(cube_format, lut_format))
                {
                    releaseIBL();
                    return false;
                }
            }

            float rotation = Math::toRadians(settings_.env_rotation_degrees);

            //--------------------------------------------------------------
            // Helper: run one bake pass over N targets
            //--------------------------------------------------------------
            struct BakeTarget
            {
                RHI::RHITexture *texture;
                uint32_t mip;
                uint32_t layer;
                uint32_t size;
                RHI::PixelFormat format;
                float roughness;
            };
            auto runBake = [&](RHI::RHIPipeline *pipeline, const std::vector<BakeTarget> &targets, bool source_is_cube)
            {
                RHI::RHICommandBuffer *cmd = device_->createCommandBuffer(RHI::CommandBufferUsage::OneTimeSubmit);
                if (!cmd) return false;
                cmd->begin(RHI::CommandBufferUsage::OneTimeSubmit);

                // Make sure the source is in a shader readable layout.
                if (source_is_cube) transitionToShaderRead(cmd, env_cubemap_);
                else transitionToShaderRead(cmd, env_equirect_);

                for (const BakeTarget &t : targets)
                {
                    RHI::RHIFramebuffer *fb = faceFramebuffer(t.texture, t.mip, t.layer, t.format, "ibl.face");
                    if (!fb)
                    {
                        GEN_LOG_ERROR("[IBL] framebuffer creation failed for a bake target");
                        cmd->end();
                        device_->destroyCommandBuffer(cmd);
                        return false;
                    }

                    IblParamsUBO params;
                    params.a = GpuVec4((float)t.layer, t.roughness, (float)t.mip,
                                       (float)(t.roughness > 0.0f ? cfg.prefilter_samples : cfg.brdf_samples));
                    params.b = GpuVec4(std::cos(rotation), std::sin(rotation), 0.0f, 0.0f);
                    device_->writeBuffer(ibl_ubo_, &params, sizeof(params), 0);

                    RHI::DescriptorBufferInfo buf{ibl_ubo_, 0, sizeof(IblParamsUBO)};
                    RHI::DescriptorImageInfo equirect{env_equirect_, ibl_sampler_, RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1};
                    RHI::DescriptorImageInfo cube{env_cubemap_, ibl_sampler_, RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 6};
                    RHI::WriteDescriptorSet writes[3];
                    writes[0] = RHI::WriteDescriptorSet{0, 0, 1, RHI::ResourceType::UniformBuffer, &buf, nullptr};
                    writes[1] = RHI::WriteDescriptorSet{1, 0, 1, RHI::ResourceType::CombinedImageSampler, nullptr, &equirect};
                    writes[2] = RHI::WriteDescriptorSet{2, 0, 1, RHI::ResourceType::CombinedImageSampler, nullptr, &cube};
                    ibl_set_->write(writes, 3);

                    RHI::RenderPassBeginInfo begin;
                    begin.clear_count = 1;
                    begin.clears[0].color[0] = begin.clears[0].color[1] = begin.clears[0].color[2] = 0.0f;
                    begin.clears[0].color[3] = 1.0f;
                    cmd->beginRenderPass(fb->renderPass(), fb, begin);
                    cmd->setViewport(RHI::Viewport{0.0f, 0.0f, (float)t.size, (float)t.size, 0.0f, 1.0f});
                    cmd->setScissor(RHI::Rect2D{0, 0, t.size, t.size});
                    cmd->bindPipeline(pipeline);
                    cmd->bindVertexBuffer(quad_vb_, 0);
                    RHI::RHIDescriptorSet *sets[1] = {ibl_set_};
                    cmd->bindDescriptorSets(pipeline, sets, 1, nullptr, 0);
                    cmd->draw(6, 0, 1, 0);
                    cmd->endRenderPass();
                }
                cmd->end();
                device_->submitAndWait(cmd);
                device_->destroyCommandBuffer(cmd);
                return true;
            };

            //--------------------------------------------------------------
            // 3. equirect -> cubemap faces
            //--------------------------------------------------------------
            {
                std::vector<BakeTarget> targets;
                for (uint32_t face = 0; face < 6; ++face)
                    targets.push_back(BakeTarget{env_cubemap_, 0, face, cube_size, cube_format, 0.0f});
                if (!runBake(ibl_equirect_pipeline_, targets, false))
                {
                    releaseIBL();
                    return false;
                }
            }

            //--------------------------------------------------------------
            // 4. build the mip chain of the environment cubemap
            //--------------------------------------------------------------
            {
                RHI::RHICommandBuffer *cmd = device_->createCommandBuffer(RHI::CommandBufferUsage::OneTimeSubmit);
                cmd->begin(RHI::CommandBufferUsage::OneTimeSubmit);
                transitionMipToTransferDst(cmd, env_cubemap_, 0);
                cmd->generateMipmaps(env_cubemap_);
                cmd->end();
                device_->submitAndWait(cmd);
                device_->destroyCommandBuffer(cmd);
            }

            //--------------------------------------------------------------
            // 5. irradiance convolution
            //--------------------------------------------------------------
            {
                std::vector<BakeTarget> targets;
                for (uint32_t face = 0; face < 6; ++face)
                    targets.push_back(BakeTarget{irradiance_cubemap_, 0, face, irradiance_size, irradiance_format, 0.0f});
                if (!runBake(ibl_irradiance_pipeline_, targets, true))
                {
                    releaseIBL();
                    return false;
                }
                RHI::RHICommandBuffer *cmd = device_->createCommandBuffer(RHI::CommandBufferUsage::OneTimeSubmit);
                cmd->begin(RHI::CommandBufferUsage::OneTimeSubmit);
                transitionToShaderRead(cmd, irradiance_cubemap_);
                cmd->end();
                device_->submitAndWait(cmd);
                device_->destroyCommandBuffer(cmd);
            }

            //--------------------------------------------------------------
            // 6. prefiltered specular (one pass per mip, roughness ramp)
            //--------------------------------------------------------------
            {
                for (uint32_t mip = 0; mip < prefilter_mips; ++mip)
                {
                    float roughness = prefilter_mips > 1 ? (float)mip / (float)(prefilter_mips - 1) : 0.0f;
                    uint32_t size = std::max(1u, prefilter_size >> mip);
                    std::vector<BakeTarget> targets;
                    for (uint32_t face = 0; face < 6; ++face)
                        targets.push_back(BakeTarget{prefiltered_cubemap_, mip, face, size, prefilter_format,
                                                     std::max(roughness, 0.0001f)});
                    if (!runBake(ibl_prefilter_pipeline_, targets, true))
                    {
                        releaseIBL();
                        return false;
                    }
                }
                RHI::RHICommandBuffer *cmd = device_->createCommandBuffer(RHI::CommandBufferUsage::OneTimeSubmit);
                cmd->begin(RHI::CommandBufferUsage::OneTimeSubmit);
                transitionToShaderRead(cmd, prefiltered_cubemap_);
                cmd->end();
                device_->submitAndWait(cmd);
                device_->destroyCommandBuffer(cmd);
            }

            //--------------------------------------------------------------
            // 7. BRDF integration LUT
            //--------------------------------------------------------------
            {
                std::vector<BakeTarget> targets;
                targets.push_back(BakeTarget{brdf_lut_, 0, 0, lut_size, lut_format, 0.0f});
                if (!runBake(ibl_brdf_pipeline_, targets, true))
                {
                    releaseIBL();
                    return false;
                }
                RHI::RHICommandBuffer *cmd = device_->createCommandBuffer(RHI::CommandBufferUsage::OneTimeSubmit);
                cmd->begin(RHI::CommandBufferUsage::OneTimeSubmit);
                transitionToShaderRead(cmd, brdf_lut_);
                cmd->end();
                device_->submitAndWait(cmd);
                device_->destroyCommandBuffer(cmd);
            }

            //--------------------------------------------------------------
            // 8. publish
            //--------------------------------------------------------------
            ibl_ready_ = true;
            writeSceneDescriptorSet();
            writeSkyboxDescriptorSet();
            GEN_LOG_INFO("[IBL] baked: cube %u(%u mips) irradiance %u prefiltered %u(%u mips) brdf %u",
                         cube_size, cube_mips, irradiance_size, prefilter_size, prefilter_mips, lut_size);
            return true;
        }

        bool RenderSystem::bakeIBL(const std::string &hdr_equirect_path, const IblBakeSettings &settings)
        {
            std::shared_ptr<ImageData> image = ResourceIO::loadImage(hdr_equirect_path);
            if (!image)
            {
                GEN_LOG_ERROR("[IBL] cannot load environment map '%s' - falling back to the procedural sky",
                              hdr_equirect_path.c_str());
                return bakeProceduralIBL(settings);
            }
            if (!image->is_hdr)
                GEN_LOG_WARN("[IBL] '%s' is not an HDR image; values will be in [0,1]", hdr_equirect_path.c_str());
            if (!image->is_hdr || image->hdr.empty())
            {
                // Promote LDR data into the HDR buffer so the bake can proceed.
                std::vector<uint8_t> rgba = image->asRGBA8();
                image->hdr.resize((size_t)image->width * image->height * 4);
                for (size_t i = 0; i < (size_t)image->width * image->height; ++i)
                {
                    image->hdr[i * 4 + 0] = rgba[i * 4 + 0] / 255.0f;
                    image->hdr[i * 4 + 1] = rgba[i * 4 + 1] / 255.0f;
                    image->hdr[i * 4 + 2] = rgba[i * 4 + 2] / 255.0f;
                    image->hdr[i * 4 + 3] = 1.0f;
                }
                image->is_hdr = true;
            }
            env_source_path_ = hdr_equirect_path;
            return bakeIBLFromImage(*image, settings);
        }

        bool RenderSystem::bakeProceduralIBL(const IblBakeSettings &settings)
        {
            ImageData image;
            generateProceduralEquirect(image, 512, 256);
            env_source_path_ = "<procedural>";
            GEN_LOG_INFO("[IBL] baking the procedural sky (512x256 equirect)");
            return bakeIBLFromImage(image, settings);
        }

        void RenderSystem::releaseIBL()
        {
            if (!device_) return;
            device_->waitIdle();
            ibl_ready_ = false;
            for (RHI::RHITexture **t : {&env_equirect_, &env_cubemap_, &irradiance_cubemap_,
                                        &prefiltered_cubemap_, &brdf_lut_})
            {
                if (*t)
                {
                    // Drop cached face framebuffers that reference this texture.
                    for (auto it = face_framebuffers_.begin(); it != face_framebuffers_.end();)
                    {
                        if (it->second) device_->destroyFramebuffer(it->second);
                        it = face_framebuffers_.erase(it);
                    }
                    device_->destroyTexture(*t);
                    *t = nullptr;
                }
            }
            writeSceneDescriptorSet();
            writeSkyboxDescriptorSet();
        }

        void RenderSystem::writeSkyboxDescriptorSet()
        {
            if (!skybox_set_) return;
            const GpuTexture &white = scene_->textures().white();
            RHI::DescriptorImageInfo equirect{env_equirect_ ? env_equirect_ : white.texture,
                                              ibl_sampler_ ? ibl_sampler_ : white.sampler,
                                              RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1};
            RHI::DescriptorImageInfo cube{env_cubemap_ ? env_cubemap_ : white.texture,
                                          ibl_sampler_ ? ibl_sampler_ : white.sampler,
                                          RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 6};
            RHI::WriteDescriptorSet writes[2];
            writes[0] = RHI::WriteDescriptorSet{0, 0, 1, RHI::ResourceType::CombinedImageSampler, nullptr, &equirect};
            writes[1] = RHI::WriteDescriptorSet{1, 0, 1, RHI::ResourceType::CombinedImageSampler, nullptr, &cube};
            skybox_set_->write(writes, 2);
        }

    } // namespace Render
} // namespace Genesis
