/*
 * Genesis Render - image based lighting baker
 *
 * Pipeline (raster only, so it runs identically on OpenGL 4.1, Vulkan and
 * Metal - no compute shaders required):
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
 * Every face / mip draw reads its parameters from its own slice of the
 * uniform ring (dynamic offset), so the descriptor sets are written once per
 * bake and many draws can be recorded into one command buffer.
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
            // IEEE-754 half float conversion (the environment maps are RGBA16F,
            // which is filterable everywhere while RGBA32F is not).
            uint16_t halfFromFloat(float value)
            {
                uint32_t f;
                std::memcpy(&f, &value, sizeof(f));
                uint32_t sign = (f >> 16) & 0x8000u;
                int32_t exponent = (int32_t)((f >> 23) & 0xFF) - 127 + 15;
                uint32_t mantissa = f & 0x7FFFFFu;
                if (((f >> 23) & 0xFF) == 0xFF) return (uint16_t)(sign | 0x7C00u | (mantissa ? 0x200u : 0u));
                if (exponent <= 0)
                {
                    if (exponent < -10) return (uint16_t)sign;
                    mantissa |= 0x800000u;
                    uint32_t shift = (uint32_t)(14 - exponent);
                    uint16_t half_mantissa = (uint16_t)(mantissa >> shift);
                    if ((mantissa >> (shift - 1)) & 1u) ++half_mantissa;
                    return (uint16_t)(sign | half_mantissa);
                }
                if (exponent >= 31) return (uint16_t)(sign | 0x7C00u);
                return (uint16_t)(sign | ((uint32_t)exponent << 10) | (mantissa >> 13));
            }

            void transition(RHI::RHICommandBuffer *cmd, RHI::RHITexture *texture, RHI::TextureLayout layout)
            {
                RHI::TextureBarrier barrier{};
                barrier.texture = texture;
                barrier.old_layout = texture->currentLayout();
                barrier.new_layout = layout;
                cmd->pipelineBarrier(&barrier, 1);
            }

            // Procedural sky used when no HDR map is supplied.  Matches the
            // fallback in scene/skybox.frag so the two agree visually.
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
                        float phi = (u - 0.5f) * Math::TWO_PI;
                        float theta = v * Math::PI;
                        Math::Vec3 dir(std::cos(phi) * std::sin(theta), std::cos(theta), std::sin(phi) * std::sin(theta));
                        Math::Vec3 color;
                        if (dir.y >= 0.0f) color = Math::lerp(sky_horizon, sky_top, std::pow(dir.y, 0.55f));
                        else color = Math::lerp(sky_horizon, ground, Math::clampf(-dir.y * 4.0f, 0.0f, 1.0f));
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
            const RHI::PixelFormat candidates[3] = {format_inout, RHI::PixelFormat::RGBA32_SFLOAT, RHI::PixelFormat::RGBA8_UNORM};
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                RHI::PixelFormat format = candidates[attempt];
                if (attempt > 0 && format == candidates[attempt - 1]) continue;
                RHI::TextureDesc desc;
                desc.type = RHI::TextureType::TexCube;
                desc.format = format;
                desc.width = desc.height = size;
                desc.array_layers = 1;
                desc.mip_levels = mips;
                desc.generate_mipmaps = mips > 1;
                desc.usage = (uint32_t)RHI::TextureUsageFlagBits::RenderTarget | (uint32_t)RHI::TextureUsageFlagBits::Sampled |
                             (uint32_t)RHI::TextureUsageFlagBits::TransferSrc | (uint32_t)RHI::TextureUsageFlagBits::TransferDst;
                desc.name = name;
                RHI::RHITexture *texture = device_->createTexture(desc);
                if (!texture) continue;
                if (faceFramebuffer(texture, 0, 0, format, name + ".probe"))
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
            if (!initialized_ && !scene_) return false;
            if (image.width == 0 || image.height == 0) return false;
            releaseIBL();
            TextureCache &tc = scene_->textures();

            // 1. equirectangular source texture
            {
                RHI::TextureDesc desc;
                desc.format = RHI::PixelFormat::RGBA16_SFLOAT;
                desc.width = image.width;
                desc.height = image.height;
                desc.mip_levels = 1;
                desc.usage = (uint32_t)RHI::TextureUsageFlagBits::Sampled | (uint32_t)RHI::TextureUsageFlagBits::TransferDst;
                desc.name = "ibl.equirect";
                env_equirect_ = device_->createTexture(desc);
                if (!env_equirect_)
                {
                    GEN_LOG_ERROR("[IBL] failed to create the equirect texture");
                    return false;
                }
                std::vector<uint16_t> half((size_t)image.width * image.height * 4);
                for (size_t i = 0; i < half.size(); ++i) half[i] = halfFromFloat(image.hdr[i]);
                device_->writeTexture(env_equirect_, half.data(), half.size() * sizeof(uint16_t), image.width, image.height, 0, 0, 1);
            }

            // 2. destination textures
            RHI::PixelFormat cube_format = RHI::PixelFormat::RGBA16_SFLOAT;
            const uint32_t cube_size = std::max(8u, cfg.cubemap_size);
            const uint32_t cube_mips = detail::mipLevelsFor(cube_size);
            env_cubemap_ = createCubeTexture(cube_size, cube_mips, "ibl.env_cube", cube_format);
            const uint32_t irradiance_size = std::max(4u, cfg.irradiance_size);
            RHI::PixelFormat irradiance_format = cube_format;
            irradiance_cubemap_ = env_cubemap_ ? createCubeTexture(irradiance_size, 1, "ibl.irradiance", irradiance_format) : nullptr;
            const uint32_t prefilter_size = std::max(8u, cfg.prefiltered_size);
            const uint32_t prefilter_mips = detail::mipLevelsFor(prefilter_size);
            RHI::PixelFormat prefilter_format = cube_format;
            prefiltered_cubemap_ = irradiance_cubemap_ ? createCubeTexture(prefilter_size, prefilter_mips, "ibl.prefiltered", prefilter_format) : nullptr;
            prefilter_mip_levels_ = prefilter_mips;

            const uint32_t lut_size = std::max(8u, cfg.brdf_lut_size);
            RHI::PixelFormat lut_format = RHI::PixelFormat::RG16_SFLOAT;
            for (RHI::PixelFormat candidate : {RHI::PixelFormat::RG16_SFLOAT, RHI::PixelFormat::RGBA16_SFLOAT})
            {
                RHI::TextureDesc desc;
                desc.format = candidate;
                desc.width = desc.height = lut_size;
                desc.mip_levels = 1;
                desc.usage = (uint32_t)RHI::TextureUsageFlagBits::RenderTarget | (uint32_t)RHI::TextureUsageFlagBits::Sampled |
                             (uint32_t)RHI::TextureUsageFlagBits::TransferSrc;
                desc.name = "ibl.brdf_lut";
                brdf_lut_ = device_->createTexture(desc);
                if (brdf_lut_ && faceFramebuffer(brdf_lut_, 0, 0, candidate, "ibl.brdf_lut.probe"))
                {
                    lut_format = candidate;
                    break;
                }
                if (brdf_lut_) device_->destroyTexture(brdf_lut_);
                brdf_lut_ = nullptr;
            }
            if (!env_cubemap_ || !irradiance_cubemap_ || !prefiltered_cubemap_ || !brdf_lut_)
            {
                GEN_LOG_ERROR("[IBL] failed to create the bake targets");
                releaseIBL();
                return false;
            }

            // 3. descriptor sets (immutable for the whole bake)
            auto makeSet = [&](RHI::RHITexture *equirect, RHI::RHITexture *cube) -> RHI::RHIDescriptorSet *
            {
                RHI::RHIDescriptorSet *set = device_->createDescriptorSet(ibl_layout_, 0);
                if (!set) return nullptr;
                RHI::DescriptorBufferInfo buf{frames_.uniformBuffer(), 0, sizeof(IblParamsUBO)};
                RHI::DescriptorImageInfo eq{equirect ? equirect : tc.black().texture, equirect ? ibl_sampler_ : tc.black().sampler,
                                            RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1};
                RHI::DescriptorImageInfo cb{cube ? cube : tc.blackCube().texture, cube ? ibl_sampler_ : tc.blackCube().sampler,
                                            RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 6};
                RHI::WriteDescriptorSet writes[3];
                writes[0] = RHI::WriteDescriptorSet{0, 0, 1, RHI::ResourceType::UniformBufferDynamic, &buf, nullptr};
                writes[1] = RHI::WriteDescriptorSet{1, 0, 1, RHI::ResourceType::CombinedImageSampler, nullptr, &eq};
                writes[2] = RHI::WriteDescriptorSet{2, 0, 1, RHI::ResourceType::CombinedImageSampler, nullptr, &cb};
                set->write(writes, 3);
                return set;
            };
            ibl_set_equirect_ = makeSet(env_equirect_, nullptr);
            ibl_set_cube_ = makeSet(nullptr, env_cubemap_);
            if (!ibl_set_equirect_ || !ibl_set_cube_)
            {
                releaseIBL();
                return false;
            }

            const float rotation = Math::toRadians(settings_.env_rotation_degrees);
            struct BakeTarget
            {
                RHI::RHITexture *texture;
                uint32_t mip, layer, size;
                RHI::PixelFormat format;
                float roughness;
            };
            auto runBake = [&](const char *frag, RHI::RHIDescriptorSet *set, const std::vector<BakeTarget> &targets,
                               uint32_t samples) -> bool
            {
                RHI::RHICommandBuffer *cmd = device_->createCommandBuffer(RHI::CommandBufferUsage::OneTimeSubmit);
                if (!cmd) return false;
                cmd->begin(RHI::CommandBufferUsage::OneTimeSubmit);
                bool ok = true;
                for (const BakeTarget &t : targets)
                {
                    RHI::RHIFramebuffer *fb = faceFramebuffer(t.texture, t.mip, t.layer, t.format, "ibl.face");
                    PipelineKey key;
                    key.vertex_shader = "common/fullscreen.vert";
                    key.fragment_shader = frag;
                    key.layout = &ibl_layout_->desc();
                    key.vertex_layout = VertexLayoutKind::Fullscreen;
                    key.cull = RHI::CullMode::None;
                    key.depth_test = false;
                    key.depth_write = false;
                    key.color_format = t.format;
                    RHI::RHIPipeline *pipeline = pipelines_.get(key, frag);
                    if (!fb || !pipeline)
                    {
                        GEN_LOG_ERROR("[IBL] cannot record the '%s' pass", frag);
                        ok = false;
                        break;
                    }
                    IblParamsUBO params;
                    params.a = GpuVec4((float)t.layer, t.roughness, (float)t.mip, (float)samples);
                    params.b = GpuVec4(std::cos(rotation), std::sin(rotation), 0.0f, 0.0f);
                    const uint32_t offset = frames_.pushUniform(params);

                    RHI::RenderPassBeginInfo begin;
                    begin.clear_count = 1;
                    cmd->beginRenderPass(fb->renderPass(), fb, begin);
                    cmd->setViewport(RHI::Viewport{0.0f, 0.0f, (float)t.size, (float)t.size, 0.0f, 1.0f});
                    cmd->setScissor(RHI::Rect2D{0, 0, t.size, t.size});
                    cmd->bindPipeline(pipeline);
                    cmd->bindVertexBuffer(quad_vb_, 0);
                    cmd->bindDescriptorSets(pipeline, &set, 1, &offset, 1);
                    cmd->draw(6, 0, 1, 0);
                    cmd->endRenderPass();
                }
                cmd->end();
                device_->submitAndWait(cmd);
                device_->destroyCommandBuffer(cmd);
                return ok;
            };
            auto finishTexture = [&](RHI::RHITexture *texture, bool mips)
            {
                RHI::RHICommandBuffer *cmd = device_->createCommandBuffer(RHI::CommandBufferUsage::OneTimeSubmit);
                cmd->begin(RHI::CommandBufferUsage::OneTimeSubmit);
                if (mips) cmd->generateMipmaps(texture);
                else transition(cmd, texture, RHI::TextureLayout::ShaderReadOnlyOptimal);
                cmd->end();
                device_->submitAndWait(cmd);
                device_->destroyCommandBuffer(cmd);
            };

            // 4. equirect -> cubemap faces, then its mip chain
            std::vector<BakeTarget> faces;
            for (uint32_t face = 0; face < 6; ++face) faces.push_back({env_cubemap_, 0, face, cube_size, cube_format, 0.0f});
            bool ok = runBake("ibl/equirect_to_cube.frag", ibl_set_equirect_, faces, 0);
            if (ok) finishTexture(env_cubemap_, true);

            // 5. irradiance convolution
            if (ok)
            {
                faces.clear();
                for (uint32_t face = 0; face < 6; ++face) faces.push_back({irradiance_cubemap_, 0, face, irradiance_size, irradiance_format, 0.0f});
                ok = runBake("ibl/irradiance.frag", ibl_set_cube_, faces, 0);
                if (ok) finishTexture(irradiance_cubemap_, false);
            }

            // 6. prefiltered specular (roughness ramp over the mips)
            if (ok)
            {
                faces.clear();
                for (uint32_t mip = 0; mip < prefilter_mips; ++mip)
                {
                    const float roughness = prefilter_mips > 1 ? (float)mip / (float)(prefilter_mips - 1) : 0.0f;
                    const uint32_t size = std::max(1u, prefilter_size >> mip);
                    for (uint32_t face = 0; face < 6; ++face)
                        faces.push_back({prefiltered_cubemap_, mip, face, size, prefilter_format, std::max(roughness, 0.0001f)});
                }
                ok = runBake("ibl/prefilter.frag", ibl_set_cube_, faces, cfg.prefilter_samples);
                if (ok) finishTexture(prefiltered_cubemap_, false);
            }

            // 7. BRDF integration LUT
            if (ok)
            {
                faces.clear();
                faces.push_back({brdf_lut_, 0, 0, lut_size, lut_format, 0.0f});
                ok = runBake("ibl/brdf_lut.frag", ibl_set_cube_, faces, cfg.brdf_samples);
                if (ok) finishTexture(brdf_lut_, false);
            }
            if (!ok)
            {
                releaseIBL();
                return false;
            }

            ibl_ready_ = true;
            writeSceneDescriptorSet();
            writeSkyboxDescriptorSet();
            GEN_LOG_INFO("[IBL] baked: cube %u(%u mips) irradiance %u prefiltered %u(%u mips) brdf %u", cube_size, cube_mips,
                         irradiance_size, prefilter_size, prefilter_mips, lut_size);
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
            if (!image->is_hdr || image->hdr.empty())
            {
                GEN_LOG_WARN("[IBL] '%s' is not an HDR image; values will be in [0,1]", hdr_equirect_path.c_str());
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
            for (RHI::RHIDescriptorSet **s : {&ibl_set_equirect_, &ibl_set_cube_})
            {
                if (*s) device_->destroyDescriptorSet(*s);
                *s = nullptr;
            }
            for (auto &fb : face_framebuffers_)
                if (fb.second) device_->destroyFramebuffer(fb.second);
            face_framebuffers_.clear();
            for (RHI::RHITexture **t : {&env_equirect_, &env_cubemap_, &irradiance_cubemap_, &prefiltered_cubemap_, &brdf_lut_})
            {
                if (*t) device_->destroyTexture(*t);
                *t = nullptr;
            }
            if (scene_)
            {
                writeSceneDescriptorSet();
                writeSkyboxDescriptorSet();
            }
        }

    } // namespace Render
} // namespace Genesis
