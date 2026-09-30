#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct main0_out
{
    float2 v_uv [[user(locn0)]];
    float4 gl_Position [[position]];
};

struct main0_in
{
    float2 a_position [[attribute(0)]];
    float2 a_uv [[attribute(1)]];
};

vertex main0_out main0(main0_in in [[stage_in]])
{
    main0_out out = {};
    out.v_uv = in.a_uv;
    out.gl_Position = float4(in.a_position, 0.0, 1.0);
    out.gl_Position.y = -(out.gl_Position.y);    // Invert Y-axis for Metal
    return out;
}

