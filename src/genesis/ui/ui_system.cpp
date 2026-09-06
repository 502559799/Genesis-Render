/*
 * Genesis Render - UI system implementation
 */
#include "ui/ui_system.hpp"
#include "foundation_layer/genesis_log.hpp"

#include "imgui.h"
#include "imgui_impl_sdl.h"

#if GENESIS_RHI_OPENGL
#include "imgui_impl_opengl3.h"
#endif
#if GENESIS_RHI_VULKAN
#include "imgui_impl_vulkan.h"
#include "rhi/vulkan/vk_backend.hpp"
#endif

namespace Genesis
{
    namespace UI
    {
        UISystem::~UISystem()
        {
            shutdown();
        }

        bool UISystem::initialize(RHI::RHIDevice *device, SDL_Window *window)
        {
            if (!device || !window)
            {
                GEN_LOG_ERROR("[UI] initialise requires a device and a window");
                return false;
            }
            device_ = device;
            window_ = window;
            use_vulkan_ = device->backend() == RHI::BackendType::Vulkan;

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO &io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.IniFilename = nullptr; // no imgui.ini churn in the repository
            io.LogFilename = nullptr;

            ImGui::StyleColorsDark();
            setStyle(StylePreset::Genesis);

            // imgui 1.86 has no generic init; pick the one matching the backend.
            bool sdl_ok = use_vulkan_ ? ImGui_ImplSDL2_InitForVulkan(window)
                                      : ImGui_ImplSDL2_InitForOpenGL(window, (void *)SDL_GL_GetCurrentContext());
            if (!sdl_ok)
            {
                GEN_LOG_ERROR("[UI] ImGui_ImplSDL2_Init failed");
                return false;
            }

            bool ok = use_vulkan_ ? createVulkanBackend(device, window) : createOpenGLBackend(window);
            if (!ok)
            {
                GEN_LOG_ERROR("[UI] backend initialisation failed");
                ImGui_ImplSDL2_Shutdown();
                ImGui::DestroyContext();
                return false;
            }

            initialized_ = true;
            GEN_LOG_INFO("[UI] ImGui %s initialised (backend=%s)", IMGUI_VERSION,
                         RHI::backendTypeName(device->backend()));
            return true;
        }

        bool UISystem::createOpenGLBackend(SDL_Window *window)
        {
            (void)window;
#if GENESIS_RHI_OPENGL
            // GLSL 410 keeps the UI shaders inside the OpenGL 4.1 core profile
            // that macOS provides.
            if (!ImGui_ImplOpenGL3_Init("#version 410"))
            {
                GEN_LOG_ERROR("[UI] ImGui_ImplOpenGL3_Init failed");
                return false;
            }
            return true;
#else
            GEN_LOG_ERROR("[UI] OpenGL backend is not compiled in");
            return false;
#endif
        }

        bool UISystem::createVulkanBackend(RHI::RHIDevice *device, SDL_Window *window)
        {
            (void)window;
#if GENESIS_RHI_VULKAN
            RHI::VkDeviceRHI *vk = static_cast<RHI::VkDeviceRHI *>(device);
            if (!vk->device() || !vk->vkSwapchainRenderPass())
            {
                GEN_LOG_ERROR("[UI] Vulkan backend needs a valid device and swapchain render pass");
                return false;
            }

            VkDescriptorPoolSize pool_sizes[] = {
                {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
                {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
                {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
            };
            VkDescriptorPoolCreateInfo pool_info{};
            pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
            pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
            pool_info.pPoolSizes = pool_sizes;
            if (vkCreateDescriptorPool(vk->device(), &pool_info, nullptr,
                                       reinterpret_cast<VkDescriptorPool *>(&vk_descriptor_pool_)) != VK_SUCCESS)
            {
                GEN_LOG_ERROR("[UI] failed to create the ImGui descriptor pool");
                return false;
            }

            ImGui_ImplVulkan_InitInfo init_info{};
            init_info.Instance = vk->instance();
            init_info.PhysicalDevice = vk->physicalDevice();
            init_info.Device = vk->device();
            init_info.QueueFamily = vk->graphicsQueueFamily();
            init_info.Queue = vk->graphicsQueue();
            init_info.PipelineCache = VK_NULL_HANDLE;
            init_info.DescriptorPool = reinterpret_cast<VkDescriptorPool>(vk_descriptor_pool_);
            init_info.Subpass = 0;
            init_info.MinImageCount = 2;
            init_info.ImageCount = std::max(2u, vk->swapchainImageCount());
            init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            init_info.Allocator = nullptr;
            init_info.CheckVkResultFn = nullptr;

            if (!ImGui_ImplVulkan_Init(&init_info, vk->vkSwapchainRenderPass()))
            {
                GEN_LOG_ERROR("[UI] ImGui_ImplVulkan_Init failed");
                return false;
            }

            // Upload the font atlas through a one-time command buffer.
            VkCommandBuffer cmd = vk->beginSingleTimeCommands();
            ImGui_ImplVulkan_CreateFontsTexture(cmd);
            vk->endSingleTimeCommands(cmd);
            ImGui_ImplVulkan_DestroyFontUploadObjects();
            return true;
#else
            (void)device;
            GEN_LOG_ERROR("[UI] Vulkan backend is not compiled in");
            return false;
#endif
        }

        void UISystem::destroyNativeBackend()
        {
#if GENESIS_RHI_VULKAN
            if (use_vulkan_)
            {
                ImGui_ImplVulkan_Shutdown();
                if (vk_descriptor_pool_ && device_)
                {
                    RHI::VkDeviceRHI *vk = static_cast<RHI::VkDeviceRHI *>(device_);
                    vkDestroyDescriptorPool(vk->device(), reinterpret_cast<VkDescriptorPool>(vk_descriptor_pool_), nullptr);
                    vk_descriptor_pool_ = nullptr;
                }
                return;
            }
#endif
#if GENESIS_RHI_OPENGL
            ImGui_ImplOpenGL3_Shutdown();
#endif
        }

        void UISystem::shutdown()
        {
            if (!initialized_) return;
            destroyNativeBackend();
            ImGui_ImplSDL2_Shutdown();
            ImGui::DestroyContext();
            initialized_ = false;
            device_ = nullptr;
            window_ = nullptr;
        }

        void UISystem::processEvent(const SDL_Event &event)
        {
            if (!initialized_) return;
            ImGui_ImplSDL2_ProcessEvent(&event);
        }

        void UISystem::beginFrame(Platform::InputSystem *input, float delta_time)
        {
            if (!initialized_) return;
            time_ += delta_time;

#if GENESIS_RHI_VULKAN
            if (use_vulkan_) ImGui_ImplVulkan_NewFrame();
#endif
#if GENESIS_RHI_OPENGL
            if (!use_vulkan_) ImGui_ImplOpenGL3_NewFrame();
#endif
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();

            if (input)
            {
                ImGuiIO &io = ImGui::GetIO();
                io.DeltaTime = delta_time > 0.0f ? delta_time : 0.0001f;
                input->ui_wants_mouse = io.WantCaptureMouse;
                input->ui_wants_keyboard = io.WantCaptureKeyboard;
            }
        }

        void UISystem::endFrame()
        {
            if (!initialized_) return;
            ImGui::Render();
        }

        void UISystem::renderInto(RHI::RHICommandBuffer *cmd, uint32_t width, uint32_t height)
        {
            if (!initialized_ || !ui_visible || !cmd) return;
            ImDrawData *draw_data = ImGui::GetDrawData();
            if (!draw_data) return;

#if GENESIS_RHI_VULKAN
            if (use_vulkan_)
            {
                ImGui_ImplVulkan_RenderDrawData(draw_data, reinterpret_cast<VkCommandBuffer>(cmd->nativeHandle()));
                return;
            }
#endif
#if GENESIS_RHI_OPENGL
            // The GL backend executes immediately; make sure the viewport is
            // restored for whatever follows.
            ImGui_ImplOpenGL3_RenderDrawData(draw_data);
            cmd->setViewport(RHI::Viewport{0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f});
            cmd->setScissor(RHI::Rect2D{0, 0, width, height});
#endif
        }

        void UISystem::setStyle(StylePreset preset)
        {
            ImGuiStyle &style = ImGui::GetStyle();
            style.WindowRounding = 4.0f;
            style.FrameRounding = 2.0f;
            style.GrabRounding = 2.0f;
            style.ChildRounding = 3.0f;
            style.PopupRounding = 3.0f;
            style.ScrollbarRounding = 3.0f;
            style.FramePadding = ImVec2(6, 4);
            style.ItemSpacing = ImVec2(8, 5);
            style.WindowPadding = ImVec2(8, 8);

            ImVec4 *c = style.Colors;
            switch (preset)
            {
            case StylePreset::Light:
                ImGui::StyleColorsLight(&style);
                break;
            case StylePreset::Dark:
                ImGui::StyleColorsDark(&style);
                break;
            case StylePreset::Genesis:
            default:
                ImGui::StyleColorsDark(&style);
                c[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.08f, 0.10f, 0.94f);
                c[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.12f, 0.16f, 1.00f);
                c[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.28f, 0.42f, 1.00f);
                c[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.15f, 0.19f, 0.90f);
                c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.32f, 0.45f, 0.90f);
                c[ImGuiCol_Button] = ImVec4(0.18f, 0.26f, 0.36f, 1.00f);
                c[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.42f, 0.58f, 1.00f);
                c[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.30f, 0.44f, 1.00f);
                c[ImGuiCol_Header] = ImVec4(0.20f, 0.32f, 0.46f, 0.80f);
                c[ImGuiCol_CheckMark] = ImVec4(0.35f, 0.75f, 0.95f, 1.00f);
                c[ImGuiCol_SliderGrab] = ImVec4(0.35f, 0.65f, 0.90f, 1.00f);
                c[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 0.80f, 1.00f, 1.00f);
                c[ImGuiCol_Text] = ImVec4(0.92f, 0.94f, 0.96f, 1.00f);
                break;
            }
        }

        void UISystem::pushLog(const std::string &line)
        {
            logs_.push_back(line);
            if (logs_.size() > 400) logs_.erase(logs_.begin(), logs_.begin() + (logs_.size() - 400));
        }

    } // namespace UI
} // namespace Genesis
