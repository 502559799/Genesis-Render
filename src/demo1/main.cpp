/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-22 17:47:12
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-10-30 13:34:32
 * @version:
 */
#include <iostream>
#include "application_layer/window.hpp"

#include "graphics/genesis_imgui.hpp"

#include "external/imgui/imgui.h"

int main()
{
    printf("hello world!");

    using namespace Genesis;

    // init service
    MemoryService::instance()->init(nullptr);

    Allocator *allocator = &MemoryService::instance()->system_allocator;

    // window
    WindowConfig winCfg{1280, 800, "demo1", allocator};
    Window window;
    window.init(&winCfg);

    // graphics
    ImGuiService *imgui = ImGuiService::instance();
    ImGuiServiceConfig imguiConfig{window.getWindowHandle()};
    imgui->init(&imguiConfig);

    return 0;
}