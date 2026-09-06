/*
 * Genesis Render - platform layer: window + input + timer (SDL2 based)
 *
 * The legacy `Genesis::Window` (application_layer/window.hpp) is kept for the
 * original demo1; the new engine uses `WindowSystem`, which additionally knows
 * which RHI backend will attach to the window so that the correct SDL window
 * flags are requested (SDL_WINDOW_OPENGL vs SDL_WINDOW_VULKAN).
 */
#ifndef __GENESIS_WINDOW_SYSTEM_HPP__
#define __GENESIS_WINDOW_SYSTEM_HPP__

#include "rhi/rhi_enums.hpp"

#include <SDL2/SDL.h>

#include <cstdint>
#include <functional>
#include <string>

struct SDL_Window;

namespace Genesis
{
    namespace Platform
    {
        struct WindowDesc
        {
            std::string title = "Genesis Render";
            uint32_t width = 1280;
            uint32_t height = 720;
            RHI::BackendType backend = RHI::BackendType::OpenGL;
            bool resizable = true;
            bool hidden = false;      // used by headless test runs
            bool high_dpi = true;
            bool fullscreen = false;
            int32_t x = SDL_WINDOWPOS_CENTERED;
            int32_t y = SDL_WINDOWPOS_CENTERED;
        };

        class WindowSystem
        {
        public:
            WindowSystem() {}
            ~WindowSystem();

            bool initialize(const WindowDesc &desc);
            void shutdown();

            // Polls SDL events.  Returns false when the user asked to quit.
            // `event_filter` is called for every event before the engine's own
            // handlers (used by the UI layer to feed ImGui).
            bool pollEvents(const std::function<void(const SDL_Event &)> &event_filter = nullptr);

            void swapBuffers();

            SDL_Window *handle() const { return window_; }
            uint32_t width() const { return width_; }
            uint32_t height() const { return height_; }
            uint32_t drawableWidth() const { return drawable_width_; }
            uint32_t drawableHeight() const { return drawable_height_; }
            float aspect() const { return drawable_height_ ? (float)drawable_width_ / (float)drawable_height_ : 1.0f; }

            bool shouldClose() const { return should_close_; }
            void requestClose() { should_close_ = true; }
            bool wasResized() const { return resized_; }
            void clearResizeFlag() { resized_ = false; }

            void setTitle(const std::string &title);
            bool isHidden() const { return hidden_; }

            static bool sdlInitialized();

        private:
            void queryDrawableSize(int *w, int *h) const;

            SDL_Window *window_ = nullptr;
            uint32_t width_ = 1;
            uint32_t height_ = 1;
            uint32_t drawable_width_ = 1;
            uint32_t drawable_height_ = 1;
            bool should_close_ = false;
            bool resized_ = false;
            bool hidden_ = false;
        };

        //==================================================================
        // High resolution timer (SDL performance counter based)
        //==================================================================
        class Timer
        {
        public:
            Timer() { reset(); }
            void reset() { start_ = SDL_GetPerformanceCounter(); last_tick_ = start_; }
            double seconds() const
            {
                return (double)(SDL_GetPerformanceCounter() - start_) / (double)SDL_GetPerformanceFrequency();
            }
            double milliseconds() const { return seconds() * 1000.0; }
            // Wall clock tick: returns the delta since the last call in seconds.
            double tick()
            {
                uint64_t now = SDL_GetPerformanceCounter();
                double dt = (double)(now - last_tick_) / (double)SDL_GetPerformanceFrequency();
                last_tick_ = now;
                return dt;
            }
            static double nowSeconds()
            {
                return (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
            }

        private:
            uint64_t start_ = 0;
            uint64_t last_tick_ = 0;
        };

    } // namespace Platform
} // namespace Genesis

#endif /* __GENESIS_WINDOW_SYSTEM_HPP__ */
