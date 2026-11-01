/*
 * Genesis Render - engine facade implementation
 */
#include "engine/engine.hpp"
#include "animation/animation_system.hpp"
#include "api/compute_api.hpp"
#include "api/encoder_decoder_api.hpp"
#include "api/ray_tracing_api.hpp"
#include "api/rendering_api.hpp"
#include "foundation_layer/genesis_log.hpp"
#include "resource/asset_package.hpp"
#include "resource/resource_types.hpp"
#include "reflection/engine_bindings.hpp"
#include "script/js_system.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace Genesis
{
    namespace
    {
        void uiPostPresentHook(RHI::RHICommandBuffer *cmd, const Render::TargetRef &target, void *user)
        {
            Engine *engine = static_cast<Engine *>(user);
            if (engine) engine->ui().renderInto(cmd, target);
        }
    } // namespace

    Engine::Engine() {}

    Engine::~Engine()
    {
        shutdown();
    }

    bool Engine::initialize(const EngineConfig &config)
    {
        config_ = config;
        timer_.reset();

        // ---- window -----------------------------------------------------
        Platform::WindowDesc wdesc;
        wdesc.title = config_.app_name;
        wdesc.width = config_.width;
        wdesc.height = config_.height;
        wdesc.backend = config_.backend;
        wdesc.hidden = config_.headless;
        // Offscreen renders (tests, screenshots) must produce exactly the
        // requested pixel size, so Retina backing scale is only used on screen.
        wdesc.high_dpi = !config_.headless;
        wdesc.resizable = true;
        if (!window_.initialize(wdesc))
        {
            GEN_LOG_ERROR("[Engine] window initialisation failed");
            return false;
        }

        // ---- RHI device -------------------------------------------------
        RHI::DeviceDesc ddesc;
        ddesc.backend = config_.backend;
        ddesc.native_window = window_.handle();
        ddesc.width = window_.drawableWidth();
        ddesc.height = window_.drawableHeight();
        // GENESIS_VALIDATION=1 turns on API validation for any run (tests,
        // screenshots): Vulkan validation layers / per-command GL checks.
        ddesc.enable_validation = config_.enable_validation || std::getenv("GENESIS_VALIDATION") != nullptr;
        ddesc.headless = config_.headless;
        ddesc.vsync = config_.vsync;
        ddesc.srgb_backbuffer = config_.srgb_backbuffer;
        ddesc.app_name = config_.app_name;
        device_ = RHI::RHIDevice::create(ddesc);
        if (!device_)
        {
            GEN_LOG_ERROR("[Engine] failed to create the %s backend", RHI::backendTypeName(config_.backend));
            return false;
        }

        // ---- world + systems -------------------------------------------
        // The scheduler owns every system; `order()` decides the tick sequence
        // (Script 50 -> CameraController 90 -> Transform 100 -> Render 200).
        world_ = std::make_unique<ECS::World>("main");

        render_ = world_->scheduler().addSystem<Render::RenderSystem>(device_);
        // Resolve the SDF font: explicit path, or a default under the asset dir.
        std::string font_path = config_.sdf_font_path;
        if (font_path.empty())
        {
            std::string candidate = ResourceIO::joinPath(config_.asset_dir, "fonts/DejaVuSans.ttf");
            if (ResourceIO::fileExists(candidate)) font_path = std::move(candidate);
        }
        if (!render_->initialize(window_.drawableWidth(), window_.drawableHeight(), font_path))
        {
            GEN_LOG_ERROR("[Engine] render system initialisation failed");
            return false;
        }

        // The four public APIs.  Rendering/Compute are available immediately;
        // ray tracing and codecs initialise lazily but are reported here.
        rendering_ = std::make_unique<API::RenderingAPI>();
        rendering_->initialize(device_, render_, world_.get());
        compute_ = std::make_unique<API::ComputeAPI>();
        compute_->initialize(device_);
        ray_tracing_ = std::make_unique<API::RayTracingAPI>();
        ray_tracing_->initialize(compute_.get(), device_);
        encoder_decoder_ = std::make_unique<API::EncoderDecoderAPI>();
        encoder_decoder_->initialize();

        // Everything scripts / tools reach through RTTI.
        Reflect::registerEngineTypes();
        Reflect::context() = Reflect::EngineContext{this, world_.get(), render_, &input_};

        physics_ = world_->scheduler().addSystem<Physics::PhysicsSystem>();
        physics_->initialize(world_.get());

        animation_ = world_->scheduler().addSystem<Animation::AnimationSystem>();
        animation_->initialize(device_, render_, compute_.get());

        if (config_.enable_script)
        {
            script_ = world_->scheduler().addSystem<Script::ScriptSystem>();
            if (!script_->initialize(world_.get(), render_, &input_))
            {
                GEN_LOG_WARN("[Engine] Lua initialisation failed - continuing without scripting");
                script_ = nullptr;
                config_.enable_script = false;
            }
        }

#if GENESIS_ENABLE_V8
        if (config_.enable_script && config_.enable_js)
        {
            js_ = world_->scheduler().addSystem<Script::JSSystem>();
            if (!js_->initialize(world_.get(), render_, &input_))
            {
                GEN_LOG_WARN("[Engine] V8 initialisation failed - continuing without JavaScript");
                js_ = nullptr;
            }
        }
#endif

        camera_controller_system_ = world_->scheduler().addSystem<ECS::CameraControllerSystem>(&input_);
        transform_system_ = world_->scheduler().addSystem<ECS::TransformSystem>();

        // ---- UI ---------------------------------------------------------
        if (config_.enable_ui)
        {
            if (!ui_.initialize(device_, render_, window_.handle()))
            {
                GEN_LOG_WARN("[Engine] UI initialisation failed - continuing without ImGui");
                config_.enable_ui = false;
            }
            else
            {
                render_->setPostPresentCallback(uiPostPresentHook, this);
            }
        }

        initialized_ = true;
        GEN_LOG_INFO("[Engine] ready: %s %ux%u backend=%s ui=%d script=%d",
                     config_.app_name.c_str(), window_.drawableWidth(), window_.drawableHeight(),
                     RHI::backendTypeName(config_.backend), (int)config_.enable_ui, (int)config_.enable_script);
        return true;
    }

    void Engine::shutdown()
    {
        if (!initialized_ && !device_) return;
        if (device_) device_->waitIdle();

        if (config_.enable_ui) ui_.shutdown();
        if (Reflect::context().engine == this) Reflect::context() = Reflect::EngineContext();

        // RenderSystem must release its GPU objects before the device dies.
        if (render_) render_->shutdown();
        if (script_) script_->shutdown();
        if (js_) js_->shutdown();
        if (animation_) animation_->shutdown();
        if (ray_tracing_) ray_tracing_->shutdown();
        render_ = nullptr;
        script_ = nullptr;
        js_ = nullptr;
        physics_ = nullptr;
        animation_ = nullptr;
        transform_system_ = nullptr;
        camera_controller_system_ = nullptr;

        if (world_)
        {
            world_->clear();
            world_.reset();
        }

        ray_tracing_.reset();
        encoder_decoder_.reset();
        if (compute_) compute_->shutdown();
        compute_.reset();
        rendering_.reset();
        if (device_)
        {
            device_->shutdown();
            delete device_;
            device_ = nullptr;
        }
        window_.shutdown();
        initialized_ = false;
    }

    bool Engine::tick()
    {
        if (!initialized_) return false;

        double dt_seconds = timer_.tick();
        float delta_time = stepDelta(dt_seconds);
        frame_ms_ = dt_seconds * 1000.0;

        fps_accumulator_ += dt_seconds;
        ++fps_frames_;
        if (fps_accumulator_ >= 0.5)
        {
            fps_ = (float)((double)fps_frames_ / fps_accumulator_);
            fps_accumulator_ = 0.0;
            fps_frames_ = 0;
        }

        // ---- input ------------------------------------------------------
        input_.beginFrame();
        window_.pollEvents([this](const SDL_Event &event)
                           {
            if (config_.enable_ui) ui_.processEvent(event);
            input_.processEvent(event); });

        if (window_.shouldClose() || quit_requested_) return false;
        if (input_.escapePressed() && !input_.ui_wants_keyboard) return false;

        // ---- resize -----------------------------------------------------
        if (window_.wasResized())
        {
            uint32_t w = window_.drawableWidth();
            uint32_t h = window_.drawableHeight();
            if (w > 0 && h > 0)
            {
                device_->resizeSwapchain(w, h);
                render_->resize(w, h);
            }
            window_.clearResizeFlag();
        }

        // ---- UI frame (must be finished before the render pass records it)
        if (config_.enable_ui)
        {
            ui_.beginFrame(&input_, delta_time);
            if (ui_draw_callback_) ui_draw_callback_(*this);
            ui_.endFrame();
        }

        // ---- simulation + render ---------------------------------------
        if (!device_->beginFrame())
        {
            GEN_LOG_WARN("[Engine] device->beginFrame() failed; skipping this frame");
            return true;
        }
        world_->update(delta_time);
        render_->renderFrame(*world_, delta_time, !config_.headless);
        device_->endFrame();

        ++frame_count_;
        if (config_.max_frames > 0 && frame_count_ >= config_.max_frames) return false;
        return true;
    }

    void Engine::run()
    {
        while (tick())
        {
        }
        GEN_LOG_INFO("[Engine] main loop finished after %llu frames", (unsigned long long)frame_count_);
    }

    double Engine::uptimeSeconds() const
    {
        return config_.fixed_delta_time > 0.0f ? sim_time_ : timer_.seconds();
    }

    float Engine::stepDelta(double real_seconds)
    {
        float dt = config_.fixed_delta_time > 0.0f ? config_.fixed_delta_time
                                                   : (float)std::min<double>(real_seconds, config_.max_delta_time);
        sim_time_ += dt;
        return dt;
    }

    bool Engine::captureFrame(std::vector<uint8_t> &rgba_out, uint32_t &width, uint32_t &height)
    {
        if (!initialized_ || !render_) return false;
        float delta_time = stepDelta(timer_.tick());
        // Same order as tick(): UI frame, simulation systems, render.
        if (config_.enable_ui)
        {
            ui_.beginFrame(&input_, delta_time);
            if (ui_draw_callback_) ui_draw_callback_(*this);
            ui_.endFrame();
        }
        world_->update(delta_time);
        return render_->renderAndCapture(*world_, delta_time, rgba_out, width, height);
    }

    bool Engine::saveScreenshot(const std::string &path)
    {
        std::vector<uint8_t> rgba;
        uint32_t w = 0, h = 0;
        if (!captureFrame(rgba, w, h)) return false;
        std::string ext = ResourceIO::pathExtension(path);
        if (ext == "tga") return ResourceIO::writeImageTGA(path, w, h, rgba.data());
        return ResourceIO::writeImagePNG(path, w, h, rgba.data());
    }

    AssetPackagePtr Engine::loadAssetPackage(const std::string &path, std::string *error)
    {
        AssetPackagePtr package = std::make_shared<AssetPackage>();
        if (!package->loadFromFile(path, error)) return nullptr;

        std::string safe_name = package->name();
        for (char &c : safe_name)
            if (!(std::isalnum((unsigned char)c) || c == '-' || c == '_')) c = '_';
        const std::string root = ResourceIO::joinPath(ResourceIO::joinPath(config_.asset_dir, ".package_cache"), safe_name);
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        if (!package->extractAll(root, error))
        {
            GEN_LOG_ERROR("[Package] extraction failed for %s", path.c_str());
            return nullptr;
        }
        unloadAssetPackage();
        active_package_ = package;
        package_root_ = root;
        GEN_LOG_INFO("[Package] '%s' ready at %s", package->name().c_str(), root.c_str());
        return package;
    }

    void Engine::unloadAssetPackage()
    {
        active_package_.reset();
        package_root_.clear();
    }

    ECS::Entity Engine::spawnModel(const std::string &path, const Math::Vec3 &position, float scale,
                                   const std::string &name_override)
    {
        std::shared_ptr<ModelData> model = ResourceIO::loadModel(path);
        if (!model)
        {
            GEN_LOG_ERROR("[Engine] spawnModel failed for '%s'", path.c_str());
            return ECS::nullEntity();
        }

        std::string name = name_override.empty() ? ResourceIO::pathFilename(path) : name_override;
        ECS::Entity entity = world_->createEntity(name);

        ECS::TransformComponent transform;
        transform.position = position;
        transform.scale = Math::Vec3(scale);
        world_->addComponent<ECS::TransformComponent>(entity, transform);

        ECS::MeshComponent mesh;
        mesh.model = model;
        mesh.visible = true;
        mesh.pick_id = entity.id + 1;
        world_->addComponent<ECS::MeshComponent>(entity, mesh);

        std::vector<uint32_t> gpu_meshes = render_->scene().uploadModel(*model, name);
        Render::RenderSystem::EntityMeshBinding binding;
        binding.entity = entity;
        binding.mesh_indices = gpu_meshes;
        render_->bindEntityMeshes(binding);

        GEN_LOG_INFO("[Engine] spawned '%s' from %s (%zu meshes, %zu triangles)", name.c_str(), path.c_str(),
                     model->meshes.size(),
                     [&] { size_t t = 0; for (const auto &m : model->meshes) t += m.indices.size() / 3; return t; }());
        return entity;
    }

} // namespace Genesis
