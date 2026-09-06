/*
 * Genesis Render - Engine
 * ui_service.cpp
 */
#include "ui_service.hpp"
#include "foundation_layer/log.hpp"

#include "external/imgui/imgui.h"

#include <cmath>
#include <algorithm>

namespace Genesis
{
void UIService::init(IRHIDevice *device)
{
    device_ = device;
    IMGUI_CHECKVERSION();
    ctx_ = ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsDark();
    ImGuiStyle &s = ImGui::GetStyle();
    s.WindowRounding = 6.0f;
    s.FrameRounding = 4.0f;
    s.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.13f, 0.92f);
    device_->imguiBackendInit();
}

void UIService::shutdown()
{
    if (device_) device_->imguiBackendShutdown();
    if (ctx_) ImGui::DestroyContext(ctx_);
    ctx_ = nullptr;
}

void UIService::newFrame()
{
    device_->imguiBackendBeginFrame();
    ImGui::NewFrame();
}

void UIService::renderDrawData()
{
    ImGui::Render();
    device_->imguiBackendRenderDrawData(ImGui::GetDrawData());
}

// ----------------------------------------------------------- editor panels
static void eulerFromQuat(const Quat &q, float &pitch, float &yaw, float &roll)
{
    // Inverse of quatFromEuler: R = Rz(roll) * Ry(yaw) * Rx(pitch).
    float m20 = 2.0f * (q.x * q.z - q.w * q.y);
    float m21 = 2.0f * (q.y * q.z + q.w * q.x);
    float m22 = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
    float m10 = 2.0f * (q.x * q.y + q.w * q.z);
    float m00 = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
    pitch = std::atan2(m21, m22);
    yaw = std::asin(std::clamp(-m20, -1.0f, 1.0f));
    roll = std::atan2(m10, m00);
}

static void drawHierarchy(EditorState &st)
{
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(235, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin("Scene Hierarchy");
    static int add_type = 0;
    const char *types[] = {"Cube", "Sphere", "Plane"};
    ImGui::SetNextItemWidth(110);
    ImGui::Combo("Prim", &add_type, types, 3);
    ImGui::SameLine();
    if (ImGui::Button("Add")) st.addPrimitive(types[add_type]);
    ImGui::Separator();
    if (st.scene)
    {
        for (Entity e : st.scene->allEntities())
        {
            std::string name = st.scene->entityName(e);
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (st.selected == e) flags |= ImGuiTreeNodeFlags_Selected;
            ImGui::TreeNodeEx((void *)(intptr_t)(e.index + 1), flags, "%s", name.c_str());
            if (ImGui::IsItemClicked()) st.selected = e;
        }
    }
    ImGui::End();
}

static void drawInspector(EditorState &st)
{
    ImGui::SetNextWindowPos(ImVec2(10, 322), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(270, 340), ImGuiCond_FirstUseEver);
    ImGui::Begin("Inspector");
    if (!st.scene || !st.scene->isValid(st.selected))
    {
        ImGui::TextUnformatted("No entity selected.");
        ImGui::End();
        return;
    }
    Entity e = st.selected;
    Scene *sc = st.scene;
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", sc->entityName(e).c_str());
    if (ImGui::InputText("Name", buf, sizeof(buf))) sc->setEntityName(e, buf);

    if (auto *t = sc->getComponent<TransformComponent>(e))
    {
        ImGui::Separator(); ImGui::TextUnformatted("Transform");
        float p[3] = {t->position.x, t->position.y, t->position.z};
        if (ImGui::DragFloat3("Position", p, 0.02f)) t->position = Vec3(p[0], p[1], p[2]);
        float pitch, yaw, roll;
        eulerFromQuat(t->rotation, pitch, yaw, roll);
        float euler[3] = {degrees(pitch), degrees(yaw), degrees(roll)};
        if (ImGui::DragFloat3("Rotation", euler, 0.3f))
            t->rotation = quatFromEuler(radians(euler[0]), radians(euler[1]), radians(euler[2]));
        float s[3] = {t->scale.x, t->scale.y, t->scale.z};
        if (ImGui::DragFloat3("Scale", s, 0.02f, 0.01f, 64.0f)) t->scale = Vec3(s[0], s[1], s[2]);
    }
    if (auto *cam = sc->getComponent<CameraComponent>(e))
    {
        ImGui::Separator(); ImGui::TextUnformatted("Camera");
        ImGui::DragFloat("FOV", &cam->fov_y, 0.5f, 10.f, 120.f);
        ImGui::DragFloat("Distance", &cam->distance, 0.1f, 0.2f, 200.f);
        ImGui::DragFloat("Near", &cam->near_z, 0.01f, 0.01f, 10.f);
        ImGui::DragFloat("Far", &cam->far_z, 1.f, 10.f, 2000.f);
        float tg[3] = {cam->target.x, cam->target.y, cam->target.z};
        if (ImGui::DragFloat3("Target", tg, 0.02f)) cam->target = Vec3(tg[0], tg[1], tg[2]);
    }
    if (auto *dl = sc->getComponent<DirectionalLightComponent>(e))
    {
        ImGui::Separator(); ImGui::TextUnformatted("Directional Light");
        float c[3] = {dl->color.x, dl->color.y, dl->color.z};
        if (ImGui::ColorEdit3("Color", c)) dl->color = Vec3(c[0], c[1], c[2]);
        ImGui::SliderFloat("Intensity", &dl->intensity, 0.0f, 12.0f);
    }
    if (auto *pl = sc->getComponent<PointLightComponent>(e))
    {
        ImGui::Separator(); ImGui::TextUnformatted("Point Light");
        float c[3] = {pl->color.x, pl->color.y, pl->color.z};
        if (ImGui::ColorEdit3("Color", c)) pl->color = Vec3(c[0], c[1], c[2]);
        ImGui::SliderFloat("Intensity", &pl->intensity, 0.0f, 100.0f);
        ImGui::SliderFloat("Range", &pl->range, 1.0f, 100.0f);
    }
    if (auto *mc = sc->getComponent<MeshComponent>(e))
    {
        ImGui::Separator(); ImGui::TextUnformatted("Mesh");
        ImGui::TextUnformatted(mc->model ? mc->model->name.c_str() : "(none)");
        if (mc->model)
            for (size_t i = 0; i < mc->model->materials.size() && i < 4; ++i)
            {
                auto &m = mc->model->materials[i];
                ImGui::PushID((int)i);
                float bc[3] = {m->ubo.base_color_factor.x, m->ubo.base_color_factor.y,
                               m->ubo.base_color_factor.z};
                if (ImGui::ColorEdit3("Base Color", bc)) m->setBaseColor(Vec3(bc[0], bc[1], bc[2]));
                float metal = m->metallic(), rough = m->roughness();
                if (ImGui::SliderFloat("Metallic", &metal, 0, 1)) m->setMetallic(metal);
                if (ImGui::SliderFloat("Roughness", &rough, 0.045f, 1)) m->setRoughness(rough);
                float em[3] = {m->ubo.emissive_factor.x, m->ubo.emissive_factor.y,
                               m->ubo.emissive_factor.z};
                if (ImGui::ColorEdit3("Emissive", em))
                    m->ubo.emissive_factor = Vec4(em[0], em[1], em[2], m->ubo.emissive_factor.w);
                ImGui::PopID();
            }
    }
    if (auto *env = sc->getComponent<EnvironmentComponent>(e))
    {
        ImGui::Separator(); ImGui::TextUnformatted("Environment (IBL)");
        ImGui::TextWrapped("%s", env->equirect_path.empty() ? "procedural sky" : env->equirect_path.c_str());
        ImGui::SliderFloat("Env Intensity", &env->intensity, 0.0f, 4.0f);
        ImGui::Checkbox("Show Skybox", &env->show_skybox);
    }
    if (auto *lua = sc->getComponent<LuaScriptComponent>(e))
    {
        ImGui::Separator(); ImGui::TextUnformatted("Lua Script");
        ImGui::TextWrapped("%s", lua->path.c_str());
        ImGui::Checkbox("Enabled", &lua->enabled);
    }
    ImGui::End();
}

static void drawRendering(EditorState &st)
{
    ImGui::SetNextWindowPos(ImVec2(257.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(292.0f, 270.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Rendering");
    if (st.device)
    {
        const DeviceCaps &c = st.device->caps();
        ImGui::Text("Backend: %s", backendName(c.backend));
        ImGui::Text("Device : %s", c.device_name);
        ImGui::Text("API    : %s", c.api_version);
    }
    ImGui::Separator();
    if (st.settings)
    {
        ImGui::Checkbox("Direct Lighting", &st.settings->direct_lighting);
        ImGui::Checkbox("Image Based Lighting", &st.settings->image_based_lighting);
        ImGui::Checkbox("Skybox", &st.settings->show_skybox);
        const char *models[] = {"PBR (GGX Cook-Torrance)", "Blinn-Phong"};
        ImGui::Combo("Direct Model", &st.settings->direct_model, models, 2);
        ImGui::SliderFloat("IBL Intensity", &st.settings->env_intensity, 0.0f, 4.0f);
        float amb[3] = {st.settings->ambient.x, st.settings->ambient.y, st.settings->ambient.z};
        if (ImGui::ColorEdit3("Ambient", amb)) st.settings->ambient = Vec3(amb[0], amb[1], amb[2]);
    }
    ImGui::Separator();
    ImGui::Text("Frame: %.2f ms (%d FPS)", st.frame_ms, st.fps);
    if (ImGui::Button("Reload Lua Scripts")) st.reloadScripts();
    if (!st.lua_error.empty())
    {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "Lua:");
        ImGui::TextWrapped("%s", st.lua_error.c_str());
    }
    ImGui::End();
}

void drawEditor(EditorState &st)
{
    drawHierarchy(st);
    drawInspector(st);
    drawRendering(st);
}
}
