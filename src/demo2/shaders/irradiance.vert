// IBL 烘焙公共顶点着色器: 全屏三角形
#include "common.glsl"

layout(location = 0) out vec2 vNdc;

void main()
{
    vec2 pos = vec2(float((GENESIS_VERTEX_ID << 1) & 2), float(GENESIS_VERTEX_ID & 2)) * 2.0 - 1.0;
    vNdc = pos;
    gl_Position = vec4(pos, 0.0, 1.0);
}
