/*
 * Genesis Render - render system implementation (pipelines, targets, frame)
 *
 * Descriptor / binding plan (mirrors shaders/common/genesis_shader.h):
 *
 *   mesh pipelines : set 0 = scene, set 1 = material, set 2 = per draw (dynamic)
 *   skybox         : set 0 = scene, set 1 = sky textures
 *   tone mapping   : set 0 = post params + scene colour
 *   IBL bakes      : set 0 = bake params + source textures
 *   debug lines    : push constants only
 */
#include "renderer/render_system.hpp"
#include "renderer/render_internal.hpp"
#include "foundation_layer/genesis_log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace Genesis
{
    namespace Render
    {

        //==================================================================
        // Lifetime
        //==================================================================
        RenderSystem::RenderSystem(RHI::RHIDevice *device) : device_(device) {}

        RenderSystem::~RenderSystem()
        {
            shutdown();
        }

        bool RenderSystem::initialize(uint32_t width, uint32_t height, const std::string &ttf_path)
        {
            if (!device_)
            {
                GEN_LOG_ERROR("[Render] no RHI device");
                return false;
            }
            camera_.backend = device_->backend();
            settings_.hdr_width = width;
            settings_.hdr_height = height;

            if (!createPersistentResources()) return false;
            if (!createPipelines()) return false;
            if (!createTargets(width, height)) return false;

            // Free command-list / programmable post / SDF text subsystem.
            // Optional: if the font is missing the overlay silently disables.
            if (!ttf_path.empty())
            {
                overlay_.initialize(device_, ttf_path);
                overlay_enabled_ = overlay_.ready();
            }

            initialized_ = true;
            GEN_LOG_INFO("[Render] initialised (%ux%u, backend=%s, shaders=%zu overlay=%d)",
                         width, height, RHI::backendTypeName(device_->backend()), Shaders::shaderCount(),
                         (int)overlay_enabled_);
            return true;
        }

        void RenderSystem::shutdown()
        {
            if (!device_) return;
            device_->waitIdle();

            overlay_.shutdown();
            overlay_enabled_ = false;

            clearDebugLines();
            if (debug_vb_) { device_->destroyBuffer(debug_vb_); debug_vb_ = nullptr; }
            if (quad_vb_) { device_->destroyBuffer(quad_vb_); quad_vb_ = nullptr; }

            releaseIBL();
            destroyTargets();

            scene_.reset();

            for (auto &fb : face_framebuffers_)
                if (fb.second) device_->destroyFramebuffer(fb.second);
            face_framebuffers_.clear();

            if (ibl_set_) { device_->destroyDescriptorSet(ibl_set_); ibl_set_ = nullptr; }
            if (post_set_) { device_->destroyDescriptorSet(post_set_); post_set_ = nullptr; }
            if (skybox_set_) { device_->destroyDescriptorSet(skybox_set_); skybox_set_ = nullptr; }
            if (per_draw_set_) { device_->destroyDescriptorSet(per_draw_set_); per_draw_set_ = nullptr; }
            if (scene_set_) { device_->destroyDescriptorSet(scene_set_); scene_set_ = nullptr; }

            if (ibl_ubo_) { device_->destroyBuffer(ibl_ubo_); ibl_ubo_ = nullptr; }
            if (post_ubo_) { device_->destroyBuffer(post_ubo_); post_ubo_ = nullptr; }
            if (per_draw_ring_) { device_->destroyBuffer(per_draw_ring_); per_draw_ring_ = nullptr; }
            if (environment_ubo_) { device_->destroyBuffer(environment_ubo_); environment_ubo_ = nullptr; }
            if (lights_ubo_) { device_->destroyBuffer(lights_ubo_); lights_ubo_ = nullptr; }
            if (per_frame_ubo_) { device_->destroyBuffer(per_frame_ubo_); per_frame_ubo_ = nullptr; }

            for (RHI::RHIPipeline **p : {&pbr_opaque_, &pbr_opaque_two_sided_, &pbr_alpha_mask_, &pbr_blend_,
                                         &skybox_pipeline_, &tonemap_pipeline_, &unlit_pipeline_, &line_pipeline_,
                                         &ibl_equirect_pipeline_, &ibl_irradiance_pipeline_, &ibl_prefilter_pipeline_,
                                         &ibl_brdf_pipeline_})
            {
                if (*p) { device_->destroyPipeline(*p); *p = nullptr; }
            }
            // scene_layout_, material_layout_ and per_draw_layout_ all alias
            // mesh_layout_ (the mesh pipelines use one combined layout spanning
            // sets 0..2), so destroy each distinct object exactly once.
            RHI::RHIResourceLayout *distinct_layouts[] = {mesh_layout_, skybox_layout_, post_layout_, ibl_layout_, line_layout_};
            for (RHI::RHIResourceLayout *l : distinct_layouts)
                if (l) device_->destroyResourceLayout(l);
            mesh_layout_ = nullptr;
            scene_layout_ = nullptr;
            material_layout_ = nullptr;
            per_draw_layout_ = nullptr;
            skybox_layout_ = nullptr;
            post_layout_ = nullptr;
            ibl_layout_ = nullptr;
            line_layout_ = nullptr;
            initialized_ = false;
        }

        //==================================================================
        // Persistent resources: layouts, UBOs, quad, descriptor sets, scene
        //==================================================================
        bool RenderSystem::createPersistentResources()
        {
            const uint32_t VS = (uint32_t)RHI::ShaderStageFlagBits::Vertex;
            const uint32_t FS = (uint32_t)RHI::ShaderStageFlagBits::Fragment;

            auto ubo = [&](uint32_t set, uint32_t binding, const char *name, uint32_t stages, bool dynamic = false)
            {
                RHI::ResourceBinding b;
                b.set = set; b.binding = binding; b.name = name; b.stages = stages;
                b.type = dynamic ? RHI::ResourceType::UniformBufferDynamic : RHI::ResourceType::UniformBuffer;
                return b;
            };
            // NOTE: no default arguments here - a lambda default argument may
            // not reference captured local variables.
            auto tex = [&](uint32_t set, uint32_t binding, const char *name, uint32_t stages)
            {
                RHI::ResourceBinding b;
                b.set = set; b.binding = binding; b.name = name; b.stages = stages;
                b.type = RHI::ResourceType::CombinedImageSampler;
                return b;
            };

            // --- scene (set 0) ---
            RHI::ResourceLayoutDesc scene_desc;
            scene_desc.name = "scene-set0";
            scene_desc.bindings = {
                ubo(0, 0, "PerFrame", VS | FS),
                ubo(0, 1, "Lights", VS | FS),
                ubo(0, 2, "Environment", VS | FS),
                tex(0, 3, "u_irradiance_map", FS),
                tex(0, 4, "u_prefilter_map", FS),
                tex(0, 5, "u_brdf_lut", FS),
            };

            // --- material (set 1) ---
            // NOTE: stage flags must list EVERY stage that touches the block.
            // pbr_mesh.vert reads u_material_extra (uv scale) and
            // u_normal_matrix, so both blocks need VS | FS; declaring only one
            // stage is a Vulkan validation error and yields undefined (zero)
            // values in the other stage, while OpenGL silently ignores it.
            RHI::ResourceLayoutDesc material_desc;
            material_desc.name = "material-set1";
            material_desc.bindings = {
                ubo(1, 0, "Material", VS | FS),
                tex(1, 1, "u_base_color", FS),
                tex(1, 2, "u_metallic_roughness", FS),
                tex(1, 3, "u_normal", FS),
                tex(1, 4, "u_occlusion", FS),
                tex(1, 5, "u_emissive", FS),
            };

            // --- per draw (set 2) ---
            RHI::ResourceLayoutDesc per_draw_desc;
            per_draw_desc.name = "perdraw-set2";
            per_draw_desc.bindings = {ubo(2, 0, "PerDraw", VS | FS, true)};

            // The mesh pipelines use one combined layout spanning sets 0..2 so
            // that a single vkPipelineLayout / GL program binding table covers
            // every descriptor set bound at draw time.
            RHI::ResourceLayoutDesc mesh_desc;
            mesh_desc.name = "mesh-combined";
            mesh_desc.bindings = scene_desc.bindings;
            mesh_desc.bindings.insert(mesh_desc.bindings.end(), material_desc.bindings.begin(), material_desc.bindings.end());
            mesh_desc.bindings.insert(mesh_desc.bindings.end(), per_draw_desc.bindings.begin(), per_draw_desc.bindings.end());
            mesh_layout_ = device_->createResourceLayout(mesh_desc);
            scene_layout_ = mesh_layout_;
            material_layout_ = mesh_layout_;
            per_draw_layout_ = mesh_layout_;
            if (!mesh_layout_)
            {
                GEN_LOG_ERROR("[Render] failed to create the mesh resource layout");
                return false;
            }

            // --- skybox (set 0 = scene, set 1 = sky textures) ---
            RHI::ResourceLayoutDesc skybox_desc;
            skybox_desc.name = "skybox";
            skybox_desc.bindings = scene_desc.bindings;
            skybox_desc.bindings.push_back(tex(1, 0, "u_sky_equirect", FS));
            skybox_desc.bindings.push_back(tex(1, 1, "u_sky_cube", FS));
            skybox_layout_ = device_->createResourceLayout(skybox_desc);

            // --- post (set 0) ---
            RHI::ResourceLayoutDesc post_desc;
            post_desc.name = "post";
            post_desc.bindings = {
                ubo(0, 0, "PostParams", VS | FS),
                tex(0, 1, "u_scene_color", FS),
            };
            post_layout_ = device_->createResourceLayout(post_desc);

            // --- IBL bakes (set 0) ---
            RHI::ResourceLayoutDesc ibl_desc;
            ibl_desc.name = "ibl";
            ibl_desc.bindings = {
                ubo(0, 0, "IBLParams", VS | FS),
                tex(0, 1, "u_src_equirect", FS),
                tex(0, 2, "u_src_cube", FS),
            };
            ibl_layout_ = device_->createResourceLayout(ibl_desc);

            // --- debug lines (push constants only) ---
            RHI::ResourceLayoutDesc line_desc;
            line_desc.name = "debug-line";
            line_desc.push_constant_size = 80; // mat4 + vec4
            line_desc.push_constant_stages = VS | FS;
            line_layout_ = device_->createResourceLayout(line_desc);

            if (!skybox_layout_ || !post_layout_ || !ibl_layout_ || !line_layout_)
            {
                GEN_LOG_ERROR("[Render] failed to create one of the resource layouts");
                return false;
            }

            // --- uniform buffers ---
            auto makeUBO = [&](const char *name, uint64_t size, const void *initial)
            {
                RHI::BufferDesc d;
                d.name = name;
                d.size = size;
                d.usage = (uint32_t)RHI::BufferUsageFlagBits::Uniform | (uint32_t)RHI::BufferUsageFlagBits::TransferDst;
                d.memory_usage = RHI::MemoryUsage::CpuToGpu;
                d.initial_data = initial;
                d.initial_data_size = initial ? size : 0;
                return device_->createBuffer(d);
            };

            per_frame_ubo_ = makeUBO("per_frame.ubo", sizeof(PerFrameUBO), nullptr);
            lights_ubo_ = makeUBO("lights.ubo", sizeof(LightsUBO), nullptr);
            environment_ubo_ = makeUBO("environment.ubo", sizeof(EnvironmentUBO), nullptr);
            post_ubo_ = makeUBO("post.ubo", sizeof(PostParamsUBO), nullptr);
            ibl_ubo_ = makeUBO("ibl.ubo", sizeof(IblParamsUBO), nullptr);
            if (!per_frame_ubo_ || !lights_ubo_ || !environment_ubo_ || !post_ubo_ || !ibl_ubo_)
            {
                GEN_LOG_ERROR("[Render] failed to create uniform buffers");
                return false;
            }

            per_draw_stride_ = RHI::alignUp(sizeof(PerDrawUBO), device_->info().min_uniform_buffer_offset_alignment);
            uint64_t ring_size = per_draw_stride_ * kPerDrawSlots;
            per_draw_ring_ = makeUBO("per_draw.ring", ring_size, nullptr);
            if (!per_draw_ring_)
            {
                GEN_LOG_ERROR("[Render] failed to create the per-draw ring buffer");
                return false;
            }
            GEN_LOG_DEBUG("[Render] per-draw ring: %llu bytes, stride %llu, %u slots",
                          (unsigned long long)ring_size, (unsigned long long)per_draw_stride_, kPerDrawSlots);

            // --- fullscreen quad ---
            {
                RHI::BufferDesc d;
                d.name = "fullscreen-quad";
                d.size = sizeof(detail::QuadVertex) * 6;
                d.usage = (uint32_t)RHI::BufferUsageFlagBits::Vertex | (uint32_t)RHI::BufferUsageFlagBits::TransferDst;
                d.memory_usage = RHI::MemoryUsage::GpuOnly;
                d.initial_data = detail::quadVertices();
                d.initial_data_size = sizeof(detail::QuadVertex) * 6;
                quad_vb_ = device_->createBuffer(d);
                if (!quad_vb_) return false;
            }

            // --- scene ---
            scene_ = std::make_unique<RenderScene>(device_, mesh_layout_);

            // --- descriptor sets ---
            scene_set_ = device_->createDescriptorSet(mesh_layout_, 0);
            per_draw_set_ = device_->createDescriptorSet(mesh_layout_, 2);
            skybox_set_ = device_->createDescriptorSet(skybox_layout_, 1);
            post_set_ = device_->createDescriptorSet(post_layout_, 0);
            ibl_set_ = device_->createDescriptorSet(ibl_layout_, 0);
            if (!scene_set_ || !per_draw_set_ || !skybox_set_ || !post_set_ || !ibl_set_)
            {
                GEN_LOG_ERROR("[Render] failed to allocate descriptor sets");
                return false;
            }
            writeSceneDescriptorSet();
            writePerDrawDescriptorSet();

            // IBL sampler (linear, clamp to edge)
            RHI::SamplerDesc sd;
            sd.min_filter = sd.mag_filter = RHI::FilterMode::Linear;
            sd.mipmap_mode = RHI::MipmapMode::Linear;
            sd.address_u = sd.address_v = sd.address_w = RHI::AddressMode::ClampToEdge;
            sd.max_lod = 16.0f;
            sd.max_anisotropy = 1.0f;
            sd.name = "ibl-sampler";
            ibl_sampler_ = device_->createSampler(sd);

            return true;
        }

        void RenderSystem::writeSceneDescriptorSet()
        {
            if (!scene_set_) return;
            RHI::DescriptorBufferInfo buffers[3] = {
                {per_frame_ubo_, 0, sizeof(PerFrameUBO)},
                {lights_ubo_, 0, sizeof(LightsUBO)},
                {environment_ubo_, 0, sizeof(EnvironmentUBO)},
            };
            const GpuTexture &white = scene_->textures().white();
            const GpuTexture &black = scene_->textures().black();
            RHI::RHITexture *irradiance = ibl_ready_ ? irradiance_cubemap_ : white.texture;
            RHI::RHITexture *prefiltered = ibl_ready_ ? prefiltered_cubemap_ : white.texture;
            RHI::RHITexture *brdf = ibl_ready_ ? brdf_lut_ : black.texture;
            RHI::RHISampler *sampler = ibl_ready_ ? ibl_sampler_ : white.sampler;

            RHI::DescriptorImageInfo images[3] = {
                {irradiance, sampler, RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1},
                {prefiltered, sampler, RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1},
                {brdf, sampler, RHI::TextureLayout::ShaderReadOnlyOptimal, 0, 0, 1},
            };
            RHI::WriteDescriptorSet writes[6];
            for (int i = 0; i < 3; ++i)
                writes[i] = RHI::WriteDescriptorSet{(uint32_t)i, 0, 1, RHI::ResourceType::UniformBuffer, &buffers[i], nullptr};
            for (int i = 0; i < 3; ++i)
                writes[3 + i] = RHI::WriteDescriptorSet{(uint32_t)(3 + i), 0, 1, RHI::ResourceType::CombinedImageSampler, nullptr, &images[i]};
            scene_set_->write(writes, 6);
        }

        void RenderSystem::writePerDrawDescriptorSet()
        {
            if (!per_draw_set_ || !per_draw_ring_) return;
            RHI::DescriptorBufferInfo info{per_draw_ring_, 0, per_draw_stride_};
            RHI::WriteDescriptorSet write{0, 0, 1, RHI::ResourceType::UniformBufferDynamic, &info, nullptr};
            per_draw_set_->write(&write, 1);
        }

        //==================================================================
        // Pipelines
        //==================================================================
        namespace
        {
            struct PipelineBuilder
            {
                RHI::PipelineDesc desc;
                RHI::RHIDevice *device = nullptr;

                bool addStage(const char *shader_name, RHI::ShaderStageFlagBits stage)
                {
                    RHI::ShaderStageDesc sd;
                    if (!detail::fillShaderStage(shader_name, device->backend(), stage, sd)) return false;
                    desc.shaders.push_back(sd);
                    return true;
                }
            };
        } // namespace

        bool RenderSystem::createPipelines()
        {
            const RHI::PixelFormat hdr_format = hdr_target_.valid ? hdr_target_.color_format : RHI::PixelFormat::RGBA16_SFLOAT;
            const RHI::PixelFormat depth_format = RHI::PixelFormat::D24_UNORM_S8_UINT;

            auto meshPipeline = [&](const char *name, const char *vert, const char *frag,
                                    RHI::CullMode cull, bool blend, RHI::PixelFormat color_format,
                                    RHI::PixelFormat depth_fmt) -> RHI::RHIPipeline *
            {
                RHI::PipelineDesc d;
                d.name = name;
                RHI::ShaderStageDesc vs, fs;
                if (!detail::fillShaderStage(vert, device_->backend(), RHI::ShaderStageFlagBits::Vertex, vs)) return nullptr;
                if (!detail::fillShaderStage(frag, device_->backend(), RHI::ShaderStageFlagBits::Fragment, fs)) return nullptr;
                d.shaders = {vs, fs};
                d.vertex_layout = detail::meshVertexLayout();
                d.topology = RHI::PrimitiveTopology::TriangleList;
                // GENESIS_DBG_NOCULL=1 disables culling and depth testing for
                // every mesh pipeline - a quick way to tell "nothing drawn"
                // apart from "drawn but rejected".
                static const bool dbg_nocull = std::getenv("GENESIS_DBG_NOCULL") != nullptr;
                d.rasterizer.cull_mode = dbg_nocull ? RHI::CullMode::None : cull;
                d.rasterizer.front_face = RHI::FrontFace::CounterClockwise;
                d.rasterizer.polygon_mode = settings_.wireframe ? RHI::PolygonMode::Line : RHI::PolygonMode::Fill;
                d.depth_stencil.depth_test = !dbg_nocull;
                d.depth_stencil.depth_write = !blend;
                d.depth_stencil.depth_compare = RHI::CompareOp::LessOrEqual;
                d.blend.attachment_count = 1;
                d.blend.attachments[0].blend_enable = blend;
                if (blend)
                {
                    d.blend.attachments[0].src_color = RHI::BlendFactor::SrcAlpha;
                    d.blend.attachments[0].dst_color = RHI::BlendFactor::OneMinusSrcAlpha;
                    d.blend.attachments[0].color_op = RHI::BlendOp::Add;
                    d.blend.attachments[0].src_alpha = RHI::BlendFactor::One;
                    d.blend.attachments[0].dst_alpha = RHI::BlendFactor::OneMinusSrcAlpha;
                }
                d.resource_layout = mesh_layout_->desc();
                d.color_formats[0] = color_format;
                d.color_format_count = 1;
                d.depth_format = depth_fmt;
                return device_->createPipeline(d);
            };

            pbr_opaque_ = meshPipeline("pbr.opaque", "scene/pbr_mesh.vert", "scene/pbr_mesh.frag",
                                       RHI::CullMode::Back, false, hdr_format, depth_format);
            pbr_opaque_two_sided_ = meshPipeline("pbr.opaque.two_sided", "scene/pbr_mesh.vert", "scene/pbr_mesh.frag",
                                                 RHI::CullMode::None, false, hdr_format, depth_format);
            pbr_alpha_mask_ = meshPipeline("pbr.alpha_mask", "scene/pbr_mesh.vert", "scene/pbr_mesh.frag",
                                           RHI::CullMode::Back, false, hdr_format, depth_format);
            pbr_blend_ = meshPipeline("pbr.blend", "scene/pbr_mesh.vert", "scene/pbr_mesh.frag",
                                      RHI::CullMode::None, true, hdr_format, depth_format);
            if (!pbr_opaque_ || !pbr_blend_)
            {
                GEN_LOG_ERROR("[Render] failed to create the PBR pipelines");
                return false;
            }

            // Unlit (debug / verification) - same layouts, no textures needed.
            {
                RHI::PipelineDesc d;
                d.name = "unlit";
                RHI::ShaderStageDesc vs, fs;
                if (detail::fillShaderStage("scene/unlit_mesh.vert", device_->backend(), RHI::ShaderStageFlagBits::Vertex, vs) &&
                    detail::fillShaderStage("scene/unlit_mesh.frag", device_->backend(), RHI::ShaderStageFlagBits::Fragment, fs))
                {
                    d.shaders = {vs, fs};
                    d.vertex_layout = detail::meshVertexLayout();
                    d.depth_stencil.depth_compare = RHI::CompareOp::LessOrEqual;
                    d.resource_layout = mesh_layout_->desc();
                    d.resource_layout.push_constant_size = 16; // vec4 colour
                    d.resource_layout.push_constant_stages = (uint32_t)RHI::ShaderStageFlagBits::Vertex |
                                                             (uint32_t)RHI::ShaderStageFlagBits::Fragment;
                    d.color_formats[0] = hdr_format;
                    d.color_format_count = 1;
                    d.depth_format = depth_format;
                    unlit_pipeline_ = device_->createPipeline(d);
                }
            }

            // Skybox
            {
                RHI::PipelineDesc d;
                d.name = "skybox";
                RHI::ShaderStageDesc vs, fs;
                if (!detail::fillShaderStage("common/fullscreen.vert", device_->backend(), RHI::ShaderStageFlagBits::Vertex, vs)) return false;
                if (!detail::fillShaderStage("scene/skybox.frag", device_->backend(), RHI::ShaderStageFlagBits::Fragment, fs)) return false;
                d.shaders = {vs, fs};
                d.vertex_layout = detail::quadVertexLayout();
                d.rasterizer.cull_mode = RHI::CullMode::None;
                d.depth_stencil.depth_test = true;
                d.depth_stencil.depth_write = false;
                d.depth_stencil.depth_compare = RHI::CompareOp::LessOrEqual;
                d.resource_layout = skybox_layout_->desc();
                d.color_formats[0] = hdr_format;
                d.color_format_count = 1;
                d.depth_format = depth_format;
                skybox_pipeline_ = device_->createPipeline(d);
                if (!skybox_pipeline_)
                {
                    GEN_LOG_ERROR("[Render] failed to create the skybox pipeline");
                    return false;
                }
            }

            // Debug lines
            {
                RHI::PipelineDesc d;
                d.name = "debug-line";
                RHI::ShaderStageDesc vs, fs;
                if (detail::fillShaderStage("scene/debug_line.vert", device_->backend(), RHI::ShaderStageFlagBits::Vertex, vs) &&
                    detail::fillShaderStage("scene/debug_line.frag", device_->backend(), RHI::ShaderStageFlagBits::Fragment, fs))
                {
                    d.shaders = {vs, fs};
                    d.vertex_layout = detail::lineVertexLayout();
                    d.topology = RHI::PrimitiveTopology::LineList;
                    d.rasterizer.cull_mode = RHI::CullMode::None;
                    d.rasterizer.line_width = 1.0f;
                    d.depth_stencil.depth_test = true;
                    d.depth_stencil.depth_write = false;
                    d.depth_stencil.depth_compare = RHI::CompareOp::LessOrEqual;
                    d.resource_layout = line_layout_->desc();
                    d.color_formats[0] = hdr_format;
                    d.color_format_count = 1;
                    d.depth_format = depth_format;
                    line_pipeline_ = device_->createPipeline(d);
                }
            }

            // IBL bake pipelines (created lazily on first bake so that the HDR
            // float formats are only requested when they are really needed)
            return true;
        }

        bool RenderSystem::createIBLPipelines(RHI::PixelFormat cube_format, RHI::PixelFormat lut_format)
        {
            if (ibl_equirect_pipeline_ && ibl_irradiance_pipeline_ && ibl_prefilter_pipeline_ && ibl_brdf_pipeline_)
                return true;

            auto bake = [&](const char *name, const char *frag, RHI::PixelFormat color_format) -> RHI::RHIPipeline *
            {
                RHI::PipelineDesc d;
                d.name = name;
                RHI::ShaderStageDesc vs, fs;
                if (!detail::fillShaderStage("common/fullscreen.vert", device_->backend(), RHI::ShaderStageFlagBits::Vertex, vs)) return nullptr;
                if (!detail::fillShaderStage(frag, device_->backend(), RHI::ShaderStageFlagBits::Fragment, fs)) return nullptr;
                d.shaders = {vs, fs};
                d.vertex_layout = detail::quadVertexLayout();
                d.rasterizer.cull_mode = RHI::CullMode::None;
                d.depth_stencil.depth_test = false;
                d.depth_stencil.depth_write = false;
                d.resource_layout = ibl_layout_->desc();
                d.color_formats[0] = color_format;
                d.color_format_count = 1;
                d.depth_format = RHI::PixelFormat::Undefined;
                return device_->createPipeline(d);
            };

            if (!ibl_equirect_pipeline_) ibl_equirect_pipeline_ = bake("ibl.equirect_to_cube", "ibl/equirect_to_cube.frag", cube_format);
            if (!ibl_irradiance_pipeline_) ibl_irradiance_pipeline_ = bake("ibl.irradiance", "ibl/irradiance.frag", cube_format);
            if (!ibl_prefilter_pipeline_) ibl_prefilter_pipeline_ = bake("ibl.prefilter", "ibl/prefilter.frag", cube_format);
            if (!ibl_brdf_pipeline_) ibl_brdf_pipeline_ = bake("ibl.brdf_lut", "ibl/brdf_lut.frag", lut_format);

            if (!ibl_equirect_pipeline_ || !ibl_irradiance_pipeline_ || !ibl_prefilter_pipeline_ || !ibl_brdf_pipeline_)
            {
                GEN_LOG_ERROR("[Render] failed to create the IBL bake pipelines");
                return false;
            }
            return true;
        }

        //==================================================================
        // Render targets
        //==================================================================
        bool RenderSystem::createRenderTarget(RenderTarget &target, const std::string &name, uint32_t width, uint32_t height,
                                              RHI::PixelFormat color_format, bool with_depth)
        {
            width = std::max(1u, width);
            height = std::max(1u, height);

            RHI::TextureDesc cd;
            cd.type = RHI::TextureType::Tex2D;
            cd.format = color_format;
            cd.width = width;
            cd.height = height;
            cd.mip_levels = 1;
            cd.usage = (uint32_t)RHI::TextureUsageFlagBits::RenderTarget |
                       (uint32_t)RHI::TextureUsageFlagBits::Sampled |
                       (uint32_t)RHI::TextureUsageFlagBits::TransferSrc;
            cd.name = name + ".color";
            target.color = device_->createTexture(cd);
            if (!target.color) return false;

            if (with_depth)
            {
                RHI::TextureDesc dd;
                dd.type = RHI::TextureType::Tex2D;
                dd.format = RHI::PixelFormat::D24_UNORM_S8_UINT;
                dd.width = width;
                dd.height = height;
                dd.mip_levels = 1;
                dd.usage = (uint32_t)RHI::TextureUsageFlagBits::DepthStencil;
                dd.name = name + ".depth";
                target.depth = device_->createTexture(dd);
                if (!target.depth)
                {
                    // D24S8 is not universally supported; fall back to D32F.
                    dd.format = RHI::PixelFormat::D32_SFLOAT;
                    target.depth = device_->createTexture(dd);
                }
                if (!target.depth)
                {
                    device_->destroyTexture(target.color);
                    target.color = nullptr;
                    return false;
                }
            }

            RHI::RenderPassDesc rpd;
            rpd.name = name + ".pass";
            rpd.attachment_count = with_depth ? 2u : 1u;
            rpd.attachments[0].format = color_format;
            rpd.attachments[0].load_op = RHI::LoadOp::Clear;
            rpd.attachments[0].store_op = RHI::StoreOp::Store;
            rpd.attachments[0].initial_layout = RHI::TextureLayout::Undefined;
            rpd.attachments[0].final_layout = RHI::TextureLayout::ShaderReadOnlyOptimal;
            rpd.attachments[0].is_depth_stencil = false;
            if (with_depth)
            {
                rpd.attachments[1].format = target.depth->format();
                rpd.attachments[1].load_op = RHI::LoadOp::Clear;
                rpd.attachments[1].store_op = RHI::StoreOp::DontCare;
                rpd.attachments[1].stencil_load_op = RHI::LoadOp::Clear;
                rpd.attachments[1].stencil_store_op = RHI::StoreOp::DontCare;
                rpd.attachments[1].initial_layout = RHI::TextureLayout::Undefined;
                rpd.attachments[1].final_layout = RHI::TextureLayout::DepthStencilAttachmentOptimal;
                rpd.attachments[1].is_depth_stencil = true;
            }
            target.pass = device_->createRenderPass(rpd);
            if (!target.pass)
            {
                GEN_LOG_ERROR("[Render] render pass creation failed for '%s'", name.c_str());
                return false;
            }

            RHI::FramebufferDesc fbd;
            fbd.render_pass = rpd;
            fbd.width = width;
            fbd.height = height;
            fbd.name = name + ".fb";
            fbd.attachments.push_back(RHI::FramebufferDesc::Attachment{target.color, 0, 0});
            if (with_depth)
                fbd.attachments.push_back(RHI::FramebufferDesc::Attachment{target.depth, 0, 0});
            target.framebuffer = device_->createFramebuffer(fbd);
            if (!target.framebuffer)
            {
                // Float colour targets need EXT_color_buffer_float on OpenGL;
                // retry with progressively more compatible formats.
                GEN_LOG_WARN("[Render] framebuffer '%s' incomplete with format %u - retrying",
                             name.c_str(), (unsigned)color_format);
                device_->destroyTexture(target.color);
                target.color = nullptr;
                if (target.depth) { device_->destroyTexture(target.depth); target.depth = nullptr; }
                device_->destroyRenderPass(target.pass);
                target.pass = nullptr;

                RHI::PixelFormat fallbacks[] = {RHI::PixelFormat::RGBA32_SFLOAT, RHI::PixelFormat::RGBA8_UNORM};
                for (RHI::PixelFormat f : fallbacks)
                {
                    if (f == color_format) continue;
                    if (createRenderTarget(target, name, width, height, f, with_depth))
                    {
                        GEN_LOG_WARN("[Render] '%s' now uses pixel format %u", name.c_str(), (unsigned)f);
                        return true;
                    }
                }
                return false;
            }

            target.width = width;
            target.height = height;
            target.color_format = color_format;
            target.valid = true;
            target.name = name;
            return true;
        }

        bool RenderSystem::createTargets(uint32_t width, uint32_t height)
        {
            destroyTargets();
            if (!createRenderTarget(hdr_target_, "hdr", width, height, RHI::PixelFormat::RGBA16_SFLOAT, true))
            {
                GEN_LOG_ERROR("[Render] failed to create the HDR target");
                return false;
            }
            if (!createRenderTarget(capture_target_, "capture", width, height, RHI::PixelFormat::RGBA8_UNORM, false))
            {
                GEN_LOG_ERROR("[Render] failed to create the capture target");
                return false;
            }
            settings_.hdr_width = hdr_target_.width;
            settings_.hdr_height = hdr_target_.height;
            GEN_LOG_INFO("[Render] targets: hdr %ux%u (fmt %u), capture %ux%u",
                         hdr_target_.width, hdr_target_.height, (unsigned)hdr_target_.color_format,
                         capture_target_.width, capture_target_.height);
            return true;
        }

        void RenderSystem::destroyTargets()
        {
            RenderTarget *targets[2] = {&hdr_target_, &capture_target_};
            for (RenderTarget *t : targets)
            {
                if (t->framebuffer) { device_->destroyFramebuffer(t->framebuffer); t->framebuffer = nullptr; }
                if (t->pass) { device_->destroyRenderPass(t->pass); t->pass = nullptr; }
                if (t->depth) { device_->destroyTexture(t->depth); t->depth = nullptr; }
                if (t->color) { device_->destroyTexture(t->color); t->color = nullptr; }
                t->valid = false;
            }
        }

        void RenderSystem::resize(uint32_t width, uint32_t height)
        {
            if (!device_ || width == 0 || height == 0) return;
            if (hdr_target_.valid && hdr_target_.width == width && hdr_target_.height == height) return;
            device_->waitIdle();
            createTargets(width, height);
        }

        RHI::RHIFramebuffer *RenderSystem::faceFramebuffer(RHI::RHITexture *texture, uint32_t mip, uint32_t layer,
                                                           RHI::PixelFormat format, const std::string &name)
        {
            uint64_t key = (uint64_t)(uintptr_t)texture->nativeHandle();
            key = key * 1000003u ^ (uint64_t)mip;
            key = key * 1000003u ^ (uint64_t)layer;
            key = key * 1000003u ^ (uint64_t)format;
            auto it = face_framebuffers_.find(key);
            if (it != face_framebuffers_.end()) return it->second;

            uint32_t w = std::max(1u, texture->width() >> mip);
            uint32_t h = std::max(1u, texture->height() >> mip);

            RHI::RenderPassDesc rpd;
            rpd.name = name + ".pass";
            rpd.attachment_count = 1;
            rpd.attachments[0].format = format;
            rpd.attachments[0].load_op = RHI::LoadOp::Clear;
            rpd.attachments[0].store_op = RHI::StoreOp::Store;
            rpd.attachments[0].initial_layout = RHI::TextureLayout::Undefined;
            rpd.attachments[0].final_layout = RHI::TextureLayout::ColorAttachmentOptimal;
            rpd.attachments[0].is_depth_stencil = false;

            RHI::FramebufferDesc fbd;
            fbd.render_pass = rpd;
            fbd.width = w;
            fbd.height = h;
            fbd.name = name;
            fbd.attachments.push_back(RHI::FramebufferDesc::Attachment{texture, mip, layer});

            RHI::RHIFramebuffer *fb = device_->createFramebuffer(fbd);
            if (fb) face_framebuffers_[key] = fb;
            return fb;
        }

    } // namespace Render
} // namespace Genesis
