/*
 * Genesis Render - ImGui editor implementation.
 */
#include "ui/editor_ui.hpp"
#include "application_layer/window.hpp"
#include "resource/asset_manager.hpp"
#include "render/pbr_renderer.hpp"
#include "foundation_layer/log.hpp"

#include "imgui.h"
#include "imgui_impl_sdl.h"

#if defined(GENESIS_VULKAN)
#include "imgui_impl_vulkan.h"
#endif
#if defined(GENESIS_OPENGL)
#include "imgui_impl_opengl3.h"
#endif

#include <SDL.h>
#include <cstring>

namespace Genesis {

void EditorUI::init(IDevice* device, Window* window) {
    device_ = device;
    window_ = window->nativeHandle();
    if (window->headless()) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    if (device->backend() == RHIBackend::OpenGL) {
#if defined(GENESIS_OPENGL)
        ImGui_ImplSDL2_InitForOpenGL((SDL_Window*)window_, nullptr);
        ImGui_ImplOpenGL3_Init("#version 410");
        initialized_ = true;
#endif
    } else {
#if defined(GENESIS_VULKAN)
        ImGui_ImplSDL2_InitForVulkan((SDL_Window*)window_);
        ImGui_ImplVulkan_InitInfo init{};
        init.Instance = (VkInstance)device->nativeHandle("vk_instance");
        init.PhysicalDevice = (VkPhysicalDevice)device->nativeHandle("vk_physical_device");
        init.Device = (VkDevice)device->nativeHandle("vk_device");
        init.QueueFamily = (uint32_t)(uintptr_t)device->nativeHandle("vk_queue_family");
        init.Queue = (VkQueue)device->nativeHandle("vk_queue");
        init.DescriptorPool = (VkDescriptorPool)device->nativeHandle("vk_descriptor_pool");
        init.MinImageCount = 2;
        init.ImageCount = (uint32_t)(uintptr_t)device->nativeHandle("min_image_count");
        init.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        VkRenderPass rp = (VkRenderPass)device->nativeHandle("imgui_render_pass");
        ImGui_ImplVulkan_Init(&init, rp);
        // fonts texture
        device->immediateGraphics([](void* c) {
            ImGui_ImplVulkan_CreateFontsTexture((VkCommandBuffer)c);
        });
        initialized_ = true;
#endif
    }
}

void EditorUI::shutdown() {
    if (!initialized_) return;
    if (device_->backend() == RHIBackend::OpenGL) {
#if defined(GENESIS_OPENGL)
        ImGui_ImplOpenGL3_Shutdown();
#endif
    } else {
#if defined(GENESIS_VULKAN)
        ImGui_ImplVulkan_Shutdown();
#endif
    }
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
}

void EditorUI::processSdlEvent(void* ev) {
    if (initialized_) ImGui_ImplSDL2_ProcessEvent((SDL_Event*)ev);
}

void EditorUI::newFrame(Window* window) {
    if (!initialized_) return;
    if (device_->backend() == RHIBackend::OpenGL) {
#if defined(GENESIS_OPENGL)
        ImGui_ImplOpenGL3_NewFrame();
#endif
    } else {
#if defined(GENESIS_VULKAN)
        ImGui_ImplVulkan_NewFrame();
#endif
    }
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

bool EditorUI::wantCaptureMouse() const {
    return initialized_ && ImGui::GetIO().WantCaptureMouse;
}

void EditorUI::renderEditor(Scene& scene, AssetManager& assets, PBRRenderer& renderer) {
    if (!initialized_) return;

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Quit")) { SDL_Event e; e.type = SDL_QUIT; SDL_PushEvent(&e); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("ImGui demo", nullptr, &showDemo);
            ImGui::EndMenu();
        }
        ImGui::Text("  Genesis Render  |  %s  ", device_->info().name.c_str());
        ImGui::SameLine(ImGui::GetWindowWidth() - 250);
        ImGui::Text("%s", device_->info().device.c_str());
        ImGui::EndMainMenuBar();
    }

    // ---------- hierarchy ----------
    ImGui::Begin("Scene Hierarchy");
    scene.registry.view<TagComponent>([&](Entity e) {
        auto* tag = scene.registry.get<TagComponent>(e);
        bool selected = selected_ == e.id;
        if (ImGui::Selectable(tag->name.c_str(), selected)) selected_ = e.id;
    });
    ImGui::End();

    // ---------- inspector ----------
    ImGui::Begin("Inspector");
    Entity sel{selected_};
    if (selected_ && scene.registry.alive(sel)) {
        if (auto* tag = scene.registry.get<TagComponent>(sel))
            ImGui::Text("name: %s", tag->name.c_str());
        if (auto* tr = scene.registry.get<TransformComponent>(sel)) {
            ImGui::Separator(); ImGui::Text("Transform");
            float p[3] = {tr->position.x, tr->position.y, tr->position.z};
            if (ImGui::DragFloat3("position", p, 0.01f)) tr->position = {p[0], p[1], p[2]};
            float e[3] = {tr->euler.x * kRad2Deg, tr->euler.y * kRad2Deg, tr->euler.z * kRad2Deg};
            if (ImGui::DragFloat3("rotation(deg)", e, 0.5f))
                tr->euler = {e[0] * kDeg2Rad, e[1] * kDeg2Rad, e[2] * kDeg2Rad};
            float s[3] = {tr->scale.x, tr->scale.y, tr->scale.z};
            if (ImGui::DragFloat3("scale", s, 0.01f, 0.001f)) tr->scale = {s[0], s[1], s[2]};
        }
        if (auto* cam = scene.registry.get<CameraComponent>(sel)) {
            ImGui::Separator(); ImGui::Text("Camera");
            ImGui::SliderFloat("fov", &cam->fov, 20 * kDeg2Rad, 110 * kDeg2Rad, "%.0f deg");
            ImGui::DragFloat("distance", &cam->distance, 0.05f, 0.1f, 100.0f);
            ImGui::DragFloat3("target", &cam->target.x, 0.01f);
        }
        if (auto* l = scene.registry.get<LightComponent>(sel)) {
            ImGui::Separator(); ImGui::Text("Light");
            const char* types[] = {"Directional", "Point", "Spot"};
            int t = (int)l->type;
            if (ImGui::Combo("type", &t, types, 3)) l->type = (LightType)t;
            ImGui::ColorEdit3("color", &l->color.x);
            ImGui::DragFloat("intensity", &l->intensity, 0.05f, 0.0f, 50.0f);
            ImGui::DragFloat("range", &l->range, 0.1f, 0.1f, 100.0f);
            ImGui::Checkbox("enabled", &l->enabled);
        }
        if (auto* mc = scene.registry.get<MeshComponent>(sel)) {
            ImGui::Separator(); ImGui::Text("Mesh");
            ImGui::Text("model: %s", mc->model ? mc->model->source->name.c_str() : "(none)");
            ImGui::Text("node: %d", mc->node);
            ImGui::Checkbox("visible", &mc->visible);
            ImGui::ColorEdit3("tint", &mc->tint.x);
            ImGui::SliderFloat("tint amount", &mc->tintAlpha, 0.0f, 1.0f);
        }
        if (auto* sc = scene.registry.get<ScriptComponent>(sel)) {
            ImGui::Separator(); ImGui::Text("Script");
            char buf[256]; strncpy(buf, sc->path.c_str(), 255); buf[255] = 0;
            if (ImGui::InputText("lua file", buf, 256)) sc->path = buf;
            ImGui::Checkbox("enabled", &sc->enabled);
            if (ImGui::Button("run/restart")) {
                sc->started = false; sc->handle = -1;
                if (onRunScript) onRunScript(sc->path);
            }
        }
    } else {
        ImGui::TextDisabled("select an entity");
    }
    ImGui::End();

    // ---------- render settings ----------
    ImGui::Begin("Render Settings");
    auto& rs = scene.settings;
    ImGui::Checkbox("direct lighting", &rs.directLightEnabled);
    int lm = rs.lightingModel;
    ImGui::Combo("BRDF", &lm, "Cook-Torrance (H)\0Blinn-Phong\0");
    rs.lightingModel = lm;
    ImGui::Checkbox("IBL (indirect)", &rs.iblEnabled);
    ImGui::Checkbox("skybox", &rs.skyboxEnabled);
    ImGui::SliderFloat("IBL strength", &rs.iblIntensity, 0.0f, 4.0f);
    ImGui::SliderFloat("exposure", &rs.exposure, 0.05f, 4.0f);
    ImGui::SliderAngle("env rotation", &rs.envRotation);
    ImGui::Separator();
    ImGui::Text("Sun");
    ImGui::Checkbox("sun enabled", &rs.sun.enabled);
    ImGui::ColorEdit3("sun color", &rs.sun.color.x);
    ImGui::DragFloat("sun intensity", &rs.sun.intensity, 0.05f, 0.0f, 20.0f);
    float sunDir[3] = {rs.sun.direction.x, rs.sun.direction.y, rs.sun.direction.z};
    if (ImGui::DragFloat3("sun direction", sunDir, 0.01f, -1.0f, 1.0f))
        rs.sun.direction = normalize(Vec3{sunDir[0], sunDir[1], sunDir[2]});
    ImGui::End();

    // ---------- assets / import ----------
    ImGui::Begin("Assets");
    ImGui::InputText("model path", importPath, sizeof importPath);
    ImGui::Text("glTF/glb parsed by cgltf; FBX via Assimp");
    if (ImGui::Button("import & instance"))
        if (onImportModel) onImportModel(importPath);
    ImGui::SameLine();
    if (ImGui::Button("helmet.glb")) {
        strcpy(importPath, "assets/models/DamagedHelmet.glb");
        if (onImportModel) onImportModel(importPath);
    }
    ImGui::SameLine();
    if (ImGui::Button("helmet.fbx")) {
        strcpy(importPath, "assets/models/DamagedHelmet.fbx");
        if (onImportModel) onImportModel(importPath);
    }
    ImGui::Text("busy loading: %s", assets.busy() ? "yes" : "no");
    ImGui::End();

    // ---------- stats ----------
    float now = (float)SDL_GetTicks() / 1000.0f;
    static float last = now, acc = 0; static int frames = 0;
    frames++; acc += now - last; last = now;
    if (acc > 0.5f) { fps_ = frames / acc; frames = 0; acc = 0; }
    ImGui::Begin("Stats");
    ImGui::Text("fps: %.1f", fps_);
    ImGui::Text("draw calls: %u", renderer.drawCalls);
    ImGui::Text("triangles: %u", renderer.triangles);
    ImGui::Text("entities: %zu", scene.registry.entityCount());
    ImGui::Text("backend: %s", device_->info().version.c_str());
    ImGui::Text("device: %s", device_->info().device.c_str());
    ImGui::Text("IBL baked: %s", renderer.isBaked() ? "yes" : "no");
    if (onResetCamera && ImGui::Button("reset camera")) onResetCamera();
    if (onToggleBackend && ImGui::Button("restart with other backend")) onToggleBackend();
    ImGui::End();

    if (showDemo) ImGui::ShowDemoWindow(&showDemo);
}

void EditorUI::renderDrawData() {
    if (!initialized_) return;
    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    if (device_->backend() == RHIBackend::OpenGL) {
#if defined(GENESIS_OPENGL)
        ImGui_ImplOpenGL3_RenderDrawData(dd);
#endif
    } else {
#if defined(GENESIS_VULKAN)
        ImGui_ImplVulkan_RenderDrawData(dd, (VkCommandBuffer)device_->nativeHandle("current_cmd"));
#endif
    }
}

} // namespace Genesis
