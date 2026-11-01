/*
 * Genesis Render - engine facade
 *
 * Owns the subsystems and drives the frame:
 *
 *   input.beginFrame -> window.pollEvents -> ui.beginFrame -> [demo UI panels]
 *   -> ui.endFrame (ImGui::Render) -> device.beginFrame -> world.update
 *   (ScriptSystem 50 -> CameraControllerSystem 90 -> TransformSystem 100 ->
 *    RenderSystem 200 sync) -> RenderSystem.renderFrame (HDR pass + present
 *    pass, with the ImGui draw data composited inside the present pass)
 *   -> device.endFrame (submit + present)
 */
#ifndef __GENESIS_ENGINE_HPP__
#define __GENESIS_ENGINE_HPP__

#include "api/engine_apis.hpp"
#include "ecs/components.hpp"
#include "ecs/systems.hpp"
#include "platform_layer/input_system.hpp"
#include "physics/physics.hpp"
#include "platform_layer/window_system.hpp"
#include "renderer/render_system.hpp"
#include "rhi/rhi.hpp"
#include "script/script_system.hpp"
#include "ui/ui_system.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Genesis
{
    class AssetPackage;

    namespace Script
    {
        class JSSystem;
    }
    namespace API
    {
        class ComputeAPI;
        class RenderingAPI;
        class RayTracingAPI;
        class EncoderDecoderAPI;
    }
    namespace Animation
    {
        class AnimationSystem;
    }
    using AssetPackagePtr = std::shared_ptr<AssetPackage>;

    struct EngineConfig
    {
        std::string app_name = "Genesis Render";
        std::string asset_dir = "assets";
        uint32_t width = 1280;
        uint32_t height = 720;
        RHI::BackendType backend = RHI::BackendType::OpenGL;
        bool headless = false;          // hidden window, no presentation
        bool vsync = true;
        bool enable_validation = false;
        bool srgb_backbuffer = false;
        bool enable_ui = true;
        bool enable_script = true;
        bool enable_js = true;
        float max_delta_time = 0.1f;    // clamp huge frame steps (e.g. after a stall)
        float fixed_delta_time = 0.0f;  // > 0: every frame advances by exactly this (recording)
        uint32_t max_frames = 0;        // 0 == run until the window closes
        std::string sdf_font_path;      // TTF for SDF text; "" disables text/overlay
    };

    class Engine
    {
    public:
        Engine();
        ~Engine();

        bool initialize(const EngineConfig &config);
        void shutdown();

        // Runs until the window closes or `max_frames` is reached.
        void run();
        // Executes exactly one frame.  Returns false when the engine wants to quit.
        bool tick();

        void requestQuit() { quit_requested_ = true; }
        bool quitRequested() const { return quit_requested_; }

        // Subsystem access
        ECS::World &world() { return *world_; }
        Render::RenderSystem &render() { return *render_; }
        UI::UISystem &ui() { return ui_; }
        Script::ScriptSystem &script() { return *script_; }
        bool hasScript() const { return script_ != nullptr; }
        Script::JSSystem &js() { return *js_; }
        bool hasJS() const { return js_ != nullptr; }
        EngineAPIs apis()
        {
            return EngineAPIs{rendering_.get(), compute_.get(), ray_tracing_.get(), encoder_decoder_.get()};
        }
        API::RenderingAPI &rendering() { return *rendering_; }
        API::ComputeAPI &compute() { return *compute_; }
        API::RayTracingAPI &rayTracing() { return *ray_tracing_; }
        API::EncoderDecoderAPI &encoderDecoder() { return *encoder_decoder_; }
        Animation::AnimationSystem &animation() { return *animation_; }
        bool hasAnimation() const { return animation_ != nullptr; }
        Physics::PhysicsSystem &physics() { return *physics_; }
        bool hasPhysics() const { return physics_ != nullptr; }
        Platform::InputSystem &input() { return input_; }
        Platform::WindowSystem &window() { return window_; }
        RHI::RHIDevice *device() { return device_; }
        const EngineConfig &config() const { return config_; }
        bool isInitialized() const { return initialized_; }

        // Demo / editor hook: called every frame between ImGui::NewFrame and
        // ImGui::Render, so panels can be built with the regular ImGui API.
        using UiDrawCallback = std::function<void(Engine &)>;
        void setUiDrawCallback(UiDrawCallback cb) { ui_draw_callback_ = cb; }

        // Statistics
        // With a fixed time step these report the simulated rate.
        float fps() const { return config_.fixed_delta_time > 0.0f ? 1.0f / config_.fixed_delta_time : fps_; }
        double frameTimeMs() const { return config_.fixed_delta_time > 0.0f ? config_.fixed_delta_time * 1000.0 : frame_ms_; }
        uint64_t frameCount() const { return frame_count_; }
        double uptimeSeconds() const;

        // Offscreen capture (RGBA8, row 0 == top of the image).
        bool captureFrame(std::vector<uint8_t> &rgba_out, uint32_t &width, uint32_t &height);
        bool saveScreenshot(const std::string &path); // .png or .tga by extension

        // ---- dynamic asset packages ---------------------------------
        // Loads a .gpkg and extracts it into <asset_dir>/.package_cache/<name>
        // so that the existing file based importers can consume it.  The
        // package stays alive (and is exposed to the package browser) until
        // unloadAssetPackage().
        AssetPackagePtr loadAssetPackage(const std::string &path, std::string *error = nullptr);
        void unloadAssetPackage();
        const AssetPackagePtr &activePackage() const { return active_package_; }
        const std::string &packageRoot() const { return package_root_; }

        // Loads a model, creates an entity for it and wires up the GPU meshes.
        ECS::Entity spawnModel(const std::string &path, const Math::Vec3 &position = Math::Vec3(0.0f),
                               float scale = 1.0f, const std::string &name_override = "");

    private:
        EngineConfig config_;
        Platform::WindowSystem window_;
        Platform::InputSystem input_;
        Platform::Timer timer_;
        UI::UISystem ui_;
        RHI::RHIDevice *device_ = nullptr;
        std::unique_ptr<ECS::World> world_;
        // Systems are owned by the world's scheduler; the engine keeps raw
        // pointers for direct access (render settings, capture, scripting).
        Render::RenderSystem *render_ = nullptr;
        Script::ScriptSystem *script_ = nullptr;
        Script::JSSystem *js_ = nullptr;
        Animation::AnimationSystem *animation_ = nullptr;
        Physics::PhysicsSystem *physics_ = nullptr;
        ECS::TransformSystem *transform_system_ = nullptr;
        std::unique_ptr<API::RenderingAPI> rendering_;
        std::unique_ptr<API::ComputeAPI> compute_;
        std::unique_ptr<API::RayTracingAPI> ray_tracing_;
        std::unique_ptr<API::EncoderDecoderAPI> encoder_decoder_;
        ECS::CameraControllerSystem *camera_controller_system_ = nullptr;

        AssetPackagePtr active_package_;
        std::string package_root_;

        UiDrawCallback ui_draw_callback_;
        bool initialized_ = false;
        bool quit_requested_ = false;
        uint64_t frame_count_ = 0;
        float fps_ = 0.0f;
        double frame_ms_ = 0.0;
        double sim_time_ = 0.0;
        float stepDelta(double real_seconds);
        double fps_accumulator_ = 0.0;
        uint32_t fps_frames_ = 0;
    };

} // namespace Genesis

#endif /* __GENESIS_ENGINE_HPP__ */
