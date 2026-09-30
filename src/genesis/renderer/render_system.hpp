/*
 * Genesis Render - render system
 *
 * Responsibilities
 *   1. Synchronise the ECS world into the GPU-side RenderScene (camera,
 *      lights, environment / IBL requests, draw items).
 *   2. Own every resource layout, render target and transient allocator used
 *      by a frame (pipelines come from the PipelineCache).
 *   3. Bake image based lighting (equirect -> cubemap -> irradiance ->
 *      prefiltered specular -> BRDF LUT) with raster passes, identically on
 *      OpenGL, Vulkan and Metal.
 *   4. Execute the frame, either with the built-in forward pipeline (with
 *      CommandBuffers injected at CameraEvents) or entirely from user
 *      CommandBuffers (PipelineMode::Custom).
 *
 * Frame orientation
 *   Offscreen targets always receive NDC y = -1 in row 0 (the bottom of the
 *   view) on every backend.  Only when an image is *displayed* (present pass,
 *   capture readback, 2D screen space drawing, UI) is the orientation of the
 *   destination taken into account (TargetRef::row0_top).
 */
#ifndef __GENESIS_RENDER_SYSTEM_HPP__
#define __GENESIS_RENDER_SYSTEM_HPP__

#include "ecs/components.hpp"
#include "renderer/command_buffer.hpp"
#include "renderer/frame_resources.hpp"
#include "renderer/pipeline_cache.hpp"
#include "renderer/render_scene.hpp"
#include "renderer/render_texture.hpp"
#include "renderer/render_ubo.hpp"
#include "rhi/rhi.hpp"
#include "text/font.hpp"

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

            // Built-in post effect applied by the present pass.
            PostEffect post_effect = PostEffect::None;
            Math::Vec4 post_params = Math::Vec4(1.0f, 2.0f, 0.0f, 0.0f); // strength, radius / size, extra, extra
            Math::Color post_tint = Math::Color(1.0f, 0.85f, 0.6f, 1.0f);

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
            bool draw_text_components = true;

            PipelineMode pipeline_mode = PipelineMode::Forward;

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
            uint32_t command_buffers = 0;
            uint32_t commands = 0;
            uint32_t batched_quads = 0;
            uint32_t passes = 0;
            uint32_t feedback_skips = 0; // draws skipped because they sampled the target being rendered
            double scene_ms = 0.0;
            double present_ms = 0.0;
            uint64_t uniform_bytes = 0;
            uint64_t frame_index = 0;
        };

        //==================================================================
        class RenderSystem : public ECS::System
        {
        public:
            explicit RenderSystem(RHI::RHIDevice *device);
            ~RenderSystem() override;

            bool initialize(uint32_t width, uint32_t height, const std::string &ttf_path = "");
            void shutdown();
            void resize(uint32_t width, uint32_t height);

            const char *name() const override { return "RenderSystem"; }
            int32_t order() const override { return 200; }
            void update(ECS::World &world, float delta_time) override;

            // ---- frame -----------------------------------------------
            void syncFromWorld(ECS::World &world, float delta_time);
            void renderFrame(ECS::World &world, float delta_time, bool present_to_swapchain);
            // Renders into an internal RGBA8 target and reads it back
            // (row 0 = top of the image).  Used by tests and screenshots.
            bool renderAndCapture(ECS::World &world, float delta_time,
                                  std::vector<uint8_t> &rgba_out, uint32_t &width, uint32_t &height);

            RenderSettings &settings() { return settings_; }
            const RenderSettings &settings() const { return settings_; }
            RenderScene &scene() { return *scene_; }
            RenderStats &stats() { return stats_; }
            const RenderStats &stats() const { return stats_; }
            CameraState &camera() { return camera_; }
            RHI::RHIDevice *device() { return device_; }
            FrameAllocator &frames() { return frames_; }
            PipelineCache &pipelines() { return pipelines_; }
            Text::Font *font() { return font_.ready() ? &font_ : nullptr; }
            const RenderTexturePtr &hdrTarget() const { return hdr_target_; }
            bool iblReady() const { return ibl_ready_; }
            ECS::World *currentWorld() const { return world_; }

            // ---- IBL -------------------------------------------------
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
            bool bakeProceduralIBL(const IblBakeSettings &settings);
            const std::string &environmentSource() const { return env_source_path_; }
            void releaseIBL();

            // ---- scene registration ----------------------------------
            uint32_t registerModel(const ModelData &model, const std::string &name_hint = "");
            struct EntityMeshBinding
            {
                ECS::Entity entity;
                std::vector<uint32_t> mesh_indices;
                std::vector<int32_t> material_overrides;
            };
            void bindEntityMeshes(const EntityMeshBinding &binding);
            const std::unordered_map<uint32_t, EntityMeshBinding> &bindings() const { return bindings_; }
            const EntityMeshBinding *binding(ECS::Entity e) const;

            // ---- programmable rendering ------------------------------
            RenderTexturePtr createRenderTexture(const std::string &name, uint32_t width, uint32_t height,
                                                 bool hdr = false, bool depth = true);
            void addCommandBuffer(CameraEvent event, const CommandBufferPtr &cb);
            void removeCommandBuffer(CameraEvent event, const CommandBufferPtr &cb);
            void clearCommandBuffers();
            const std::vector<CommandBufferPtr> &commandBuffers(CameraEvent event) const
            {
                return injected_[(size_t)event];
            }
            // Executed once, next frame (Forward: before post; Custom: in
            // submission order), then dropped.
            void executeCommandBuffer(const CommandBufferPtr &cb);
            // Command buffers that build the whole frame in PipelineMode::Custom.
            void addCustomCommandBuffer(const CommandBufferPtr &cb);
            void removeCustomCommandBuffer(const CommandBufferPtr &cb);
            const std::vector<CommandBufferPtr> &customCommandBuffers() const { return custom_; }
            // Per-frame immediate buffers (cleared after each frame).
            CommandBuffer &immediate(CameraEvent event) { return immediate_[(size_t)event]; }

            // Descriptor set binding `texture` for a batch / UI draw (cached,
            // released automatically when the texture goes away).
            RHI::RHIDescriptorSet *textureSet(RHI::RHIResourceLayout *layout, RHI::RHITexture *texture,
                                              RHI::RHISampler *sampler, bool dynamic_ubo_at_binding0);
            void forgetTexture(RHI::RHITexture *texture);

            // UI hook, called inside the final pass on the presented image.
            using PostPresentCallback = void (*)(RHI::RHICommandBuffer *cmd, const TargetRef &target, void *user);
            void setPostPresentCallback(PostPresentCallback cb, void *user)
            {
                post_present_callback_ = cb;
                post_present_user_ = user;
            }

            // ---- debug drawing (flushed every frame) -----------------
            void drawLine(const Math::Vec3 &a, const Math::Vec3 &b, const Math::Color &color);
            void drawBox(const Math::AABB &box, const Math::Color &color);
            void drawSphere(const Math::Vec3 &center, float radius, const Math::Color &color, int segments = 24);
            void clearDebugLines();

            // Layouts shared with the UI renderer.
            RHI::RHIResourceLayout *uiLayout() const { return ui_layout_; }
            RHI::RHIResourceLayout *batchLayout() const { return batch_layout_; }
            RHI::RHISampler *linearClampSampler() const { return linear_clamp_; }

        private:
            friend class FrameExecutor;

            bool createLayouts();
            bool createTargets(uint32_t width, uint32_t height);
            void destroyTargets();
            RHI::RHIFramebuffer *faceFramebuffer(RHI::RHITexture *texture, uint32_t mip, uint32_t layer,
                                                 RHI::PixelFormat format, const std::string &name);
            RHI::RHITexture *createCubeTexture(uint32_t size, uint32_t mips, const std::string &name,
                                               RHI::PixelFormat &format_inout);
            void writeSceneDescriptorSet();
            void writeSkyboxDescriptorSet();
            void executeFrame(RHI::RHICommandBuffer *cmd, ECS::World &world, float delta_time, const TargetRef &present,
                              bool run_ui);
            void finishFrame();
            TargetRef swapchainTarget();

            RHI::RHIDevice *device_ = nullptr;
            std::unique_ptr<RenderScene> scene_;
            RenderSettings settings_;
            RenderStats stats_;
            CameraState camera_;
            FrameAllocator frames_;
            PipelineCache pipelines_;
            Text::Font font_;
            ECS::World *world_ = nullptr;
            bool initialized_ = false;
            bool ibl_ready_ = false;
            double time_ = 0.0;
            uint64_t exec_serial_ = 0;       // unique per executed frame (material UBO caching)
            float camera_aspect_override_ = 0.0f;

            // Resource layouts
            RHI::RHIResourceLayout *mesh_layout_ = nullptr;   // sets 0..2 of the mesh pipelines
            RHI::RHIResourceLayout *skybox_layout_ = nullptr; // set 0 scene + set 1 sky textures
            RHI::RHIResourceLayout *post_layout_ = nullptr;   // set 0: PostParams (dyn) + source
            RHI::RHIResourceLayout *ibl_layout_ = nullptr;    // set 0: IBLParams (dyn) + sources
            RHI::RHIResourceLayout *line_layout_ = nullptr;   // push constants only
            RHI::RHIResourceLayout *batch_layout_ = nullptr;  // set 0: BatchParams (dyn) + texture
            RHI::RHIResourceLayout *ui_layout_ = nullptr;     // push constants + texture

            // Long lived descriptor sets (immutable while frames are in flight)
            RHI::RHIDescriptorSet *scene_set_ = nullptr;
            RHI::RHIDescriptorSet *per_draw_set_ = nullptr;
            RHI::RHIDescriptorSet *skybox_set_ = nullptr;
            RHI::RHIDescriptorSet *ibl_set_equirect_ = nullptr;
            RHI::RHIDescriptorSet *ibl_set_cube_ = nullptr;
            struct TextureSetEntry
            {
                RHI::RHIDescriptorSet *set = nullptr;
                RHI::RHITexture *texture = nullptr;
            };
            std::unordered_map<uint64_t, TextureSetEntry> texture_sets_;

            // IBL resources
            RHI::RHITexture *env_equirect_ = nullptr;
            RHI::RHITexture *env_cubemap_ = nullptr;
            RHI::RHITexture *irradiance_cubemap_ = nullptr;
            RHI::RHITexture *prefiltered_cubemap_ = nullptr;
            RHI::RHITexture *brdf_lut_ = nullptr;
            RHI::RHISampler *ibl_sampler_ = nullptr;
            RHI::RHISampler *linear_clamp_ = nullptr;
            uint32_t prefilter_mip_levels_ = 1;
            std::string env_source_path_;
            std::unordered_map<uint64_t, RHI::RHIFramebuffer *> face_framebuffers_;

            // Targets
            RenderTexturePtr hdr_target_;     // camera HDR scene colour + depth
            RenderTexturePtr capture_target_; // RGBA8, row 0 = top (renderAndCapture)
            std::vector<std::weak_ptr<RenderTexture>> render_textures_;
            RHI::RHIRenderPass *swapchain_load_pass_ = nullptr;
            RHI::PixelFormat swapchain_load_format_ = RHI::PixelFormat::Undefined;
            std::vector<RenderTexturePtr> scratch_targets_; // blit ping-pong targets

            // Fullscreen quad
            RHI::RHIBuffer *quad_vb_ = nullptr;
            RHI::RHITexture *white_ = nullptr; // 1x1 white for untextured batches

            // Draw lists of the main camera
            std::vector<RenderItem> opaque_items_;
            std::vector<RenderItem> transparent_items_;
            std::unordered_map<uint32_t, EntityMeshBinding> bindings_;

            // Programmable rendering
            std::vector<CommandBufferPtr> injected_[(size_t)CameraEvent::Count];
            std::vector<CommandBufferPtr> once_;
            std::vector<CommandBufferPtr> custom_;
            CommandBuffer immediate_[(size_t)CameraEvent::Count];

            PostPresentCallback post_present_callback_ = nullptr;
            void *post_present_user_ = nullptr;

            // Debug lines
            struct DebugVertex
            {
                float position[3];
                float color[4];
            };
            std::vector<DebugVertex> debug_vertices_;
        };

    } // namespace Render
} // namespace Genesis

#endif /* __GENESIS_RENDER_SYSTEM_HPP__ */
