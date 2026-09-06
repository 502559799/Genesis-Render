#version 450
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 12) uniform sampler2D sceneColor;

void main()
{
    vec2 uv = vUv;
#ifdef GENESIS_GL
    uv.y = 1.0 - uv.y;
#endif
    outColor = texture(sceneColor, uv);
}
