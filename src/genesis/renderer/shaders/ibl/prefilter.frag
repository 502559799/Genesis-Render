/*
 * Genesis Render - IBL bake: prefiltered specular (split-sum part 1)
 *
 * For each output texel we know the reflection direction R (the face normal).
 * We importance sample the GGX NDF around R for the roughness of the current
 * mip level and average the environment radiance:
 *
 *     L(R, roughness) ~= sum_i L(H_i) * (N.L_i) / sum_i (N.L_i)
 *
 * The classic Karis formulation.  The roughness for each mip comes from
 * IBLParams so the CPU can drive a mip chain in one pass per level.
 */
#include "common/genesis_shader.h"
#include "common/pbr.h"

GS_UBO(0, 0, IBLParams)
{
    vec4 u_ibl_a; // (face index, roughness, mip level, sample count)
    vec4 u_ibl_b; // (cos(env rotation), sin(env rotation), unused, unused)
};
GS_TEXCUBE(0, 2, u_src_cube);

GS_IN(0, vec2, v_uv);
GS_OUT(0, vec4, out_color);

float radicalInverseVdc(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 2^32
}

vec2 hammersley(uint i, uint n)
{
    return vec2(float(i) / float(n), radicalInverseVdc(i));
}

// Importance sample the GGX NDF; returns the half vector.
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

void main()
{
    int face = int(u_ibl_a.x + 0.5);
    float roughness = max(u_ibl_a.y, 0.0);
    uint sample_count = uint(max(u_ibl_a.w, 16.0));

    vec3 N = normalize(gsCubeFaceDirection(face, v_uv));
    vec3 R = N; // for a reflection probe the view direction equals the normal
    vec3 V = R;

    float total_weight = 0.0;
    vec3 prefiltered = vec3(0.0);

    float mip_bias = 0.0;
    for (uint i = 0u; i < sample_count; ++i)
    {
        vec2 xi = hammersley(i, sample_count);
        vec3 H = importanceSampleGGX(xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NoL = max(dot(N, L), 0.0);
        if (NoL > 0.0)
        {
            // Sample the source cube with a roughness dependent LOD to reduce
            // variance (needs a source with a full mip chain).
            float NoH = max(dot(N, H), 0.0);
            float VoH = max(dot(V, H), 0.0);
            float D = gsDistributionGGX(NoH, max(roughness, GS_MIN_ROUGHNESS));
            float pdf = D * NoH / max(4.0 * VoH, 1e-5) + 0.0001;
            float sa_texel = 4.0 * GS_PI / (6.0 * 256.0 * 256.0);
            float sa_sample = 1.0 / (float(sample_count) * pdf + 1e-5);
            mip_bias = roughness == 0.0 ? 0.0 : 0.5 * log2(sa_sample / sa_texel);

            prefiltered += textureLod(u_src_cube, L, mip_bias).rgb * NoL;
            total_weight += NoL;
        }
    }

    prefiltered = total_weight > 0.0 ? prefiltered / total_weight : vec3(0.0);
    out_color = vec4(max(prefiltered, vec3(0.0)), 1.0);
}
