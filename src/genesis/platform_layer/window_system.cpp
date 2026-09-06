/*
 * Genesis Render - platform layer implementation (window)
 */
#include "platform_layer/window_system.hpp"
#include "foundation_layer/genesis_log.hpp"

#include <SDL2/SDL_vulkan.h>

namespace Genesis
{
    namespace Platform
    {
        static bool s_sdl_initialized = false;
        static int s_sdl_refcount = 0;

        bool WindowSystem::sdlInitialized() { return s_sdl_initialized; }

        WindowSystem::~WindowSystem()
        {
            shutdown();
        }

        bool WindowSystem::initialize(const WindowDesc &desc)
        {
            if (!s_sdl_initialized)
            {
                if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
                {
                    GEN_LOG_ERROR("[Window] SDL_Init failed: %s", SDL_GetError());
                    return false;
                }
                s_sdl_initialized = true;
            }
            ++s_sdl_refcount;

            Uint32 flags = 0;
            if (desc.backend == RHI::BackendType::Vulkan) flags |= SDL_WINDOW_VULKAN;
            else flags |= SDL_WINDOW_OPENGL;
            if (desc.resizable) flags |= SDL_WINDOW_RESIZABLE;
            if (desc.hidden) flags |= SDL_WINDOW_HIDDEN;
            if (desc.high_dpi) flags |= SDL_WINDOW_ALLOW_HIGHDPI;
            if (desc.fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

            window_ = SDL_CreateWindow(desc.title.c_str(), desc.x, desc.y, (int)desc.width, (int)desc.height, flags);
            if (!window_)
            {
                GEN_LOG_ERROR("[Window] SDL_CreateWindow failed: %s", SDL_GetError());
                return false;
            }
            hidden_ = desc.hidden;
            width_ = desc.width;
            height_ = desc.height;

            int dw = 0, dh = 0;
            queryDrawableSize(&dw, &dh);
            drawable_width_ = (uint32_t)(dw > 0 ? dw : (int)desc.width);
            drawable_height_ = (uint32_t)(dh > 0 ? dh : (int)desc.height);

            GEN_LOG_INFO("[Window] created '%s' %ux%u (drawable %ux%u) backend=%s",
                         desc.title.c_str(), width_, height_, drawable_width_, drawable_height_,
                         RHI::backendTypeName(desc.backend));
            return true;
        }

        void WindowSystem::shutdown()
        {
            if (window_)
            {
                SDL_DestroyWindow(window_);
                window_ = nullptr;
            }
            if (s_sdl_refcount > 0 && --s_sdl_refcount == 0 && s_sdl_initialized)
            {
                SDL_Quit();
                s_sdl_initialized = false;
            }
        }

        void WindowSystem::queryDrawableSize(int *w, int *h) const
        {
            // SDL_GetWindowSizeInPixels only exists from SDL 2.24; the Vulkan /
            // GL specific helpers are available in every supported version.
            if (window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_VULKAN))
                SDL_Vulkan_GetDrawableSize(window_, w, h);
            else
                SDL_GL_GetDrawableSize(window_, w, h);
            if (*w <= 0 || *h <= 0) SDL_GetWindowSize(window_, w, h);
        }

        bool WindowSystem::pollEvents(const std::function<void(const SDL_Event &)> &event_filter)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                if (event_filter) event_filter(event);
                switch (event.type)
                {
                case SDL_QUIT:
                    should_close_ = true;
                    break;
                case SDL_WINDOWEVENT:
                    switch (event.window.event)
                    {
                    case SDL_WINDOWEVENT_SIZE_CHANGED:
                    case SDL_WINDOWEVENT_RESIZED:
                        width_ = (uint32_t)event.window.data1;
                        height_ = (uint32_t)event.window.data2;
                        {
                            int dw = 0, dh = 0;
                            queryDrawableSize(&dw, &dh);
                            if (dw > 0 && dh > 0)
                            {
                                drawable_width_ = (uint32_t)dw;
                                drawable_height_ = (uint32_t)dh;
                            }
                        }
                        resized_ = true;
                        break;
                    case SDL_WINDOWEVENT_CLOSE:
                        should_close_ = true;
                        break;
                    case SDL_WINDOWEVENT_MINIMIZED:
                    case SDL_WINDOWEVENT_RESTORED:
                        break;
                    default: break;
                    }
                    break;
                case SDL_APP_TERMINATING:
                case SDL_APP_LOWMEMORY:
                    should_close_ = true;
                    break;
                default:
                    break;
                }
            }
            return !should_close_;
        }

        void WindowSystem::swapBuffers()
        {
            // Only meaningful for the OpenGL backend; Vulkan presents through
            // the RHI.  SDL_GL_SwapWindow on a non-GL window is a no-op error,
            // so guard it.
            if (window_ && !(SDL_GetWindowFlags(window_) & SDL_WINDOW_VULKAN))
                SDL_GL_SwapWindow(window_);
        }

        void WindowSystem::setTitle(const std::string &title)
        {
            if (window_) SDL_SetWindowTitle(window_, title.c_str());
        }

    } // namespace Platform
} // namespace Genesis
