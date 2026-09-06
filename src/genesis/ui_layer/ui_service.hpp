/*
 * @Descripttion: UI 服务: ImGui 实时交互界面, 双后端 (Vulkan / OpenGL3)
 * @version: v1.0
 */
#ifndef __GENESIS_UI_SERVICE_H__
#define __GENESIS_UI_SERVICE_H__

#include "application_layer/window.hpp"
#include "rhi_layer/rhi_device.hpp"

union SDL_Event;

namespace Genesis
{
    namespace ui
    {
        class UIService
        {
        public:
            UIService() = default;
            ~UIService() = default;

            bool init(Window *window, rhi::RHIDevice *device);
            void shutdown();

            void processEvent(SDL_Event *event);
            void newFrame();
            // 在交换链 pass 内绘制 UI (loadOp=LOAD)
            void render();

            bool wantCaptureMouse() const;
            bool wantCaptureKeyboard() const;

        private:
            Window *m_window = nullptr;
            rhi::RHIDevice *m_device = nullptr;
            bool m_initialized = false;
        };

    } // namespace ui
} // namespace Genesis

#endif // __GENESIS_UI_SERVICE_H__
