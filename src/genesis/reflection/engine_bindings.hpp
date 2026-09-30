/*
 * Genesis Render - reflection of the engine types.
 *
 * registerEngineTypes() describes every scriptable engine object to the RTTI
 * registry: Engine, World, Entity, all ECS components, Renderer +
 * RenderSettings, Material, RenderTexture, CommandBuffer, MeshAsset and
 * Input.  Scripts (and the editor inspector) then reach all of them through
 * the generic reflection API.
 */
#ifndef __GENESIS_ENGINE_BINDINGS_HPP__
#define __GENESIS_ENGINE_BINDINGS_HPP__

#include "ecs/components.hpp"
#include "reflection/reflection.hpp"
#include "renderer/command_buffer.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Genesis
{
    class Engine;
    namespace Render
    {
        class RenderSystem;
    }
    namespace Platform
    {
        class InputSystem;
    }

    namespace Reflect
    {
        // The objects bindings operate on (set by Engine::initialize).
        struct EngineContext
        {
            Engine *engine = nullptr;
            ECS::World *world = nullptr;
            Render::RenderSystem *render = nullptr;
            Platform::InputSystem *input = nullptr;
        };
        EngineContext &context();

        // Idempotent; called by the engine before scripts start.
        void registerEngineTypes();

        // Entities are passed around as handles that validate their
        // generation on every access.
        struct EntityHandle
        {
            ECS::World *world = nullptr;
            ECS::Entity entity;
        };
        Value entityValue(ECS::World *world, ECS::Entity e);
        bool entityFromValue(const Value &v, ECS::Entity &out);

        // Component access by name (used by Entity:get/add/has/remove).
        struct ComponentBinding
        {
            std::string name;
            const TypeInfo *type = nullptr;
            std::function<bool(ECS::World &, ECS::Entity)> has;
            std::function<Value(ECS::World &, ECS::Entity)> add;
            std::function<Value(ECS::World &, ECS::Entity)> get;
            std::function<void(ECS::World &, ECS::Entity)> remove;
        };
        const std::vector<ComponentBinding> &componentBindings();
        const ComponentBinding *findComponent(const std::string &name);

        // A GPU-resident mesh (procedural or loaded) that entities and
        // command buffers can draw.
        struct MeshAsset
        {
            std::string name;
            std::shared_ptr<ModelData> model;
            std::vector<uint32_t> gpu_meshes;
        };
        using MeshAssetPtr = std::shared_ptr<MeshAsset>;
        // kind: sphere | box | cube | plane | torus.  size: radius / extent.
        MeshAssetPtr createMeshAsset(const std::string &kind, float size = 1.0f, int detail = 32);
        MeshAssetPtr loadMeshAsset(const std::string &path);
        // Attaches a mesh (+ optional material for every sub mesh) to an entity.
        bool attachMesh(ECS::Entity e, const MeshAssetPtr &mesh, const std::shared_ptr<Render::Material> &material);

        // "camera" | "backbuffer" | RenderTexture object -> command buffer target.
        Render::RenderTargetId targetFromValue(const Value &v);

        template <>
        struct Traits<ECS::Entity>
        {
            static Value to(ECS::Entity e) { return entityValue(context().world, e); }
            static ECS::Entity from(const Value &v)
            {
                ECS::Entity e = ECS::nullEntity();
                entityFromValue(v, e);
                return e;
            }
            static ValueType type() { return ValueType::Object; }
        };

    } // namespace Reflect
} // namespace Genesis

#endif /* __GENESIS_ENGINE_BINDINGS_HPP__ */
