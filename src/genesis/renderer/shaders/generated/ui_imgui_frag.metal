#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct main0_out
{
    float4 out_color [[color(0)]];
};

struct main0_in
{
    float2 v_uv [[user(locn0)]];
    float4 v_color [[user(locn1)]];
};

fragment main0_out main0(main0_in in [[stage_in]], texture2d<float> u_ui_texture [[texture(0)]], sampler u_ui_textureSmplr [[sampler(0)]])
{
    main0_out out = {};
    out.out_color = in.v_color * u_ui_texture.sample(u_ui_textureSmplr, in.v_uv);
    return out;
}

