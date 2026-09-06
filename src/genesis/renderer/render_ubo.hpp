/*
 * Genesis Render - GPU uniform block layouts (C++ mirror of the GLSL)
 *
 * !!! Must match src/genesis/renderer/shaders/common/ubo_layout.h exactly !!!
 * Only mat4 / vec4 / float[4] members are used, so std140 layout is trivial:
 * every member is 16-byte aligned and the struct sizes below are multiples of
 * 16.  static_asserts guard against accidental drift.
 */
#ifndef __GENESIS_RENDER_UBO_HPP__
#define __GENESIS_RENDER_UBO_HPP__

#include "foundation_layer/math/math.hpp"

#include <cstdint>

namespace Genesis
{
    namespace Render
    {
        static constexpr uint32_t kMaxLights = 8; // == GS_MAX_LIGHTS

        // Matches GLSL mat4 / vec4 in std140.
        struct alignas(16) GpuMat4
        {
            float m[16];
            static GpuMat4 from(const Math::Mat4 &src)
            {
                GpuMat4 out;
                for (int i = 0; i < 16; ++i) out.m[i] = src.m[i];
                return out;
            }
            static GpuMat4 identity() { return from(Math::Mat4::identity()); }
        };

        struct alignas(16) GpuVec4
        {
            float x = 0, y = 0, z = 0, w = 0;
            GpuVec4() {}
            GpuVec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
            static GpuVec4 from(const Math::Vec3 &v, float w_) { return GpuVec4(v.x, v.y, v.z, w_); }
            static GpuVec4 from(const Math::Vec4 &v) { return GpuVec4(v.x, v.y, v.z, v.w); }
            static GpuVec4 from(const Math::Color &c) { return GpuVec4(c.r, c.g, c.b, c.a); }
        };

        //==================================================================
        // set 0 / binding 0
        //==================================================================
        struct PerFrameUBO
        {
            GpuMat4 view;
            GpuMat4 projection;
            GpuMat4 view_projection;
            GpuMat4 inv_view_projection;
            GpuMat4 inv_view;
            GpuVec4 camera_position; // xyz eye, w = vertical fov (radians)
            GpuVec4 frame_params;    // time, delta, frame index, exposure
            GpuVec4 viewport_params; // width, height, 1/w, 1/h
            GpuVec4 backend_params;  // flip uv.y (GL offscreen), depth zero-to-one, flip clip y, unused
        };
        static_assert(sizeof(PerFrameUBO) == 384, "PerFrameUBO must match the GLSL layout");

        //==================================================================
        // set 0 / binding 1
        //==================================================================
        struct GpuLight
        {
            GpuVec4 position_range;  // xyz position, w range
            GpuVec4 direction_cone;  // xyz direction, w unused
            GpuVec4 color_intensity; // rgb linear, w intensity
            GpuVec4 type_params;     // type, cos(inner), cos(outer), enabled
        };
        static_assert(sizeof(GpuLight) == 64, "GpuLight must match the GLSL layout");

        struct LightsUBO
        {
            GpuLight lights[kMaxLights];
            GpuVec4 light_count_params; // x = count
            GpuVec4 ambient_color;      // rgb, w = intensity
        };
        static_assert(sizeof(LightsUBO) == 544, "LightsUBO must match the GLSL layout");

        //==================================================================
        // set 0 / binding 2
        //==================================================================
        struct EnvironmentUBO
        {
            GpuVec4 env_params;   // intensity, cos(rotation), sin(rotation), show skybox
            GpuVec4 ibl_params;   // use IBL, prefilter mip count, sky mode, unused
            GpuVec4 sky_tint;
            GpuVec4 ground_tint;
        };
        static_assert(sizeof(EnvironmentUBO) == 64, "EnvironmentUBO must match the GLSL layout");

        //==================================================================
        // set 1 / binding 0
        //==================================================================
        struct MaterialUBO
        {
            GpuVec4 base_color_factor; // linear rgba
            GpuVec4 emissive_factor;   // linear rgb, a = strength
            GpuVec4 params0;           // metallic, roughness, normal scale, occlusion strength
            GpuVec4 params1;           // alpha cutoff, alpha mode, double sided, shading mode
            GpuVec4 texture_mask;      // base colour, metallic-roughness, normal, occlusion (0/1)
            GpuVec4 extra;             // emissive texture (0/1), uv scale x, uv scale y, unused
        };
        static_assert(sizeof(MaterialUBO) == 96, "MaterialUBO must match the GLSL layout");

        //==================================================================
        // set 2 / binding 0 (dynamic offset per draw)
        //==================================================================
        struct PerDrawUBO
        {
            GpuMat4 model;
            GpuMat4 normal_matrix;
            GpuVec4 object_params; // pick id, wireframe, uv offset x, uv offset y
        };
        static_assert(sizeof(PerDrawUBO) == 144, "PerDrawUBO must match the GLSL layout");

        //==================================================================
        // IBL bake passes (set 0 / binding 0 of the bake pipelines)
        //==================================================================
        struct IblParamsUBO
        {
            GpuVec4 a; // face index, roughness, mip level, sample count
            GpuVec4 b; // cos(env rotation), sin(env rotation), unused, unused
        };
        static_assert(sizeof(IblParamsUBO) == 32, "IblParamsUBO must match the GLSL layout");

        //==================================================================
        // Post processing (set 0 / binding 0 of the tonemap pipeline)
        //==================================================================
        struct PostParamsUBO
        {
            GpuVec4 a; // exposure, gamma, tonemap mode, flip uv.y
            GpuVec4 b; // vignette, saturation, time, unused
        };
        static_assert(sizeof(PostParamsUBO) == 32, "PostParamsUBO must match the GLSL layout");

        //==================================================================
        // Shading modes (mirrors GS_SHADING_* in shaders/common/pbr.h)
        //==================================================================
        enum class ShadingMode : uint32_t
        {
            CookTorrance = 0,
            BlinnPhong = 1,
        };

        enum class SkyMode : uint32_t
        {
            Procedural = 0,
            Equirect = 1,
            Cubemap = 2,
        };

        enum class TonemapMode : uint32_t
        {
            None = 0,
            Reinhard = 1,
            Aces = 2,
        };

    } // namespace Render
} // namespace Genesis

#endif /* __GENESIS_RENDER_UBO_HPP__ */
