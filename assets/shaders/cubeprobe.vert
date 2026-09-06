#version 450
// Full-screen triangle, no vertex buffer.
layout(location = 0) out vec2 vUv;
void main() {
#ifdef VULKAN
    uint vid = gl_VertexIndex;
#else
    uint vid = uint(gl_VertexID);
#endif
    vec2 uv = vec2((vid << 1) & 2u, vid & 2u);
    vUv = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
