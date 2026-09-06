/*
 * Genesis Render - Engine
 * window.cpp : SDL2 window, created with Vulkan or OpenGL flags.
 */
#include "application_layer/window.hpp"

#include "foundation_layer/log.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

namespace Genesis
{
    void Window::init(void *config)
    {
        GenesisPrint("init window service\n");
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
        {
            GenesisPrint("SDL initialize error: %s\n", SDL_GetError());
            return;
        }

        WindowConfig &win_config = *(WindowConfig *)config;
        backend_ = win_config.backend;

        uint32_t flags = SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN;
        if (win_config.resizable) flags |= SDL_WINDOW_RESIZABLE;
        if (backend_ == RenderBackend::Vulkan)
            flags |= SDL_WINDOW_VULKAN;
        else
        {
            // Must be set before SDL_CreateWindow for an OpenGL context.
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
            SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
            flags |= SDL_WINDOW_OPENGL;
        }

        SDL_Window *window = SDL_CreateWindow(
            win_config.title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            win_config.width, win_config.height, (SDL_WindowFlags)flags);
        if (!window)
        {
            GenesisPrint("SDL_CreateWindow failed: %s\n", SDL_GetError());
            return;
        }
        platform_handle_ = window;

        int w = 0, h = 0;
        if (backend_ == RenderBackend::Vulkan)
            SDL_Vulkan_GetDrawableSize(window, &w, &h);
        else
            SDL_GL_GetDrawableSize(window, &w, &h);
        width_ = (uint32_t)w;
        height_ = (uint32_t)h;

        SDL_DisplayMode dm;
        if (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.refresh_rate > 0)
            refresh_ = 1.0f / (float)dm.refresh_rate;
    }

    void Window::shutdown()
    {
        os_message_callback_data_.destroy();
        os_message_callbacks_.destroy();
        if (platform_handle_) SDL_DestroyWindow((SDL_Window *)platform_handle_);
        platform_handle_ = nullptr;
        SDL_Quit();
    }

    void Window::setFullScreen(bool full)
    {
        SDL_SetWindowFullscreen((SDL_Window *)platform_handle_, full ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    }
    void Window::setTitle(const char *title)
    {
        SDL_SetWindowTitle((SDL_Window *)platform_handle_, title);
    }
    void Window::getDrawableSize(uint32_t &w, uint32_t &h) const
    {
        int iw = 0, ih = 0;
        if (backend_ == RenderBackend::Vulkan)
            SDL_Vulkan_GetDrawableSize((SDL_Window *)platform_handle_, &iw, &ih);
        else
            SDL_GL_GetDrawableSize((SDL_Window *)platform_handle_, &iw, &ih);
        w = (uint32_t)iw; h = (uint32_t)ih;
    }

    void Window::register_os_message_callback(OsMessageCallback callback, void *data)
    {
        os_message_callbacks_.push(callback);
        os_message_callback_data_.push(data);
    }
    void Window::unregister_os_message_callback(OsMessageCallback callback)
    {
        for (uint32_t i = 0; i < os_message_callbacks_.size(); i++)
            if (os_message_callbacks_[i] == callback)
            {
                os_message_callbacks_.delete_swap(i);
                os_message_callback_data_.delete_swap(i);
                return;
            }
    }
}
