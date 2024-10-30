/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-22 18:10:32
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-10-25 18:48:21
 * @version:
 */
#include "application_layer/window.hpp"

#include "foundation_layer/log.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
// #include <SDL2/SDL_video.h>

#include "external/imgui/imgui.h"
#include "external/imgui/imgui_impl_sdl.h"

static SDL_Window *window = nullptr;

namespace Genesis
{

    static float sdl_get_monitor_refresh()
    {
        SDL_DisplayMode current;
        int should_be_zero = SDL_GetCurrentDisplayMode(0, &current);
        GENESISASSERT(!should_be_zero);
        return 1.0f / current.refresh_rate;
    }

    void Window::init(void *config)
    {
        GenesisPrint("init windowService\n");
        if (SDL_Init(SDL_INIT_EVERYTHING) != 0)
        {
            GenesisPrint("SDL initialize error: %s\n", SDL_GetError());
            return;
        }

        SDL_DisplayMode current;
        SDL_GetCurrentDisplayMode(0, &current);

        WindowConfig &win_config = *(WindowConfig *)config;
        SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);

        window = SDL_CreateWindow(win_config.title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_config.width, win_config.height, window_flags);

        GenesisPrint("Window created successfully!\n");

        int window_width, window_height;
        SDL_Vulkan_GetDrawableSize(window, &window_width, &window_height);

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