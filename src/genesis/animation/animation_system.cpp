/*
 * Genesis Render - animation system implementation
 */
#include "animation/animation_system.hpp"

#include "foundation_layer/genesis_log.hpp"
#include "resource/resource_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Genesis
{
    namespace Animation
    {
        namespace
        {
            constexpr uint64_t kVertexFloats = 22; // interleaved Vertex (88 bytes)

            struct AnimationParams
            {
                float p0[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // time, amplitude, frequency, speed
                float p1[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // vertex count, ...
            };
        } // namespace

        AnimationSystem::AnimationSystem() {}
        AnimationSystem::~AnimationSystem() { shutdown(); }

        bool AnimationSystem::initialize(RHI::RHIDevice *device, Render::RenderSystem *render,
                                         API::ComputeAPI *compute)
        {
            device_ = device;
            render_ = render;
            compute_ = compute;
            return device_ != nullptr && render_ != nullptr;
        }

        bool AnimationSystem::computeAvailable() const
        {
            return compute_ && compute_->supportsHardwareCompute();
        }

        void AnimationSystem::releaseRuntime(VertexRuntime &runtime, bool destroy_output)
        {
            if (destroy_output && runtime.mesh && runtime.mesh->animated_vertex_buffer == runtime.output.buffer)
            {
                runtime.mesh->use_animated_vertex_buffer = false;
                runtime.mesh->animated_vertex_buffer = nullptr;
            }
            if (destroy_output && compute_) compute_->destroyBuffer(runtime.output);
            if (compute_) compute_->destroyBuffer(runtime.params);
            runtime.kernel.reset();
            runtime.output = API::ComputeAPI::Buffer{};
            runtime.params = API::ComputeAPI::Buffer{};
            runtime.mesh = nullptr;
            runtime.initialized = false;
        }

        void AnimationSystem::shutdown()
        {
            for (auto &kv : runtimes_) releaseRuntime(*kv.second, false);
            runtimes_.clear();
            sprite_textures_.clear();
        }

        Render::GpuTexture *AnimationSystem::spriteTexture(const std::string &path)
        {
            auto it = sprite_textures_.find(path);
            if (it != sprite_textures_.end()) return &it->second;
            if (!render_) return nullptr;
            Render::GpuTexture texture = render_->scene().textures().acquirePath(path, true);
            if (!texture.valid())
            {
                GEN_LOG_WARN("[Animation] cannot load sprite sheet '%s'", path.c_str());
                return nullptr;
            }
            auto inserted = sprite_textures_.emplace(path, texture);
            return &inserted.first->second;
        }

        void AnimationSystem::updateSprite(ECS::World &world, ECS::Entity entity,
                                           ECS::SpriteAnimationComponent &sprite, float dt)
        {
            if (sprite.texture.empty()) return;
            const uint32_t frames = std::max(1u, sprite.frame_count);
            if (sprite.playing && sprite.fps > 0.0f)
            {
                sprite.time += dt;
                const float frame_time = 1.0f / sprite.fps;
                while (sprite.time >= frame_time)
                {
                    sprite.time -= frame_time;
                    if (sprite.loop) sprite.frame = (sprite.frame + 1) % frames;
                    else sprite.frame = std::min(sprite.frame + 1, frames - 1);
                }
            }
            sprite.frame = std::min(sprite.frame, frames - 1);

            Render::GpuTexture *texture = spriteTexture(sprite.texture);
            if (!texture || !texture->valid() || !render_) return;

            const uint32_t columns = std::max(1u, sprite.columns);
            const uint32_t rows = std::max(1u, sprite.rows);
            const uint32_t frame = std::min(sprite.frame, columns * rows - 1);
            const uint32_t col = frame % columns;
            const uint32_t row = frame / columns;
            const Math::Vec2 uv0((float)col / (float)columns, (float)row / (float)rows);
            const Math::Vec2 uv1((float)(col + 1) / (float)columns, (float)(row + 1) / (float)rows);

            float x = sprite.screen_position.x;
            float y = sprite.screen_position.y;
            float size = sprite.size;
            if (!sprite.screen_space)
            {
                ECS::TransformComponent *transform = world.getComponent<ECS::TransformComponent>(entity);
                if (!transform) return;
                const Render::CameraState &camera = render_->camera();
                const Math::Vec3 pos = transform->world_matrix.translation();
                const Math::Vec4 clip = camera.view_projection * Math::Vec4(pos, 1.0f);
                if (clip.w <= 1e-4f) return;
                const float ndc_x = clip.x / clip.w;
                const float ndc_y = clip.y / clip.w;
                Render::RenderTexturePtr target = render_->hdrTarget();
                const float width = target ? (float)target->width() : 1280.0f;
                const float height = target ? (float)target->height() : 720.0f;
                const float depth = Math::dot(pos - camera.position, camera.forward);
                if (depth <= camera.near_plane) return;
                const float focal = 0.5f * height / std::tan(Math::toRadians(camera.fov_degrees) * 0.5f);
                size = std::max(2.0f, sprite.size * focal / depth);
                x = (ndc_x * 0.5f + 0.5f) * width - size * 0.5f;
                y = (1.0f - (ndc_y * 0.5f + 0.5f)) * height - size * 0.5f;
            }

            render_->immediate(Render::CameraEvent::Overlay)
                .drawSprite(texture->texture, texture->sampler, x, y, size, size, uv0, uv1, sprite.tint);
            ++sprite_count_;
        }

        bool AnimationSystem::ensureRuntime(ECS::World &world, ECS::Entity entity, VertexRuntime &runtime)
        {
            if (runtime.initialized) return true;
            if (!compute_ || !compute_->supportsHardwareCompute() || !render_)
            {
                if (!compute_warned_)
                {
                    GEN_LOG_WARN("[Animation] compute is unavailable on this backend - vertex animation skipped");
                    compute_warned_ = true;
                }
                return false;
            }
            const Render::RenderSystem::EntityMeshBinding *binding = render_->binding(entity);
            if (!binding || binding->mesh_indices.empty())
            {
                GEN_LOG_WARN("[Animation] vertex animation entity has no renderable mesh");
                return false;
            }
            Render::GpuMesh *mesh = render_->scene().mesh(binding->mesh_indices[0]);
            if (!mesh || !mesh->vertex_buffer || mesh->vertex_count == 0) return false;

            runtime.entity = entity;
            runtime.mesh = mesh;
            runtime.output = compute_->createBuffer(
                mesh->vertex_buffer->size(),
                (uint32_t)RHI::BufferUsageFlagBits::Vertex | (uint32_t)RHI::BufferUsageFlagBits::TransferSrc |
                    (uint32_t)RHI::BufferUsageFlagBits::TransferDst,
                RHI::MemoryUsage::GpuOnly);
            runtime.params = compute_->createBuffer(sizeof(AnimationParams),
                                                    (uint32_t)RHI::BufferUsageFlagBits::Uniform,
                                                    RHI::MemoryUsage::CpuToGpu);
            if (!runtime.output.valid() || !runtime.params.valid()) return false;

            API::ComputeAPI::KernelDesc desc;
            desc.shader = "anim/vertex_anim.comp";
            desc.workgroup_x = 64;
            desc.bindings = {{0, "BaseVertexBuffer", true},
                             {1, "AnimatedVertexBuffer", false},
                             {2, "AnimParams", false}};
            std::string error;
            runtime.kernel = compute_->createKernel(desc, &error);
            if (!runtime.kernel)
            {
                GEN_LOG_ERROR("[Animation] compute kernel creation failed: %s", error.c_str());
                return false;
            }
            std::vector<API::ComputeAPI::Buffer *> buffers = {nullptr, &runtime.output, &runtime.params};
            // The base vertex buffer is not owned by the Compute API; wrap it.
            API::ComputeAPI::Buffer base;
            base.buffer = mesh->vertex_buffer;
            base.size = mesh->vertex_buffer->size();
            base.memory_usage = mesh->vertex_buffer->memoryUsage();
            buffers[0] = &base;
            if (!compute_->bind(*runtime.kernel, buffers, &error))
            {
                GEN_LOG_ERROR("[Animation] compute binding failed: %s", error.c_str());
                return false;
            }
            mesh->animated_vertex_buffer = runtime.output.buffer;
            mesh->use_animated_vertex_buffer = true;
            runtime.initialized = true;
            return true;
        }

        void AnimationSystem::updateVertex(ECS::World &world, ECS::Entity entity,
                                           ECS::VertexAnimationComponent &animation, float dt)
        {
            if (!animation.enabled) return;
            auto &slot = runtimes_[entity.id];
            if (!slot) slot = std::make_unique<VertexRuntime>();
            VertexRuntime &runtime = *slot;
            if (!ensureRuntime(world, entity, runtime)) return;
            if (!runtime.mesh || !runtime.mesh->vertex_count) return;

            AnimationParams params;
            params.p0[0] = time_;
            params.p0[1] = animation.amplitude;
            params.p0[2] = animation.frequency;
            params.p0[3] = animation.speed;
            params.p1[0] = (float)runtime.mesh->vertex_count;
            if (!compute_->write(runtime.params, &params, sizeof(params))) return;

            const uint32_t groups = (runtime.mesh->vertex_count + 63u) / 64u;
            if (RHI::RHICommandBuffer *cmd = render_->device()->currentCommandBuffer())
                compute_->dispatch(cmd, *runtime.kernel, groups, 1, 1, &runtime.output,
                                   RHI::BufferAccess::VertexAttributeRead);
            else
                compute_->execute(*runtime.kernel, groups, 1, 1, &runtime.output,
                                  RHI::BufferAccess::VertexAttributeRead);
            ++dispatch_count_;
        }

        void AnimationSystem::update(ECS::World &world, float delta_time)
        {
            if (!render_) return;
            sprite_count_ = 0;
            dispatch_count_ = 0;
            time_ += delta_time;

            world.each<ECS::SpriteAnimationComponent>([&](ECS::Entity entity, ECS::SpriteAnimationComponent &sprite)
                                                      { updateSprite(world, entity, sprite, delta_time); });
            world.each<ECS::VertexAnimationComponent>([&](ECS::Entity entity, ECS::VertexAnimationComponent &animation)
                                                      { updateVertex(world, entity, animation, delta_time); });

            // Release runtimes whose entities are gone or no longer animated.
            for (auto it = runtimes_.begin(); it != runtimes_.end();)
            {
                ECS::Entity e = it->second->entity;
                const bool alive = world.isAlive(e);
                const bool still_animated = alive && world.getComponent<ECS::VertexAnimationComponent>(e) != nullptr;
                if (!alive || !still_animated)
                {
                    releaseRuntime(*it->second, true);
                    it = runtimes_.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

    } // namespace Animation
} // namespace Genesis
