/*
 * Genesis Render - Engine
 * ui_service.hpp : ImGui context + backend hookup (Vulkan / OpenGL via RHI)
 *                  and the in-engine editor panels.
 */
#pragma once

#include "rhi/rhi.hpp"
#include "ecs/scene.hpp"
#include "render/renderer.hpp"

#include <functional>
#include <string>

struct ImGuiContext;

namespace Genesis
{
    class UIService
    {
    public:
        void init(IRHIDevice *device);
        void shutdown();
        void newFrame();
        void renderDrawData(); // inside the swapchain render pass
        ImGuiContext *context() const { return ctx_; }

    private:
        IRHIDevice *device_ = nullptr;
        ImGuiContext *ctx_ = nullptr;
    };

    struct EditorState
    {
        IRHIDevice *device = nullptr;
        Scene *scene = nullptr;
        Entity selected{};
        RenderSettings *settings = nullptr;
        float frame_ms = 0.0f;
        int fps = 0;
        std::string model_path;
        std::string lua_error;
        std::function<void(const std::string &)> loadModelAt;
        std::function<void(const std::string &)> addPrimitive;
        std::function<void()> reloadScripts;
        std::function<void(const std::string &)> loadEnvironment;
        std::string env_path;
    };

    void drawEditor(EditorState &state);
}
