/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-25 21:22:12
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-10-30 14:22:09
 * @version:
 */
#ifndef __VULKAN_RENDERER_GENESIS_IMGUI_H__
#define __VULKAN_RENDERER_GENESIS_IMGUI_H__

#include <stdint.h>

#include "foundation_layer/service.hpp"

namespace Genesis
{
    enum class ImGuiStyles : uint32_t
    {
        Classic = 0,
        DarkRed,
        DarkGold,
        GreenBlue,
    };

    struct ImGuiServiceConfig
    {
        void *window_handle;
    };

    class ImGuiService : public Service
    {
    public:
        GENESIS_DECLARE_SERVICE(ImGuiService);
        void init(void *config) override;
        void shutdown() override;

        void render();
        void newFrame();

        void setStyle(ImGuiStyles style);

        static constexpr const char *name_{"Genesis_ImGui_Services"};
        GENESIS_SINGLETON_CLASS(ImGuiService);
    };

    void imgui_log_init();
    void imgui_log_shutdown();
    void imgui_log_draw();

    void imgui_fps_init();
    void imgui_fps_shutdown();
    void imgui_fps_draw();
    void imgui_fps_add(uint32_t deltaTime);

}

#endif /* __VULKAN_RENDERER_GENESIS_IMGUI_H__ */