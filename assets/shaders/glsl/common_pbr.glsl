// Genesis Render - PBR / IBL math shared by the PBR and baker shaders.
#ifndef COMMON_PBR_GLSL
#define COMMON_PBR_GLSL

const float PI = 3.14159265359;

// Schlick Fresnel.
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    vec3 m = max(vec3(1.0 - roughness), F0);
    return F0 + (m - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// GGX/Trowbridge-Reitz normal distribution.
float distributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 1e-7);
}
float schlickGGX(float NdotX, float k)
{
    return NdotX / (NdotX * (1.0 - k) + k);
}
float geometrySmith(float NdotV, float NdotL, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return schlickGGX(NdotV, k) * schlickGGX(NdotL, k);
}

// Cook-Torrance specular BRDF for one light.
vec3 cookTorrance(vec3 N, vec3 V, vec3 L, float roughness, vec3 F0, out vec3 fresnel)
{
    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotL = max(dot(N, L), 0.0);
    float D = distributionGGX(NdotH, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    fresnel = fresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 numerator = D * G * fresnel;
    float denominator = 4.0 * NdotV * NdotL + 0.001;
    return numerator / denominator;
}

// Classic Blinn-Phong with explicit Fresnel / specular / diffuse / metallic
// terms (the required educational direct-lighting model).
vec3 blinnPhong(vec3 N, vec3 V, vec3 L, vec3 albedo, float roughness, float metallic)
{
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);

    // diffuse (Lambert), killed for metals
    vec3 kd = (vec3(1.0) - metallic);
    vec3 diffuse = kd * albedo / PI;

    // Fresnel at half vector, metallic surfaces take F0 from albedo
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = fresnelSchlick(NdotH, F0);

    // shininess grows as roughness drops
    float shininess = mix(8.0, 256.0, 1.0 - roughness);
    vec3 specular = F * pow(NdotH, shininess);

    return (diffuse + specular) * NdotL;
}

vec3 tangentSpaceNormal(sampler2D normalMap, vec2 uv, vec3 N, vec4 tangent)
{
    vec3 T = normalize(tangent.xyz - N * dot(N, tangent.xyz));
    T *= tangent.w >= 0.0 ? 1.0 : -1.0;
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);
    vec3 n = texture(normalMap, uv).rgb * 2.0 - 1.0;
    return normalize(TBN * n);
}

// Narkowicz 2015 ACES filmic approximation + linear -> sRGB.
vec3 ACESFilm(vec3 x)
{
    float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
vec3 linearToSrgb(vec3 c)
{
    return mix(c * 12.92, 1.055 * pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.4)) - 0.055,
               step(vec3(0.0031308), c));
}
vec3 tonemapEncode(vec3 c)
{
    c = ACESFilm(c);
    return linearToSrgb(c);
}

const float kEnvMaxLOD = 7.0; // prefilter mip count - 1

#endif
