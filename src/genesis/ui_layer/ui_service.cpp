/*
 * @Descripttion: UI 服务实现 (ImGui + SDL2 + Vulkan/OpenGL3 后端)
 */
#include "ui_layer/ui_service.hpp"

#include "foundation_layer/log.hpp"
#include "rhi_layer/vulkan/vulkan_device.hpp"

#include "external/imgui/imgui.h"
#include "external/imgui/imgui_impl_sdl.h"
#include "external/imgui/imgui_impl_vulkan.h"
#include "external/imgui/imgui_impl_opengl3.h"

#include <SDL2/SDL.h>

namespace Genesis
{
    namespace ui
    {
        bool UIService::init(Window *window, rhi::RHIDevice *device)
        {
            m_window = window;
            m_device = device;

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGui::StyleColorsDark();

            ImGuiIO &io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

            SDL_Window *sdl_window = (SDL_Window *)window->getWindowHandle();

            if (device->getBackend() == rhi::Backend::Vulkan)
            {
                ImGui_ImplSDL2_InitForVulkan(sdl_window);

                rhi::VulkanDevice *vk = (rhi::VulkanDevice *)device;
                ImGui_ImplVulkan_InitInfo init_info{};
                init_info.Instance = vk->getVkInstance();
                init_info.PhysicalDevice = vk->getVkPhysicalDevice();
                init_info.Device = vk->getVkDevice();
                init_info.QueueFamily = vk->getVkQueueFamily();
                init_info.Queue = vk->getVkQueue();
                init_info.DescriptorPool = vk->getVkDescriptorPool();
                init_info.MinImageCount = 2;
                init_info.ImageCount = 3;
                init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

                ImGui_ImplVulkan_Init(&init_info, vk->getMainRenderPass());

                // 字体纹理上传 (一次性命令)
                vk->beginCommands();
                ImGui_ImplVulkan_CreateFontsTexture(vk->getCurrentCommandBuffer());
                vk->endCommands();
                ImGui_ImplVulkan_DestroyFontUploadObjects();
            }
            else
            {
                ImGui_ImplSDL2_InitForOpenGL(sdl_window, SDL_GL_GetCurrentContext());
#ifdef __APPLE__
                ImGui_ImplOpenGL3_Init("#version 410");
#else
                ImGui_ImplOpenGL3_Init("#version 420");
#endif
            }

            m_initialized = true;
            GenesisPrint("[UI] imgui service initialized (backend=%s)\n",
                         device->getBackend() == rhi::Backend::Vulkan ? "vulkan" : "opengl");
            return true;
        }

        void UIService::shutdown()
        {
            if (!m_initialized)
                return;

            if (m_device->getBackend() == rhi::Backend::Vulkan)
                ImGui_ImplVulkan_Shutdown();
            else
                ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplSDL2_Shutdown();
            ImGui::DestroyContext();
            m_initialized = false;
        }

        void UIService::processEvent(SDL_Event *event)
        {
            ImGui_ImplSDL2_ProcessEvent((::SDL_Event *)event);
        }

        void UIService::newFrame()
        {
            if (m_device->getBackend() == rhi::Backend::Vulkan)
                ImGui_ImplVulkan_NewFrame();
            else
                ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
        }

        void UIService::render()
        {
            ImGui::Render();

            // UI 叠加 pass: 不清屏 (load)
            float clear[4] = {0, 0, 0, 1};
            m_device->beginRenderPass({}, clear, 1.0f, false);

            if (m_device->getBackend() == rhi::Backend::Vulkan)
            {
                rhi::VulkanDevice *vk = (rhi::VulkanDevice *)m_device;
                ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vk->getCurrentCommandBuffer());
            }
            else
            {
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            }

            m_device->endRenderPass();
        }

        bool UIService::wantCaptureMouse() const
        {
            return m_initialized && ImGui::GetIO().WantCaptureMouse;
        }

        bool UIService::wantCaptureKeyboard() const
        {
            return m_initialized && ImGui::GetIO().WantCaptureKeyboard;
        }

    } // namespace ui
} // namespace Genesis
