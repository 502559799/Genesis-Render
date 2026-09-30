/*
 * Genesis Render - real time UI (Dear ImGui rendered through the RHI)
 *
 * ImGui's draw data is rendered by the engine itself (shaders/ui/imgui.*)
 * using only RHI objects, so the UI works identically on OpenGL, Vulkan and
 * Metal and needs no backend specific integration code.  The UI is drawn at
 * the very end of the frame into the presented image; the target's
 * orientation (TargetRef::row0_top) and the HiDPI framebuffer scale are
 * honoured, so screenshots / captures contain the UI upright as well.
 *
 * Textures: an ImTextureID is an RHI::RHITexture*.  Use UI::image() to show
 * a RenderTexture (it flips v because rendered images are stored bottom-up).
 */
#ifndef __GENESIS_UI_SYSTEM_HPP__
#define __GENESIS_UI_SYSTEM_HPP__

#include "platform_layer/input_system.hpp"
#include "renderer/render_texture.hpp"
#include "rhi/rhi.hpp"

#include <SDL2/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Genesis
{
    namespace Render
    {
        class RenderSystem;
    }

    namespace UI
    {
        enum class StylePreset : uint32_t
        {
            Dark = 0,
            Light,
            Genesis,
        };

        class UISystem
        {
        public:
            UISystem() {}
            ~UISystem();

            bool initialize(RHI::RHIDevice *device, Render::RenderSystem *render, SDL_Window *window);
            void shutdown();

            void processEvent(const SDL_Event &event);

            // Per frame
            void beginFrame(Platform::InputSystem *input, float delta_time);
            void endFrame(); // ImGui::Render()

            // Draws the accumulated ImDrawData into `target` (inside an open
            // render pass on that target).
            void renderInto(RHI::RHICommandBuffer *cmd, const Render::TargetRef &target);

            bool isInitialized() const { return initialized_; }
            void setStyle(StylePreset preset);
            bool show_demo_window = false;
            bool ui_visible = true;

            void pushLog(const std::string &line);
            const std::vector<std::string> &logs() const { return logs_; }
            void clearLogs() { logs_.clear(); }

            RHI::RHIDevice *device() { return device_; }
            uint32_t drawCallsLastFrame() const { return last_draw_calls_; }

        private:
            RHI::RHIDevice *device_ = nullptr;
            Render::RenderSystem *render_ = nullptr;
            SDL_Window *window_ = nullptr;
            RHI::RHITexture *font_texture_ = nullptr;
            bool initialized_ = false;
            std::vector<std::string> logs_;
            float time_ = 0.0f;
            uint32_t last_draw_calls_ = 0;
        };

        // ImGui::Image for a render texture (upright).
        void image(const Render::RenderTexturePtr &texture, float width, float height);

    } // namespace UI
} // namespace Genesis

#endif /* __GENESIS_UI_SYSTEM_HPP__ */
