#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct PushConstants
{
    float4x4 u_pc_mvp;
    float4 u_pc_color;
};

struct main0_out
{
    float4 v_color [[user(locn0)]];
    float4 gl_Position [[position]];
};

struct main0_in
{
    float3 a_position [[attribute(0)]];
    float4 a_color [[attribute(1)]];
};

vertex main0_out main0(main0_in in [[stage_in]], constant PushConstants& _36 [[buffer(24)]])
{
    main0_out out = {};
    float use_vertex = step(9.9999997473787516355514526367188e-05, ((in.a_color.x + in.a_color.y) + in.a_color.z) + in.a_color.w);
    out.v_color = mix(_36.u_pc_color, in.a_color, float4(use_vertex));
    out.gl_Position = _36.u_pc_mvp * float4(in.a_position, 1.0);
    out.gl_Position.y = -(out.gl_Position.y);    // Invert Y-axis for Metal
    return out;
}

