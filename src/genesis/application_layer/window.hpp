/*
 * Genesis Render - Engine
 * window.hpp : SDL2 window service, backend aware (Vulkan / OpenGL).
 */
#ifndef __GENESIS_WINDOW_H__
#define __GENESIS_WINDOW_H__

#include "foundation_layer/service.hpp"
#include "foundation_layer/array.hpp"
#include "rhi/rhi_types.hpp"

namespace Genesis
{
    typedef void (*OsMessageCallback)(void *os_event, void *usr_data);
    struct WindowConfig
    {
        uint32_t width = 1280;
        uint32_t height = 720;
        const char *title = "Genesis Render";
        RenderBackend backend = RenderBackend::Vulkan;
        bool resizable = true;
        Allocator *allocator = nullptr;
    };

    class Window : public Service
    {
    public:
        void init(void *config) override;
        void shutdown() override;

        void setFullScreen(bool full);
        void setTitle(const char *title);
        void getDrawableSize(uint32_t &w, uint32_t &h) const;

        void register_os_message_callback(OsMessageCallback callback, void *data);
        void unregister_os_message_callback(OsMessageCallback callback);

        void *getWindowHandle() { return platform_handle_; }
        bool shouldClose() const { return should_close_; }
        void requestClose() { should_close_ = true; }
        RenderBackend backend() const { return backend_; }

    private:
        uint32_t width_ = 0, height_ = 0;
        float refresh_{1.0f / 60.0f};
        void *platform_handle_{nullptr};
        RenderBackend backend_{RenderBackend::Vulkan};
        bool should_close_{false};
        Array<OsMessageCallback> os_message_callbacks_;
        Array<void *> os_message_callback_data_;
    };
}

#endif
