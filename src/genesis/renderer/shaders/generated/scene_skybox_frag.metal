#pragma clang diagnostic ignored "-Wmissing-prototypes"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct PerFrame
{
    float4x4 u_view;
    float4x4 u_projection;
    float4x4 u_view_projection;
    float4x4 u_inv_view_projection;
    float4x4 u_inv_view;
    float4 u_camera_position;
    float4 u_frame_params;
    float4 u_viewport_params;
    float4 u_backend_params;
};

struct Environment
{
    float4 u_env_params;
    float4 u_ibl_params;
    float4 u_sky_tint;
    float4 u_ground_tint;
};

struct GpuLight
{
    float4 position_range;
    float4 direction_cone;
    float4 color_intensity;
    float4 type_params;
};

struct Lights
{
    GpuLight u_lights[8];
    float4 u_light_count_params;
    float4 u_ambient_color;
};

struct Material
{
    float4 u_base_color_factor;
    float4 u_emissive_factor;
    float4 u_material_params0;
    float4 u_material_params1;
    float4 u_texture_mask;
    float4 u_material_extra;
    float4 u_uv_transform;
};

struct PerDraw
{
    float4x4 u_model;
    float4x4 u_normal_matrix;
    float4 u_object_params;
};

struct main0_out
{
    float4 out_color [[color(0)]];
    float gl_FragDepth [[depth(any)]];
};

struct main0_in
{
    float2 v_uv [[user(locn0)]];
};

static inline __attribute__((always_inline))
float2 gsDirectionToEquirectUV(thread const float3& dir)
{
    float3 d = fast::normalize(dir);
    float u = (precise::atan2(d.z, d.x) / 6.283185482025146484375) + 0.5;
    float v = acos(fast::clamp(d.y, -1.0, 1.0)) / 3.1415927410125732421875;
    return float2(u, v);
}

static inline __attribute__((always_inline))
float gsSaturate(thread const float& v)
{
    return fast::clamp(v, 0.0, 1.0);
}

fragment main0_out main0(main0_in in [[stage_in]], constant PerFrame& _75 [[buffer(0)]], constant Environment& _109 [[buffer(2)]], texture2d<float> u_sky_equirect [[texture(8)]], texturecube<float> u_sky_cube [[texture(9)]], sampler u_sky_equirectSmplr [[sampler(8)]], sampler u_sky_cubeSmplr [[sampler(9)]])
{
    main0_out out = {};
    float3 ndc = float3((in.v_uv.x * 2.0) - 1.0, (in.v_uv.y * 2.0) - 1.0, 1.0);
    float4 world = _75.u_inv_view_projection * float4(ndc, 1.0);
    float3 dir = fast::normalize((world.xyz / float3(fast::max(abs(world.w), 9.9999999747524270787835121154785e-07))) - _75.u_camera_position.xyz);
    float2 rot = _109.u_env_params.yz;
    float3 rotated = float3((rot.x * dir.x) + (rot.y * dir.z), dir.y, ((-rot.y) * dir.x) + (rot.x * dir.z));
    int mode = int(_109.u_ibl_params.z + 0.5);
    float3 color;
    if (mode == 1)
    {
        float3 param = rotated;
        float2 uv = gsDirectionToEquirectUV(param);
        color = u_sky_equirect.sample(u_sky_equirectSmplr, uv).xyz;
    }
    else
    {
        if (mode == 2)
        {
            color = u_sky_cube.sample(u_sky_cubeSmplr, rotated, level(0.0)).xyz;
        }
        else
        {
            float param_1 = (rotated.y * 0.5) + 0.5;
            float t = gsSaturate(param_1);
            color = mix(_109.u_ground_tint.xyz, _109.u_sky_tint.xyz, float3(t));
            float3 sun_dir = float3(0.51214754581451416015625, 0.76822125911712646484375, 0.384110629558563232421875);
            float param_2 = dot(rotated, sun_dir);
            float sun = powr(gsSaturate(param_2), 512.0);
            float param_3 = dot(rotated, sun_dir);
            float glow = powr(gsSaturate(param_3), 8.0);
            color += (((float3(1.0, 0.89999997615814208984375, 0.699999988079071044921875) * sun) * 60.0) + ((float3(0.4000000059604644775390625, 0.3499999940395355224609375, 0.300000011920928955078125) * glow) * 0.5));
        }
    }
    out.out_color = float4(fast::max(color * _109.u_env_params.x, float3(0.0)), 1.0);
    out.gl_FragDepth = 1.0;
    return out;
}

