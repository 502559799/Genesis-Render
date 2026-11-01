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

struct Hit
{
    float t;
    float3 normal;
    float3 albedo;
    float metallic;
};

struct RtParams
{
    float4 p0;
    float4 p1;
    float4 p2;
    float4 p3;
    float4 p4;
};

struct SceneBuffer
{
    float4 scene[1];
};

struct RtOutput
{
    uint pixels[1];
};

constant uint3 gl_WorkGroupSize [[maybe_unused]] = uint3(8u, 8u, 1u);

static inline __attribute__((always_inline))
float hash12(thread const float2& p)
{
    float3 p3 = fract(float3(p.xyx) * 0.103100001811981201171875);
    p3 += float3(dot(p3, p3.yzx + float3(33.3300018310546875)));
    return fract((p3.x + p3.y) * p3.z);
}

static inline __attribute__((always_inline))
bool intersectSphere(thread const float3& ro, thread const float3& rd, thread const float4& sphere, thread float& t)
{
    float3 oc = ro - sphere.xyz;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - (sphere.w * sphere.w);
    float disc = (b * b) - c;
    if (disc < 0.0)
    {
        return false;
    }
    float s = sqrt(disc);
    t = (-b) - s;
    if (t < 0.001000000047497451305389404296875)
    {
        t = (-b) + s;
    }
    return t > 0.001000000047497451305389404296875;
}

static inline __attribute__((always_inline))
Hit traceScene(thread const float3& ro, thread const float3& rd, thread const bool& primary, const device RtParams& rt_params, const device SceneBuffer& scene_data)
{
    Hit hit;
    hit.t = 1000000015047466219876688855040.0;
    hit.normal = float3(0.0, 1.0, 0.0);
    hit.albedo = float3(0.0);
    hit.metallic = 0.0;
    uint count = uint(((device float*)&rt_params.p4)[3u]);
    float param_3;
    for (uint i = 0u; i < count; i++)
    {
        float4 sphere = scene_data.scene[i * 2u];
        float3 param = ro;
        float3 param_1 = rd;
        float4 param_2 = sphere;
        bool _162 = intersectSphere(param, param_1, param_2, param_3);
        float t = param_3;
        bool _170;
        if (_162)
        {
            _170 = t < hit.t;
        }
        else
        {
            _170 = _162;
        }
        if (_170)
        {
            hit.t = t;
            hit.normal = fast::normalize((ro + (rd * t)) - sphere.xyz);
            float4 mat = scene_data.scene[(i * 2u) + 1u];
            hit.albedo = mat.xyz;
            hit.metallic = mat.w;
        }
    }
    if (abs(rd.y) > 9.9999997473787516355514526367188e-06)
    {
        float t_1 = (-ro.y) / rd.y;
        bool _215 = t_1 > 0.001000000047497451305389404296875;
        bool _222;
        if (_215)
        {
            _222 = t_1 < hit.t;
        }
        else
        {
            _222 = _215;
        }
        if (_222)
        {
            float3 p = ro + (rd * t_1);
            float checker = mod(floor(p.x) + floor(p.z), 2.0);
            hit.t = t_1;
            hit.normal = float3(0.0, 1.0, 0.0);
            hit.albedo = mix(float3(0.07999999821186065673828125, 0.0900000035762786865234375, 0.10999999940395355224609375), float3(0.2800000011920928955078125, 0.300000011920928955078125, 0.3400000035762786865234375), float3(checker));
            hit.metallic = 0.0;
        }
    }
    return hit;
}

static inline __attribute__((always_inline))
float3 sky(thread const float3& rd)
{
    float t = fast::clamp((rd.y * 0.5) + 0.5, 0.0, 1.0);
    return mix(float3(0.02999999932944774627685546875, 0.039999999105930328369140625, 0.070000000298023223876953125), float3(0.25, 0.449999988079071044921875, 0.85000002384185791015625), float3(t));
}

static inline __attribute__((always_inline))
float3 shadeDirect(thread const Hit& hit, thread const float3& p, thread const float3& rd, const device RtParams& rt_params, const device SceneBuffer& scene_data)
{
    float3 sun = float3(0.4580879509449005126953125, 0.814378559589385986328125, 0.3562906086444854736328125);
    float ndl = fast::max(dot(hit.normal, sun), 0.0);
    float shadow = 1.0;
    float3 param = p + (hit.normal * 0.00200000009499490261077880859375);
    float3 param_1 = sun;
    bool param_2 = false;
    Hit shadow_hit = traceScene(param, param_1, param_2, rt_params, scene_data);
    if (shadow_hit.t < 100000001504746621987668885504.0)
    {
        shadow = 0.1500000059604644775390625;
    }
    float3 color = hit.albedo * (0.180000007152557373046875 + ((0.819999992847442626953125 * ndl) * shadow));
    color += ((float3(0.3499999940395355224609375, 0.449999988079071044921875, 0.60000002384185791015625) * pow(fast::max(0.0, dot(reflect(-sun, hit.normal), -rd)), 32.0)) * 0.25);
    return color;
}

static inline __attribute__((always_inline))
float3 shade(thread const float3& ro, thread const float3& rd, thread const bool& allow_reflection, const device RtParams& rt_params, const device SceneBuffer& scene_data)
{
    float3 param = ro;
    float3 param_1 = rd;
    bool param_2 = true;
    Hit hit = traceScene(param, param_1, param_2, rt_params, scene_data);
    if (hit.t >= 100000001504746621987668885504.0)
    {
        float3 param_3 = rd;
        return sky(param_3);
    }
    float3 p = ro + (rd * hit.t);
    Hit param_4 = hit;
    float3 param_5 = p;
    float3 param_6 = rd;
    float3 color = shadeDirect(param_4, param_5, param_6, rt_params, scene_data);
    if ((hit.metallic > 0.5) && allow_reflection)
    {
        float3 r = reflect(rd, hit.normal);
        float3 param_7 = p + (hit.normal * 0.00200000009499490261077880859375);
        float3 param_8 = r;
        bool param_9 = false;
        Hit reflected_hit = traceScene(param_7, param_8, param_9, rt_params, scene_data);
        float3 _433;
        if (reflected_hit.t >= 100000001504746621987668885504.0)
        {
            float3 param_10 = r;
            _433 = sky(param_10);
        }
        else
        {
            Hit param_11 = reflected_hit;
            float3 param_12 = (p + (hit.normal * 0.00200000009499490261077880859375)) + (r * reflected_hit.t);
            float3 param_13 = r;
            _433 = shadeDirect(param_11, param_12, param_13, rt_params, scene_data);
        }
        float3 reflected = _433;
        color = mix(color, (reflected * hit.albedo) * 1.39999997615814208984375, float3(0.64999997615814208984375));
    }
    return color;
}

static inline __attribute__((always_inline))
uint packRGBA(thread const float3& color)
{
    uint3 c = uint3((fast::clamp(color, float3(0.0), float3(1.0)) * 255.0) + float3(0.5));
    return ((c.x | (c.y << uint(8))) | (c.z << uint(16))) | 4278190080u;
}

kernel void main0(const device SceneBuffer& scene_data [[buffer(0)]], const device RtParams& rt_params [[buffer(1)]], device RtOutput& output_image [[buffer(2)]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    uint width = uint(((device float*)&rt_params.p0)[0u]);
    uint height = uint(((device float*)&rt_params.p0)[1u]);
    uint samples = max(1u, uint(((device float*)&rt_params.p0)[2u]));
    uint x = gl_GlobalInvocationID.x;
    uint y = gl_GlobalInvocationID.y;
    if ((x >= width) || (y >= height))
    {
        return;
    }
    float3 origin = rt_params.p1.xyz;
    float tan_half_fov = ((device float*)&rt_params.p1)[3u];
    float3 right = rt_params.p2.xyz;
    float aspect = ((device float*)&rt_params.p2)[3u];
    float3 up = rt_params.p3.xyz;
    float3 forward = rt_params.p4.xyz;
    float3 sum = float3(0.0);
    for (uint s = 0u; s < samples; s++)
    {
        float2 param = float2(float(x), float(y)) + float2(float(s) * 17.0);
        float2 param_1 = float2(float(y), float(x)) + float2(float(s) * 29.0);
        float2 jitter = float2(hash12(param), hash12(param_1)) - float2(0.5);
        float2 uv = ((((float2(float(x), float(y)) + float2(0.5)) + jitter) / float2(float(width), float(height))) * 2.0) - float2(1.0);
        uv.y = -uv.y;
        float3 dir = fast::normalize((forward + (right * ((uv.x * tan_half_fov) * aspect))) + (up * (uv.y * tan_half_fov)));
        float3 param_2 = origin;
        float3 param_3 = dir;
        bool param_4 = true;
        sum += shade(param_2, param_3, param_4, rt_params, scene_data);
    }
    float3 param_5 = sum / float3(float(samples));
    output_image.pixels[(y * width) + x] = packRGBA(param_5);
}

