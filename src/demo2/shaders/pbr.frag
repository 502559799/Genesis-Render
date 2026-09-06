// PBR 片元着色器: 直接光 (Blinn-Phong / Cook-Torrance) + 间接光 IBL
#include "common.glsl"

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec3 vTangent;
layout(location = 4) in float vBitangentSign;

DECLARE_FRAME_UBO(0)
DECLARE_MATERIAL_UBO(1)

LAYOUT_SET(1, 1) uniform sampler2D uAlbedoMap;
LAYOUT_SET(1, 2) uniform sampler2D uNormalMap;
LAYOUT_SET(1, 3) uniform sampler2D uMetallicRoughnessMap;
LAYOUT_SET(1, 4) uniform sampler2D uAOMap;
LAYOUT_SET(1, 5) uniform sampler2D uEmissiveMap;
LAYOUT_SET(1, 6) uniform samplerCube uIrradianceMap;
LAYOUT_SET(1, 7) uniform samplerCube uPrefilterMap;
LAYOUT_SET(1, 8) uniform sampler2D uBrdfLUT;

layout(location = 0) out vec4 fragColor;

//-----------------------------------------------------------
// 直接光: Blinn-Phong (fresnel + specular + diffuse, metallic 影响 F0 与高光染色)
//-----------------------------------------------------------
vec3 blinnPhongDirect(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness, vec3 radiance)
{
    vec3 H = normalize(V + L);
    float NoL = max(dot(N, L), 0.0);
    float NoH = max(dot(N, H), 0.0);
    float HoV = max(dot(H, V), 0.0);

    // diffuse
    vec3 diffuse = albedo / PI;

    // specular: Blinn-Phong, shininess 由 roughness 映射
    float shininess = 2.0 / max(roughness * roughness, 1e-3) - 2.0;
    float spec = pow(NoH, shininess) * (shininess + 8.0) / (8.0 * PI); // 归一化

    // fresnel (Schlick), metallic 下 F0 取 albedo (高光染色)
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = F_Schlick(HoV, F0);

    vec3 specular = spec * F;
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    return (kD * diffuse + specular) * radiance * NoL;
}

//-----------------------------------------------------------
// 直接光: Cook-Torrance PBR (D GGX + G Smith + F Schlick)
//-----------------------------------------------------------
vec3 cookTorranceDirect(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness, vec3 radiance)
{
    vec3 H = normalize(V + L);
    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 1e-4);
    float NoH = max(dot(N, H), 0.0);
    float HoV = max(dot(H, V), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float D = D_GGX(NoH, roughness);
    float G = G_Smith(NoV, NoL, roughness);
    vec3 F = F_Schlick(HoV, F0);

    vec3 specular = (D * G * F) / max(4.0 * NoV * NoL, 1e-4);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * radiance * NoL;
}

//-----------------------------------------------------------
// 间接光: IBL (diffuseIrradiance + prefilteredCubemap + BRDF LUT)
//-----------------------------------------------------------
vec3 iblAmbient(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, float ao)
{
    vec3 R = reflect(-V, N);
    float NoV = max(dot(N, V), 1e-4);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = F_SchlickRoughness(NoV, F0, roughness);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    // 漫反射: 辐照度图
    vec3 irradiance = texture(uIrradianceMap, N).rgb;
    vec3 diffuseIBL = kD * irradiance * albedo;

    // 镜面: 预滤波环境图 + BRDF LUT
    vec3 prefiltered = textureLod(uPrefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(uBrdfLUT, vec2(NoV, roughness)).rg;
    vec3 specularIBL = prefiltered * (F * brdf.x + brdf.y);

    return (diffuseIBL + specularIBL) * ao;
}

void main()
{
    //---------------- 材质参数 ----------------//
    vec3 albedo = materialUBO.baseColorFactor.rgb;
    float alpha = materialUBO.baseColorFactor.a;
    if (materialUBO.texFlags.x != 0)
    {
        vec4 tex = texture(uAlbedoMap, vUV);
        albedo *= tex.rgb;
        alpha *= tex.a;
    }

    float metallic = materialUBO.factors.x;
    float roughness = materialUBO.factors.y;
    float ao = materialUBO.factors.z;
    if (materialUBO.texFlags.z != 0)
    {
        vec2 mr = texture(uMetallicRoughnessMap, vUV).bg; // glTF 约定: G=roughness B=metallic
        roughness *= mr.x;
        metallic *= mr.y;
    }
    roughness = clamp(roughness, 0.04, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);

    if (materialUBO.texFlags.w != 0)
        ao *= texture(uAOMap, vUV).r;

    //---------------- 法线 ----------------//
    vec3 N = normalize(vNormal);
    if (materialUBO.texFlags.y != 0)
    {
        vec3 tangentNormal = texture(uNormalMap, vUV).xyz * 2.0 - 1.0;
        vec3 T = normalize(vTangent);
        T = normalize(T - dot(T, N) * N); // 正交化
        vec3 B = cross(N, T) * vBitangentSign;
        mat3 TBN = mat3(T, B, N);
        N = normalize(TBN * tangentNormal);
    }

    vec3 V = normalize(frameUBO.camPos_time.xyz - vWorldPos);

    //---------------- 直接光 ----------------//
    vec3 L = normalize(-frameUBO.lightDir_intensity.xyz);
    vec3 radiance = frameUBO.lightColor_exposure.rgb * frameUBO.lightDir_intensity.w;

    vec3 Lo;
    if (frameUBO.flags.x == 0)
        Lo = blinnPhongDirect(N, V, L, albedo, metallic, roughness, radiance);
    else
        Lo = cookTorranceDirect(N, V, L, albedo, metallic, roughness, radiance);

    //---------------- 间接光 ----------------//
    vec3 ambient;
    if (frameUBO.flags.y != 0)
        ambient = iblAmbient(N, V, albedo, metallic, roughness, ao);
    else
        ambient = albedo * 0.03 * ao;

    //---------------- 自发光 ----------------//
    vec3 emissive = materialUBO.emissiveFactor.rgb;
    vec3 color = Lo + ambient + emissive;

    //---------------- 色调映射 + gamma ----------------//
    color = acesTonemap(color * frameUBO.lightColor_exposure.w);
    color = gammaCorrect(color);

    //---------------- 调试通道 (flags.w) ----------------//
    int dbg = frameUBO.flags.w;
    if (dbg == 1) color = vec3(max(dot(N, normalize(-frameUBO.lightDir_intensity.xyz)), 0.0));
    else if (dbg == 2) color = normalize(-frameUBO.lightDir_intensity.xyz) * 0.5 + 0.5;
    else if (dbg == 3) color = albedo;
    else if (dbg == 4) color = vec3(metallic);
    else if (dbg == 5) color = radiance / 10.0;
    else if (dbg == 6) color = N * 0.5 + 0.5;
    else if (dbg == 7) color = vec3(frameUBO.lightDir_intensity.w / 10.0);

    fragColor = vec4(color, alpha);
}
