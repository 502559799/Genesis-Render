#pragma clang diagnostic ignored "-Wmissing-prototypes"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct PbrSurface
{
    float3 N;
    float3 V;
    float3 albedo;
    float metallic;
    float roughness;
    float ao;
    float3 F0;
    float3 emissive;
    int shading_mode;
};

struct Environment
{
    float4 u_env_params;
    float4 u_ibl_params;
    float4 u_sky_tint;
    float4 u_ground_tint;
};

struct Material
{
    float4 u_base_color_factor;
    float4 u_emissive_factor;
    float4 u_material_params0;
    float4 u_material_params1;
    float4 u_texture_mask;
    float4 u_material_extra;
    float4 u_uv_transform;
};

struct PerFrame
{
    float4x4 u_view;
    float4x4 u_projection;
    float4x4 u_view_projection;
    float4x4 u_inv_view_projection;
    float4x4 u_inv_view;
    float4 u_camera_position;
    float4 u_frame_params;
    float4 u_viewport_params;
    float4 u_backend_params;
};

struct GpuLight
{
    float4 position_range;
    float4 direction_cone;
    float4 color_intensity;
    float4 type_params;
};

struct Lights
{
    GpuLight u_lights[8];
    float4 u_light_count_params;
    float4 u_ambient_color;
};

struct GpuLight_1
{
    float4 position_range;
    float4 direction_cone;
    float4 color_intensity;
    float4 type_params;
};

struct PerDraw
{
    float4x4 u_model;
    float4x4 u_normal_matrix;
    float4 u_object_params;
};

struct main0_out
{
    float4 out_color [[color(0)]];
};

struct main0_in
{
    float3 v_world_position [[user(locn0)]];
    float3 v_normal [[user(locn1)]];
    float4 v_tangent [[user(locn2)]];
    float2 v_uv0 [[user(locn3)]];
};

static inline __attribute__((always_inline))
float gsSaturate(thread const float& v)
{
    return fast::clamp(v, 0.0, 1.0);
}

static inline __attribute__((always_inline))
PbrSurface gsMakeSurface(thread const float3& N, thread const float3& V, thread const float3& albedo, thread const float& metallic, thread const float& roughness, thread const float& ao, thread const float3& emissive, thread const int& shading_mode)
{
    PbrSurface s;
    s.N = fast::normalize(N);
    s.V = fast::normalize(V);
    s.albedo = albedo;
    float param = metallic;
    s.metallic = gsSaturate(param);
    float param_1 = roughness;
    s.roughness = fast::max(gsSaturate(param_1), 0.04500000178813934326171875);
    float param_2 = ao;
    s.ao = gsSaturate(param_2);
    s.emissive = emissive;
    s.shading_mode = shading_mode;
    s.F0 = mix(float3(0.039999999105930328369140625), albedo, float3(s.metallic));
    return s;
}

static inline __attribute__((always_inline))
float gsDistanceAttenuation(thread const float& _distance, thread const float& range)
{
    if (range <= 0.0)
    {
        return 1.0;
    }
    float d = fast::max(_distance, 9.9999997473787516355514526367188e-05);
    float param = 1.0 - pow(d / range, 4.0);
    float window = gsSaturate(param);
    return (window * window) / (d * d);
}

static inline __attribute__((always_inline))
float gsSpotAttenuation(thread const float3& light_dir, thread const float3& to_fragment, thread const float& cos_inner, thread const float& cos_outer)
{
    float cos_angle = dot(fast::normalize(-light_dir), fast::normalize(-to_fragment));
    if (cos_outer >= cos_inner)
    {
        float param = cos_angle;
        return gsSaturate(param);
    }
    float param_1 = (cos_angle - cos_outer) / fast::max(cos_inner - cos_outer, 9.9999997473787516355514526367188e-06);
    return gsSaturate(param_1);
}

static inline __attribute__((always_inline))
float gsDistributionBlinnPhong(thread const float& NoH, thread const float& roughness)
{
    float a = fast::max(roughness * roughness, 9.9999997473787516355514526367188e-05);
    float shininess = fast::max((2.0 / (a * a)) - 2.0, 1.0);
    float norm = (shininess + 2.0) / 6.283185482025146484375;
    return norm * pow(fast::max(NoH, 0.0), shininess);
}

static inline __attribute__((always_inline))
float gsDistributionGGX(thread const float& NoH, thread const float& roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (((NoH * a2) - NoH) * NoH) + 1.0;
    return a2 / fast::max((3.1415927410125732421875 * d) * d, 1.0000000116860974230803549289703e-07);
}

static inline __attribute__((always_inline))
float gsDistribution(thread const float& NoH, thread const float& roughness, thread const int& shading_mode)
{
    if (shading_mode == 1)
    {
        float param = NoH;
        float param_1 = roughness;
        return gsDistributionBlinnPhong(param, param_1);
    }
    float param_2 = NoH;
    float param_3 = roughness;
    return gsDistributionGGX(param_2, param_3);
}

static inline __attribute__((always_inline))
float gsGeometrySchlickGGX(thread const float& NoV, thread const float& roughness)
{
    float k = ((roughness + 1.0) * (roughness + 1.0)) / 8.0;
    return NoV / ((NoV * (1.0 - k)) + k);
}

static inline __attribute__((always_inline))
float gsGeometrySmith(thread const float& NoV, thread const float& NoL, thread const float& roughness)
{
    float param = NoV;
    float param_1 = roughness;
    float param_2 = NoL;
    float param_3 = roughness;
    return gsGeometrySchlickGGX(param, param_1) * gsGeometrySchlickGGX(param_2, param_3);
}

static inline __attribute__((always_inline))
float3 gsFresnelSchlick(thread const float& cosTheta, thread const float3& F0)
{
    float f = pow(fast::clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    return F0 + ((float3(1.0) - F0) * f);
}

static inline __attribute__((always_inline))
float3 gsEvaluateDirectLight(thread const PbrSurface& s, thread const float3& L, thread const float3& radiance)
{
    float3 Lv = fast::normalize(L);
    float3 H = fast::normalize(s.V + Lv);
    float NoV = fast::max(dot(s.N, s.V), 9.9999997473787516355514526367188e-06);
    float NoL = fast::max(dot(s.N, Lv), 0.0);
    float NoH = fast::max(dot(s.N, H), 0.0);
    float VoH = fast::max(dot(s.V, H), 0.0);
    if (NoL <= 0.0)
    {
        return float3(0.0);
    }
    float param = NoH;
    float param_1 = s.roughness;
    int param_2 = s.shading_mode;
    float D = gsDistribution(param, param_1, param_2);
    float param_3 = NoV;
    float param_4 = NoL;
    float param_5 = s.roughness;
    float G = gsGeometrySmith(param_3, param_4, param_5);
    float param_6 = VoH;
    float3 param_7 = s.F0;
    float3 F = gsFresnelSchlick(param_6, param_7);
    float3 specular = (F * (D * G)) / float3(fast::max((4.0 * NoV) * NoL, 9.9999997473787516355514526367188e-05));
    float3 kS = F;
    float3 kD = (float3(1.0) - kS) * (1.0 - s.metallic);
    float3 diffuse = (kD * s.albedo) / float3(3.1415927410125732421875);
    return ((diffuse + specular) * radiance) * NoL;
}

static inline __attribute__((always_inline))
float3 gsProceduralSky(thread const float3& dir, constant Environment& v_573)
{
    float param = (dir.y * 0.5) + 0.5;
    float t = gsSaturate(param);
    float3 sky = mix(v_573.u_ground_tint.xyz, v_573.u_sky_tint.xyz, float3(t));
    float param_1 = dot(fast::normalize(dir), float3(0.51214754581451416015625, 0.76822125911712646484375, 0.384110629558563232421875));
    float sun = pow(gsSaturate(param_1), 256.0);
    return (sky * v_573.u_env_params.x) + float3(sun * 8.0);
}

static inline __attribute__((always_inline))
float2 gsSaturate(thread const float2& v)
{
    return fast::clamp(v, float2(0.0), float2(1.0));
}

static inline __attribute__((always_inline))
float2 gsEnvBRDFApprox(thread const float& NoV, thread const float& roughness)
{
    float4 r = (float4(-1.0, -0.0274999998509883880615234375, -0.572000026702880859375, 0.02199999988079071044921875) * roughness) + float4(1.0, 0.0425000004470348358154296875, 1.03999996185302734375, -0.039999999105930328369140625);
    float a004 = (fast::min(r.x * r.x, exp2((-9.27999973297119140625) * NoV)) * r.x) + r.y;
    float2 ab = (float2(-1.03999996185302734375, 1.03999996185302734375) * a004) + r.zw;
    float2 param = ab;
    return gsSaturate(param);
}

static inline __attribute__((always_inline))
float3 gsFresnelSchlickRough(thread const float& cosTheta, thread const float3& F0, thread const float& roughness)
{
    float3 one = fast::max(float3(1.0 - roughness), F0);
    float f = pow(fast::clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    return F0 + ((one - F0) * f);
}

static inline __attribute__((always_inline))
float3 gsEvaluateIBL(thread const PbrSurface& s, thread const float3& irradiance, thread const float3& prefiltered, thread const float2& brdf)
{
    float NoV = fast::max(dot(s.N, s.V), 9.9999997473787516355514526367188e-06);
    float param = NoV;
    float3 param_1 = s.F0;
    float param_2 = s.roughness;
    float3 F = gsFresnelSchlickRough(param, param_1, param_2);
    float3 kS = F;
    float3 kD = (float3(1.0) - kS) * (1.0 - s.metallic);
    float3 diffuse = (kD * irradiance) * s.albedo;
    float3 specular = prefiltered * ((F * brdf.x) + float3(brdf.y));
    return (diffuse + specular) * s.ao;
}

fragment main0_out main0(main0_in in [[stage_in]], constant PerFrame& _783 [[buffer(0)]], constant Lights& _855 [[buffer(1)]], constant Environment& v_573 [[buffer(2)]], constant Material& _610 [[buffer(8)]], texturecube<float> u_irradiance_map [[texture(3)]], texturecube<float> u_prefilter_map [[texture(4)]], texture2d<float> u_brdf_lut [[texture(5)]], texture2d<float> u_base_color [[texture(9)]], texture2d<float> u_metallic_roughness [[texture(10)]], texture2d<float> u_normal [[texture(11)]], texture2d<float> u_occlusion [[texture(12)]], texture2d<float> u_emissive [[texture(13)]], sampler u_irradiance_mapSmplr [[sampler(3)]], sampler u_prefilter_mapSmplr [[sampler(4)]], sampler u_brdf_lutSmplr [[sampler(5)]], sampler u_base_colorSmplr [[sampler(9)]], sampler u_metallic_roughnessSmplr [[sampler(10)]], sampler u_normalSmplr [[sampler(11)]], sampler u_occlusionSmplr [[sampler(12)]], sampler u_emissiveSmplr [[sampler(13)]])
{
    main0_out out = {};
    float4 base_color = _610.u_base_color_factor;
    if (_610.u_texture_mask.x > 0.5)
    {
        base_color *= u_base_color.sample(u_base_colorSmplr, in.v_uv0);
    }
    float metallic = _610.u_material_params0.x;
    float roughness = _610.u_material_params0.y;
    if (_610.u_texture_mask.y > 0.5)
    {
        float4 mr = u_metallic_roughness.sample(u_metallic_roughnessSmplr, in.v_uv0);
        metallic *= mr.z;
        roughness *= mr.y;
    }
    float occlusion = 1.0;
    if (_610.u_texture_mask.w > 0.5)
    {
        float ao = u_occlusion.sample(u_occlusionSmplr, in.v_uv0).x;
        occlusion = mix(1.0, ao, _610.u_material_params0.w);
    }
    float3 emissive = _610.u_emissive_factor.xyz * _610.u_emissive_factor.w;
    if (_610.u_material_extra.x > 0.5)
    {
        emissive *= u_emissive.sample(u_emissiveSmplr, in.v_uv0).xyz;
    }
    float alpha = base_color.w;
    int alpha_mode = int(_610.u_material_params1.y + 0.5);
    bool _699 = alpha_mode == 1;
    bool _706;
    if (_699)
    {
        _706 = alpha < _610.u_material_params1.x;
    }
    else
    {
        _706 = _699;
    }
    if (_706)
    {
        discard_fragment();
    }
    float3 N = fast::normalize(in.v_normal);
    if (_610.u_texture_mask.z > 0.5)
    {
        float3 T = fast::normalize(in.v_tangent.xyz - (N * dot(N, in.v_tangent.xyz)));
        float3 B = cross(N, T) * in.v_tangent.w;
        float3 n_ts = (u_normal.sample(u_normalSmplr, in.v_uv0).xyz * 2.0) - float3(1.0);
        float3 _752 = n_ts;
        float2 _754 = _752.xy * _610.u_material_params0.z;
        n_ts.x = _754.x;
        n_ts.y = _754.y;
        N = fast::normalize(float3x3(float3(T), float3(B), float3(N)) * n_ts);
    }
    float3 V = fast::normalize(_783.u_camera_position.xyz - in.v_world_position);
    bool _793 = _610.u_material_params1.z > 0.5;
    bool _800;
    if (_793)
    {
        _800 = dot(N, V) < 0.0;
    }
    else
    {
        _800 = _793;
    }
    if (_800)
    {
        N = -N;
    }
    int shading_model = int(_610.u_material_params1.w + 0.5);
    if (shading_model == 2)
    {
        out.out_color = float4(fast::max((base_color.xyz * occlusion) + emissive, float3(0.0)), alpha);
        return out;
    }
    float3 param = N;
    float3 param_1 = V;
    float3 param_2 = base_color.xyz;
    float param_3 = metallic;
    float param_4 = roughness;
    float param_5 = occlusion;
    float3 param_6 = emissive;
    int param_7 = shading_model;
    PbrSurface surface = gsMakeSurface(param, param_1, param_2, param_3, param_4, param_5, param_6, param_7);
    float3 color = float3(0.0);
    int light_count = int(_855.u_light_count_params.x + 0.5);
    GpuLight_1 light;
    float3 L;
    for (int i = 0; i < 8; i++)
    {
        if (i >= light_count)
        {
            break;
        }
        light.position_range = _855.u_lights[0].position_range;
        light.direction_cone = _855.u_lights[0].direction_cone;
        light.color_intensity = _855.u_lights[0].color_intensity;
        light.type_params = _855.u_lights[0].type_params;
        if (i == 1)
        {
            light.position_range = _855.u_lights[1].position_range;
            light.direction_cone = _855.u_lights[1].direction_cone;
            light.color_intensity = _855.u_lights[1].color_intensity;
            light.type_params = _855.u_lights[1].type_params;
        }
        else
        {
            if (i == 2)
            {
                light.position_range = _855.u_lights[2].position_range;
                light.direction_cone = _855.u_lights[2].direction_cone;
                light.color_intensity = _855.u_lights[2].color_intensity;
                light.type_params = _855.u_lights[2].type_params;
            }
            else
            {
                if (i == 3)
                {
                    light.position_range = _855.u_lights[3].position_range;
                    light.direction_cone = _855.u_lights[3].direction_cone;
                    light.color_intensity = _855.u_lights[3].color_intensity;
                    light.type_params = _855.u_lights[3].type_params;
                }
                else
                {
                    if (i == 4)
                    {
                        light.position_range = _855.u_lights[4].position_range;
                        light.direction_cone = _855.u_lights[4].direction_cone;
                        light.color_intensity = _855.u_lights[4].color_intensity;
                        light.type_params = _855.u_lights[4].type_params;
                    }
                    else
                    {
                        if (i == 5)
                        {
                            light.position_range = _855.u_lights[5].position_range;
                            light.direction_cone = _855.u_lights[5].direction_cone;
                            light.color_intensity = _855.u_lights[5].color_intensity;
                            light.type_params = _855.u_lights[5].type_params;
                        }
                        else
                        {
                            if (i == 6)
                            {
                                light.position_range = _855.u_lights[6].position_range;
                                light.direction_cone = _855.u_lights[6].direction_cone;
                                light.color_intensity = _855.u_lights[6].color_intensity;
                                light.type_params = _855.u_lights[6].type_params;
                            }
                            else
                            {
                                if (i == 7)
                                {
                                    light.position_range = _855.u_lights[7].position_range;
                                    light.direction_cone = _855.u_lights[7].direction_cone;
                                    light.color_intensity = _855.u_lights[7].color_intensity;
                                    light.type_params = _855.u_lights[7].type_params;
                                }
                            }
                        }
                    }
                }
            }
        }
        if (light.type_params.w < 0.5)
        {
            continue;
        }
        int type = int(light.type_params.x + 0.5);
        float3 radiance = light.color_intensity.xyz * light.color_intensity.w;
        if (type == 0)
        {
            float3 light_dir = light.direction_cone.xyz;
            if (dot(light_dir, light_dir) < 9.9999999392252902907785028219223e-09)
            {
                light_dir = float3(0.0, -1.0, 0.0);
            }
            L = -fast::normalize(light_dir);
        }
        else
        {
            float3 delta = light.position_range.xyz - in.v_world_position;
            float _distance = fast::max(length(delta), 9.9999997473787516355514526367188e-05);
            L = delta / float3(_distance);
            float param_8 = _distance;
            float param_9 = light.position_range.w;
            float attenuation = gsDistanceAttenuation(param_8, param_9);
            if (type == 2)
            {
                float3 param_10 = light.direction_cone.xyz;
                float3 param_11 = delta;
                float param_12 = light.type_params.y;
                float param_13 = light.type_params.z;
                attenuation *= gsSpotAttenuation(param_10, param_11, param_12, param_13);
            }
            radiance *= attenuation;
        }
        PbrSurface param_14 = surface;
        float3 param_15 = L;
        float3 param_16 = radiance;
        color += gsEvaluateDirectLight(param_14, param_15, param_16);
    }
    color += (((_855.u_ambient_color.xyz * _855.u_ambient_color.w) * surface.albedo) * surface.ao);
    float3 R = reflect(-V, N);
    float NoV = fast::max(dot(N, V), 9.9999997473787516355514526367188e-06);
    if (v_573.u_ibl_params.x > 0.5)
    {
        float3 irradiance;
        float3 prefiltered;
        float2 brdf;
        if (v_573.u_ibl_params.z > 1.5)
        {
            irradiance = u_irradiance_map.sample(u_irradiance_mapSmplr, N).xyz;
            float mip_levels = fast::max(v_573.u_ibl_params.y, 1.0);
            float lod = surface.roughness * (mip_levels - 1.0);
            prefiltered = u_prefilter_map.sample(u_prefilter_mapSmplr, R, level(lod)).xyz;
            brdf = u_brdf_lut.sample(u_brdf_lutSmplr, float2(NoV, surface.roughness)).xy;
        }
        else
        {
            float3 param_17 = N;
            irradiance = gsProceduralSky(param_17, v_573);
            float3 param_18 = R;
            prefiltered = gsProceduralSky(param_18, v_573);
            float param_19 = NoV;
            float param_20 = surface.roughness;
            brdf = gsEnvBRDFApprox(param_19, param_20);
        }
        PbrSurface param_21 = surface;
        float3 param_22 = irradiance;
        float3 param_23 = prefiltered;
        float2 param_24 = brdf;
        color += (gsEvaluateIBL(param_21, param_22, param_23, param_24) * v_573.u_env_params.x);
    }
    color += surface.emissive;
    out.out_color = float4(fast::max(color, float3(0.0)), alpha);
    return out;
}

