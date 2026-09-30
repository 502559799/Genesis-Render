#pragma clang diagnostic ignored "-Wmissing-prototypes"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

// Implementation of the GLSL mod() function, which is slightly different than Metal fmod()
template<typename Tx, typename Ty>
inline Tx mod(Tx x, Ty y)
{
    return x - y * floor(x / y);
}

struct PostParams
{
    float4 u_post_a;
    float4 u_post_b;
    float4 u_post_c;
    float4 u_post_d;
    float4 u_tint;
};

struct main0_out
{
    float4 out_color [[color(0)]];
};

struct main0_in
{
    float2 v_uv [[user(locn0)]];
};

static inline __attribute__((always_inline))
float3 gsSaturate(thread const float3& v)
{
    return fast::clamp(v, float3(0.0), float3(1.0));
}

static inline __attribute__((always_inline))
float3 acesFilm(thread const float3& x)
{
    float3 param = (x * ((x * 2.5099999904632568359375) + float3(0.02999999932944774627685546875))) / ((x * ((x * 2.4300000667572021484375) + float3(0.589999973773956298828125))) + float3(0.14000000059604644775390625));
    return gsSaturate(param);
}

static inline __attribute__((always_inline))
float3 toDisplayLinear(thread float3& color, constant PostParams& _81)
{
    if (_81.u_post_d.z < 0.5)
    {
        return color;
    }
    color *= fast::max(_81.u_post_a.x, 0.0);
    int mode = int(_81.u_post_a.z + 0.5);
    if (mode == 1)
    {
        color /= (color + float3(1.0));
    }
    else
    {
        if (mode == 2)
        {
            float3 param = color;
            color = acesFilm(param);
        }
        else
        {
            float3 param_1 = color;
            color = gsSaturate(param_1);
        }
    }
    return color;
}

static inline __attribute__((always_inline))
float gsSaturate(thread const float& v)
{
    return fast::clamp(v, 0.0, 1.0);
}

static inline __attribute__((always_inline))
float gsLuminance(thread const float3& c)
{
    return dot(c, float3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875));
}

static inline __attribute__((always_inline))
float3 fetchColor(thread const float2& uv, constant PostParams& _81, texture2d<float> u_scene_color, sampler u_scene_colorSmplr)
{
    float3 param = u_scene_color.sample(u_scene_colorSmplr, uv).xyz;
    float3 _143 = toDisplayLinear(param, _81);
    return _143;
}

fragment main0_out main0(main0_in in [[stage_in]], constant PostParams& _81 [[buffer(0)]], texture2d<float> u_scene_color [[texture(1)]], sampler u_scene_colorSmplr [[sampler(1)]], float4 gl_FragCoord [[position]])
{
    main0_out out = {};
    float2 uv = in.v_uv;
    if (_81.u_post_a.w > 0.5)
    {
        uv.y = 1.0 - uv.y;
    }
    int effect = int(_81.u_post_b.w + 0.5);
    float2 texel = _81.u_post_d.xy;
    float strength = _81.u_post_c.x;
    float4 source = u_scene_color.sample(u_scene_colorSmplr, uv);
    float3 param = source.xyz;
    float3 _183 = toDisplayLinear(param, _81);
    float3 color = _183;
    if (effect == 1)
    {
        float param_1 = strength;
        color = mix(color, float3(1.0) - color, float3(gsSaturate(param_1)));
    }
    else
    {
        if (effect == 2)
        {
            float3 param_2 = color;
            float param_3 = strength;
            color = mix(color, float3(gsLuminance(param_2)), float3(gsSaturate(param_3)));
        }
        else
        {
            if (effect == 3)
            {
                float3 sepia = float3(dot(color, float3(0.39300000667572021484375, 0.768999993801116943359375, 0.18899999558925628662109375)), dot(color, float3(0.3490000069141387939453125, 0.68599998950958251953125, 0.16799999773502349853515625)), dot(color, float3(0.272000014781951904296875, 0.533999979496002197265625, 0.13099999725818634033203125)));
                float3 param_4 = sepia;
                float param_5 = strength;
                color = mix(color, gsSaturate(param_4), float3(gsSaturate(param_5)));
            }
            else
            {
                if (effect == 4)
                {
                    float radius = fast::max(_81.u_post_c.y, 0.5);
                    float3 sum = float3(0.0);
                    float wsum = 0.0;
                    for (int y = -2; y <= 2; y++)
                    {
                        for (int x = -2; x <= 2; x++)
                        {
                            float w = exp(((-0.5) * float((x * x) + (y * y))) / 2.0);
                            float2 param_6 = uv + ((float2(float(x), float(y)) * texel) * radius);
                            sum += (fetchColor(param_6, _81, u_scene_color, u_scene_colorSmplr) * w);
                            wsum += w;
                        }
                    }
                    float param_7 = strength;
                    color = mix(color, sum / float3(wsum), float3(gsSaturate(param_7)));
                }
                else
                {
                    if (effect == 5)
                    {
                        float r = fast::max(_81.u_post_c.y, 1.0);
                        float2 param_8 = uv + ((float2(-1.0) * texel) * r);
                        float3 param_9 = fetchColor(param_8, _81, u_scene_color, u_scene_colorSmplr);
                        float tl = gsLuminance(param_9);
                        float2 param_10 = uv + ((float2(0.0, -1.0) * texel) * r);
                        float3 param_11 = fetchColor(param_10, _81, u_scene_color, u_scene_colorSmplr);
                        float tc = gsLuminance(param_11);
                        float2 param_12 = uv + ((float2(1.0, -1.0) * texel) * r);
                        float3 param_13 = fetchColor(param_12, _81, u_scene_color, u_scene_colorSmplr);
                        float tr = gsLuminance(param_13);
                        float2 param_14 = uv + ((float2(-1.0, 0.0) * texel) * r);
                        float3 param_15 = fetchColor(param_14, _81, u_scene_color, u_scene_colorSmplr);
                        float ml = gsLuminance(param_15);
                        float2 param_16 = uv + ((float2(1.0, 0.0) * texel) * r);
                        float3 param_17 = fetchColor(param_16, _81, u_scene_color, u_scene_colorSmplr);
                        float mr = gsLuminance(param_17);
                        float2 param_18 = uv + ((float2(-1.0, 1.0) * texel) * r);
                        float3 param_19 = fetchColor(param_18, _81, u_scene_color, u_scene_colorSmplr);
                        float bl = gsLuminance(param_19);
                        float2 param_20 = uv + ((float2(0.0, 1.0) * texel) * r);
                        float3 param_21 = fetchColor(param_20, _81, u_scene_color, u_scene_colorSmplr);
                        float bc = gsLuminance(param_21);
                        float2 param_22 = uv + ((float2(1.0) * texel) * r);
                        float3 param_23 = fetchColor(param_22, _81, u_scene_color, u_scene_colorSmplr);
                        float br = gsLuminance(param_23);
                        float gx = (((((-tl) - (2.0 * ml)) - bl) + tr) + (2.0 * mr)) + br;
                        float gy = (((((-tl) - (2.0 * tc)) - tr) + bl) + (2.0 * bc)) + br;
                        float param_24 = length(float2(gx, gy)) * fast::max(strength, 0.0);
                        float edge = gsSaturate(param_24);
                        color = mix(color * 0.25, _81.u_tint.xyz, float3(edge));
                    }
                    else
                    {
                        if (effect == 6)
                        {
                            float2 cell = texel * fast::max(_81.u_post_c.y, 1.0);
                            float2 snapped = (floor(uv / cell) + float2(0.5)) * cell;
                            float2 param_25 = snapped;
                            color = fetchColor(param_25, _81, u_scene_color, u_scene_colorSmplr);
                        }
                        else
                        {
                            if (effect == 7)
                            {
                                float2 d = in.v_uv - float2(0.5);
                                float param_26 = 1.0 - ((dot(d, d) * 2.5) * fast::max(strength, 0.0));
                                color *= gsSaturate(param_26);
                            }
                            else
                            {
                                if (effect == 8)
                                {
                                    float lines = fast::max(_81.u_post_c.y, 1.0);
                                    float row = floor(gl_FragCoord.y / lines);
                                    float _548;
                                    if (mod(row, 2.0) < 1.0)
                                    {
                                        float param_27 = strength;
                                        _548 = 1.0 - gsSaturate(param_27);
                                    }
                                    else
                                    {
                                        _548 = 1.0;
                                    }
                                    float darken = _548;
                                    color *= darken;
                                }
                                else
                                {
                                    if (effect == 9)
                                    {
                                        float2 dir = (((in.v_uv - float2(0.5)) * texel) * fast::max(_81.u_post_c.y, 1.0)) * 8.0;
                                        float2 param_28 = uv + dir;
                                        float2 param_29 = uv - dir;
                                        color = float3(fetchColor(param_28, _81, u_scene_color, u_scene_colorSmplr).x, color.y, fetchColor(param_29, _81, u_scene_color, u_scene_colorSmplr).z);
                                    }
                                    else
                                    {
                                        if (effect == 10)
                                        {
                                            float param_30 = strength;
                                            color = mix(color, color * _81.u_tint.xyz, float3(gsSaturate(param_30)));
                                        }
                                        else
                                        {
                                            if (effect == 11)
                                            {
                                                float levels = fast::max(_81.u_post_c.y, 2.0);
                                                color = floor((color * levels) + float3(0.5)) / float3(levels);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    float sat = fast::max(_81.u_post_b.y, 0.0);
    if (abs(sat - 1.0) > 9.9999997473787516355514526367188e-05)
    {
        float3 param_31 = color;
        color = mix(float3(gsLuminance(param_31)), color, float3(sat));
    }
    float vignette = fast::max(_81.u_post_b.x, 0.0);
    if (vignette > 9.9999997473787516355514526367188e-05)
    {
        float2 d_1 = in.v_uv - float2(0.5);
        float param_32 = 1.0 - ((dot(d_1, d_1) * vignette) * 2.0);
        color *= gsSaturate(param_32);
    }
    if (_81.u_post_d.z > 0.5)
    {
        float gamma = fast::max(_81.u_post_a.y, 0.100000001490116119384765625);
        color = powr(fast::max(color, float3(0.0)), float3(1.0 / gamma));
        float3 param_33 = color;
        out.out_color = float4(gsSaturate(param_33), 1.0);
    }
    else
    {
        out.out_color = float4(fast::max(color, float3(0.0)), source.w);
    }
    return out;
}

