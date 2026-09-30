/*
 * Genesis Render - GPU uniform block layouts (C++ mirror of the GLSL)
 *
 * !!! Must match src/genesis/renderer/shaders/common/ubo_layout.h (and the
 *     blocks declared inline in the post / batch / ui shaders) exactly !!!
 * Only mat4 / vec4 members are used, so std140 layout is trivial: every
 * member is 16-byte aligned and the struct sizes below are multiples of 16.
 * static_asserts guard against accidental drift.
 */
#ifndef __GENESIS_RENDER_UBO_HPP__
#define __GENESIS_RENDER_UBO_HPP__

#include "foundation_layer/math/math.hpp"

#include <cstdint>
#include <string>

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
        // set 0 / binding 0 (dynamic, one slice per rendered view)
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
            GpuVec4 backend_params;  // unused, depth zero-to-one, unused, unused
        };
        static_assert(sizeof(PerFrameUBO) == 384, "PerFrameUBO must match the GLSL layout");

        //==================================================================
        // set 0 / binding 1 (dynamic, one slice per frame)
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
        // set 0 / binding 2 (dynamic, one slice per frame)
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
        // set 1 / binding 0 (dynamic, one slice per material per frame)
        //==================================================================
        struct MaterialUBO
        {
            GpuVec4 base_color_factor; // linear rgba
            GpuVec4 emissive_factor;   // linear rgb, a = strength
            GpuVec4 params0;           // metallic, roughness, normal scale, occlusion strength
            GpuVec4 params1;           // alpha cutoff, alpha mode, double sided, shading model
            GpuVec4 texture_mask;      // base colour, metallic-roughness, normal, occlusion (0/1)
            GpuVec4 extra;             // emissive texture (0/1), unused x3
            GpuVec4 uv_transform;      // uv scale xy, uv offset zw
        };
        static_assert(sizeof(MaterialUBO) == 112, "MaterialUBO must match the GLSL layout");

        //==================================================================
        // set 2 / binding 0 (dynamic offset per draw)
        //==================================================================
        struct PerDrawUBO
        {
            GpuMat4 model;
            GpuMat4 normal_matrix;
            GpuVec4 object_params; // pick id, wireframe, unused, unused
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
        // Fullscreen post / blit pass (post/effects.frag, set 0 / binding 0)
        //==================================================================
        struct PostParamsUBO
        {
            GpuVec4 a;    // exposure, gamma, tonemap mode, flip uv.y
            GpuVec4 b;    // vignette, saturation, time, effect id
            GpuVec4 c;    // effect parameters (strength, radius / size, extra, extra)
            GpuVec4 d;    // source texel size xy, apply tone mapping (0/1), unused
            GpuVec4 tint; // colour parameter of the effect
        };
        static_assert(sizeof(PostParamsUBO) == 80, "PostParamsUBO must match the GLSL layout");

        //==================================================================
        // Batched 2D / 3D primitives and SDF text (ui/batch.*, set 0 / binding 0)
        //==================================================================
        struct BatchParamsUBO
        {
            GpuMat4 view_proj; // world (3D) or target pixel space (2D) -> clip
            GpuVec4 params;    // unused
        };
        static_assert(sizeof(BatchParamsUBO) == 80, "BatchParamsUBO must match the GLSL layout");

        //==================================================================
        // Shading / tone mapping / sky enums (mirror GS_* in the shaders)
        //==================================================================
        enum class ShadingMode : uint32_t
        {
            CookTorrance = 0,
            BlinnPhong = 1,
            Unlit = 2,
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

        // Fullscreen effects available to the built-in post stack and to
        // CommandBuffer::blit (mirrors the switch in post/effects.frag).
        enum class PostEffect : uint32_t
        {
            None = 0,
            Invert = 1,
            Grayscale = 2,
            Sepia = 3,
            Blur = 4,
            Edge = 5,
            Pixelate = 6,
            Vignette = 7,
            Scanline = 8,
            Chromatic = 9,
            Tint = 10,
            Posterize = 11,
            Count
        };
        const char *postEffectName(PostEffect effect);
        bool postEffectFromName(const std::string &name, PostEffect &out);

    } // namespace Render
} // namespace Genesis

#endif /* __GENESIS_RENDER_UBO_HPP__ */
