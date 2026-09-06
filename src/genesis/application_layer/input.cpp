/*
 * @Descripttion: 输入系统实现
 */
#include "application_layer/input.hpp"

#include "foundation_layer/log.hpp"

#include <SDL2/SDL.h>
#include <cstring>

namespace Genesis
{
    Input *Input::instance()
    {
        static Input s_input;
        return &s_input;
    }

    void Input::init(void *config)
    {
        (void)config;
        keyboard_state_ = SDL_GetKeyboardState(nullptr);
        GenesisPrint("init input service\n");
    }

    void Input::shutdown()
    {
    }

    bool Input::pollEvents()
    {
        mouse_dx_ = 0;
        mouse_dy_ = 0;
        wheel_delta_ = 0;

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (m_event_callback)
                m_event_callback(&event);

            switch (event.type)
            {
            case SDL_QUIT:
                quit_requested_ = true;
                break;
            case SDL_MOUSEMOTION:
                mouse_dx_ += (float)event.motion.xrel;
                mouse_dy_ += (float)event.motion.yrel;
                break;
            case SDL_MOUSEWHEEL:
                wheel_delta_ += (float)event.wheel.y;
                break;
            default:
                break;
            }
        }

        mouse_state_ = SDL_GetMouseState(nullptr, nullptr);
        int x, y;
        SDL_GetMouseState(&x, &y);
        mouse_x_ = (float)x;
        mouse_y_ = (float)y;

        return !quit_requested_;
    }

    bool Input::isKeyDown(const char *key_name) const
    {
        if (!keyboard_state_ || !key_name)
            return false;
        SDL_Scancode scancode = SDL_GetScancodeFromName(key_name);
        if (scancode == SDL_SCANCODE_UNKNOWN)
            return false;
        return keyboard_state_[scancode] != 0;
    }

    bool Input::isMouseButtonDown(int button) const
    {
        return (mouse_state_ & SDL_BUTTON(button)) != 0;
    }

    void Input::getMousePosition(float &x, float &y) const
    {
        x = mouse_x_;
        y = mouse_y_;
    }

    void Input::getMouseDelta(float &dx, float &dy) const
    {
        dx = mouse_dx_;
        dy = mouse_dy_;
    }
}
