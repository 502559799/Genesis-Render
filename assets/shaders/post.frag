#version 450
layout(set = 0, binding = 0) uniform PostBlock {
    vec4 params; // x exposure, y tonemapMode, z gamma, w unused
} post;
layout(set = 0, binding = 30) uniform sampler2D sceneColor;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
vec3 reinhard(vec3 x) { return x / (1.0 + x); }

void main() {
    vec3 hdr = texture(sceneColor, vUv).rgb * post.params.x;
    int mode = int(post.params.y + 0.5);
    vec3 mapped = mode == 0 ? aces(hdr) : mode == 1 ? reinhard(hdr) : hdr;
    float gamma = post.params.z > 0.0 ? post.params.z : 2.2;
    outColor = vec4(pow(clamp(mapped, 0.0, 1.0), vec3(1.0 / gamma)), 1.0);
}
