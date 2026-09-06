/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-22 18:10:38
 * @Descripttion: 窗口服务, 支持 Vulkan / OpenGL 两种图形后端
 * @version: v2.0
 */
#ifndef __VULKAN_RENDERER_WINDOW_H__
#define __VULKAN_RENDERER_WINDOW_H__

#include "foundation_layer/service.hpp"
#include "foundation_layer/array.hpp"

namespace Genesis
{
    enum class WindowGraphicsApi : uint8_t
    {
        Vulkan,
        OpenGL,
    };

    typedef void (*OsMessageCallback)(void *os_event, void *usr_data);
    struct WindowConfig
    {
        uint32_t width;
        uint32_t height;

        const char *title;

        Allocator *allocator;

        WindowGraphicsApi graphics_api{WindowGraphicsApi::Vulkan};
        bool hidden{false}; // 无头测试模式
    };
    class Window : public Service
    {
    public:
        void init(void *config) override;
        void shutdown() override;

        void setFullScreen(bool full);

        Array<OsMessageCallback> os_message_callbacks_;
        Array<void *> os_message_callback_data_;

        void register_os_message_callback(OsMessageCallback callback, void *data);
        void unregister_os_message_callback(OsMessageCallback callback);

        void *getWindowHandle() { return platform_handle_; }

        WindowGraphicsApi getGraphicsApi() const { return graphics_api_; }
        uint32_t getWidth() const { return width_; }
        uint32_t getHeight() const { return height_; }

    private:
        uint32_t width_;
        uint32_t height_;
        float refresh_{1.0f / 60.0f};
        WindowGraphicsApi graphics_api_{WindowGraphicsApi::Vulkan};

        void *platform_handle_{nullptr};
    };
}

#endif // __VULKAN_RENDERER_WINDOW_H__
