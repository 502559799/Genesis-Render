// 天空盒顶点着色器
#include "common.glsl"

layout(location = 0) in vec3 aPosition;

DECLARE_FRAME_UBO(0)

layout(location = 0) out vec3 vDirection;

void main()
{
    vDirection = aPosition;
    // 去除平移
    mat4 rotView = mat4(mat3(frameUBO.view));
    vec4 pos = frameUBO.proj * rotView * vec4(aPosition, 1.0);
    gl_Position = pos.xyww; // 深度锁到远平面
}
