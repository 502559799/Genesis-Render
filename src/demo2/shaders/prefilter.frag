// 镜面预滤波环境图 (GGX 重要性采样)
#include "common.glsl"

layout(location = 0) in vec2 vNdc;

DECLARE_BAKE_UBO(0)

LAYOUT_SET(0, 1) uniform samplerCube uSource;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec2 uv = bakeUV(vNdc);
    vec3 N = normalize(bakeUBO.forward.xyz + bakeUBO.right.xyz * uv.x + bakeUBO.up.xyz * uv.y);
    float roughness = bakeUBO.params.x;

    vec3 V = N;
    vec3 prefiltered = vec3(0.0);
    float totalWeight = 0.0;

    const uint SAMPLE_COUNT = 512u;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 H = importanceSampleGGX(Xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NoL = max(dot(N, L), 0.0);
        if (NoL > 0.0)
        {
            prefiltered += texture(uSource, L).rgb * NoL;
            totalWeight += NoL;
        }
    }
    prefiltered = prefiltered / max(totalWeight, 1e-4);

    fragColor = vec4(prefiltered, 1.0);
}
