// 漫反射辐照度图: 半球余弦卷积
#include "common.glsl"

layout(location = 0) in vec2 vNdc;

DECLARE_BAKE_UBO(0)

LAYOUT_SET(0, 1) uniform samplerCube uSource;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec2 uv = bakeUV(vNdc);
    vec3 N = normalize(bakeUBO.forward.xyz + bakeUBO.right.xyz * uv.x + bakeUBO.up.xyz * uv.y);

    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = cross(N, right);

    vec3 irradiance = vec3(0.0);
    uint sampleCount = 0u;
    // 半球均匀采样
    float delta = 0.05;
    for (float phi = 0.0; phi < 2.0 * PI; phi += delta * 4.0)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += delta)
        {
            vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 sampleVec = right * tangentSample.x + up * tangentSample.y + N * tangentSample.z;
            irradiance += texture(uSource, sampleVec).rgb * cos(theta) * sin(theta);
            ++sampleCount;
        }
    }
    irradiance = PI * irradiance / float(sampleCount);

    fragColor = vec4(irradiance, 1.0);
}
