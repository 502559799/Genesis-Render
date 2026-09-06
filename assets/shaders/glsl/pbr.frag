#version 450
#extension GL_GOOGLE_include_directive : require
#include "common_pbr.glsl"

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vTangent;
layout(location = 3) in vec2 vUv;
layout(location = 0) out vec4 outColor;

struct PointLight
{
    vec4 positionRange;
    vec4 colorIntensity;
};

layout(set = 0, binding = 0) uniform CameraBlock
{
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec4 cameraPos;
} camera;

layout(set = 0, binding = 1) uniform LightingBlock
{
    vec4 dirDirection;  // xyz TO light, w enabled
    vec4 dirColor;      // xyz color, w intensity
    vec4 flags;         // x direct, y ibl, z model(0 PBR,1 Blinn-Phong), w env intensity
    PointLight pointLights[4];
    vec4 pointCount;
    vec4 ambient;
} light;

layout(set = 1, binding = 2) uniform MaterialBlock
{
    vec4 baseColorFactor;
    vec4 metallicRoughness;   // x metallic, y roughness, z ao strength
    vec4 emissiveFactor;      // xyz color, w strength
    vec4 textureFlags;        // x albedo, y normal, z orm, w emissive
    vec4 uvTransform;         // xy scale, zw offset
} material;

layout(set = 1, binding = 4) uniform sampler2D baseColorMap;
layout(set = 1, binding = 5) uniform sampler2D normalMap;
layout(set = 1, binding = 6) uniform sampler2D ormMap;
layout(set = 1, binding = 7) uniform sampler2D emissiveMap;

layout(set = 0, binding = 8) uniform samplerCube irradianceMap;
layout(set = 0, binding = 9) uniform samplerCube prefilteredMap;
layout(set = 0, binding = 10) uniform sampler2D brdfLUT;

vec3 getAlbedo()
{
    vec3 c = material.baseColorFactor.rgb;
    if (material.textureFlags.x > 0.5) c *= texture(baseColorMap, vUv * material.uvTransform.xy
                                                                       + material.uvTransform.zw).rgb;
    return c;
}
float getAO()
{
    return material.textureFlags.z > 0.5 ? texture(ormMap, vUv).r : 1.0;
}
vec3 getEmissive()
{
    vec3 e = material.emissiveFactor.rgb * material.emissiveFactor.w;
    if (material.textureFlags.w > 0.5) e *= texture(emissiveMap, vUv).rgb;
    return e;
}

// PBR (Cook-Torrance, metallic-roughness workflow) for a single directional light.
vec3 directPBR(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
               vec3 L, vec3 radiance)
{
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 fresnel;
    vec3 specular = cookTorrance(N, V, L, roughness, F0, fresnel);
    vec3 kS = fresnel;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    float NdotL = max(dot(N, L), 0.0);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

vec3 directBlinnPhong(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
                      vec3 L, vec3 radiance)
{
    return blinnPhong(N, V, L, albedo, roughness, metallic) * radiance;
}

void main()
{
    float metallic = material.metallicRoughness.x;
    float roughness = material.metallicRoughness.y;
    if (material.textureFlags.z > 0.5)
    {
        // glTF packed metallic-roughness: g = roughness, b = metallic, r = occlusion
        vec3 orm = texture(ormMap, vUv).rgb;
        roughness = clamp(roughness * orm.g, 0.045, 1.0);
        metallic = clamp(metallic * orm.b, 0.0, 1.0);
    }
    vec3 albedo = getAlbedo();

    vec3 N = normalize(vNormal);
    if (material.textureFlags.y > 0.5)
        N = tangentSpaceNormal(normalMap, vUv, N, vTangent);
    vec3 V = normalize(camera.cameraPos.xyz - vWorldPos);
    float NdotV = max(dot(N, V), 0.0);

    vec3 Lo = vec3(0.0);
    bool directOn = light.flags.x > 0.5;
    bool useBP = light.flags.z > 0.5;
    if (directOn && light.dirDirection.w > 0.5)
    {
        vec3 L = normalize(light.dirDirection.xyz);
        vec3 radiance = light.dirColor.rgb * light.dirColor.w;
        Lo += useBP ? directBlinnPhong(N, V, albedo, metallic, roughness, L, radiance)
                   : directPBR(N, V, albedo, metallic, roughness, L, radiance);
    }
    if (directOn)
    {
        int n = int(light.pointCount.x + 0.5);
        for (int i = 0; i < 4; ++i)
        {
            if (i >= n) break;
            vec3 lp = light.pointLights[i].positionRange.xyz;
            float range = light.pointLights[i].positionRange.w;
            vec3 delta = lp - vWorldPos;
            float dist = length(delta);
            vec3 L = delta / max(dist, 1e-4);
            vec3 radiance = light.pointLights[i].colorIntensity.rgb
                          * light.pointLights[i].colorIntensity.w
                          / (1.0 + dist * dist);
            float window = clamp(1.0 - pow(dist / max(range, 0.001), 4.0), 0.0, 1.0);
            radiance *= window * window;
            Lo += useBP ? directBlinnPhong(N, V, albedo, metallic, roughness, L, radiance)
                       : directPBR(N, V, albedo, metallic, roughness, L, radiance);
        }
        // small flat ambient so unlit faces aren't black in direct-only mode
        Lo += albedo * light.ambient.rgb;
    }

    vec3 ambient = vec3(0.0);
    if (light.flags.y > 0.5)
    {
        vec3 F0 = mix(vec3(0.04), albedo, metallic);
        vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
        vec3 kS = F;
        vec3 kD = (1.0 - kS) * (1.0 - metallic);
        vec3 irradiance = texture(irradianceMap, N).rgb;
        vec3 diffuseIrr = irradiance * albedo;
        vec3 R = reflect(-V, N);
        vec3 prefiltered = textureLod(prefilteredMap, R, roughness * kEnvMaxLOD).rgb;
        vec2 brdf = texture(brdfLUT, vec2(NdotV, roughness)).rg;
        vec3 specularIrr = prefiltered * (F * brdf.x + brdf.y);
        float ao = getAO();
        ambient = (kD * diffuseIrr + specularIrr) * ao;
        ambient *= light.flags.w;
    }

    vec3 color = Lo + ambient + getEmissive();
    outColor = vec4(tonemapEncode(color), material.baseColorFactor.a);
}
