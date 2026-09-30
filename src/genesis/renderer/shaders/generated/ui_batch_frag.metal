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
    float4 v_params [[user(locn2)]];
    float4 v_outline [[user(locn3)]];
};

fragment main0_out main0(main0_in in [[stage_in]], texture2d<float> u_batch_texture [[texture(1)]], sampler u_batch_textureSmplr [[sampler(1)]])
{
    main0_out out = {};
    float4 tex = u_batch_texture.sample(u_batch_textureSmplr, in.v_uv);
    float d = tex.x;
    float aa = fast::max(fwidth(d) * 0.75, 9.9999997473787516355514526367188e-05);
    if (in.v_params.x < 0.5)
    {
        out.out_color = tex * in.v_color;
        return out;
    }
    float fill = smoothstep(0.5 - aa, 0.5 + aa, d);
    float outline_width = in.v_params.y;
    if (outline_width > 0.0)
    {
        float edge = 0.5 - outline_width;
        float outer = smoothstep(edge - aa, edge + aa, d);
        float3 rgb = mix(in.v_outline.xyz, in.v_color.xyz, float3(fill));
        float a = mix(in.v_outline.w * outer, in.v_color.w, fill);
        if (a < 0.0030000000260770320892333984375)
        {
            discard_fragment();
        }
        out.out_color = float4(rgb, a);
    }
    else
    {
        if (fill < 0.0030000000260770320892333984375)
        {
            discard_fragment();
        }
        out.out_color = float4(in.v_color.xyz, in.v_color.w * fill);
    }
    return out;
}

