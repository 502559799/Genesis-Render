/*
 * Genesis Render - Ray Tracing API
 *
 * The public surface is backend independent:
 *
 *   initialize(compute, device)
 *   setScene(spheres)
 *   trace(camera, settings) -> RGBA8 texture
 *
 * On every backend the default path is a GPU compute ray tracer
 * (brute-force sphere/plane intersections, shadow rays, one reflection
 * bounce) recorded through the Compute API.  Native hardware ray tracing
 * (VK_KHR_ray_tracing / Metal ray tracing) can be added behind the same API:
 * `hardwareRayTracing()` reports whether a native path is active.
 *
 * When compute is unavailable (e.g. OpenGL 4.1 on macOS) a CPU tracer with
 * the same shading model is used, so the page and tests remain functional.
 */
#ifndef __GENESIS_RAY_TRACING_API_HPP__
#define __GENESIS_RAY_TRACING_API_HPP__

#include "api/compute_api.hpp"
#include "foundation_layer/math/math.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Genesis
{
    namespace API
    {
        struct RTSphere
        {
            Math::Vec3 center = Math::Vec3(0.0f);
            float radius = 1.0f;
            Math::Color color = Math::Color(1.0f, 1.0f, 1.0f, 1.0f);
            float metallic = 0.0f;
        };

        struct RTCamera
        {
            Math::Vec3 eye = Math::Vec3(0.0f, 1.5f, 5.0f);
            Math::Vec3 forward = Math::Vec3(0.0f, 0.0f, -1.0f);
            Math::Vec3 right = Math::Vec3(1.0f, 0.0f, 0.0f);
            Math::Vec3 up = Math::Vec3(0.0f, 1.0f, 0.0f);
            float fov_degrees = 50.0f;
            float aspect = 16.0f / 9.0f;
        };

        struct RTSettings
        {
            uint32_t width = 320;
            uint32_t height = 180;
            uint32_t samples = 2;
            float time = 0.0f;
        };

        struct RTStats
        {
            double trace_ms = 0.0;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t samples = 0;
            uint32_t spheres = 0;
            bool gpu = false;
            bool hardware_ray_tracing = false;
        };

        class RayTracingAPI
        {
        public:
            RayTracingAPI() = default;
            ~RayTracingAPI();

            bool initialize(ComputeAPI *compute, RHI::RHIDevice *device);
            void shutdown();
            bool available() const { return device_ != nullptr; }

            void setScene(const std::vector<RTSphere> &spheres);
            const std::vector<RTSphere> &scene() const { return spheres_; }

            // Traces the scene and returns an RGBA8 texture (row 0 = top).
            bool trace(const RTCamera &camera, const RTSettings &settings);

            RHI::RHITexture *outputTexture() const { return output_texture_; }
            RHI::RHISampler *sampler() const { return sampler_; }
            const std::vector<uint8_t> &pixels() const { return pixels_; }
            const RTStats &stats() const { return stats_; }
            bool hardwareRayTracing() const { return hardware_rt_; }
            std::string backendName() const;

        private:
            bool traceGPU(const RTCamera &camera, const RTSettings &settings);
            void traceCPU(const RTCamera &camera, const RTSettings &settings);
            bool ensureResources(const RTSettings &settings);
            void releaseResources();

            ComputeAPI *compute_ = nullptr;
            RHI::RHIDevice *device_ = nullptr;
            RHI::RHITexture *output_texture_ = nullptr;
            RHI::RHISampler *sampler_ = nullptr;
            std::unique_ptr<ComputeAPI::Kernel> kernel_;
            ComputeAPI::Buffer scene_buffer_;
            ComputeAPI::Buffer params_buffer_;
            ComputeAPI::Buffer output_buffer_;
            std::vector<RTSphere> spheres_;
            std::vector<uint8_t> pixels_;
            RTStats stats_;
            uint32_t width_ = 0;
            uint32_t height_ = 0;
            bool hardware_rt_ = false;
        };
    } // namespace API
} // namespace Genesis

#endif /* __GENESIS_RAY_TRACING_API_HPP__ */
