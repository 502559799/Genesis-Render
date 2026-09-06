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

layout(set = 0, binding = 1) uniform DrawBlock {
    mat4 model;
    mat4 normalMatrix;
    vec4 tint;
} draw;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUv;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec4 vTangent;
layout(location = 3) out vec2 vUv;

void main() {
    vec4 world = draw.model * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = normalize(aNormal);
    vTangent = vec4(normalize((draw.normalMatrix * vec4(aTangent.xyz, 0.0)).xyz), aTangent.w);
    vUv = aUv;
    gl_Position = frame.viewProj * world;
}
