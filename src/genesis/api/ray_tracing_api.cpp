/*
 * Genesis Render - Ray Tracing API implementation
 */
#include "api/ray_tracing_api.hpp"

#include "foundation_layer/genesis_log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace Genesis
{
    namespace API
    {
        namespace
        {
            struct RTParams
            {
                float p0[4] = {0, 0, 0, 0};
                float p1[4] = {0, 0, 0, 0};
                float p2[4] = {0, 0, 0, 0};
                float p3[4] = {0, 0, 0, 0};
                float p4[4] = {0, 0, 0, 0};
            };

            constexpr uint32_t kMaxSpheres = 32;

            using Math::normalize;
            using Math::dot;

            struct CpuHit
            {
                float t = 1e30f;
                Math::Vec3 normal = Math::Vec3(0, 1, 0);
                Math::Vec3 albedo = Math::Vec3(0.0f);
                float metallic = 0.0f;
            };

            bool intersectSphere(const Math::Vec3 &origin, const Math::Vec3 &dir, const RTSphere &sphere, float &t)
            {
                const Math::Vec3 oc = origin - sphere.center;
                const float b = dot(oc, dir);
                const float c = dot(oc, oc) - sphere.radius * sphere.radius;
                const float disc = b * b - c;
                if (disc < 0.0f) return false;
                const float root = std::sqrt(disc);
                t = -b - root;
                if (t < 0.001f) t = -b + root;
                return t > 0.001f;
            }

            CpuHit traceSceneCPU(const std::vector<RTSphere> &spheres, const Math::Vec3 &origin, const Math::Vec3 &dir)
            {
                CpuHit hit;
                for (const RTSphere &sphere : spheres)
                {
                    float t;
                    if (intersectSphere(origin, dir, sphere, t) && t < hit.t)
                    {
                        hit.t = t;
                        hit.normal = normalize(origin + dir * t - sphere.center);
                        hit.albedo = Math::Vec3(sphere.color.r, sphere.color.g, sphere.color.b);
                        hit.metallic = sphere.metallic;
                    }
                }
                if (std::fabs(dir.y) > 1e-5f)
                {
                    const float t = -origin.y / dir.y;
                    if (t > 0.001f && t < hit.t)
                    {
                        const Math::Vec3 p = origin + dir * t;
                        const float checker = (float)((int)std::floor(p.x) + (int)std::floor(p.z)) ;
                        const float c = std::fmod(std::fabs(checker), 2.0f) < 1.0f ? 0.08f : 0.28f;
                        hit.t = t;
                        hit.normal = Math::Vec3(0, 1, 0);
                        hit.albedo = Math::Vec3(c, c, c * 1.1f);
                        hit.metallic = 0.0f;
                    }
                }
                return hit;
            }

            Math::Vec3 skyCPU(const Math::Vec3 &dir)
            {
                const float t = Math::clampf(dir.y * 0.5f + 0.5f, 0.0f, 1.0f);
                return Math::Vec3(0.03f, 0.04f, 0.07f) * (1.0f - t) + Math::Vec3(0.25f, 0.45f, 0.85f) * t;
            }

            Math::Vec3 shadeDirectCPU(const std::vector<RTSphere> &spheres, const CpuHit &hit,
                                      const Math::Vec3 &p, const Math::Vec3 &dir)
            {
                const Math::Vec3 sun = normalize(Math::Vec3(0.45f, 0.8f, 0.35f));
                const float ndl = std::max(0.0f, dot(hit.normal, sun));
                float shadow = 1.0f;
                CpuHit shadow_hit = traceSceneCPU(spheres, p + hit.normal * 0.002f, sun);
                if (shadow_hit.t < 1e29f) shadow = 0.15f;
                Math::Vec3 color = hit.albedo * (0.18f + 0.82f * ndl * shadow);
                const Math::Vec3 reflected = dir - hit.normal * (2.0f * dot(dir, hit.normal));
                const float spec = std::pow(std::max(0.0f, dot(-sun, reflected)), 32.0f);
                color += Math::Vec3(0.35f, 0.45f, 0.6f) * spec * 0.25f;
                return color;
            }

            Math::Vec3 tracePixelCPU(const std::vector<RTSphere> &spheres, const Math::Vec3 &origin, const Math::Vec3 &dir)
            {
                CpuHit hit = traceSceneCPU(spheres, origin, dir);
                if (hit.t >= 1e29f) return skyCPU(dir);
                const Math::Vec3 p = origin + dir * hit.t;
                Math::Vec3 color = shadeDirectCPU(spheres, hit, p, dir);
                if (hit.metallic > 0.5f)
                {
                    const Math::Vec3 r = dir - hit.normal * (2.0f * dot(dir, hit.normal));
                    CpuHit reflected_hit = traceSceneCPU(spheres, p + hit.normal * 0.002f, r);
                    const Math::Vec3 reflected = reflected_hit.t >= 1e29f
                                                     ? skyCPU(r)
                                                     : shadeDirectCPU(spheres, reflected_hit,
                                                                      p + hit.normal * 0.002f + r * reflected_hit.t, r);
                    color = color * 0.35f + reflected * Math::Vec3(hit.albedo.x, hit.albedo.y, hit.albedo.z) * 0.65f * 1.4f;
                }
                return color;
            }

            uint8_t toByte(float v) { return (uint8_t)(Math::saturate(v) * 255.0f + 0.5f); }

            float hash12(float x, float y, uint32_t s)
            {
                float v = std::sin(x * 12.9898f + y * 78.233f + (float)s * 37.719f) * 43758.5453f;
                return v - std::floor(v);
            }
        } // namespace

        RayTracingAPI::~RayTracingAPI() { shutdown(); }

        bool RayTracingAPI::initialize(ComputeAPI *compute, RHI::RHIDevice *device)
        {
            compute_ = compute;
            device_ = device;
            sampler_ = nullptr;
            return device_ != nullptr;
        }

        void RayTracingAPI::releaseResources()
        {
            if (kernel_) kernel_.reset();
            if (compute_)
            {
                compute_->destroyBuffer(scene_buffer_);
                compute_->destroyBuffer(params_buffer_);
                compute_->destroyBuffer(output_buffer_);
            }
            scene_buffer_ = ComputeAPI::Buffer{};
            params_buffer_ = ComputeAPI::Buffer{};
            output_buffer_ = ComputeAPI::Buffer{};
            if (device_ && output_texture_) device_->destroyTexture(output_texture_);
            if (device_ && sampler_) device_->destroySampler(sampler_);
            output_texture_ = nullptr;
            sampler_ = nullptr;
            width_ = height_ = 0;
            pixels_.clear();
        }

        void RayTracingAPI::shutdown()
        {
            releaseResources();
            compute_ = nullptr;
            device_ = nullptr;
        }

        std::string RayTracingAPI::backendName() const
        {
            if (!device_) return "unavailable";
            if (hardware_rt_) return std::string(RHI::backendTypeName(device_->backend())) + " hardware ray tracing";
            if (compute_ && compute_->supportsHardwareCompute())
                return std::string(RHI::backendTypeName(device_->backend())) + " compute ray tracer";
            return "CPU ray tracer";
        }

        void RayTracingAPI::setScene(const std::vector<RTSphere> &spheres)
        {
            spheres_ = spheres;
            if (spheres_.size() > kMaxSpheres) spheres_.resize(kMaxSpheres);
        }

        bool RayTracingAPI::ensureResources(const RTSettings &settings)
        {
            const uint32_t width = std::max(16u, settings.width);
            const uint32_t height = std::max(16u, settings.height);
            if (!device_) return false;

            const bool can_gpu = compute_ && compute_->supportsHardwareCompute();
            bool output_recreated = false;
            if (width_ != width || height_ != height || !output_texture_)
            {
                if (compute_ && output_buffer_.valid()) compute_->destroyBuffer(output_buffer_);
                if (output_texture_) device_->destroyTexture(output_texture_);
                RHI::TextureDesc desc;
                desc.format = RHI::PixelFormat::RGBA8_UNORM;
                desc.width = width;
                desc.height = height;
                desc.usage = (uint32_t)RHI::TextureUsageFlagBits::Sampled |
                             (uint32_t)RHI::TextureUsageFlagBits::TransferDst;
                desc.name = "ray_tracing_output";
                output_texture_ = device_->createTexture(desc);
                if (can_gpu)
                    output_buffer_ = compute_->createBuffer((uint64_t)width * height * 4u,
                                                            (uint32_t)RHI::BufferUsageFlagBits::TransferDst,
                                                            RHI::MemoryUsage::GpuToCpu);
                width_ = width;
                height_ = height;
                pixels_.assign((size_t)width * height * 4u, 0);
                output_recreated = true;
            }
            if (!sampler_)
            {
                RHI::SamplerDesc desc;
                desc.min_filter = RHI::FilterMode::Linear;
                desc.mag_filter = RHI::FilterMode::Linear;
                desc.address_u = RHI::AddressMode::ClampToEdge;
                desc.address_v = RHI::AddressMode::ClampToEdge;
                desc.name = "ray_tracing_sampler";
                sampler_ = device_->createSampler(desc);
            }
            if (!can_gpu) return output_texture_ && sampler_;

            if (!scene_buffer_.valid() || scene_buffer_.size < sizeof(float) * 8u * kMaxSpheres)
                scene_buffer_ = compute_->createBuffer(sizeof(float) * 8u * kMaxSpheres,
                                                       (uint32_t)RHI::BufferUsageFlagBits::Uniform,
                                                       RHI::MemoryUsage::CpuToGpu);
            if (!params_buffer_.valid())
                params_buffer_ = compute_->createBuffer(sizeof(RTParams), (uint32_t)RHI::BufferUsageFlagBits::Uniform,
                                                        RHI::MemoryUsage::CpuToGpu);
            if (!scene_buffer_.valid() || !params_buffer_.valid() || !output_buffer_.valid()) return false;

            std::string error;
            if (!kernel_)
            {
                ComputeAPI::KernelDesc desc;
                desc.shader = "rt/raytrace.comp";
                desc.workgroup_x = 8;
                desc.workgroup_y = 8;
                desc.bindings = {{0, "SceneBuffer", true}, {1, "RtParams", true}, {2, "RtOutput", false}};
                kernel_ = compute_->createKernel(desc, &error);
                if (!kernel_)
                {
                    GEN_LOG_WARN("[RT] GPU compute path unavailable: %s", error.c_str());
                    return false;
                }
                output_recreated = true;
            }
            if (output_recreated)
            {
                std::vector<ComputeAPI::Buffer *> buffers = {&scene_buffer_, &params_buffer_, &output_buffer_};
                if (!compute_->bind(*kernel_, buffers, &error))
                {
                    GEN_LOG_WARN("[RT] cannot bind GPU buffers: %s", error.c_str());
                    kernel_.reset();
                    return false;
                }
            }
            return true;
        }

        bool RayTracingAPI::trace(const RTCamera &camera, const RTSettings &settings)
        {
            if (!device_) return false;
            const auto start = std::chrono::steady_clock::now();
            stats_.width = std::max(16u, settings.width);
            stats_.height = std::max(16u, settings.height);
            stats_.samples = std::max(1u, settings.samples);
            stats_.spheres = (uint32_t)spheres_.size();
            stats_.hardware_ray_tracing = hardware_rt_;

            // The CPU fallback still needs a presentation texture and sampler.
            // Allocate/resize these even on OpenGL 4.1 without compute support.
            const bool resources_ready = ensureResources(settings);
            if (!output_texture_ || !sampler_) return false;
            const bool gpu_ok = resources_ready && compute_ && compute_->supportsHardwareCompute() &&
                                traceGPU(camera, settings);
            if (!gpu_ok)
            {
                traceCPU(camera, settings);
                stats_.gpu = false;
            }
            else
            {
                stats_.gpu = true;
            }

            if (output_texture_ && !pixels_.empty())
            {
                device_->writeTexture(output_texture_, pixels_.data(), pixels_.size(), stats_.width, stats_.height);
            }
            stats_.trace_ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            return true;
        }

        bool RayTracingAPI::traceGPU(const RTCamera &camera, const RTSettings &settings)
        {
            if (!compute_ || !kernel_) return false;
            const uint32_t width = stats_.width, height = stats_.height;

            // Scene buffer: 2 vec4 per sphere.
            std::vector<float> scene(kMaxSpheres * 8u, 0.0f);
            for (size_t i = 0; i < spheres_.size(); ++i)
            {
                const RTSphere &s = spheres_[i];
                float *p = &scene[i * 8u];
                p[0] = s.center.x; p[1] = s.center.y; p[2] = s.center.z; p[3] = std::max(0.001f, s.radius);
                p[4] = s.color.r; p[5] = s.color.g; p[6] = s.color.b; p[7] = s.metallic;
            }
            compute_->write(scene_buffer_, scene.data(), scene.size() * sizeof(float));

            RTParams params;
            params.p0[0] = (float)width;
            params.p0[1] = (float)height;
            params.p0[2] = (float)std::max(1u, settings.samples);
            params.p0[3] = settings.time;
            params.p1[0] = camera.eye.x; params.p1[1] = camera.eye.y; params.p1[2] = camera.eye.z;
            params.p1[3] = std::tan(Math::toRadians(camera.fov_degrees) * 0.5f);
            params.p2[0] = camera.right.x; params.p2[1] = camera.right.y; params.p2[2] = camera.right.z;
            params.p2[3] = camera.aspect;
            params.p3[0] = camera.up.x; params.p3[1] = camera.up.y; params.p3[2] = camera.up.z; params.p3[3] = 0.0f;
            params.p4[0] = camera.forward.x; params.p4[1] = camera.forward.y; params.p4[2] = camera.forward.z;
            params.p4[3] = (float)spheres_.size();
            compute_->write(params_buffer_, &params, sizeof(params));

            const uint32_t gx = (width + 7u) / 8u;
            const uint32_t gy = (height + 7u) / 8u;
            if (!compute_->execute(*kernel_, gx, gy, 1, &output_buffer_, RHI::BufferAccess::HostRead)) return false;
            if (!compute_->read(output_buffer_, pixels_.data(), pixels_.size())) return false;
            return true;
        }

        void RayTracingAPI::traceCPU(const RTCamera &camera, const RTSettings &settings)
        {
            const uint32_t width = stats_.width, height = stats_.height;
            pixels_.assign((size_t)width * height * 4u, 0);
            const float tan_half = std::tan(Math::toRadians(camera.fov_degrees) * 0.5f);
            const uint32_t samples = std::max(1u, settings.samples);
            for (uint32_t y = 0; y < height; ++y)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    Math::Vec3 sum(0.0f);
                    for (uint32_t s = 0; s < samples; ++s)
                    {
                        const float jx = hash12((float)x, (float)y, s) - 0.5f;
                        const float jy = hash12((float)y, (float)x, s + 77u) - 0.5f;
                        const float nx = ((float)x + 0.5f + jx) / (float)width * 2.0f - 1.0f;
                        const float ny = 1.0f - ((float)y + 0.5f + jy) / (float)height * 2.0f;
                        const Math::Vec3 dir = normalize(camera.forward + camera.right * (nx * tan_half * camera.aspect) +
                                                         camera.up * (ny * tan_half));
                        sum += tracePixelCPU(spheres_, camera.eye, dir);
                    }
                    sum = sum / (float)samples;
                    const size_t index = ((size_t)y * width + x) * 4u;
                    pixels_[index + 0] = toByte(sum.x);
                    pixels_[index + 1] = toByte(sum.y);
                    pixels_[index + 2] = toByte(sum.z);
                    pixels_[index + 3] = 255;
                }
            }
        }

    } // namespace API
} // namespace Genesis
