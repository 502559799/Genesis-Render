/*
 * Genesis Render - Engine
 * renderer.hpp : forward PBR renderer + runtime IBL baker.
 *
 *  - direct lighting: GGX/Cook-Torrance metallic-roughness OR Blinn-Phong
 *    (fresnel + specular + diffuse + metallic), with one directional and
 *    up to four point lights;
 *  - indirect lighting: equirectangular HDR -> environment cubemap,
 *    diffuse irradiance cubemap, pre-filtered mipmapped cubemap, BRDF LUT
 *    (split-sum approximation), all baked at runtime via the RHI.
 */
#pragma once

#include "rhi/rhi.hpp"
#include "resource/asset_manager.hpp"
#include "ecs/scene.hpp"
#include "ubos.hpp"

namespace Genesis
{
    struct RenderSettings
    {
        bool direct_lighting = true;
        bool image_based_lighting = true;
        bool show_skybox = true;
        int direct_model = 0; // 0 = PBR GGX, 1 = Blinn-Phong
        float env_intensity = 1.0f;
        Vec3 ambient{0.03f, 0.03f, 0.035f};
    };

    class Renderer
    {
    public:
        void init(IRHIDevice *device, AssetManager *assets);
        void shutdown();

        // Build (once) env/irradiance/prefilter/BRDF textures from an equirect source.
        void bakeEnvironment(TextureHandle equirect);
        bool environmentReady() const { return env_cube_.valid(); }

        // Renders scene into the RHI offscreen color+depth targets.
        // Call between IRHIDevice::beginFrame() and beginSwapchainPass().
        void renderScene(Scene &scene, const CameraComponent &camera,
                         const RenderSettings &settings, uint32_t width, uint32_t height,
                         float time);

        // Draws the offscreen scene color onto the swapchain (inside the
        // swapchain render pass, before ImGui).
        void drawBlit();

        TextureHandle envCube() const { return env_cube_; }
        TextureHandle irradiance() const { return irradiance_cube_; }
        TextureHandle prefilter() const { return prefilter_cube_; }
        TextureHandle brdfLut() const { return brdf_lut_; }

    private:
        void createPipelines();
        void buildCubeGeometry();
        void renderToCubemapFace(TextureHandle target, uint32_t face, uint32_t mip,
                                 uint32_t size, PipelineHandle pipeline,
                                 DescriptorSetHandle set0, DescriptorSetHandle set1,
                                 Vec4 bake_params, bool clear_depth);
        void ensureFrameResources();

        IRHIDevice *device_ = nullptr;
        AssetManager *assets_ = nullptr;

        // pipelines
        PipelineHandle pbr_pipeline_, skybox_pipeline_;
        PipelineHandle equirect_pipeline_, irradiance_pipeline_, prefilter_pipeline_;
        PipelineHandle brdf_pipeline_, blit_pipeline_;

        // per-frame global resources
        static constexpr uint32_t kFrames = 2;
        BufferHandle camera_ubo_[kFrames]{};
        BufferHandle light_ubo_[kFrames]{};
        DescriptorSetHandle global_set_[kFrames]{};
        DescriptorSetHandle blit_set_;
        BufferHandle bake_ubo_;
        DescriptorSetHandle equirect_set_, env_set_, bake_set1_;

        // unit cube used by skybox and the IBL baker
        BufferHandle cube_vbo_, cube_ibo_;
        uint32_t cube_index_count_ = 0;

        // IBL
        TextureHandle env_cube_, irradiance_cube_, prefilter_cube_, brdf_lut_;
        TextureHandle cube_depth_512_, cube_depth_128_, cube_depth_64_;

        RenderSettings last_settings_{};
    };
}
