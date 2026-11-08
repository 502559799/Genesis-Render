#include "model_viewer.hpp"
#include "resource/resource_types.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace Demo
{
    using namespace Genesis;

    bool ModelViewer::accepts(const std::string &path)
    {
        const std::string ext = ResourceIO::pathExtension(path);
        return ext == "glb" || ext == "gltf";
    }

    ECS::TransformComponent ModelViewer::fittedTransform(const Math::AABB &bounds)
    {
        ECS::TransformComponent t;
        if (!bounds.valid) return t;
        const Math::Vec3 size = bounds.size();
        const float longest = std::max({size.x, size.y, size.z});
        if (!std::isfinite(longest) || longest <= 1e-6f) return t;
        const float scale = 2.8f / longest;
        const Math::Vec3 center = bounds.center();
        t.setScale(Math::Vec3(scale));
        t.setPosition(Math::Vec3(-center.x * scale, -bounds.minPoint.y * scale, -center.z * scale));
        return t;
    }

    void ModelViewer::detach(Engine &engine)
    {
        if (engine.world().isAlive(target_))
        {
            if (auto *t = engine.world().getComponent<ECS::TransformComponent>(target_))
                for (auto &asset : assets_) if (asset.path == path_) asset.transform = *t;
            engine.world().destroyEntity(target_);
        }
        target_ = ECS::nullEntity();
    }

    bool ModelViewer::load(Engine &engine, const std::string &path, std::string &error)
    {
        error.clear();
        if (!accepts(path)) { error = "Choose a .glb or .gltf file."; return false; }
        std::error_code ec;
        auto canonical = std::filesystem::canonical(path, ec);
        if (ec || !std::filesystem::is_regular_file(canonical, ec))
        { error = "File not found: " + path; return false; }
        const std::string key = canonical.string();
        auto it = std::find_if(assets_.begin(), assets_.end(), [&](const Asset &a) { return a.path == key; });
        if (it == assets_.end())
        {
            auto model = ResourceIO::loadModel(key);
            if (!model || model->meshes.empty() || model->flattenNodes().empty() || !model->bounds.valid)
            { error = "Cannot import model. See Log for format/validation details: " + path; return false; }
            const auto &b = model->bounds;
            for (float f : {b.minPoint.x, b.minPoint.y, b.minPoint.z, b.maxPoint.x, b.maxPoint.y, b.maxPoint.z})
                if (!std::isfinite(f)) { error = "Model contains non-finite bounds."; return false; }
            Asset asset;
            asset.path = key;
            asset.model = std::move(model);
            asset.transform = fittedTransform(asset.model->bounds);
            asset.gpu_meshes = engine.render().scene().uploadModel(*asset.model);
            for (uint32_t index : asset.gpu_meshes)
                if (!engine.render().scene().mesh(index)) { error = "GPU mesh upload failed."; return false; }
            assets_.push_back(std::move(asset));
            it = assets_.end() - 1;
        }
        // Only replace the old entity after parsing, validation and upload succeed.
        detach(engine);
        path_ = key;
        target_ = engine.world().createEntity("GLB Target: " + canonical.filename().string());
        engine.world().addComponent<ECS::TransformComponent>(target_, it->transform);
        ECS::MeshComponent mesh;
        mesh.model = it->model;
        mesh.pick_id = target_.id + 1;
        engine.world().addComponent<ECS::MeshComponent>(target_, mesh);
        Render::RenderSystem::EntityMeshBinding binding;
        binding.entity = target_;
        binding.mesh_indices = it->gpu_meshes;
        engine.render().bindEntityMeshes(binding);
        return true;
    }

    void ModelViewer::resetTransform(Engine &engine)
    {
        auto *mesh = engine.world().getComponent<ECS::MeshComponent>(target_);
        auto *t = engine.world().getComponent<ECS::TransformComponent>(target_);
        if (t && mesh && mesh->model) *t = fittedTransform(mesh->model->bounds);
    }

    void ModelViewer::focus(Engine &engine, ECS::Entity camera)
    {
        auto *mesh = engine.world().getComponent<ECS::MeshComponent>(target_);
        auto *t = engine.world().getComponent<ECS::TransformComponent>(target_);
        auto *cam = engine.world().getComponent<ECS::CameraComponent>(camera);
        auto *ctrl = engine.world().getComponent<ECS::CameraControllerComponent>(camera);
        if (!mesh || !mesh->model || !t || !cam || !ctrl) return;
        const Math::AABB bounds = mesh->model->bounds.transformed(Math::Mat4::compose(t->position, t->rotation, t->scale));
        const float aspect = (float)engine.render().hdrTarget()->width() / std::max(1u, engine.render().hdrTarget()->height());
        const float half_fov = std::atan(std::tan(Math::toRadians(cam->fov_degrees) * 0.5f) * std::min(1.0f, aspect));
        ctrl->mode = ECS::CameraControllerComponent::Mode::Orbit;
        ctrl->orbit_target = bounds.center();
        ctrl->orbit_distance = std::max(0.2f, bounds.radius() * 1.25f / std::sin(half_fov));
        cam->far_plane = std::max(cam->far_plane, ctrl->orbit_distance + bounds.radius() * 3.0f);
    }
}
