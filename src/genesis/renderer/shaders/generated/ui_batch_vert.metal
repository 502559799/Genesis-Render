#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct BatchParams
{
    float4x4 u_batch_view_proj;
    float4 u_batch_params;
};

struct main0_out
{
    float2 v_uv [[user(locn0)]];
    float4 v_color [[user(locn1)]];
    float4 v_params [[user(locn2)]];
    float4 v_outline [[user(locn3)]];
    float4 gl_Position [[position]];
};

struct main0_in
{
    float3 a_position [[attribute(0)]];
    float2 a_uv [[attribute(1)]];
    float4 a_color [[attribute(2)]];
    float4 a_params [[attribute(3)]];
    float4 a_outline [[attribute(4)]];
};

vertex main0_out main0(main0_in in [[stage_in]], constant BatchParams& _36 [[buffer(0)]])
{
    main0_out out = {};
    out.v_uv = in.a_uv;
    out.v_color = in.a_color;
    out.v_params = in.a_params;
    out.v_outline = in.a_outline;
    out.gl_Position = _36.u_batch_view_proj * float4(in.a_position, 1.0);
    out.gl_Position.y = -(out.gl_Position.y);    // Invert Y-axis for Metal
    return out;
}

