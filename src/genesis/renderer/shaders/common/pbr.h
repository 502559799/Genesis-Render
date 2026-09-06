/*
 * Genesis Render - PBR shading library (shared by mesh / IBL shaders)
 *
 * Two direct-light BRDF models are available, selected at runtime by
 * `u_material_extra`/engine shading mode so that the classic Blinn-Phong
 * decomposition (diffuse + specular + Fresnel + metallic) can be compared
 * against the Cook-Torrance GGX model:
 *
 *   GS_SHADING_COOK_TORRANCE : D = GGX/Trowbridge-Reitz
 *                              G = Smith-Schlick-GGX
 *                              F = Schlick
 *   GS_SHADING_BLINN_PHONG   : D = Blinn-Phong lobe ((n+1)/2pi * (N.H)^n)
 *                              G = Smith-Schlick-GGX (keeps energy sane)
 *                              F = Schlick
 *
 * Both use the metallic-roughness workflow:
 *   F0 = mix(vec3(0.04), albedo, metallic)
 *   kD = (1 - F) * (1 - metallic)
 *
 * Indirect light is the standard split-sum approximation:
 *   Li = kD * irradiance(N) * albedo + prefiltered(R, roughness) * (F * brdf.x + brdf.y)
 * exactly like Piccolo's mesh_lighting.inl, so results can be compared
 * 1:1 between the two engines.
 */
#ifndef GENESIS_PBR_H
#define GENESIS_PBR_H

#include "common/genesis_shader.h"

#define GS_SHADING_COOK_TORRANCE 0
#define GS_SHADING_BLINN_PHONG 1

const float GS_PI = 3.14159265358979323846;
const float GS_MIN_ROUGHNESS = 0.045; // keeps D/G finite (Piccolo clamps to 0.05)

//----------------------------------------------------------------------
// Surface description handed to the light evaluators
//----------------------------------------------------------------------
struct PbrSurface
{
    vec3 N;          // world space normal
    vec3 V;          // world space view direction (surface -> eye)
    vec3 albedo;     // linear diffuse reflectance
    float metallic;  // 0..1
    float roughness; // 0..1 (perceptual, squared internally)
    float ao;        // 0..1 occlusion
    vec3 F0;         // normal-incidence reflectance
    vec3 emissive;   // linear emissive radiance
    int shading_mode;
};

PbrSurface gsMakeSurface(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
                         float ao, vec3 emissive, int shading_mode)
{
    PbrSurface s;
    s.N = normalize(N);
    s.V = normalize(V);
    s.albedo = albedo;
    s.metallic = gsSaturate(metallic);
    s.roughness = max(gsSaturate(roughness), GS_MIN_ROUGHNESS);
    s.ao = gsSaturate(ao);
    s.emissive = emissive;
    s.shading_mode = shading_mode;
    s.F0 = mix(vec3(0.04), albedo, s.metallic);
    return s;
}

//----------------------------------------------------------------------
// Normal distribution functions
//----------------------------------------------------------------------
float gsDistributionGGX(float NoH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / max(GS_PI * d * d, 1e-7);
}

// Blinn-Phong lobe, normalised so that it integrates to 1 over the hemisphere.
// shininess is derived from the perceptual roughness: n = 2/a^2 - 2
float gsDistributionBlinnPhong(float NoH, float roughness)
{
    float a = max(roughness * roughness, 1e-4);
    float shininess = max(2.0 / (a * a) - 2.0, 1.0);
    float norm = (shininess + 2.0) / (2.0 * GS_PI);
    return norm * pow(max(NoH, 0.0), shininess);
}

float gsDistribution(float NoH, float roughness, int shading_mode)
{
    if (shading_mode == GS_SHADING_BLINN_PHONG)
        return gsDistributionBlinnPhong(NoH, roughness);
    return gsDistributionGGX(NoH, roughness);
}

//----------------------------------------------------------------------
// Geometry / visibility
//----------------------------------------------------------------------
float gsGeometrySchlickGGX(float NoV, float roughness)
{
    // Direct lighting uses k = (r+1)^2 / 8 (the Schlick-GGX approximation).
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return NoV / (NoV * (1.0 - k) + k);
}

float gsGeometrySmith(float NoV, float NoL, float roughness)
{
    return gsGeometrySchlickGGX(NoV, roughness) * gsGeometrySchlickGGX(NoL, roughness);
}

//----------------------------------------------------------------------
// Fresnel
//----------------------------------------------------------------------
vec3 gsFresnelSchlick(float cosTheta, vec3 F0)
{
    float f = pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    return F0 + (vec3(1.0) - F0) * f;
}

// Roughness aware variant used for the IBL specular term.
vec3 gsFresnelSchlickRough(float cosTheta, vec3 F0, float roughness)
{
    vec3 one = max(vec3(1.0 - roughness), F0);
    float f = pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    return F0 + (one - F0) * f;
}

//----------------------------------------------------------------------
// Analytic environment BRDF approximation (Karalis), used when no BRDF LUT
// is bound (and as a cross-check for the baked LUT in the test suite).
//----------------------------------------------------------------------
vec2 gsEnvBRDFApprox(float NoV, float roughness)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return gsSaturate(ab);
}

//----------------------------------------------------------------------
// Direct light evaluation
//
// `radiance` is the incident light radiance already attenuated by distance /
// spot cone / NdotL-free geometry (the caller multiplies by NoL).
//----------------------------------------------------------------------
vec3 gsEvaluateDirectLight(PbrSurface s, vec3 L, vec3 radiance)
{
    vec3 Lv = normalize(L);
    vec3 H = normalize(s.V + Lv);

    float NoV = max(dot(s.N, s.V), 1e-5);
    float NoL = max(dot(s.N, Lv), 0.0);
    float NoH = max(dot(s.N, H), 0.0);
    float VoH = max(dot(s.V, H), 0.0);

    if (NoL <= 0.0)
        return vec3(0.0);

    float D = gsDistribution(NoH, s.roughness, s.shading_mode);
    float G = gsGeometrySmith(NoV, NoL, s.roughness);
    vec3 F = gsFresnelSchlick(VoH, s.F0);

    vec3 specular = (D * G * F) / max(4.0 * NoV * NoL, 1e-4);

    // Energy conservation: what is not reflected specularly is diffuse, and
    // metals have no diffuse response.
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - s.metallic);
    vec3 diffuse = kD * s.albedo / GS_PI;

    return (diffuse + specular) * radiance * NoL;
}

//----------------------------------------------------------------------
// Indirect light (split sum approximation)
//
//   irradiance   : cosine weighted convolution of the environment, sampled
//                  with N
//   prefiltered  : GGX convolution of the environment, sampled with R at a
//                  mip level derived from the roughness
//   brdf         : (scale, bias) from the BRDF integration LUT
//----------------------------------------------------------------------
vec3 gsEvaluateIBL(PbrSurface s, vec3 irradiance, vec3 prefiltered, vec2 brdf)
{
    float NoV = max(dot(s.N, s.V), 1e-5);
    vec3 F = gsFresnelSchlickRough(NoV, s.F0, s.roughness);
    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - s.metallic);

    vec3 diffuse = kD * irradiance * s.albedo;
    vec3 specular = prefiltered * (F * brdf.x + brdf.y);

    return (diffuse + specular) * s.ao;
}

// Convenience overload used when only a BRDF approximation is available.
vec3 gsEvaluateIBLApprox(PbrSurface s, vec3 irradiance, vec3 prefiltered)
{
    float NoV = max(dot(s.N, s.V), 1e-5);
    return gsEvaluateIBL(s, irradiance, prefiltered, gsEnvBRDFApprox(NoV, s.roughness));
}

//----------------------------------------------------------------------
// Light attenuation helpers (shared by the mesh shader)
//----------------------------------------------------------------------
float gsDistanceAttenuation(float distance, float range)
{
    if (range <= 0.0)
        return 1.0;
    // Unreal style smooth windowed inverse square falloff.
    float d = max(distance, 1e-4);
    float window = gsSaturate(1.0 - pow(d / range, 4.0));
    return (window * window) / (d * d);
}

float gsSpotAttenuation(vec3 light_dir, vec3 to_fragment, float cos_inner, float cos_outer)
{
    float cos_angle = dot(normalize(-light_dir), normalize(-to_fragment));
    if (cos_outer >= cos_inner)
        return gsSaturate(cos_angle);
    return gsSaturate((cos_angle - cos_outer) / max(cos_inner - cos_outer, 1e-5));
}

#endif // GENESIS_PBR_H
