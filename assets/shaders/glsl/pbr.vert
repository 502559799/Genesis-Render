#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent;
layout(location = 3) in vec2 aUv;

layout(set = 0, binding = 0) uniform CameraBlock
{
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec4 cameraPos;
} camera;

layout(push_constant) uniform ObjectBlock
{
    mat4 model;
    mat4 normalMatrix;
} pc;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec4 vTangent;
layout(location = 3) out vec2 vUv;

void main()
{
    vec4 world = pc.model * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    vNormal = normalize(mat3(pc.normalMatrix) * aNormal);
    vTangent = vec4(normalize(mat3(pc.normalMatrix) * aTangent.xyz), aTangent.w);
    vUv = aUv;
    gl_Position = camera.viewProj * world;
}
