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

fragment main0_out main0(main0_in in [[stage_in]], constant IBLParams& _109 [[buffer(0)]], texturecube<float> u_src_cube [[texture(2)]], sampler u_src_cubeSmplr [[sampler(2)]])
{
    main0_out out = {};
    int face = int(_109.u_ibl_a.x + 0.5);
    int param = face;
    float2 param_1 = in.v_uv;
    float3 N = fast::normalize(gsCubeFaceDirection(param, param_1));
    float3 up = select(float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), bool3(abs(N.z) < 0.999000012874603271484375));
    float3 right = fast::normalize(cross(up, N));
    float3 tangent_up = cross(N, right);
    float3 irradiance = float3(0.0);
    float sample_count = 0.0;
    for (float phi = 0.0; phi < 6.283185482025146484375; phi += 0.052359879016876220703125)
    {
        for (float theta = 0.0; theta < 1.57079637050628662109375; theta += 0.0261799395084381103515625)
        {
            float3 tangent_sample = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            float3 sample_vec = ((right * tangent_sample.x) + (tangent_up * tangent_sample.y)) + (N * tangent_sample.z);
            irradiance += ((u_src_cube.sample(u_src_cubeSmplr, sample_vec).xyz * cos(theta)) * sin(theta));
            sample_count += 1.0;
        }
    }
    irradiance = (irradiance * 3.1415927410125732421875) / float3(fast::max(sample_count, 1.0));
    out.out_color = float4(fast::max(irradiance, float3(0.0)), 1.0);
    return out;
}

