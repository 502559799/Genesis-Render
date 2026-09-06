#version 450
#extension GL_GOOGLE_include_directive : require
#include "common_pbr.glsl"

layout(location = 0) in vec3 vDir;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 11) uniform samplerCube environmentMap;

void main()
{
    vec3 env = texture(environmentMap, normalize(vDir)).rgb;
    outColor = vec4(tonemapEncode(env), 1.0);
}
