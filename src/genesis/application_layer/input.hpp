/*
 * @Descripttion: 输入系统: 键盘 / 鼠标状态查询 (SDL2)
 * @version: v1.0
 */
#ifndef __GENESIS_INPUT_H__
#define __GENESIS_INPUT_H__

#include "foundation_layer/service.hpp"

#include <cstdint>
#include <functional>

union SDL_Event;

namespace Genesis
{
    class Input : public Service
    {
    public:
        GENESIS_DECLARE_SERVICE(Input);

        void init(void *config) override;
        void shutdown() override;

        // 每帧开始处理 SDL 事件 (返回 false 表示收到退出请求)
        bool pollEvents();

        // 事件转发 (UI 系统用)
        std::function<void(SDL_Event *)> m_event_callback;

        bool isKeyDown(const char *key_name) const;
        bool isMouseButtonDown(int button) const;
        void getMousePosition(float &x, float &y) const;
        void getMouseDelta(float &dx, float &dy) const;
        float getWheelDelta() const { return wheel_delta_; }

    private:
        const uint8_t *keyboard_state_ = nullptr;
        uint32_t mouse_state_ = 0;
        float mouse_x_ = 0, mouse_y_ = 0;
        float mouse_dx_ = 0, mouse_dy_ = 0;
        float wheel_delta_ = 0;
        bool quit_requested_ = false;
    };
}

#endif // __GENESIS_INPUT_H__
