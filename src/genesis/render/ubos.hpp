/*
 * Genesis Render - Engine
 * ubos.hpp : std140 uniform block layouts shared between C++ and the shaders.
 */
#pragma once
#include "math/math.hpp"
#include "rhi/rhi.hpp"

namespace Genesis
{
    // binding 0 (set 0) -- one per frame-in-flight
    struct alignas(16) CameraUBO
    {
        Mat4 view;            // 0
        Mat4 proj;            // 64
        Mat4 view_proj;       // 128
        Vec4 camera_pos;      // 192 (xyz)
    };
    static_assert(sizeof(CameraUBO) == 208, "CameraUBO layout");

    // std140 padded point light (matches PointLightGPU in lighting.glsl)
    struct alignas(16) PointLightGPU
    {
        Vec4 position_range; // xyz position, w range
        Vec4 color_intensity; // xyz color, w intensity
    };
    static_assert(sizeof(PointLightGPU) == 32, "PointLightGPU layout");

    static constexpr uint32_t kMaxPointLights = 4;

    // binding 1 (set 0)
    struct alignas(16) LightingUBO
    {
        Vec4 dir_direction{0, -1, 0, 0};      // xyz TO-light direction, w enabled
        Vec4 dir_color{1, 1, 1, 3};           // xyz color, w intensity
        Vec4 flags{1, 1, 0, 1};               // x direct on, y IBL on, z model(0 PBR/1 BP), w env intensity
        PointLightGPU points[kMaxPointLights]; // 48
        Vec4 point_count{0, 0, 0, 0};          // x count
        Vec4 ambient{0.03f, 0.03f, 0.035f, 0}; // xyz ambient color
    };
    static_assert(sizeof(LightingUBO) % 16 == 0, "LightingUBO 16-aligned");
}
