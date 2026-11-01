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
    float _114 = radicalInverseVdc(param);
    return float2(float(i) / float(n), _114);
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
float geometrySchlickGGX_IBL(thread const float& NoX, thread const float& roughness)
{
    float k = (roughness * roughness) * 0.5;
    return NoX / ((NoX * (1.0 - k)) + k);
}

static inline __attribute__((always_inline))
float geometrySmithIBL(thread const float& NoV, thread const float& NoL, thread const float& roughness)
{
    float param = NoV;
    float param_1 = roughness;
    float param_2 = NoL;
    float param_3 = roughness;
    return geometrySchlickGGX_IBL(param, param_1) * geometrySchlickGGX_IBL(param_2, param_3);
}

static inline __attribute__((always_inline))
float2 integrateBRDF(thread const float& NoV, thread const float& roughness, constant IBLParams& v_239)
{
    float3 V = float3(sqrt(1.0 - (NoV * NoV)), 0.0, NoV);
    float3 N = float3(0.0, 0.0, 1.0);
    uint sample_count = uint(fast::max(v_239.u_ibl_a.w, 256.0));
    float A = 0.0;
    float B = 0.0;
    for (uint i = 0u; i < sample_count; i++)
    {
        uint param = i;
        uint param_1 = sample_count;
        float2 xi = hammersley(param, param_1);
        float2 param_2 = xi;
        float3 param_3 = N;
        float param_4 = roughness;
        float3 H = importanceSampleGGX(param_2, param_3, param_4);
        float3 L = fast::normalize((H * (2.0 * dot(V, H))) - V);
        float NoL = fast::max(L.z, 0.0);
        float NoH = fast::max(H.z, 0.0);
        float VoH = fast::max(dot(V, H), 0.0);
        if (NoL > 0.0)
        {
            float param_5 = NoV;
            float param_6 = NoL;
            float param_7 = roughness;
            float G = geometrySmithIBL(param_5, param_6, param_7);
            float g_vis = (G * VoH) / fast::max(NoH * NoV, 9.9999997473787516355514526367188e-06);
            float fc = pow(1.0 - VoH, 5.0);
            A += ((1.0 - fc) * g_vis);
            B += (fc * g_vis);
        }
    }
    return float2(A, B) / float2(float(sample_count));
}

static inline __attribute__((always_inline))
float gsSaturate(thread const float& v)
{
    return fast::clamp(v, 0.0, 1.0);
}

fragment main0_out main0(main0_in in [[stage_in]], constant IBLParams& v_239 [[buffer(0)]])
{
    main0_out out = {};
    float NoV = fast::clamp(in.v_uv.x, 9.9999997473787516355514526367188e-05, 1.0);
    float roughness = fast::clamp(in.v_uv.y, 0.04500000178813934326171875, 1.0);
    float param = NoV;
    float param_1 = roughness;
    float2 brdf = integrateBRDF(param, param_1, v_239);
    float param_2 = brdf.x;
    float param_3 = brdf.y;
    out.out_color = float4(gsSaturate(param_2), gsSaturate(param_3), 0.0, 1.0);
    return out;
}

