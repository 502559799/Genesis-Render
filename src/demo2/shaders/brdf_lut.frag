// BRDF 积分 LUT: 输入 (NoV, roughness), 输出 (scale, bias)
#include "common.glsl"

layout(location = 0) in vec2 vNdc;

DECLARE_BAKE_UBO(0)

layout(location = 0) out vec4 fragColor;

// 几何函数 (IBL 变体, k = r^2 / 2)
float G_SchlickGGX_IBL(float NoX, float roughness)
{
    float a = roughness * roughness;
    float k = a / 2.0;
    return NoX / (NoX * (1.0 - k) + k);
}

float G_Smith_IBL(float NoV, float NoL, float roughness)
{
    return G_SchlickGGX_IBL(NoV, roughness) * G_SchlickGGX_IBL(NoL, roughness);
}

vec2 integrateBRDF(float NoV, float roughness)
{
    vec3 V;
    V.x = sqrt(1.0 - NoV * NoV);
    V.y = 0.0;
    V.z = NoV;

    float A = 0.0;
    float B = 0.0;
    vec3 N = vec3(0.0, 0.0, 1.0);

    const uint SAMPLE_COUNT = 512u;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 H = importanceSampleGGX(Xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NoL = max(L.z, 0.0);
        float NoH = max(H.z, 0.0);
        float VoH = max(dot(V, H), 0.0);

        if (NoL > 0.0)
        {
            float G = G_Smith_IBL(NoV, NoL, roughness);
            float G_Vis = (G * VoH) / (NoH * NoV);
            float Fc = pow(1.0 - VoH, 5.0);
            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    return vec2(A, B) / float(SAMPLE_COUNT);
}

void main()
{
    vec2 uv = vNdc * 0.5 + 0.5; // [-1,1] -> [0,1]
    vec2 integrated = integrateBRDF(uv.x, max(uv.y, 0.02));
    fragColor = vec4(integrated, 0.0, 1.0);
}
