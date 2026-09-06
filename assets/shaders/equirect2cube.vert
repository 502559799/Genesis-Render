#version 450
layout(set = 0, binding = 0) uniform FrameBlock {
    mat4 viewProj;
    vec4 cameraPos;
    vec4 dirLightDir;
    vec4 dirLightColor;
    vec4 params;
    vec4 pointPos[4];
    vec4 pointData[4];
    vec4 misc;
} frame;

layout(location = 0) in vec3 aPos;
layout(location = 0) out vec3 vDir;

void main() {
    vDir = aPos;
    gl_Position = frame.viewProj * vec4(aPos, 1.0);
}
