#version 450
const float PI = 3.14159265359;

layout(location = 0) in vec3 vDir;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 11) uniform samplerCube environmentMap;

// Cosine-weighted diffuse irradiance convolution.
void main()
{
    vec3 N = normalize(vDir);
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);

    const uint SAMPLE_COUNT = 96u;
    vec3 irradiance = vec3(0.0);
    float sampleCount = 0.0;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        // quasi-fibonacci hemisphere directions
        float fi = float(i);
        float u1 = fi / float(SAMPLE_COUNT);
        float u2 = fract(fi * 0.61803398875);
        float phi = 2.0 * PI * u2;
        float cosTheta = sqrt(1.0 - u1);
        float sinTheta = sqrt(u1);
        vec3 H;
        H.x = sinTheta * cos(phi);
        H.y = cosTheta;
        H.z = sinTheta * sin(phi);
        vec3 L = T * H.x + B * H.z + N * H.y;
        irradiance += texture(environmentMap, L).rgb * max(dot(N, L), 0.0);
        sampleCount += 1.0;
    }
    irradiance = PI * irradiance / sampleCount;
    outColor = vec4(irradiance, 1.0);
}
