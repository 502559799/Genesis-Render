/*
 * Genesis Render - cross-platform window / platform layer
 * One Window class drives both RHI backends:
 *   - Vulkan  : SDL_WINDOW_VULKAN + VkSurfaceKHR (created by the backend)
 *   - OpenGL  : SDL_WINDOW_OPENGL, core 4.1+ context created by the backend
 *   - headless: no real window; the GL backend makes an EGL pbuffer context
 *               (Linux) and Vulkan renders purely offscreen (CI / tests).
 */
#pragma once

#include <vector>
#include "rhi/rhi.hpp"

namespace Genesis {

typedef void (*OsMessageCallback)(void* os_event, void* usr_data);

struct WindowConfig {
    uint32_t width = 1280;
    uint32_t height = 720;
    const char* title = "Genesis Render";
    RHIBackend backend = RHIBackend::Vulkan;
    bool headless = false;
};

class Window {
public:
    void init(const WindowConfig& config);
    void shutdown();

    void pollEvents();
    bool shouldClose() const { return should_close_; }
    void setShouldClose(bool v) { should_close_ = v; }

    void* nativeHandle() const { return platform_handle_; } // SDL_Window*
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    bool headless() const { return headless_; }
    RHIBackend backend() const { return backend_; }

    // Returns true and fills w/h when the drawable size changed this poll.
    bool consumeResize(uint32_t& w, uint32_t& h);

    // Input state, refreshed in pollEvents()
    struct InputState {
        bool keys[512] = {};
        bool mouse_buttons[5] = {};
        float mouse_x = 0, mouse_y = 0;
        float mouse_dx = 0, mouse_dy = 0;
        float scroll = 0;
        bool resized = false;
    };
    const InputState& input() const { return input_; }

    void register_os_message_callback(OsMessageCallback callback, void* data);

private:
    void* platform_handle_ = nullptr; // SDL_Window*
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    bool should_close_ = false;
    bool headless_ = false;
    RHIBackend backend_ = RHIBackend::Vulkan;
    InputState input_;

    std::vector<OsMessageCallback> os_message_callbacks_;
    std::vector<void*> os_message_callback_data_;
};

} // namespace Genesis
