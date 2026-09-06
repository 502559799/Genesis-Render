/*
 * Genesis Render - real-time editor UI (Dear ImGui, both RHI backends).
 * Panels: scene hierarchy, component inspector, render settings, asset
 * import, stats and help.
 */
#pragma once

#include "ecs/scene.hpp"
#include "rhi/rhi.hpp"
#include <functional>
#include <string>

namespace Genesis {

class Window;
class AssetManager;
class PBRRenderer;

class EditorUI {
public:
    void init(IDevice* device, Window* window);
    void shutdown();

    void processSdlEvent(void* sdlEvent);
    void newFrame(Window* window);
    void renderEditor(Scene& scene, AssetManager& assets, PBRRenderer& renderer);
    void renderDrawData(); // called inside the final RHI render pass

    // UI -> app requests
    std::function<void(const std::string&)> onImportModel;
    std::function<void()> onResetCamera;
    std::function<void()> onToggleBackend;
    std::function<void(const std::string&)> onRunScript;

    char importPath[512] = "assets/models/DamagedHelmet.glb";
    char scriptPath[256] = "rotate.lua";
    bool showDemo = false;
    bool wantCaptureMouse() const;

private:
    IDevice* device_ = nullptr;
    void* window_ = nullptr;
    bool initialized_ = false;

    uint32_t selected_ = 0;
    float fps_ = 0.0f;
};

} // namespace Genesis
