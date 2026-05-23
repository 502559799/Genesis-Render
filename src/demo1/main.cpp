/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-22 17:47:12
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-10-30 13:34:32
 * @version:
 */
#include <iostream>
#include <atomic>
#include <mutex>
#include <vector>

#include "application_layer/window.hpp"
#include "graphics/genesis_imgui.hpp"
#include "graphics/gpu_device.hpp"

#include "camera/camera_capture.h"
#include "filter/filter_system.h"

#include "external/imgui/imgui.h"
#include "external/imgui/imgui_impl_sdl.h"
#include "external/imgui/imgui_impl_vulkan.h"

#include <SDL2/SDL.h>
#include <vulkan/vulkan.h>

// Global camera frame data
std::mutex g_frame_mutex;
std::vector<uint8_t> g_latest_frame;
uint32_t g_frame_width = 0;
uint32_t g_frame_height = 0;
std::atomic<bool> g_new_frame_available = false;
std::atomic<bool> g_camera_initialized = false;

// Filtered buffer
std::vector<uint8_t> g_filtered_buffer;

// ==========================================
// Vulkan 纹理创建辅助函数
// ==========================================

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    printf("!!! Failed to find suitable memory type!\n");
    return 0;
}

// ==========================================
// 相机纹理 - 使用 ImGui 纯 CPU + GPU 混合方案
// ==========================================

// 为了简单可靠，我们先用 CPU 绘制方式确认相机工作
// 然后再切换到纯 GPU 纹理上传

int main()
{
    printf("=== Genesis Render - Camera & Filter Demo ===\n");

    using namespace Genesis;

    // init service
    MemoryService::instance()->init(nullptr);

    Allocator *allocator = &MemoryService::instance()->system_allocator;

    // window
    WindowConfig winCfg{1440, 900, "Genesis Camera Filter", allocator};
    Window window;
    window.init(&winCfg);

    // init Vulkan GPU device
    DeviceCreateInfo deviceCfg{};
    deviceCfg.setAllocator(allocator);
    deviceCfg.setWindow(window.getWindowHandle(), 1440, 900);

    GpuDevice* gpu = GpuDevice::instance();
    gpu->init(deviceCfg);

    VkDevice vk_device = gpu->getDevice();
    VkPhysicalDevice vk_physical = gpu->getPhysicalDevice();  // 我们需要在 GpuDevice 添加这个接口

    printf("Vulkan initialized successfully\n");

    // graphics
    ImGuiService *imgui = ImGuiService::instance();
    ImGuiServiceConfig imguiConfig{window.getWindowHandle()};
    imgui->init(&imguiConfig);

    // init imgui vulkan backend
    gpu->initImgui();

    printf("ImGui Vulkan backend initialized\n");

    // ==========================================
    // 📷 初始化相机
    // ==========================================
    const uint32_t CAM_W = 640;
    const uint32_t CAM_H = 480;

    CameraCapture camera;
    if (!camera.init(CAM_W, CAM_H, 30.0f))
    {
        printf("!!! FAILED TO INITIALIZE CAMERA !!!\n");
    }
    else
    {
        printf("Camera initialized: %dx%d\n", CAM_W, CAM_H);
    }

    // Set up frame callback
    camera.setFrameCallback([&](const CameraFrame& frame) {
        std::lock_guard<std::mutex> lock(g_frame_mutex);
        size_t frame_size = frame.bytesPerRow * frame.height;
        if (g_latest_frame.size() != frame_size)
        {
            g_latest_frame.resize(frame_size);
        }
        std::copy(frame.data, frame.data + frame_size, g_latest_frame.data());
        g_frame_width = frame.width;
        g_frame_height = frame.height;
        g_new_frame_available = true;
        g_camera_initialized = true;
    });

    camera.start();
    printf("Camera started\n");

    // Initialize filter system
    FilterSystem filter_system;
    filter_system.init(CAM_W, CAM_H);
    g_filtered_buffer.resize(CAM_W * CAM_H * 4);

    printf("Filter system initialized\n");

    // main event loop
    bool running = true;
    uint64_t frame_count = 0;
    float fps_timer = 0.0f;
    uint32_t fps_count = 0;
    float current_fps = 0.0f;

    uint64_t last_time = SDL_GetPerformanceCounter();
    uint64_t freq = SDL_GetPerformanceFrequency();

    int selected_filter = 0;
    float brightness = 0.0f;
    float contrast = 1.0f;

    // 显示缓冲区 - 用于 ImDrawList 绘制
    std::vector<uint32_t> display_pixels(CAM_W * CAM_H, 0xFF00FF00);  // 初始绿色

    printf("=== Entering main loop ===\n");

    while (running)
    {
        uint64_t now = SDL_GetPerformanceCounter();
        float delta_time = (float)(now - last_time) / freq;
        last_time = now;

        fps_timer += delta_time;
        fps_count++;
        frame_count++;

        if (fps_timer >= 1.0f)
        {
            current_fps = (float)fps_count / fps_timer;
            fps_timer = 0.0f;
            fps_count = 0;
        }

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                running = false;
            }
            ImGui_ImplSDL2_ProcessEvent(&event);
        }

        // ==========================================
        // 处理相机帧和滤镜
        // ==========================================
        bool has_frame = false;
        if (g_new_frame_available && !g_latest_frame.empty())
        {
            std::lock_guard<std::mutex> lock(g_frame_mutex);

            uint8_t* src_data = g_latest_frame.data();

            // Apply filter if selected
            if (selected_filter > 0)
            {
                FilterParams params;
                params.brightness = brightness;
                params.contrast = contrast;
                filter_system.setFilterParams(params);
                filter_system.setActiveFilter((FilterType)selected_filter);

                filter_system.applyFilterCPU(g_latest_frame.data(), g_filtered_buffer.data(),
                                            g_frame_width, g_frame_height, g_frame_width * 4);
                src_data = g_filtered_buffer.data();
            }

            // 拷贝到显示缓冲区 (BGRA -> uint32_t ARGB)
            for (size_t i = 0; i < g_frame_width * g_frame_height; i++)
            {
                uint8_t b = src_data[i * 4 + 0];
                uint8_t g = src_data[i * 4 + 1];
                uint8_t r = src_data[i * 4 + 2];
                display_pixels[i] = (0xFF << 24) | (r << 16) | (g << 8) | b;
            }

            g_new_frame_available = false;
            has_frame = true;
        }

        // start the frame
        gpu->beginFrame();

        // imgui new frame
        imgui->newFrame();

        // ==========================================
        // 🎥 主窗口 - 显示相机画面
        // ==========================================
        ImGui::SetNextWindowSize(ImVec2(800, 650), ImGuiCond_FirstUseEver);
        ImGui::Begin("📷 LIVE CAMERA FEED");

        ImGui::Text("FPS: %.1f", current_fps);
        ImGui::Separator();

        if (g_camera_initialized && !display_pixels.empty())
        {
            ImGui::Text("✅ Camera initialized - %dx%d", g_frame_width, g_frame_height);

            // 使用 ImDrawList 逐块绘制（性能优化版本）
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
            ImVec2 canvas_size = ImGui::GetContentRegionAvail();

            // 保持宽高比
            float aspect = (float)g_frame_width / (float)g_frame_height;
            float display_w = canvas_size.x;
            float display_h = display_w / aspect;
            if (display_h > canvas_size.y) {
                display_h = canvas_size.y;
                display_w = display_h * aspect;
            }

            // 绘制缩放后的图像 - 使用 4x4 像素块优化性能
            const int block_size = 2;
            for (uint32_t y = 0; y < g_frame_height; y += block_size)
            {
                for (uint32_t x = 0; x < g_frame_width; x += block_size)
                {
                    size_t idx = y * g_frame_width + x;
                    if (idx < display_pixels.size())
                    {
                        uint32_t color = display_pixels[idx];
                        float px = canvas_pos.x + (float)x / g_frame_width * display_w;
                        float py = canvas_pos.y + (float)y / g_frame_height * display_h;
                        float px2 = canvas_pos.x + (float)(x + block_size) / g_frame_width * display_w;
                        float py2 = canvas_pos.y + (float)(y + block_size) / g_frame_height * display_h;

                        draw_list->AddRectFilled(ImVec2(px, py), ImVec2(px2, py2), color);
                    }
                }
            }
        }
        else
        {
            // 相机未就绪时的提示
            ImGui::Text("⏳ Waiting for camera...");
            ImGui::Text("");
            ImGui::Text("💡 Please allow camera permission if requested");
            ImGui::Text("");
            ImGui::Text("Camera status:");
            ImGui::Text("  - Initialized: %s", g_camera_initialized ? "YES" : "NO");
            ImGui::Text("  - Frame ready: %s", g_new_frame_available ? "YES" : "NO");
            ImGui::Text("  - Buffer size: %zu bytes", g_latest_frame.size());

            // 绘制一个测试渐变图案确认渲染工作
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetCursorScreenPos();
            for (int y = 0; y < 200; y++) {
                for (int x = 0; x < 320; x++) {
                    uint8_t r = (uint8_t)((x * 255) / 320);
                    uint8_t g = (uint8_t)((y * 255) / 200);
                    uint8_t b = 128;
                    uint32_t color = (0xFF << 24) | (r << 16) | (g << 8) | b;
                    draw_list->AddRectFilled(ImVec2(p.x + x, p.y + y),
                                             ImVec2(p.x + x + 1, p.y + y + 1), color);
                }
            }
        }

        ImGui::End();

        // ==========================================
        // 🎛️ 控制面板
        // ==========================================
        ImGui::SetNextWindowPos(ImVec2(820, 50), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(580, 400), ImGuiCond_FirstUseEver);
        ImGui::Begin("🎨 FILTER CONTROLS");

        auto filter_names = FilterSystem::getFilterNames();

        ImGui::Text("SELECT FILTER:");
        for (int i = 0; i < filter_names.size(); i++)
        {
            bool active = (selected_filter == i);
            std::string label = filter_names[i];
            if (active) label = "✅ " + label;
            else label = "   " + label;

            if (ImGui::Button(label.c_str(), ImVec2(270, 35)))
            {
                selected_filter = i;
                printf("Filter changed: %d -> %s\n", i, filter_names[i].c_str());
            }

            if ((i + 1) % 2 != 0) ImGui::SameLine();
        }

        ImGui::Separator();
        ImGui::Text("ADJUSTMENTS:");
        ImGui::PushItemWidth(400);
        ImGui::SliderFloat("☀️ Brightness", &brightness, -1.0f, 1.0f);
        ImGui::SliderFloat("🔆 Contrast", &contrast, 0.0f, 3.0f);
        ImGui::PopItemWidth();

        ImGui::Separator();
        ImGui::Text("Current Filter: %s", filter_names[selected_filter].c_str());

        ImGui::End();

        // ==========================================
        // 📊 性能统计
        // ==========================================
        ImGui::SetNextWindowPos(ImVec2(820, 470), ImGuiCond_FirstUseEver);
        ImGui::Begin("📊 PERFORMANCE");

        ImGui::Columns(2, nullptr, false);
        ImGui::Text("Render FPS"); ImGui::NextColumn();
        ImGui::Text("%.1f", current_fps); ImGui::NextColumn();

        ImGui::Text("Camera FPS"); ImGui::NextColumn();
        ImGui::Text("30"); ImGui::NextColumn();

        ImGui::Text("Resolution"); ImGui::NextColumn();
        ImGui::Text("%dx%d", g_frame_width, g_frame_height); ImGui::NextColumn();

        ImGui::Text("Active Filter"); ImGui::NextColumn();
        ImGui::Text("%s", filter_names[selected_filter].c_str()); ImGui::NextColumn();

        ImGui::Text("Pixels/sec"); ImGui::NextColumn();
        ImGui::Text("%.1f M", current_fps * g_frame_width * g_frame_height / 1000000.0f);
        ImGui::Columns(1);

        ImGui::End();

        // ==========================================
        // ℹ️ 关于窗口
        // ==========================================
        ImGui::SetNextWindowPos(ImVec2(820, 670), ImGuiCond_FirstUseEver);
        ImGui::Begin("ℹ️ ABOUT");
        ImGui::Text("Genesis Render Engine v1.0");
        ImGui::Separator();
        ImGui::Text("✅ Vulkan 1.0 + ImGui Rendering");
        ImGui::Text("✅ macOS AVFoundation Camera");
        ImGui::Text("✅ 8 Real-time CPU Filter Effects");
        ImGui::Text("✅ GPU-Accelerated Display");
        ImGui::Text("");
        ImGui::Text("Rendering: ImGui ImDrawList + Vulkan");
        ImGui::End();

        // render imgui
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), gpu->getCurrentCommandBuffer());

        // end the frame and present
        gpu->endFrame();
    }

    // wait for device to idle before cleanup
    camera.stop();
    vkDeviceWaitIdle(gpu->getDevice());

    // cleanup
    imgui->shutdown();
    gpu->shutdown();
    window.shutdown();
    MemoryService::instance()->shutdown();

    printf("Genesis Render shutdown complete.\n");
    return 0;
}
