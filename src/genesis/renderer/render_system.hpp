/*
 * Genesis Render - render system
 *
 * Responsibilities
 *   1. Synchronise the ECS world into the GPU-side RenderScene (upload new
 *      meshes/materials, collect lights, pick the active camera).
 *   2. Own every pipeline / descriptor set / render target used by a frame.
 *   3. Bake image based lighting (equirect -> cubemap -> irradiance ->
 *      prefiltered specular -> BRDF LUT) with plain raster passes, so the same
 *      code runs on OpenGL (which has no compute in the 4.1 profile we target)
 *      and Vulkan.
 *   4. Execute the frame: HDR scene pass (skybox + opaque + transparent) then
 *      a tone mapping pass into the present target.
 *
 * Frame orientation
 *   Every offscreen render target uses "row 0 == top of the image" on both
 *   backends.  CameraState::update() applies the required projection Y flip
 *   (always on Vulkan, only for offscreen targets on OpenGL).  The final
 *   present pass into the OpenGL default framebuffer flips the sampling V
 *   coordinate, because GL's window origin is bottom-left.
 */
#ifndef __GENESIS_RENDER_SYSTEM_HPP__
#define __GENESIS_RENDER_SYSTEM_HPP__

#include "ecs/components.hpp"
#include "renderer/render_scene.hpp"
#include "renderer/render_ubo.hpp"
#include "rhi/rhi.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Genesis
{
    namespace Render
    {
        //==================================================================
        struct RenderSettings
        {
            ShadingMode shading_mode = ShadingMode::CookTorrance;
            TonemapMode tonemap_mode = TonemapMode::Aces;
            SkyMode sky_mode = SkyMode::Procedural;

            float exposure = 1.0f;
            float gamma = 2.2f;
            float saturation = 1.0f;
            float vignette = 0.0f;

            Math::Color ambient_color = Math::Color(0.03f, 0.035f, 0.045f);
            float ambient_intensity = 1.0f;
            Math::Color sky_tint = Math::Color(0.35f, 0.55f, 0.95f);
            Math::Color ground_tint = Math::Color(0.10f, 0.09f, 0.08f);

            float env_intensity = 1.0f;
            float env_rotation_degrees = 0.0f;
            bool use_ibl = true;
            bool show_skybox = true;
            bool wireframe = false;
            bool frustum_culling = true;
            bool flip_post_uv = false; // computed per frame from backend + target

            uint32_t hdr_width = 1280;
            uint32_t hdr_height = 720;
        };

        //==================================================================
        struct RenderStats
        {
            uint32_t draw_calls = 0;
            uint32_t triangles = 0;
            uint32_t opaque_items = 0;
            uint32_t transparent_items = 0;
            uint32_t culled_items = 0;
            uint32_t active_lights = 0;
            uint32_t meshes = 0;
            uint32_t materials = 0;
            double scene_ms = 0.0;
            double present_ms = 0.0;
            uint64_t frame_index = 0;
        };

        //==================================================================
        // A render target = colour texture(s) + depth texture + pass + FBO
        //==================================================================
        struct RenderTarget
        {
            RHI::RHITexture *color = nullptr;
            RHI::RHITexture *depth = nullptr;
            RHI::RHIRenderPass *pass = nullptr;
            RHI::RHIFramebuffer *framebuffer = nullptr;
            uint32_t width = 0;
            uint32_t height = 0;
            RHI::PixelFormat color_format = RHI::PixelFormat::RGBA16_SFLOAT;
            bool valid = false;
            std::string name;
        };

        //==================================================================
        class RenderSystem : public ECS::System
        {
        public:
            explicit RenderSystem(RHI::RHIDevice *device);
            ~RenderSystem() override;

            bool initialize(uint32_t width, uint32_t height);
            void shutdown();
            void resize(uint32_t width, uint32_t height);

            const char *name() const override { return "RenderSystem"; }
            int32_t order() const override { return 200; }
            void update(ECS::World &world, float delta_time) override;

            // ---- explicit control -------------------------------------
            void syncFromWorld(ECS::World &world, float delta_time);
            void renderFrame(ECS::World &world, float delta_time, bool present_to_swapchain);
            // Renders the scene into an internal RGBA8 target and reads it back.
            // Used by the headless verification harness.
            bool renderAndCapture(ECS::World &world, float delta_time,
                                  std::vector<uint8_t> &rgba_out, uint32_t &width, uint32_t &height);

            RenderSettings &settings() { return settings_; }
            const RenderSettings &settings() const { return settings_; }
            RenderScene &scene() { return *scene_; }
            RenderStats &stats() { return stats_; }
            const RenderStats &stats() const { return stats_; }
            CameraState &camera() { return camera_; }
            RHI::RHIDevice *device() { return device_; }
            const RenderTarget &hdrTarget() const { return hdr_target_; }
            bool iblReady() const { return ibl_ready_; }

            // ---- IBL --------------------------------------------------
            struct IblBakeSettings
            {
                uint32_t cubemap_size = 256;
                uint32_t irradiance_size = 64;
                uint32_t prefiltered_size = 256;
                uint32_t brdf_lut_size = 256;
                uint32_t prefilter_samples = 256;
                uint32_t brdf_samples = 512;
            };
            bool bakeIBL(const std::string &hdr_equirect_path, const IblBakeSettings &settings);
            bool bakeIBLFromImage(const ImageData &image, const IblBakeSettings &cfg);
            const std::string &environmentSource() const { return env_source_path_; }
            // Procedural fallback: bake from a CPU generated gradient sky.
            bool bakeProceduralIBL(const IblBakeSettings &settings);
            void releaseIBL();

            // ---- GPU upload helpers -----------------------------------
            uint32_t registerModel(const ModelData &model, const std::string &name_hint = "");
            // Associates an ECS entity with previously registered mesh indices.
            struct EntityMeshBinding
            {
                ECS::Entity entity;
                std::vector<uint32_t> mesh_indices;
                std::vector<int32_t> material_overrides;
            };
            void bindEntityMeshes(const EntityMeshBinding &binding);
            const std::unordered_map<uint32_t, EntityMeshBinding> &bindings() const { return bindings_; }

            // Fullscreen quad (position vec2 + uv vec2), shared by every pass.
            RHI::RHIBuffer *fullscreenQuadBuffer() const { return quad_vb_; }
            uint32_t fullscreenQuadVertexCount() const { return 6; }

            // Called inside the present render pass right after tone mapping,
            // so the UI can be composited without a second pass instance.
            using PostPresentCallback = void (*)(RHI::RHICommandBuffer *cmd, uint32_t width, uint32_t height, void *user);
            void setPostPresentCallback(PostPresentCallback cb, void *user)
            {
                post_present_callback_ = cb;
                post_present_user_ = user;
            }

            // Debug drawing (immediate mode lines, flushed at the end of the frame)
            void drawLine(const Math::Vec3 &a, const Math::Vec3 &b, const Math::Color &color);
            void drawBox(const Math::AABB &box, const Math::Color &color);
            void drawSphere(const Math::Vec3 &center, float radius, const Math::Color &color, int segments = 24);
            void clearDebugLines();

        private:
            bool createPipelines();
            bool createIBLPipelines(RHI::PixelFormat cube_format, RHI::PixelFormat lut_format);
            // The tone mapping pipeline is created per colour format because a
            // Vulkan pipeline must be compatible with the render pass it is
            // used in (the swapchain may be BGRA8 while the capture target is
            // RGBA8).
            RHI::RHIPipeline *presentPipeline(RHI::PixelFormat color_format);
            bool createPersistentResources();
            bool createTargets(uint32_t width, uint32_t height);
            void destroyTargets();
            bool createRenderTarget(RenderTarget &target, const std::string &name, uint32_t width, uint32_t height,
                                    RHI::PixelFormat color_format, bool with_depth);
            RHI::RHIFramebuffer *faceFramebuffer(RHI::RHITexture *texture, uint32_t mip, uint32_t layer,
                                                 RHI::PixelFormat format, const std::string &name);

            void updatePerFrameUBO(float delta_time);
            void updateLightsUBO(ECS::World &world);
            void updateEnvironmentUBO();
            void gatherRenderItems(ECS::World &world);

            void renderScenePass(RHI::RHICommandBuffer *cmd, RHI::RHIRenderPass *pass, RHI::RHIFramebuffer *fb,
                                 uint32_t width, uint32_t height, const Math::Color &clear_color);
            void renderPresentPass(RHI::RHICommandBuffer *cmd, RHI::RHIRenderPass *pass, RHI::RHIFramebuffer *fb,
                                   uint32_t width, uint32_t height, bool flip_uv);
            void renderDebugLines(RHI::RHICommandBuffer *cmd);

            void syncMaterialSettings();
            void writeSceneDescriptorSet();
            void writeSkyboxDescriptorSet();
            RHI::RHITexture *createCubeTexture(uint32_t size, uint32_t mips, const std::string &name,
                                               RHI::PixelFormat &format_inout);
            void writePerDrawDescriptorSet();
            uint64_t allocatePerDrawSlot();
            void *perDrawSlotPointer(uint64_t offset);

            RHI::RHIDevice *device_ = nullptr;
            std::unique_ptr<RenderScene> scene_;
            RenderSettings settings_;
            RenderStats stats_;
            CameraState camera_;
            bool initialized_ = false;
            bool ibl_ready_ = false;

            // Resource layouts
            RHI::RHIResourceLayout *mesh_layout_ = nullptr;    // combined sets 0..2 used by mesh pipelines
            RHI::RHIResourceLayout *scene_layout_ = nullptr;   // set 0 for mesh/skybox pipelines
            RHI::RHIResourceLayout *material_layout_ = nullptr; // set 1 for mesh pipelines
            RHI::RHIResourceLayout *per_draw_layout_ = nullptr; // set 2 for mesh pipelines
            RHI::RHIResourceLayout *skybox_layout_ = nullptr;   // set 1 for the skybox pipeline
            RHI::RHIResourceLayout *post_layout_ = nullptr;     // set 0 for tone mapping
            RHI::RHIResourceLayout *ibl_layout_ = nullptr;      // set 0 for IBL bakes
            RHI::RHIResourceLayout *line_layout_ = nullptr;     // push constants only

            // Pipelines
            RHI::RHIPipeline *pbr_opaque_ = nullptr;
            RHI::RHIPipeline *pbr_opaque_two_sided_ = nullptr;
            RHI::RHIPipeline *pbr_alpha_mask_ = nullptr;
            RHI::RHIPipeline *pbr_blend_ = nullptr;
            RHI::RHIPipeline *skybox_pipeline_ = nullptr;
            RHI::RHIPipeline *tonemap_pipeline_ = nullptr;
            std::unordered_map<uint32_t, RHI::RHIPipeline *> present_pipelines_;
            RHI::RHIPipeline *unlit_pipeline_ = nullptr;
            RHI::RHIPipeline *line_pipeline_ = nullptr;
            RHI::RHIPipeline *ibl_equirect_pipeline_ = nullptr;
            RHI::RHIPipeline *ibl_irradiance_pipeline_ = nullptr;
            RHI::RHIPipeline *ibl_prefilter_pipeline_ = nullptr;
            RHI::RHIPipeline *ibl_brdf_pipeline_ = nullptr;

            // Descriptor sets
            RHI::RHIDescriptorSet *scene_set_ = nullptr;
            RHI::RHIDescriptorSet *per_draw_set_ = nullptr;
            RHI::RHIDescriptorSet *skybox_set_ = nullptr;
            RHI::RHIDescriptorSet *post_set_ = nullptr;
            RHI::RHIDescriptorSet *ibl_set_ = nullptr;

            // Uniform buffers
            RHI::RHIBuffer *per_frame_ubo_ = nullptr;
            RHI::RHIBuffer *lights_ubo_ = nullptr;
            RHI::RHIBuffer *environment_ubo_ = nullptr;
            RHI::RHIBuffer *per_draw_ring_ = nullptr;
            RHI::RHIBuffer *post_ubo_ = nullptr;
            RHI::RHIBuffer *ibl_ubo_ = nullptr;
            uint64_t per_draw_stride_ = 256;
            uint64_t per_draw_offset_ = 0;
            uint64_t per_draw_frame_base_ = 0;
            float last_shading_mode_ = -1.0f;
            static constexpr uint32_t kPerDrawSlots = 4096;

            // IBL resources
            RHI::RHITexture *env_equirect_ = nullptr;
            RHI::RHITexture *env_cubemap_ = nullptr;
            RHI::RHITexture *irradiance_cubemap_ = nullptr;
            RHI::RHITexture *prefiltered_cubemap_ = nullptr;
            RHI::RHITexture *brdf_lut_ = nullptr;
            RHI::RHISampler *ibl_sampler_ = nullptr;
            uint32_t prefilter_mip_levels_ = 1;
            std::string env_source_path_;

            // Targets
            RenderTarget hdr_target_;
            RenderTarget capture_target_; // RGBA8, used by renderAndCapture
            std::unordered_map<uint64_t, RHI::RHIFramebuffer *> face_framebuffers_;

            // Fullscreen quad
            RHI::RHIBuffer *quad_vb_ = nullptr;

            // Per-frame draw list
            std::vector<RenderItem> opaque_items_;
            std::vector<RenderItem> transparent_items_;
            std::unordered_map<uint32_t, EntityMeshBinding> bindings_;
            std::vector<ECS::Entity> pending_destroy_;

            PostPresentCallback post_present_callback_ = nullptr;
            void *post_present_user_ = nullptr;

            // Debug lines
            struct DebugVertex
            {
                float position[3];
                float color[4];
            };
            std::vector<DebugVertex> debug_vertices_;
            RHI::RHIBuffer *debug_vb_ = nullptr;
            uint64_t debug_vb_capacity_ = 0;
        };

    } // namespace Render
} // namespace Genesis

#endif /* __GENESIS_RENDER_SYSTEM_HPP__ */
