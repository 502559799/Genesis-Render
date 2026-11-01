#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct PerDraw
{
    float4x4 u_model;
    float4x4 u_normal_matrix;
    float4 u_object_params;
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

struct Environment
{
    float4 u_env_params;
    float4 u_ibl_params;
    float4 u_sky_tint;
    float4 u_ground_tint;
};

struct main0_out
{
    float3 v_world_position [[user(locn0)]];
    float3 v_normal [[user(locn1)]];
    float4 v_tangent [[user(locn2)]];
    float2 v_uv0 [[user(locn3)]];
    float2 v_uv1 [[user(locn4)]];
    float4 gl_Position [[position]];
};

struct main0_in
{
    float3 a_position [[attribute(0)]];
    float3 a_normal [[attribute(1)]];
    float4 a_tangent [[attribute(2)]];
    float2 a_uv0 [[attribute(3)]];
    float2 a_uv1 [[attribute(4)]];
};

vertex main0_out main0(main0_in in [[stage_in]], constant PerFrame& _110 [[buffer(0)]], constant Material& _89 [[buffer(8)]], constant PerDraw& _13 [[buffer(16)]])
{
    main0_out out = {};
    float4 world_position = _13.u_model * float4(in.a_position, 1.0);
    out.v_world_position = world_position.xyz;
    float3x3 normal_matrix = float3x3(_13.u_normal_matrix[0].xyz, _13.u_normal_matrix[1].xyz, _13.u_normal_matrix[2].xyz);
    out.v_normal = fast::normalize(normal_matrix * in.a_normal);
    float3 tangent = fast::normalize(normal_matrix * in.a_tangent.xyz);
    tangent = fast::normalize(tangent - (out.v_normal * dot(out.v_normal, tangent)));
    out.v_tangent = float4(tangent, in.a_tangent.w);
    out.v_uv0 = (in.a_uv0 * _89.u_uv_transform.xy) + _89.u_uv_transform.zw;
    out.v_uv1 = in.a_uv1;
    out.gl_Position = _110.u_view_projection * world_position;
    out.gl_Position.y = -(out.gl_Position.y);    // Invert Y-axis for Metal
    return out;
}

