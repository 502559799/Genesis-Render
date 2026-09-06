#version 450
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec2 outBRDF;

const float PI = 3.14159265359;
const uint SAMPLE_COUNT = 256u;

float radicalInverseVanDerCorput(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = (bits << 8u) | (bits >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
vec2 hammersley(uint i, uint n) {
    return vec2(float(i) / float(n), radicalInverseVanDerCorput(i));
}
vec3 importanceSampleGGX(vec2 xi, vec3 n, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    vec3 h = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3 up = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 t = normalize(cross(up, n));
    vec3 b = cross(n, t);
    return normalize(t * h.x + b * h.y + n * h.z);
}
float geometrySchlickGGX(float ndov, float roughness) {
    float a = roughness;
    float k = (a * a) / 2.0;
    return ndov / (ndov * (1.0 - k) + k);
}
float geometrySmith(float ndov, float ndol, float roughness) {
    return geometrySchlickGGX(ndov, roughness) * geometrySchlickGGX(ndol, roughness);
}
vec2 integrateBRDF(float ndov, float roughness) {
    vec3 v;
    v.x = sqrt(1.0 - ndov * ndov);
    v.y = 0.0;
    v.z = ndov;
    float a = 0.0;
    float b = 0.0;
    vec3 n = vec3(0.0, 0.0, 1.0);
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 xi = hammersley(i, SAMPLE_COUNT);
        vec3 h = importanceSampleGGX(xi, n, roughness);
        vec3 l = normalize(2.0 * dot(v, h) * h - v);
        float ndol = max(l.z, 0.0);
        float ndoh = max(h.z, 0.0);
        float vdh = max(dot(v, h), 0.0);
        if (ndol > 0.0) {
            float g = geometrySmith(ndov, ndol, roughness);
            float gVis = (g * vdh) / (ndoh * ndov);
            float fc = pow(1.0 - vdh, 5.0);
            a += (1.0 - fc) * gVis;
            b += fc * gVis;
        }
    }
    return vec2(a, b) / float(SAMPLE_COUNT);
}

void main() {
    vec2 integrated = integrateBRDF(max(vUv.x, 0.001), max(vUv.y, 0.001));
    outBRDF = integrated;
}
