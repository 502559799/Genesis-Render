/*
 * Genesis Render - Rendering API facade
 *
 * The public, cross-platform GPU rendering entry point.  It delegates to the
 * existing RHIDevice / RenderSystem (which remain the implementation), giving
 * the engine four clearly separated public APIs:
 *
 *   API::RenderingAPI      - this file
 *   API::ComputeAPI        - compute shaders / storage buffers
 *   API::RayTracingAPI     - portable GPU ray tracing
 *   API::EncoderDecoderAPI - hardware / software media codecs
 *
 * Keeping this facade thin preserves every existing demo/test/code path while
 * providing a stable surface for tools and external integrations.
 */
#ifndef __GENESIS_RENDERING_API_HPP__
#define __GENESIS_RENDERING_API_HPP__

#include "ecs/ecs.hpp"
#include "renderer/render_system.hpp"
#include "rhi/rhi.hpp"

#include <memory>
#include <string>

namespace Genesis
{
    namespace API
    {
        class RenderingAPI
        {
        public:
            RenderingAPI() = default;

            bool initialize(RHI::RHIDevice *device, Render::RenderSystem *render, ECS::World *world)
            {
                device_ = device;
                render_ = render;
                world_ = world;
                return device_ && render_;
            }

            bool valid() const { return device_ && render_; }
            RHI::RHIDevice *device() const { return device_; }
            Render::RenderSystem *renderer() const { return render_; }
            ECS::World *world() const { return world_; }

            const char *backendName() const { return device_ ? RHI::backendTypeName(device_->backend()) : "none"; }
            uint32_t width() const
            {
                return render_ && render_->hdrTarget() ? render_->hdrTarget()->width() : 0;
            }
            uint32_t height() const
            {
                return render_ && render_->hdrTarget() ? render_->hdrTarget()->height() : 0;
            }

            Render::RenderSettings &settings() { return render_->settings(); }
            Render::RenderScene &scene() { return render_->scene(); }
            Render::RenderStats &stats() { return render_->stats(); }
            Render::CameraState &camera() { return render_->camera(); }

            Render::RenderTexturePtr createRenderTexture(const std::string &name, uint32_t w, uint32_t h,
                                                         bool hdr = false, bool depth = true)
            {
                return render_ ? render_->createRenderTexture(name, w, h, hdr, depth) : nullptr;
            }

            void waitIdle() { if (device_) device_->waitIdle(); }

        private:
            RHI::RHIDevice *device_ = nullptr;
            Render::RenderSystem *render_ = nullptr;
            ECS::World *world_ = nullptr;
        };
    } // namespace API
} // namespace Genesis

#endif /* __GENESIS_RENDERING_API_HPP__ */
