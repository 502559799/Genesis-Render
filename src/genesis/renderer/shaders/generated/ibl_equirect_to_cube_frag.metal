#pragma clang diagnostic ignored "-Wmissing-prototypes"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct IBLParams
{
    float4 u_ibl_a;
    float4 u_ibl_b;
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
float3 gsCubeFaceDirection(thread const int& face, thread const float2& uv)
{
    float2 t = (uv * 2.0) - float2(1.0);
    t.y = -t.y;
    if (face == 0)
    {
        return fast::normalize(float3(1.0, t.y, -t.x));
    }
    if (face == 1)
    {
        return fast::normalize(float3(-1.0, t.y, t.x));
    }
    if (face == 2)
    {
        return fast::normalize(float3(t.x, 1.0, t.y));
    }
    if (face == 3)
    {
        return fast::normalize(float3(t.x, -1.0, -t.y));
    }
    if (face == 4)
    {
        return fast::normalize(float3(t.x, t.y, 1.0));
    }
    return fast::normalize(float3(-t.x, t.y, -1.0));
}

static inline __attribute__((always_inline))
float2 gsDirectionToEquirectUV(thread const float3& dir)
{
    float3 d = fast::normalize(dir);
    float u = (precise::atan2(d.z, d.x) / 6.283185482025146484375) + 0.5;
    float v = acos(fast::clamp(d.y, -1.0, 1.0)) / 3.1415927410125732421875;
    return float2(u, v);
}

fragment main0_out main0(main0_in in [[stage_in]], constant IBLParams& _140 [[buffer(0)]], texture2d<float> u_src_equirect [[texture(1)]], sampler u_src_equirectSmplr [[sampler(1)]])
{
    main0_out out = {};
    int face = int(_140.u_ibl_a.x + 0.5);
    int param = face;
    float2 param_1 = in.v_uv;
    float3 dir = gsCubeFaceDirection(param, param_1);
    float2 rot = _140.u_ibl_b.xy;
    float3 rotated = float3((rot.x * dir.x) + (rot.y * dir.z), dir.y, ((-rot.y) * dir.x) + (rot.x * dir.z));
    float3 param_2 = rotated;
    float2 uv = gsDirectionToEquirectUV(param_2);
    float3 color = u_src_equirect.sample(u_src_equirectSmplr, uv).xyz;
    out.out_color = float4(fast::max(color, float3(0.0)), 1.0);
    return out;
}

