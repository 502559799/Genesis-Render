#version 450
// Vertex shader for every cubemap capture pass: push constant holds the
// per-face capture viewProj matrix (a rotation-only view * 90deg proj).
layout(location = 0) in vec3 aPosition;
layout(location = 0) out vec3 vDir;

layout(push_constant) uniform ObjectBlock
{
    mat4 viewProj;
    mat4 pad;
} pc;

void main()
{
    vDir = aPosition;
    vec4 p = pc.viewProj * vec4(aPosition, 1.0);
    gl_Position = p.xyww; // force depth = 1.0
}
