/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-22 18:10:32
 * @Descripttion: 窗口服务实现, 按图形后端选择 SDL 窗口标志
 * @version: v2.0
 */
#include "application_layer/window.hpp"

#include "foundation_layer/log.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

static SDL_Window *window = nullptr;

namespace Genesis
{

    static float sdl_get_monitor_refresh()
    {
        SDL_DisplayMode current;
        int should_be_zero = SDL_GetCurrentDisplayMode(0, &current);
        if (should_be_zero != 0)
            return 1.0f / 60.0f;
        return 1.0f / (current.refresh_rate ? current.refresh_rate : 60);
    }

    void Window::init(void *config)
    {
        GenesisPrint("init windowService\n");
        if (SDL_Init(SDL_INIT_EVERYTHING) != 0)
        {
            GenesisPrint("SDL initialize error: %s\n", SDL_GetError());
            return;
        }

        WindowConfig &win_config = *(WindowConfig *)config;
        graphics_api_ = win_config.graphics_api;

        // 事件回调数组初始化 (allocator 缺省时不注册回调也能安全 shutdown)
        if (win_config.allocator)
        {
            os_message_callbacks_.init(win_config.allocator, 8);
            os_message_callback_data_.init(win_config.allocator, 8);
        }

        Uint32 window_flags = SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;

        if (graphics_api_ == WindowGraphicsApi::Vulkan)
        {
            window_flags |= SDL_WINDOW_VULKAN;
        }
        else
        {
            window_flags |= SDL_WINDOW_OPENGL;
            // core profile: macOS 最高 4.1, Linux (mesa) 支持 4.5
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
#ifdef __APPLE__
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#else
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
#endif
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
            SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        }

        if (win_config.hidden)
            window_flags |= SDL_WINDOW_HIDDEN;

        window = SDL_CreateWindow(win_config.title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_config.width, win_config.height, (SDL_WindowFlags)window_flags);
        if (!window)
        {
            GenesisPrint("SDL_CreateWindow error: %s\n", SDL_GetError());
            return;
        }

        GenesisPrint("Window created successfully!\n");

        int window_width, window_height;
        if (graphics_api_ == WindowGraphicsApi::Vulkan)
            SDL_Vulkan_GetDrawableSize(window, &window_width, &window_height);
        else
            SDL_GL_GetDrawableSize(window, &window_width, &window_height);

        width_ = (uint32_t)window_width;
        height_ = (uint32_t)window_height;

        platform_handle_ = window;

        refresh_ = sdl_get_monitor_refresh();
    }

    void Window::shutdown()
    {
        os_message_callback_data_.destroy();
        os_message_callbacks_.destroy();
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    void Window::setFullScreen(bool full)
    {
        SDL_SetWindowFullscreen(window, full ? SDL_WINDOW_FULLSCREEN : 0);
    }

    void Window::register_os_message_callback(OsMessageCallback callback, void *data)
    {
        os_message_callbacks_.push(callback);
        os_message_callback_data_.push(data);
    }

    void Window::unregister_os_message_callback(OsMessageCallback callback)
    {
        GENESISASSERT_MSG(os_message_callbacks_.size() < 8, "This array is too big for a linear search. Consider using something different.");
        for (uint32_t i = 0; i < os_message_callbacks_.size(); i++)
        {
            if (os_message_callbacks_[i] == callback)
            {
                os_message_callbacks_.delete_swap(i);
                os_message_callback_data_.delete_swap(i);
                return;
            }
        }
    }
}
