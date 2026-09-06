#version 450
layout(set = 0, binding = 0) uniform FrameBlock {
    vec4 color;
    vec2 scale;
    vec2 pad;
} frame;

layout(location = 0) out vec3 vColor;

vec2 positions[3] = vec2[](
    vec2( 0.0, -0.7),
    vec2( 0.7,  0.7),
    vec2(-0.7,  0.7)
);
vec3 colors[3] = vec3[](
    vec3(1.0, 0.2, 0.2),
    vec3(0.2, 1.0, 0.2),
    vec3(0.2, 0.4, 1.0)
);

void main() {
#ifdef VULKAN
    uint vid = gl_VertexIndex;
#else
    uint vid = uint(gl_VertexID);
#endif
    gl_Position = vec4(positions[vid] * frame.scale, 0.0, 1.0);
    vColor = colors[vid] * frame.color.rgb;
}
