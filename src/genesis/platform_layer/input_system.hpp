/*
 * Genesis Render - platform layer: input
 *
 * Thin state machine on top of SDL2 events.  The UI layer sets
 * `ui_wants_mouse` / `ui_wants_keyboard` every frame (from ImGui's io flags)
 * so gameplay can avoid acting on clicks that the UI already consumed - the
 * same pattern Piccolo uses with its EditorInputManager.
 */
#ifndef __GENESIS_INPUT_SYSTEM_HPP__
#define __GENESIS_INPUT_SYSTEM_HPP__

#include "foundation_layer/math/math.hpp"

#include <SDL2/SDL.h>

#include <string>

namespace Genesis
{
    namespace Platform
    {
        class InputSystem
        {
        public:
            static constexpr int kMaxMouseButtons = 5;

            InputSystem();

            // Frame lifecycle ------------------------------------------------
            void beginFrame();                 // clears per-frame edge states
            void processEvent(const SDL_Event &event);

            // Keyboard -------------------------------------------------------
            bool keyDown(SDL_Scancode code) const { return current_.keys[code]; }
            bool keyPressed(SDL_Scancode code) const { return current_.keys[code] && !previous_.keys[code]; }
            bool keyReleased(SDL_Scancode code) const { return !current_.keys[code] && previous_.keys[code]; }
            bool anyKeyPressed() const { return any_key_pressed_; }

            // Convenience aliases used by the demo and by Lua
            bool wDown() const { return keyDown(SDL_SCANCODE_W); }
            bool aDown() const { return keyDown(SDL_SCANCODE_A); }
            bool sDown() const { return keyDown(SDL_SCANCODE_S); }
            bool dDown() const { return keyDown(SDL_SCANCODE_D); }
            bool shiftDown() const { return keyDown(SDL_SCANCODE_LSHIFT) || keyDown(SDL_SCANCODE_RSHIFT); }
            bool ctrlDown() const { return keyDown(SDL_SCANCODE_LCTRL) || keyDown(SDL_SCANCODE_RCTRL); }
            bool altDown() const { return keyDown(SDL_SCANCODE_LALT) || keyDown(SDL_SCANCODE_RALT); }
            bool escapePressed() const { return keyPressed(SDL_SCANCODE_ESCAPE); }
            bool spaceDown() const { return keyDown(SDL_SCANCODE_SPACE); }

            // Mouse ----------------------------------------------------------
            bool mouseDown(int button) const;
            bool mousePressed(int button) const;
            bool mouseReleased(int button) const;
            bool leftDown() const { return mouseDown(0); }
            bool rightDown() const { return mouseDown(2); }

            Math::Vec2 mousePosition() const { return mouse_position_; }
            Math::Vec2 mouseDelta() const { return mouse_delta_; }
            float wheelDelta() const { return wheel_delta_; }

            // UI consumption flags (written by the UI system each frame)
            bool ui_wants_mouse = false;
            bool ui_wants_keyboard = false;
            // When true the UI consumed the current mouse event; gameplay should
            // ignore it (e.g. do not start an orbit drag).
            bool ui_captured_mouse_this_frame = false;

            // Text input (forwarded to ImGui / editor fields)
            const std::string &textInput() const { return text_input_; }

        private:
            struct KeyState
            {
                bool keys[SDL_NUM_SCANCODES] = {false};
            };
            KeyState current_;   // state at the end of the current frame
            KeyState previous_;  // state at the beginning of the current frame

            bool mouse_current_[kMaxMouseButtons] = {false};
            bool mouse_previous_[kMaxMouseButtons] = {false};

            Math::Vec2 mouse_position_ = Math::Vec2(0.0f);
            Math::Vec2 mouse_delta_ = Math::Vec2(0.0f);
            float wheel_delta_ = 0.0f;
            bool any_key_pressed_ = false;
            std::string text_input_;
        };

    } // namespace Platform
} // namespace Genesis

#endif /* __GENESIS_INPUT_SYSTEM_HPP__ */
