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
float radicalInverseVdc(thread uint& bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 1431655765u) << 1u) | ((bits & 2863311530u) >> 1u);
    bits = ((bits & 858993459u) << 2u) | ((bits & 3435973836u) >> 2u);
    bits = ((bits & 252645135u) << 4u) | ((bits & 4042322160u) >> 4u);
    bits = ((bits & 16711935u) << 8u) | ((bits & 4278255360u) >> 8u);
    return float(bits) * 2.3283064365386962890625e-10;
}

static inline __attribute__((always_inline))
float2 hammersley(thread const uint& i, thread const uint& n)
{
    uint param = i;
    float _213 = radicalInverseVdc(param);
    return float2(float(i) / float(n), _213);
}

static inline __attribute__((always_inline))
float3 importanceSampleGGX(thread const float2& xi, thread const float3& N, thread const float& roughness)
{
    float a = roughness * roughness;
    float phi = 6.283185482025146484375 * xi.x;
    float cos_theta = sqrt((1.0 - xi.y) / (1.0 + (((a * a) - 1.0) * xi.y)));
    float sin_theta = sqrt(1.0 - (cos_theta * cos_theta));
    float3 H = float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
    float3 up = select(float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 1.0), bool3(abs(N.z) < 0.999000012874603271484375));
    float3 tangent = fast::normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    return fast::normalize(((tangent * H.x) + (bitangent * H.y)) + (N * H.z));
}

static inline __attribute__((always_inline))
float gsDistributionGGX(thread const float& NoH, thread const float& roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (((NoH * a2) - NoH) * NoH) + 1.0;
    return a2 / fast::max((3.1415927410125732421875 * d) * d, 1.0000000116860974230803549289703e-07);
}

fragment main0_out main0(main0_in in [[stage_in]], constant IBLParams& _299 [[buffer(0)]], texturecube<float> u_src_cube [[texture(2)]], sampler u_src_cubeSmplr [[sampler(2)]])
{
    main0_out out = {};
    int face = int(_299.u_ibl_a.x + 0.5);
    float roughness = fast::max(_299.u_ibl_a.y, 0.0);
    uint sample_count = uint(fast::max(_299.u_ibl_a.w, 16.0));
    int param = face;
    float2 param_1 = in.v_uv;
    float3 N = fast::normalize(gsCubeFaceDirection(param, param_1));
    float3 R = N;
    float3 V = R;
    float total_weight = 0.0;
    float3 prefiltered = float3(0.0);
    float mip_bias = 0.0;
    float _417;
    for (uint i = 0u; i < sample_count; i++)
    {
        uint param_2 = i;
        uint param_3 = sample_count;
        float2 xi = hammersley(param_2, param_3);
        float2 param_4 = xi;
        float3 param_5 = N;
        float param_6 = roughness;
        float3 H = importanceSampleGGX(param_4, param_5, param_6);
        float3 L = fast::normalize((H * (2.0 * dot(V, H))) - V);
        float NoL = fast::max(dot(N, L), 0.0);
        if (NoL > 0.0)
        {
            float NoH = fast::max(dot(N, H), 0.0);
            float VoH = fast::max(dot(V, H), 0.0);
            float param_7 = NoH;
            float param_8 = fast::max(roughness, 0.04500000178813934326171875);
            float D = gsDistributionGGX(param_7, param_8);
            float pdf = ((D * NoH) / fast::max(4.0 * VoH, 9.9999997473787516355514526367188e-06)) + 9.9999997473787516355514526367188e-05;
            float sa_texel = 3.1957933970261365175247192382812e-05;
            float sa_sample = 1.0 / ((float(sample_count) * pdf) + 9.9999997473787516355514526367188e-06);
            if (roughness == 0.0)
            {
                _417 = 0.0;
            }
            else
            {
                _417 = 0.5 * log2(sa_sample / sa_texel);
            }
            mip_bias = _417;
            prefiltered += (u_src_cube.sample(u_src_cubeSmplr, L, level(mip_bias)).xyz * NoL);
            total_weight += NoL;
        }
    }
    float3 _447;
    if (total_weight > 0.0)
    {
        _447 = prefiltered / float3(total_weight);
    }
    else
    {
        _447 = float3(0.0);
    }
    prefiltered = _447;
    out.out_color = float4(fast::max(prefiltered, float3(0.0)), 1.0);
    return out;
}

