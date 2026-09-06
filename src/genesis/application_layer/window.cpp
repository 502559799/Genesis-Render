/*
 * Genesis Render - cross-platform window implementation (SDL2).
 */
#include "application_layer/window.hpp"

#include "foundation_layer/log.hpp"

#include <SDL.h>
#include <SDL_vulkan.h>
#include <cstdlib>
#include <cstring>

namespace Genesis {

void Window::init(const WindowConfig& config) {
    backend_ = config.backend;
    headless_ = config.headless;
    width_ = config.width;
    height_ = config.height;

    if (headless_) {
        // Allow SDL (timers/joystick) without a display server.
#ifdef _WIN32
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
#else
        ::setenv("SDL_VIDEODRIVER", "dummy", 0);
#endif
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        GenesisPrint("SDL initialize error: %s\n", SDL_GetError());
        return;
    }

    if (headless_) {
        // A dummy window keeps SDL event/timer APIs valid but never appears.
        platform_handle_ = SDL_CreateWindow(config.title, 0, 0, 64, 64, SDL_WINDOW_HIDDEN);
        return;
    }

    uint32_t flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (backend_ == RHIBackend::Vulkan) {
        flags |= SDL_WINDOW_VULKAN;
    } else {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
#ifdef __APPLE__
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1); // macOS cap
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        flags |= SDL_WINDOW_OPENGL;
    }

    SDL_Window* win = SDL_CreateWindow(config.title, SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, config.width,
                                       config.height, (SDL_WindowFlags)flags);
    if (!win) {
        GenesisPrint("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return;
    }
    platform_handle_ = win;

    int dw = 0, dh = 0;
    if (backend_ == RHIBackend::Vulkan)
        SDL_Vulkan_GetDrawableSize(win, &dw, &dh);
    else
        SDL_GL_GetDrawableSize(win, &dw, &dh);
    width_ = (uint32_t)dw;
    height_ = (uint32_t)dh;
}

void Window::register_os_message_callback(OsMessageCallback callback, void* data) {
    os_message_callbacks_.push_back(callback);
    os_message_callback_data_.push_back(data);
}

void Window::pollEvents() {
    input_.mouse_dx = 0;
    input_.mouse_dy = 0;
    input_.scroll = 0;
    input_.resized = false;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        for (size_t i = 0; i < os_message_callbacks_.size(); ++i)
            os_message_callbacks_[i](&event, os_message_callback_data_[i]);

        switch (event.type) {
        case SDL_QUIT: should_close_ = true; break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                int dw = 0, dh = 0;
                SDL_Window* win = (SDL_Window*)platform_handle_;
                if (backend_ == RHIBackend::Vulkan)
                    SDL_Vulkan_GetDrawableSize(win, &dw, &dh);
                else
                    SDL_GL_GetDrawableSize(win, &dw, &dh);
                width_ = (uint32_t)dw;
                height_ = (uint32_t)dh;
                input_.resized = true;
            }
            break;
        case SDL_KEYDOWN:
            if (event.key.keysym.scancode < 512) input_.keys[event.key.keysym.scancode] = true;
            if (event.key.keysym.sym == SDLK_ESCAPE) should_close_ = true;
            break;
        case SDL_KEYUP:
            if (event.key.keysym.scancode < 512) input_.keys[event.key.keysym.scancode] = false;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button < 5) input_.mouse_buttons[event.button.button] = true;
            break;
        case SDL_MOUSEBUTTONUP:
            if (event.button.button < 5) input_.mouse_buttons[event.button.button] = false;
            break;
        case SDL_MOUSEMOTION:
            input_.mouse_x = (float)event.motion.x;
            input_.mouse_y = (float)event.motion.y;
            input_.mouse_dx += (float)event.motion.xrel;
            input_.mouse_dy += (float)event.motion.yrel;
            break;
        case SDL_MOUSEWHEEL:
            input_.scroll += (float)event.wheel.y;
            break;
        default: break;
        }
    }
}

bool Window::consumeResize(uint32_t& w, uint32_t& h) {
    if (!input_.resized) return false;
    w = width_;
    h = height_;
    input_.resized = false;
    return true;
}

void Window::shutdown() {
    if (platform_handle_) SDL_DestroyWindow((SDL_Window*)platform_handle_);
    platform_handle_ = nullptr;
    SDL_Quit();
}

} // namespace Genesis
