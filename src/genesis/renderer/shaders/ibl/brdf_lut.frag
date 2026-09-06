/*
 * Genesis Render - IBL bake: environment BRDF integration LUT (split-sum part 2)
 *
 * Output is a 2 channel (RG16F/RG32F) texture where
 *      x = uv  -> NoV
 *      y = v   -> roughness
 *      rg      -> (scale, bias) so that  F = F0 * scale + bias
 *
 * This is the standard Karis "Real Shading in Unreal Engine 4" integration:
 *
 *   int dOmega' V(w) F(w,h) (1 - G) ... simplified to
 *   sum_i [ F(VoH) * (G(V,L) * VoH / (NoH * NoV)) ] / N
 */
#include "common/genesis_shader.h"
#include "common/pbr.h"

GS_UBO(0, 0, IBLParams)
{
    vec4 u_ibl_a; // (unused, roughness, unused, sample count)
    vec4 u_ibl_b;
};

GS_IN(0, vec2, v_uv);
GS_OUT(0, vec4, out_color);

float radicalInverseVdc(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint n)
{
    return vec2(float(i) / float(n), radicalInverseVdc(i));
}

vec3 importanceSampleGGX(vec2 xi, vec3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * GS_PI * xi.x;
    float cos_theta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
    vec3 H = vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);

    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

// Smith-GGX geometry with the IBL remapping k = roughness^2 / 2 (Karis /
// LearnOpenGL convention - note this differs from the direct lighting k).
float geometrySchlickGGX_IBL(float NoX, float roughness)
{
    float k = (roughness * roughness) * 0.5;
    return NoX / (NoX * (1.0 - k) + k);
}

float geometrySmithIBL(float NoV, float NoL, float roughness)
{
    return geometrySchlickGGX_IBL(NoV, roughness) * geometrySchlickGGX_IBL(NoL, roughness);
}

vec2 integrateBRDF(float NoV, float roughness)
{
    vec3 V = vec3(sqrt(1.0 - NoV * NoV), 0.0, NoV);
    vec3 N = vec3(0.0, 0.0, 1.0);

    uint sample_count = uint(max(u_ibl_a.w, 256.0));
    float A = 0.0;
    float B = 0.0;

    for (uint i = 0u; i < sample_count; ++i)
    {
        vec2 xi = hammersley(i, sample_count);
        vec3 H = importanceSampleGGX(xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NoL = max(L.z, 0.0);
        float NoH = max(H.z, 0.0);
        float VoH = max(dot(V, H), 0.0);

        if (NoL > 0.0)
        {
            // G_Vis = G * VoH / (NoH * NoV); importance sampling the GGX NDF
            // cancels the D and 1/(4 NoV NoL) terms, so the estimator is just
            // the mean of G_Vis (split into the (1-Fc) and Fc parts).
            float G = geometrySmithIBL(NoV, NoL, roughness);
            float g_vis = (G * VoH) / max(NoH * NoV, 1e-5);
            float fc = pow(1.0 - VoH, 5.0);
            A += (1.0 - fc) * g_vis;
            B += fc * g_vis;
        }
    }
    return vec2(A, B) / float(sample_count);
}

void main()
{
    float NoV = clamp(v_uv.x, 1e-4, 1.0);
    float roughness = clamp(v_uv.y, GS_MIN_ROUGHNESS, 1.0);
    vec2 brdf = integrateBRDF(NoV, roughness);
    out_color = vec4(gsSaturate(brdf.x), gsSaturate(brdf.y), 0.0, 1.0);
}
