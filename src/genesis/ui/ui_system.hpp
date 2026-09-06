/*
 * Genesis Render - real time UI (Dear ImGui on top of the RHI)
 *
 * The UI is drawn inside the *present* render pass, right after tone mapping,
 * through RenderSystem::setPostPresentCallback().  That keeps a single pass
 * instance per frame on both backends (Vulkan would otherwise need a second
 * render pass with LoadOp::Load plus duplicate framebuffers).
 *
 * Backend integration:
 *   OpenGL : imgui_impl_opengl3 (bundled mini loader, GLSL "#version 410")
 *   Vulkan : imgui_impl_vulkan  (uses the RHI's native instance/device/queue
 *            and the swapchain render pass; this is the single place in the
 *            engine where native handles cross the RHI boundary, and it is
 *            confined to third-party integration code)
 */
#ifndef __GENESIS_UI_SYSTEM_HPP__
#define __GENESIS_UI_SYSTEM_HPP__

#include "platform_layer/input_system.hpp"
#include "rhi/rhi.hpp"

#include <SDL2/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Genesis
{
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

            bool initialize(RHI::RHIDevice *device, SDL_Window *window);
            void shutdown();

            // Feed raw SDL events (called from the window event filter).
            void processEvent(const SDL_Event &event);

            // Per frame -------------------------------------------------------
            void beginFrame(Platform::InputSystem *input, float delta_time);
            void endFrame(); // ImGui::Render()

            // Draw the accumulated ImDrawData.  Must be called from inside the
            // present render pass (see RenderSystem::setPostPresentCallback).
            void renderInto(RHI::RHICommandBuffer *cmd, uint32_t width, uint32_t height);

            bool isInitialized() const { return initialized_; }
            void setStyle(StylePreset preset);
            bool show_demo_window = false;
            bool ui_visible = true;

            // Log ring buffer surfaced in the demo's log panel.
            void pushLog(const std::string &line);
            const std::vector<std::string> &logs() const { return logs_; }
            void clearLogs() { logs_.clear(); }

            RHI::RHIDevice *device() { return device_; }

        private:
            bool createVulkanBackend(RHI::RHIDevice *device, SDL_Window *window);
            bool createOpenGLBackend(SDL_Window *window);
            void destroyNativeBackend();

            RHI::RHIDevice *device_ = nullptr;
            SDL_Window *window_ = nullptr;
            bool initialized_ = false;
            bool use_vulkan_ = false;
            void *vk_descriptor_pool_ = nullptr; // VkDescriptorPool
            std::vector<std::string> logs_;
            float time_ = 0.0f;
        };

    } // namespace UI
} // namespace Genesis

#endif /* __GENESIS_UI_SYSTEM_HPP__ */
