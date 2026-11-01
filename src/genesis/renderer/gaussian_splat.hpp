/*
 * Genesis Render - 3D Gaussian splat visualisation
 *
 * This is a pragmatic, RHI-portable splat renderer built on the engine's
 * existing 2D batch path:
 *
 *   - PLY files (binary little endian or ASCII) with the classic 3D Gaussian
 *     Splatting attributes are parsed on the CPU:
 *       x y z | f_dc_0..2 | opacity | scale_0..2 | rot_0..3
 *     Plain coloured point clouds (x y z r g b [a]) are accepted as well.
 *   - Every frame the splats are transformed to view space, sorted back to
 *     front and projected to screen space.  Each one becomes an alpha blended
 *     quad using a procedurally generated gaussian falloff texture.
 *
 * The renderer deliberately avoids compute shaders / storage buffers so it
 * works unchanged on all three backends.  Anisotropic covariance projection
 * and spherical harmonics view dependence are future work; see
 * docs/GAUSSIAN_SPLATTING.md.
 */
#ifndef __GENESIS_GAUSSIAN_SPLAT_HPP__
#define __GENESIS_GAUSSIAN_SPLAT_HPP__

#include "foundation_layer/math/math.hpp"
#include "renderer/material.hpp"

#include <string>
#include <vector>

namespace Genesis
{
    namespace RHI
    {
        class RHITexture;
        class RHISampler;
    }

    namespace Render
    {
        class RenderSystem;
        class CommandBuffer;
    }

    namespace Gaussian
    {
        struct Splat
        {
            Math::Vec3 position = Math::Vec3(0.0f);
            Math::Vec3 scale = Math::Vec3(0.05f);
            Math::Quat rotation = Math::Quat::identity();
            Math::Color color = Math::Color(1.0f, 1.0f, 1.0f, 1.0f);
            float opacity = 1.0f;
        };

        struct LoadStats
        {
            uint64_t vertices = 0;
            uint64_t skipped = 0;
            bool has_sh = false;
            bool binary = false;
            std::string format;
        };

        class GaussianSplatCloud
        {
        public:
            bool loadPly(const std::string &path, std::string *error = nullptr);
            void clear();

            bool loaded() const { return loaded_; }
            size_t count() const { return splats_.size(); }
            const std::vector<Splat> &splats() const { return splats_; }
            const Math::AABB &bounds() const { return bounds_; }
            const LoadStats &stats() const { return stats_; }
            const std::string &sourcePath() const { return source_path_; }

        private:
            bool loaded_ = false;
            std::string source_path_;
            std::vector<Splat> splats_;
            Math::AABB bounds_;
            LoadStats stats_;
        };

        struct SplatSettings
        {
            float size_scale = 0.6f;    // world units multiplier
            float opacity_scale = 0.35f;
            float max_screen_radius = 96.0f;
            bool sort = true;
            bool colored = true;
            uint32_t max_splats = 0;    // 0 = all
            Math::Color tint = Math::Color(1.0f, 1.0f, 1.0f, 1.0f);
        };

        struct SplatRenderStats
        {
            size_t total = 0;
            size_t visible = 0;
            size_t drawn = 0;
            double sort_ms = 0.0;
            double project_ms = 0.0;
        };

        class GaussianSplatRenderer
        {
        public:
            // Records the splats into `cb` using screen-space pixel coordinates
            // of the render system's current camera.  Use an Overlay /
            // AfterTransparent command buffer.
            void record(Render::RenderSystem &render, Render::CommandBuffer &cb, const GaussianSplatCloud &cloud,
                        const SplatSettings &settings);
            const SplatRenderStats &stats() const { return stats_; }
            RHI::RHITexture *falloffTexture(Render::RenderSystem &render);
            RHI::RHISampler *falloffSampler(Render::RenderSystem &render);

        private:
            SplatRenderStats stats_;
            Render::GpuTexture falloff_;
            Render::RenderSystem *owner_ = nullptr;
        };

    } // namespace Gaussian
} // namespace Genesis

#endif /* __GENESIS_GAUSSIAN_SPLAT_HPP__ */
