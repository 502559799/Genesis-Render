#include "demo_app.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace Demo
{
    namespace fs = std::filesystem;

    void DemoApp::refreshModelList()
    {
        // Keep external files in the session list; rescan bundled models on demand.
        std::error_code ec;
        for (fs::directory_iterator it(asset("models"), ec), end; !ec && it != end; it.increment(ec))
        {
            if (!it->is_regular_file(ec) || !ModelViewer::accepts(it->path().string())) continue;
            const auto path = fs::weakly_canonical(it->path(), ec).string();
            if (!ec) model_files_.push_back(path);
        }
        auto priority = [](const std::string &path)
        {
            auto name = fs::path(path).filename().string();
            return name == "result.glb" ? 0 : (name == "result (1).glb" ? 1 : 2);
        };
        std::sort(model_files_.begin(), model_files_.end(), [&](const std::string &a, const std::string &b)
        { return priority(a) == priority(b) ? a < b : priority(a) < priority(b); });
        model_files_.erase(std::unique(model_files_.begin(), model_files_.end()), model_files_.end());
    }

    void DemoApp::applyPendingModel()
    {
        if (pending_model_.empty()) return;
        const std::string path = std::move(pending_model_);
        pending_model_.clear();
        engine.device()->waitIdle();
        std::string error;
        if (!model_viewer_.load(engine, path, error)) { model_status_ = error; return; }
        selected_ = model_viewer_.target();
        if (model_auto_frame_) model_viewer_.focus(engine, camera_);
        std::snprintf(model_path_, sizeof(model_path_), "%s", model_viewer_.path().c_str());
        if (std::find(model_files_.begin(), model_files_.end(), model_viewer_.path()) == model_files_.end())
            model_files_.push_back(model_viewer_.path());
        model_status_ = "Loaded " + fs::path(model_viewer_.path()).filename().string();
        if (auto mat = entityMaterial(selected_))
        {
            material_index_ = mat->index;
            scroll_to_material_ = true;
        }
        if (auto *t = engine.world().getComponent<ECS::TransformComponent>(selected_))
        {
            model_euler_ = t->eulerAnglesDegrees();
            model_ui_rotation_ = t->rotation;
        }
    }

    void DemoApp::drawModelPanel()
    {
        ImGui::SetNextWindowPos(ImVec2(8, 30), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(350, std::max(360.0f, ImGui::GetIO().DisplaySize.y - 38)), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("GLB model / Transform", &show_model_)) { ImGui::End(); return; }
        if (scene_ != SceneKind::ResultGlb)
        {
            if (ImGui::Button("Open GLB PBR viewer")) pending_scene_ = SceneKind::ResultGlb;
            ImGui::End();
            return;
        }
        ImGui::TextWrapped("One model + four PBR reference spheres");
        ImGui::TextDisabled("Drop a GLB/glTF file into this window.");
        const std::string name = fs::path(model_viewer_.path()).filename().string();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##model", name.empty() ? "Choose model" : name.c_str()))
        {
            for (const auto &path : model_files_)
            {
                ImGui::PushID(path.c_str());
                bool active = model_viewer_.path() == path;
                if (ImGui::Selectable(fs::path(path).filename().string().c_str(), active)) pending_model_ = path;
                if (active) ImGui::SetItemDefaultFocus();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Refresh list")) refreshModelList();
        ImGui::SameLine();
        if (ImGui::Button("Browse..."))
        {
            const std::string dir = model_viewer_.path().empty() ? asset("models") : fs::path(model_viewer_.path()).parent_path().string();
            std::snprintf(model_directory_, sizeof(model_directory_), "%s", dir.c_str());
            ImGui::OpenPopup("Open GLB / glTF");
        }
        ImGui::SetNextItemWidth(-1);
        bool load = ImGui::InputTextWithHint("##model_path", "Path to .glb / .gltf", model_path_, sizeof(model_path_), ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::Button("Load model", ImVec2(-1, 0))) load = true;
        if (load) pending_model_ = model_path_;
        ImGui::Checkbox("Auto frame on load", &model_auto_frame_);

        ImGui::SetNextWindowSize(ImVec2(640, 430), ImGuiCond_FirstUseEver);
        if (ImGui::BeginPopupModal("Open GLB / glTF", nullptr, ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##directory", model_directory_, sizeof(model_directory_));
            if (ImGui::Button("Parent folder"))
            {
                const auto parent = fs::path(model_directory_).parent_path().string();
                std::snprintf(model_directory_, sizeof(model_directory_), "%s", parent.c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            std::error_code ec;
            std::vector<std::pair<bool, fs::path>> entries;
            for (fs::directory_iterator it(model_directory_, ec), end; !ec && it != end; it.increment(ec))
            {
                const bool dir = it->is_directory(ec);
                if (!ec && (dir || (it->is_regular_file(ec) && ModelViewer::accepts(it->path().string()))))
                    entries.emplace_back(dir, it->path());
            }
            std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b)
            { return a.first != b.first ? a.first > b.first : a.second < b.second; });
            if (ec) ImGui::TextWrapped("Cannot read folder: %s", ec.message().c_str());
            ImGui::BeginChild("files", ImVec2(0, 0), true);
            for (const auto &entry : entries)
            {
                const std::string label = (entry.first ? "[folder] " : "") + entry.second.filename().string();
                if (ImGui::Selectable(label.c_str()))
                {
                    if (entry.first)
                        std::snprintf(model_directory_, sizeof(model_directory_), "%s", entry.second.string().c_str());
                    else
                    {
                        pending_model_ = entry.second.string();
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }

        if (!model_status_.empty()) ImGui::TextWrapped("%s", model_status_.c_str());
        auto &world = engine.world();
        auto *t = world.getComponent<ECS::TransformComponent>(model_viewer_.target());
        auto *mesh = world.getComponent<ECS::MeshComponent>(model_viewer_.target());
        if (!t || !mesh || !mesh->model) { ImGui::End(); return; }
        ImGui::Separator();
        size_t triangles = 0;
        for (const auto &m : mesh->model->meshes) triangles += m.indices.size() / 3;
        ImGui::Text("%zu meshes | %zu materials | %zu triangles", mesh->model->meshes.size(), mesh->model->materials.size(), triangles);
        ImGui::Checkbox("Visible", &mesh->visible);
        ImGui::SameLine();
        if (ImGui::Button("Edit material"))
        {
            selected_ = model_viewer_.target();
            if (auto mat = entityMaterial(selected_)) material_index_ = mat->index;
            show_material_ = scroll_to_material_ = true;
        }
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.55f, 0.8f, 1, 1), "Target Transform");
        ImGui::TextDisabled("Drag values; Ctrl+click to type. X / Y / Z");
        // Preserve continuous Euler edits through 90/180 degrees. Resync when
        // the Inspector or a reset changes the quaternion externally.
        if (t->rotation.x != model_ui_rotation_.x || t->rotation.y != model_ui_rotation_.y ||
            t->rotation.z != model_ui_rotation_.z || t->rotation.w != model_ui_rotation_.w)
            model_euler_ = t->eulerAnglesDegrees();
        const auto before = *t;
        ImGui::PushItemWidth(-1);
        ImGui::TextUnformatted("Position");
        if (ImGui::DragFloat3("##position", &t->position.x, 0.01f)) t->local_dirty = true;
        ImGui::TextUnformatted("Rotation (degrees)");
        if (ImGui::DragFloat3("##rotation", &model_euler_.x, 0.5f))
            t->setEulerDegrees(model_euler_.x, model_euler_.y, model_euler_.z);
        ImGui::Checkbox("Uniform scale", &model_uniform_scale_);
        ImGui::TextUnformatted("Scale");
        if (model_uniform_scale_)
        {
            float scale = t->scale.x;
            if (ImGui::DragFloat("##scale", &scale, std::max(0.001f, std::fabs(scale) * 0.01f), 0.001f, 10000.0f, "%.4f", ImGuiSliderFlags_AlwaysClamp))
                t->setScale(Math::Vec3(scale));
        }
        else if (ImGui::DragFloat3("##scale_xyz", &t->scale.x, 0.01f, 0.001f, 10000.0f, "%.4f", ImGuiSliderFlags_AlwaysClamp))
            t->local_dirty = true;
        ImGui::PopItemWidth();
        for (float f : {t->position.x, t->position.y, t->position.z, t->scale.x, t->scale.y, t->scale.z,
                        t->rotation.x, t->rotation.y, t->rotation.z, t->rotation.w})
            if (!std::isfinite(f)) { *t = before; model_euler_ = t->eulerAnglesDegrees(); break; }
        model_ui_rotation_ = t->rotation;
        if (ImGui::Button("Reset / fit to ground")) model_viewer_.resetTransform(engine);
        if (ImGui::Button("Focus target")) model_viewer_.focus(engine, camera_);
        ImGui::SameLine();
        if (ImGui::Button("Showroom view"))
            if (auto *ctrl = world.getComponent<ECS::CameraControllerComponent>(camera_))
            {
                ctrl->mode = ECS::CameraControllerComponent::Mode::Orbit;
                ctrl->orbit_target = Math::Vec3(-0.6f, 1.0f, -0.4f);
                ctrl->orbit_distance = 7.5f;
                ctrl->orbit_yaw_degrees = 0;
                ctrl->orbit_pitch_degrees = 16;
            }
        ImGui::End();
    }
}
