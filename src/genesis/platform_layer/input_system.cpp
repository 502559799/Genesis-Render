/*
 * Genesis Render - platform layer implementation (input)
 */
#include "platform_layer/input_system.hpp"
#include "foundation_layer/genesis_log.hpp"

#include <cstring>

namespace Genesis
{
    namespace Platform
    {
        InputSystem::InputSystem()
        {
            std::memset(&current_, 0, sizeof(current_));
            std::memset(&previous_, 0, sizeof(previous_));
        }

        void InputSystem::beginFrame()
        {
            previous_ = current_;
            std::memcpy(mouse_previous_, mouse_current_, sizeof(mouse_current_));
            mouse_delta_ = Math::Vec2(0.0f);
            wheel_delta_ = 0.0f;
            any_key_pressed_ = false;
            text_input_.clear();
            dropped_files_.clear();
            ui_captured_mouse_this_frame = false;
        }

        void InputSystem::processEvent(const SDL_Event &event)
        {
            switch (event.type)
            {
            case SDL_DROPFILE:
                if (event.drop.file) dropped_files_.emplace_back(event.drop.file);
                break;
            case SDL_KEYDOWN:
            {
                SDL_Scancode code = event.key.keysym.scancode;
                if (code >= 0 && code < SDL_NUM_SCANCODES)
                {
                    if (!current_.keys[code]) any_key_pressed_ = true;
                    current_.keys[code] = true;
                }
                if (!event.key.repeat)
                {
                    // Reserved for future action mapping (jump/fire/...)
                }
                break;
            }
            case SDL_KEYUP:
            {
                SDL_Scancode code = event.key.keysym.scancode;
                if (code >= 0 && code < SDL_NUM_SCANCODES) current_.keys[code] = false;
                break;
            }
            case SDL_MOUSEMOTION:
                mouse_position_ = Math::Vec2((float)event.motion.x, (float)event.motion.y);
                mouse_delta_.x += (float)event.motion.xrel;
                mouse_delta_.y += (float)event.motion.yrel;
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            {
                int index = -1;
                switch (event.button.button)
                {
                case SDL_BUTTON_LEFT: index = 0; break;
                case SDL_BUTTON_MIDDLE: index = 1; break;
                case SDL_BUTTON_RIGHT: index = 2; break;
                case SDL_BUTTON_X1: index = 3; break;
                case SDL_BUTTON_X2: index = 4; break;
                default: break;
                }
                if (index >= 0 && index < kMaxMouseButtons)
                {
                    bool down = (event.type == SDL_MOUSEBUTTONDOWN);
                    mouse_current_[index] = down;
                    if (down && ui_wants_mouse) ui_captured_mouse_this_frame = true;
                }
                mouse_position_ = Math::Vec2((float)event.button.x, (float)event.button.y);
                break;
            }
            case SDL_MOUSEWHEEL:
                wheel_delta_ += (float)event.wheel.y;
                break;
            case SDL_TEXTINPUT:
                text_input_ += event.text.text;
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                {
                    // Avoid "stuck keys" when the window loses focus mid-press.
                    std::memset(&current_, 0, sizeof(current_));
                    std::memset(mouse_current_, 0, sizeof(mouse_current_));
                }
                break;
            default:
                break;
            }
        }

        bool InputSystem::mouseDown(int button) const
        {
            return (button >= 0 && button < kMaxMouseButtons) ? mouse_current_[button] : false;
        }

        bool InputSystem::mousePressed(int button) const
        {
            if (button < 0 || button >= kMaxMouseButtons) return false;
            return mouse_current_[button] && !mouse_previous_[button];
        }

        bool InputSystem::mouseReleased(int button) const
        {
            if (button < 0 || button >= kMaxMouseButtons) return false;
            return !mouse_current_[button] && mouse_previous_[button];
        }

    } // namespace Platform
} // namespace Genesis
