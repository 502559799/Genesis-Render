/*
 * Genesis Render - UI system implementation (ImGui through the RHI)
 */
#include "ui/ui_system.hpp"
#include "foundation_layer/genesis_log.hpp"
#include "renderer/render_internal.hpp"
#include "renderer/render_system.hpp"

#include "imgui.h"
#include "imgui_impl_sdl.h"

#include <algorithm>
#include <cmath>

namespace Genesis
{
    namespace UI
    {
        UISystem::~UISystem()
        {
            shutdown();
        }

        bool UISystem::initialize(RHI::RHIDevice *device, Render::RenderSystem *render, SDL_Window *window)
        {
            if (!device || !render || !window)
            {
                GEN_LOG_ERROR("[UI] initialise requires a device, the render system and a window");
                return false;
            }
            device_ = device;
            render_ = render;
            window_ = window;

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO &io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.BackendRendererName = "genesis_rhi";
            io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
            setStyle(StylePreset::Genesis);

            bool sdl_ok = false;
            switch (device->backend())
            {
            case RHI::BackendType::Vulkan: sdl_ok = ImGui_ImplSDL2_InitForVulkan(window); break;
            case RHI::BackendType::Metal: sdl_ok = ImGui_ImplSDL2_InitForMetal(window); break;
            default: sdl_ok = ImGui_ImplSDL2_InitForOpenGL(window, SDL_GL_GetCurrentContext()); break;
            }
            if (!sdl_ok)
            {
                GEN_LOG_ERROR("[UI] ImGui_ImplSDL2 initialisation failed");
                ImGui::DestroyContext();
                return false;
            }

            // Font atlas -> RGBA8 texture.
            unsigned char *pixels = nullptr;
            int w = 0, h = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
            RHI::TextureDesc td;
            td.format = RHI::PixelFormat::RGBA8_UNORM;
            td.width = (uint32_t)w;
            td.height = (uint32_t)h;
            td.mip_levels = 1;
            td.usage = (uint32_t)RHI::TextureUsageFlagBits::Sampled | (uint32_t)RHI::TextureUsageFlagBits::TransferDst;
            td.name = "imgui.font_atlas";
            font_texture_ = device->createTexture(td);
            if (!font_texture_)
            {
                GEN_LOG_ERROR("[UI] font atlas creation failed");
                ImGui_ImplSDL2_Shutdown();
                ImGui::DestroyContext();
                return false;
            }
            device->writeTexture(font_texture_, pixels, (uint64_t)w * h * 4, (uint32_t)w, (uint32_t)h, 0, 0, 1);
            io.Fonts->SetTexID((ImTextureID)font_texture_);

            initialized_ = true;
            GEN_LOG_INFO("[UI] ImGui %s initialised (RHI renderer, backend=%s)", IMGUI_VERSION,
                         RHI::backendTypeName(device->backend()));
            return true;
        }

        void UISystem::shutdown()
        {
            if (!initialized_) return;
            if (device_) device_->waitIdle();
            if (render_ && font_texture_) render_->forgetTexture(font_texture_);
            if (font_texture_) device_->destroyTexture(font_texture_);
            font_texture_ = nullptr;
            ImGui_ImplSDL2_Shutdown();
            ImGui::DestroyContext();
            initialized_ = false;
            device_ = nullptr;
            render_ = nullptr;
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
            ImGui_ImplSDL2_NewFrame();
            ImGuiIO &io = ImGui::GetIO();
            io.DeltaTime = delta_time > 0.0f ? delta_time : 1.0f / 60.0f;
            ImGui::NewFrame();
            if (input)
            {
                input->ui_wants_mouse = io.WantCaptureMouse;
                input->ui_wants_keyboard = io.WantCaptureKeyboard;
            }
        }

        void UISystem::endFrame()
        {
            if (!initialized_) return;
            ImGui::Render();
        }

        void UISystem::renderInto(RHI::RHICommandBuffer *cmd, const Render::TargetRef &target)
        {
            last_draw_calls_ = 0;
            if (!initialized_ || !ui_visible || !cmd || !render_) return;
            ImDrawData *dd = ImGui::GetDrawData();
            if (!dd || dd->TotalVtxCount == 0 || dd->DisplaySize.x <= 0.0f || dd->DisplaySize.y <= 0.0f) return;

            // ImGui works in window points; the target is in pixels.  Scale
            // to the target so captures of a different size stay correct.
            const float fb_w = (float)target.width, fb_h = (float)target.height;
            const float sx = fb_w / dd->DisplaySize.x, sy = fb_h / dd->DisplaySize.y;

            Render::PipelineKey key;
            key.vertex_shader = "ui/imgui.vert";
            key.fragment_shader = "ui/imgui.frag";
            key.layout = &render_->uiLayout()->desc();
            key.vertex_layout = Render::VertexLayoutKind::ImGui;
            key.cull = RHI::CullMode::None;
            key.depth_test = false;
            key.depth_write = false;
            key.blend = Render::BlendPreset::Alpha;
            key.color_format = target.color_format;
            key.depth_format = target.depth_format;
            RHI::RHIPipeline *pipeline = render_->pipelines().get(key, "imgui");
            if (!pipeline) return;

            // Upload all vertices / indices of the frame at once.
            std::vector<ImDrawVert> vertices;
            std::vector<ImDrawIdx> indices;
            vertices.reserve((size_t)dd->TotalVtxCount);
            indices.reserve((size_t)dd->TotalIdxCount);
            for (int n = 0; n < dd->CmdListsCount; ++n)
            {
                const ImDrawList *list = dd->CmdLists[n];
                vertices.insert(vertices.end(), list->VtxBuffer.Data, list->VtxBuffer.Data + list->VtxBuffer.Size);
                indices.insert(indices.end(), list->IdxBuffer.Data, list->IdxBuffer.Data + list->IdxBuffer.Size);
            }
            Render::FrameAllocator &frames = render_->frames();
            Render::TransientAllocation vb = frames.uploadVertices(vertices.data(), vertices.size() * sizeof(ImDrawVert));
            Render::TransientAllocation ib = frames.uploadVertices(indices.data(), indices.size() * sizeof(ImDrawIdx));
            if (!vb.valid() || !ib.valid()) return;

            // points -> clip, with the target orientation folded in.
            const float ndc_sy = target.row0_top ? 2.0f / fb_h : -2.0f / fb_h;
            float push[4];
            push[0] = 2.0f * sx / fb_w;
            push[1] = ndc_sy * sy;
            push[2] = -1.0f - dd->DisplayPos.x * push[0];
            push[3] = (target.row0_top ? -1.0f : 1.0f) - dd->DisplayPos.y * push[1];

            cmd->setViewport(RHI::Viewport{0.0f, 0.0f, fb_w, fb_h, 0.0f, 1.0f});
            cmd->bindPipeline(pipeline);
            cmd->bindVertexBuffer(vb.buffer, vb.offset);
            cmd->bindIndexBuffer(ib.buffer, sizeof(ImDrawIdx) == 2 ? RHI::IndexType::UInt16 : RHI::IndexType::UInt32, ib.offset);
            cmd->setPushConstants(push, sizeof(push));

            uint32_t vtx_base = 0, idx_base = 0;
            RHI::RHITexture *bound = nullptr;
            for (int n = 0; n < dd->CmdListsCount; ++n)
            {
                const ImDrawList *list = dd->CmdLists[n];
                for (int c = 0; c < list->CmdBuffer.Size; ++c)
                {
                    const ImDrawCmd &dc = list->CmdBuffer[c];
                    if (dc.UserCallback)
                    {
                        if (dc.UserCallback != ImDrawCallback_ResetRenderState) dc.UserCallback(list, &dc);
                        continue;
                    }
                    float x0 = (dc.ClipRect.x - dd->DisplayPos.x) * sx;
                    float y0 = (dc.ClipRect.y - dd->DisplayPos.y) * sy;
                    float x1 = (dc.ClipRect.z - dd->DisplayPos.x) * sx;
                    float y1 = (dc.ClipRect.w - dd->DisplayPos.y) * sy;
                    x0 = std::max(0.0f, x0);
                    y0 = std::max(0.0f, y0);
                    x1 = std::min(fb_w, x1);
                    y1 = std::min(fb_h, y1);
                    if (x1 <= x0 || y1 <= y0) continue;
                    const float row_y = target.row0_top ? y0 : fb_h - y1;
                    cmd->setScissor(RHI::Rect2D{(int32_t)x0, (int32_t)row_y, (uint32_t)(x1 - x0 + 0.5f),
                                                (uint32_t)(y1 - y0 + 0.5f)});
                    RHI::RHITexture *tex = (RHI::RHITexture *)dc.GetTexID();
                    if (!tex) tex = font_texture_;
                    if (tex != bound)
                    {
                        RHI::RHIDescriptorSet *set = render_->textureSet(render_->uiLayout(), tex, render_->linearClampSampler(), false);
                        if (!set) continue;
                        cmd->bindDescriptorSets(pipeline, &set, 1, nullptr, 0);
                        bound = tex;
                    }
                    cmd->drawIndexed(dc.ElemCount, idx_base + dc.IdxOffset, (int32_t)(vtx_base + dc.VtxOffset), 1, 0);
                    ++last_draw_calls_;
                }
                vtx_base += (uint32_t)list->VtxBuffer.Size;
                idx_base += (uint32_t)list->IdxBuffer.Size;
            }
            cmd->setScissor(RHI::Rect2D{0, 0, target.width, target.height});
        }

        void UISystem::setStyle(StylePreset preset)
        {
            ImGuiStyle &style = ImGui::GetStyle();
            switch (preset)
            {
            case StylePreset::Light: ImGui::StyleColorsLight(&style); break;
            case StylePreset::Dark: ImGui::StyleColorsDark(&style); break;
            case StylePreset::Genesis:
            default:
            {
                ImGui::StyleColorsDark(&style);
                ImVec4 *c = style.Colors;
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
            style.WindowRounding = 4.0f;
            style.FrameRounding = 2.0f;
            style.GrabRounding = 2.0f;
            style.ChildRounding = 3.0f;
            style.PopupRounding = 3.0f;
            style.ScrollbarRounding = 3.0f;
            style.FramePadding = ImVec2(6, 4);
            style.ItemSpacing = ImVec2(8, 5);
            style.WindowPadding = ImVec2(8, 8);
        }

        void UISystem::pushLog(const std::string &line)
        {
            logs_.push_back(line);
            if (logs_.size() > 400) logs_.erase(logs_.begin(), logs_.begin() + (logs_.size() - 400));
        }

        void image(const Render::RenderTexturePtr &texture, float width, float height)
        {
            if (!texture || !texture->valid()) return;
            float u0, v0, u1, v1;
            Render::RenderTexture::displayUV(u0, v0, u1, v1);
            ImGui::Image((ImTextureID)texture->color(), ImVec2(width, height), ImVec2(u0, v0), ImVec2(u1, v1));
        }

    } // namespace UI
} // namespace Genesis
