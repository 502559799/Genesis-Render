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

#include "engine/engine.hpp"
#include "reflection/engine_bindings.hpp"
#include "renderer/command_buffer.hpp"
#include "renderer/render_texture.hpp"

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
        std::string script; // extra Lua file attached as a script entity
        std::string lab;    // pre-enabled lab for screenshots: material | commandbuffer | custom | text
    };

    class DemoApp
    {
    public:
        Engine engine;
        DemoOptions options;

        bool initialize();
        void tick(float dt); // after Engine::tick: logic + command buffer recording
        void handleShortcuts();

        std::string asset(const std::string &relative) const;

    private:
        // ---- scenes -------------------------------------------------------
        void applyPendingScene();
        void loadScene(SceneKind kind);
        void resetWorld();
        void buildShowcase(bool with_monitor = true);
        void buildMinimalRig(); // camera + sun + environment
        ECS::Entity attachScript(const std::string &path, const std::string &entity_name);
        ECS::Entity spawnFile(const std::string &path, const std::string &name, const Math::Vec3 &pos, float scale);
        ECS::Entity spawnMesh(const std::string &name, const Reflect::MeshAssetPtr &mesh, const Render::MaterialPtr &mat,
                              const Math::Vec3 &pos, float scale = 1.0f);
        ECS::Entity addLabel(ECS::Entity e, const std::string &text, float height, const Math::Color &color);
        void registerLabBuffers();

        // ---- per frame ----------------------------------------------------
        void animate(float dt);
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

        // Lua console
        char console_[2048] = "";
        std::vector<std::string> console_history_;
    };

} // namespace Demo

#endif /* __GENESIS_DEMO_APP_HPP__ */
