// Genesis 双后端着色器公共宏与函数库
// Vulkan: 构建期定义 GENESIS_VULKAN, 编译为 SPIR-V
// OpenGL: 后端注入 GENESIS_OPENGL, #version 由后端补全

#ifndef GENESIS_COMMON_GLSL
#define GENESIS_COMMON_GLSL

#ifdef GENESIS_VULKAN
#define LAYOUT_SET(set_idx, binding_idx) layout(set = set_idx, binding = binding_idx)
#define GENESIS_VERTEX_ID gl_VertexIndex
#else
#define LAYOUT_SET(set_idx, binding_idx)
#define GENESIS_VERTEX_ID gl_VertexID
#endif

const float PI = 3.14159265359;
const float MAX_REFLECTION_LOD = 4.0;

//-----------------------------------------------------------
// PBR: Cook-Torrance 微表面 BRDF
//-----------------------------------------------------------
// 法线分布函数: GGX/Trowbridge-Reitz
float D_GGX(float NoH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// 几何函数: Schlick-GGX (k = (r+1)^2 / 8)
float G_SchlickGGX(float NoX, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NoX / (NoX * (1.0 - k) + k);
}

// Smith 联合遮蔽
float G_Smith(float NoV, float NoL, float roughness)
{
    return G_SchlickGGX(NoV, roughness) * G_SchlickGGX(NoL, roughness);
}

// Fresnel: Schlick 近似
vec3 F_Schlick(float HoV, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - HoV, 0.0, 1.0), 5.0);
}

// 带粗糙度衰减的 Fresnel (IBL 用)
vec3 F_SchlickRoughness(float NoV, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - NoV, 0.0, 1.0), 5.0);
}

// 重要性采样 (GGX)
vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

// Hammersley 低差异序列
float radicalInverseVdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint N)
{
    return vec2(float(i) / float(N), radicalInverseVdC(i));
}

//-----------------------------------------------------------
// 色调映射与 gamma
//-----------------------------------------------------------
vec3 acesTonemap(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 gammaCorrect(vec3 color)
{
    return pow(color, vec3(1.0 / 2.2));
}

//-----------------------------------------------------------
// UBO 声明 (std140, 与 C++ 结构严格一致)
//-----------------------------------------------------------
#ifdef GENESIS_VULKAN
#define FRAME_UBO(layout_set_) LAYOUT_SET(layout_set_, 0) uniform FrameUBO
#else
#define FRAME_UBO(layout_set_) layout(std140) uniform FrameUBO
#endif

#define DECLARE_FRAME_UBO(layout_set_) \
    FRAME_UBO(layout_set_)             \
    {                                  \
        mat4 view;                     \
        mat4 proj;                     \
        vec4 camPos_time;              \
        vec4 lightDir_intensity;       \
        vec4 lightColor_exposure;      \
        ivec4 flags;                   \
    }                                  \
    frameUBO;

#ifdef GENESIS_VULKAN
#define OBJECT_UBO(layout_set_) LAYOUT_SET(layout_set_, 0) uniform ObjectUBO
#else
#define OBJECT_UBO(layout_set_) layout(std140) uniform ObjectUBO
#endif

#define DECLARE_OBJECT_UBO(layout_set_) \
    OBJECT_UBO(layout_set_)             \
    {                                   \
        mat4 model;                     \
        mat4 normalMat;                 \
    }                                   \
    objectUBO;

#ifdef GENESIS_VULKAN
#define MATERIAL_UBO(layout_set_) LAYOUT_SET(layout_set_, 0) uniform MaterialUBO
#else
#define MATERIAL_UBO(layout_set_) layout(std140) uniform MaterialUBO
#endif

#define DECLARE_MATERIAL_UBO(layout_set_) \
    MATERIAL_UBO(layout_set_)             \
    {                                     \
        vec4 baseColorFactor;             \
        vec4 factors;                     \
        vec4 emissiveFactor;              \
        ivec4 texFlags;                   \
    }                                     \
    materialUBO;

#ifdef GENESIS_VULKAN
#define BAKE_UBO(layout_set_) LAYOUT_SET(layout_set_, 0) uniform BakeUBO
#else
#define BAKE_UBO(layout_set_) layout(std140) uniform BakeUBO
#endif

#define DECLARE_BAKE_UBO(layout_set_) \
    BAKE_UBO(layout_set_)             \
    {                                 \
        vec4 right;                   \
        vec4 up;                      \
        vec4 forward;                 \
        vec4 params;                  \
    }                                 \
    bakeUBO;

// 烘焙方向重建
// 注: 渲染到纹理时 NDC y=-1 在两个后端都落在纹理内存第 0 行
// (GL: row0=底=NDC-1; Vulkan: row0=顶=NDC-1), 因此烘焙贴图内容天然一致,
// 无需翻转; Y 翻转只出现在主相机投影矩阵中 (交换链扫描方向差异)
vec2 bakeUV(vec2 ndc)
{
    return ndc;
}

#endif // GENESIS_COMMON_GLSL
