/*
 * Genesis Render - interactive demo application.
 *
 * Every engine capability has an entry point here:
 *
 *   Scenes          C++ showcase / scene built purely by Lua through RTTI /
 *                   CommandBuffer + custom pipeline Lua demo / empty
 *   Material Editor material library, live preview (render texture drawn by a
 *                   command buffer), reflected parameters, texture slots
 *                   (files or render textures), assign to entities
 *   Inspector       entity hierarchy + every component edited through RTTI
 *   CommandBuffer   render-to-texture security camera, post effects, the
 *   Lab             programmable (custom) pipeline, statistics
 *   SDF Text Lab    2D screen text and 3D world text (billboard / oriented)
 *   Lua Console     runs snippets against the reflected engine API
 */
#ifndef __GENESIS_DEMO_APP_HPP__
#define __GENESIS_DEMO_APP_HPP__

#include "api/encoder_decoder_api.hpp"
#include "api/ray_tracing_api.hpp"
#include "media/media_player.hpp"
#include "engine/engine.hpp"
#include "reflection/engine_bindings.hpp"
#include "renderer/command_buffer.hpp"
#include "renderer/gaussian_splat.hpp"
#include "renderer/render_texture.hpp"
#include "script/js_system.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Demo
{
    using namespace Genesis;

#ifndef GENESIS_ASSET_DIR
#define GENESIS_ASSET_DIR "assets"
#endif

    enum class SceneKind
    {
        Showcase = 0,      // C++ built scene (models, materials, labels)
        LuaScene = 1,      // assets/scripts/scene_builder.lua (pure RTTI)
        LuaCommandBuffer,  // showcase + assets/scripts/commandbuffer_demo.lua
        Physics,           // rigid bodies, colliders, hinge, debug shapes
        Game,              // physics mini-game loaded from an asset package
        Gaussian,          // 3D gaussian splat viewer
        Package,           // generic .gpkg scene loaded at runtime
        Animation,         // 2D sprite + 3D compute vertex animation
        RayTracing,        // GPU compute ray tracer page
        Media,             // libav* video + audio playback
        Empty,
        Count
    };
    const char *sceneName(SceneKind k);
    bool sceneFromName(const std::string &name, SceneKind &out);

    struct DemoOptions
    {
        RHI::BackendType backend = RHI::BackendType::OpenGL;
        bool headless = false;
        bool validation = false;
        bool enable_ui = true;
        bool enable_script = true;
        uint32_t frames = 0;
        uint32_t width = 1280;
        uint32_t height = 720;
        std::string screenshot;
        std::string showcase_dir;
        std::string asset_dir = GENESIS_ASSET_DIR;
        SceneKind scene = SceneKind::Showcase;
        std::string script; // extra script file attached as a script entity (.lua / .js)
        std::string package; // .gpkg loaded by --scene package / game
        std::string lab;    // pre-enabled lab: material | commandbuffer | custom | text | physics | gaussian | package
        bool selftest = false;
        bool tour = false;        // scripted feature walkthrough (camera, material edits, pipelines, scenes)
        std::string record_dir;   // save every frame as DIR/frame_NNNN.png (fixed time step)
        float record_fps = 15.0f;
    };

    class DemoApp
    {
    public:
        Engine engine;
        DemoOptions options;

        bool initialize();
        void tick(float dt); // after Engine::tick: logic + command buffer recording
        void handleShortcuts();
        // Drives every scene, panel and interaction path for a few frames each
        // (used by --selftest); returns the number of errors logged meanwhile.
        int selfTest();
        // Length of the --tour timeline in seconds.
        static constexpr float kTourSeconds = 16.0f;

        std::string asset(const std::string &relative) const;

    private:
        // ---- scenes -------------------------------------------------------
        void applyPendingScene();
        void loadScene(SceneKind kind);
        void resetWorld();
        void buildShowcase(bool with_monitor = true);
        void buildMinimalRig(); // camera + sun + environment
        void buildPhysicsScene();
        void buildGaussianScene();
        void buildGameScene();
        void buildAnimationScene();
        void buildRayTracingScene();
        void buildMediaScene();
        bool loadPackage(const std::string &path, std::string *error = nullptr);
        void buildFromActivePackage();
        void recordPhysicsDebug();
        void recordGaussian();
        void recordRayTracing();
        void updateMedia(float dt);
        void refreshPackageList();
        ECS::Entity attachScript(const std::string &path, const std::string &entity_name);
        ECS::Entity spawnFile(const std::string &path, const std::string &name, const Math::Vec3 &pos, float scale);
        ECS::Entity spawnMesh(const std::string &name, const Reflect::MeshAssetPtr &mesh, const Render::MaterialPtr &mat,
                              const Math::Vec3 &pos, float scale = 1.0f);
        ECS::Entity addLabel(ECS::Entity e, const std::string &text, float height, const Math::Color &color);
        void registerLabBuffers();

        // ---- per frame ----------------------------------------------------
        void animate(float dt);
        void tourStep(float dt);
        void recordMaterialPreview();
        void recordMonitor();
        void recordCustomPipeline();
        void recordTextLab();

        // ---- UI -------------------------------------------------------------
        void drawUI(Engine &e);
        void drawMenuBar();
        void drawEnginePanel();
        void drawRenderPanel();
        void drawInspectorPanel();
        void drawMaterialPanel();
        void drawCommandBufferPanel();
        void drawTextPanel();
        void drawPhysicsPanel();
        void drawGaussianPanel();
        void drawPackagePanel();
        void drawGamePanel();
        void drawAnimationPanel();
        void drawRayTracingPanel();
        void drawCodecPanel();
        void drawApisPanel();
        void drawMediaPanel();
        void drawConsolePanel();
        void drawLogPanel();
        void drawHelpPanel();
        void drawTextureSlot(const Render::MaterialPtr &mat, Render::TextureSlot slot);
        void runConsole(const std::string &code);
        Render::MaterialPtr selectedMaterial();
        Render::MaterialPtr entityMaterial(ECS::Entity e);

        // ---- state ----------------------------------------------------------
        SceneKind scene_ = SceneKind::Showcase;
        SceneKind pending_scene_ = SceneKind::Count; // Count = nothing pending
        float time_ = 0.0f;
        float tour_time_ = 0.0f;
        int tour_phase_ = -1;

        ECS::Entity camera_;
        ECS::Entity sun_;
        ECS::Entity warm_;
        ECS::Entity cool_;
        ECS::Entity selected_;
        bool animate_lights_ = true;
        float sun_elevation_ = 45.0f;
        float sun_azimuth_ = 35.0f;

        // windows
        bool show_engine_ = true;
        bool show_render_ = false;
        bool show_inspector_ = false;
        bool show_material_ = false;
        bool show_cb_ = false;
        bool show_text_ = false;
        bool show_physics_ = false;
        bool show_gaussian_ = false;
        bool show_package_ = false;
        bool show_game_ = false;
        bool show_animation_ = false;
        bool show_raytracing_ = false;
        bool show_codec_ = false;
        bool show_apis_ = false;
        bool show_media_ = false;
        bool show_console_ = false;
        bool show_log_ = false;
        bool show_help_ = false;

        // Material editor
        int material_index_ = -1;
        bool scroll_to_material_ = true;
        char material_filter_[64] = "";
        int preview_mesh_ = 0; // sphere | box | torus | helmet
        bool preview_rotate_ = true;
        bool preview_sky_ = true;
        float preview_yaw_ = 0.0f;
        Render::RenderTexturePtr preview_hdr_;
        Render::RenderTexturePtr preview_ldr_;
        Render::CommandBufferPtr preview_cb_;
        std::vector<Reflect::MeshAssetPtr> preview_meshes_;
        std::vector<std::string> texture_files_;

        // CommandBuffer lab
        bool monitor_enabled_ = true;
        Render::RenderTexturePtr monitor_rt_;
        Render::CommandBufferPtr monitor_cb_;
        Render::MaterialPtr monitor_mat_;
        ECS::Entity monitor_screen_;
        bool custom_pipeline_ = false;
        bool applied_custom_ = false; // last value written to RenderSettings::pipeline_mode
        int custom_effect_a_ = (int)Render::PostEffect::Edge;
        int custom_effect_b_ = (int)Render::PostEffect::None;
        bool custom_pip_ = true;
        bool custom_split_ = false;
        Render::RenderTexturePtr custom_scene_;
        Render::RenderTexturePtr custom_ldr_;
        Render::RenderTexturePtr custom_fx_;
        Render::CommandBufferPtr custom_cb_;
        Render::CommandBufferPtr hud_cb_;

        // SDF text lab
        bool text2d_enabled_ = true;
        char text2d_[256] = "Genesis SDF text - 2D";
        float text2d_size_ = 42.0f;
        float text2d_pos_[2] = {360.0f, 440.0f};
        Math::Color text2d_color_ = Math::Color(1.0f, 0.85f, 0.3f, 1.0f);
        float text2d_outline_ = 0.2f;
        Math::Color text2d_outline_color_ = Math::Color(0.0f, 0.0f, 0.0f, 1.0f);
        int text2d_align_ = 0;
        bool text3d_enabled_ = true;
        char text3d_[256] = "SDF text in 3D";
        float text3d_size_ = 0.45f;
        bool text3d_billboard_ = false;
        bool text3d_spin_ = true;
        bool text3d_depth_ = true;
        float text3d_outline_ = 0.15f;
        Math::Vec3 text3d_pos_ = Math::Vec3(0.0f, 3.2f, 0.0f);
        Math::Color text3d_color_ = Math::Color(0.3f, 0.9f, 1.0f, 1.0f);

        // Physics lab
        bool physics_debug_ = true;
        bool physics_paused_ = false;
        int physics_spawn_kind_ = 0; // 0 sphere, 1 box, 2 capsule
        float physics_spawn_height_ = 6.0f;
        float physics_spawn_spread_ = 0.5f;
        uint64_t physics_last_steps_ = 0;

        // Gaussian splat lab
        Gaussian::GaussianSplatCloud gaussian_cloud_;
        Gaussian::GaussianSplatRenderer gaussian_renderer_;
        Gaussian::SplatSettings gaussian_settings_;
        Render::CommandBufferPtr gaussian_cb_;
        std::string gaussian_path_;
        char gaussian_path_input_[512] = "";
        bool gaussian_auto_orbit_ = true;

        // Packages / mini-game
        std::vector<std::string> package_files_;
        int package_index_ = 0;
        std::string package_manifest_;
        std::string package_status_;
        bool package_scene_loaded_ = false;
        std::string game_status_;

        // Animation lab
        ECS::Entity anim_sprite_ = ECS::nullEntity();
        ECS::Entity anim_vertex_ = ECS::nullEntity();
        bool sprite_playing_ = true;
        float sprite_fps_ = 16.0f;
        float vertex_amplitude_ = 0.22f;
        float vertex_frequency_ = 2.2f;
        float vertex_speed_ = 2.0f;
        bool vertex_animation_enabled_ = true;

        // Ray tracing lab
        Render::CommandBufferPtr rt_cb_;
        API::RTSettings rt_settings_;
        std::vector<API::RTSphere> rt_scene_;
        bool rt_auto_trace_ = true;
        bool rt_trace_requested_ = true;
        std::string rt_status_;
        double rt_last_ms_ = 0.0;

        // Media player lab
        std::unique_ptr<Media::MediaPlayer> media_player_;
        Render::CommandBufferPtr media_cb_;
        RHI::RHITexture *media_texture_ = nullptr;
        RHI::RHISampler *media_sampler_ = nullptr;
        uint64_t media_serial_ = 0;
        int media_texture_width_ = 0;
        int media_texture_height_ = 0;
        float media_volume_ = 0.8f;
        char media_path_[512] = "";
        std::string media_status_;
        bool no_audio_device_ = false;

        // Codec lab
        std::string codec_status_;
        bool codec_busy_ = false;
        bool codec_capture_requested_ = false;

        // Console language: 0 Lua, 1 JavaScript
        int console_language_ = 0;
        char console_[2048] = "";
        std::vector<std::string> console_history_;
    };

} // namespace Demo

#endif /* __GENESIS_DEMO_APP_HPP__ */
