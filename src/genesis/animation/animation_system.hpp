/*
 * Genesis Render - animation system
 *
 * Two animation paths share one ECS system (order 92):
 *
 *   1. SpriteAnimation  - 2D sprite-sheet sequence frames.  The system
 *                         advances time / frame and records a textured quad
 *                         with a per-frame UV rect into the overlay batch.
 *   2. VertexAnimation  - 3D vertex animation driven by a compute shader.
 *                         A compute-written vertex buffer is bound instead of
 *                         the rest-pose buffer by the mesh pass.
 *
 * Both paths are backend agnostic: the sprite path uses the existing
 * ui/batch pipeline, the vertex path uses the Compute API.
 */
#ifndef __GENESIS_ANIMATION_SYSTEM_HPP__
#define __GENESIS_ANIMATION_SYSTEM_HPP__

#include "api/compute_api.hpp"
#include "ecs/components.hpp"
#include "renderer/render_system.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Genesis
{
    namespace Animation
    {
        class AnimationSystem : public ECS::System
        {
        public:
            AnimationSystem();
            ~AnimationSystem() override;

            bool initialize(RHI::RHIDevice *device, Render::RenderSystem *render, API::ComputeAPI *compute);
            void shutdown();

            const char *name() const override { return "AnimationSystem"; }
            int32_t order() const override { return 92; }
            void update(ECS::World &world, float delta_time) override;

            size_t sprites() const { return sprite_count_; }
            size_t vertex_animations() const { return runtimes_.size(); }
            size_t dispatches() const { return dispatch_count_; }
            bool computeAvailable() const;

        private:
            struct VertexRuntime
            {
                ECS::Entity entity = ECS::nullEntity();
                Render::GpuMesh *mesh = nullptr;
                API::ComputeAPI::Buffer output;
                API::ComputeAPI::Buffer params;
                std::unique_ptr<API::ComputeAPI::Kernel> kernel;
                bool initialized = false;
            };

            void updateSprite(ECS::World &world, ECS::Entity entity, ECS::SpriteAnimationComponent &sprite, float dt);
            void updateVertex(ECS::World &world, ECS::Entity entity, ECS::VertexAnimationComponent &animation, float dt);
            bool ensureRuntime(ECS::World &world, ECS::Entity entity, VertexRuntime &runtime);
            void releaseRuntime(VertexRuntime &runtime, bool destroy_output);
            Render::GpuTexture *spriteTexture(const std::string &path);

            RHI::RHIDevice *device_ = nullptr;
            Render::RenderSystem *render_ = nullptr;
            API::ComputeAPI *compute_ = nullptr;
            std::unordered_map<std::string, Render::GpuTexture> sprite_textures_;
            std::unordered_map<uint32_t, std::unique_ptr<VertexRuntime>> runtimes_;
            size_t sprite_count_ = 0;
            size_t dispatch_count_ = 0;
            float time_ = 0.0f;
            bool compute_warned_ = false;
        };
    } // namespace Animation
} // namespace Genesis

#endif /* __GENESIS_ANIMATION_SYSTEM_HPP__ */
