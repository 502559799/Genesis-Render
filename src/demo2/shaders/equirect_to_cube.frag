// equirect HDR -> cubemap 面
#include "common.glsl"

layout(location = 0) in vec2 vNdc;

DECLARE_BAKE_UBO(0)

LAYOUT_SET(0, 1) uniform sampler2D uSource;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec2 uv = bakeUV(vNdc);
    vec3 dir = normalize(bakeUBO.forward.xyz + bakeUBO.right.xyz * uv.x + bakeUBO.up.xyz * uv.y);

    // 方向 -> equirect 坐标 (v=0 为天顶, 图像第 0 行为天顶)
    float u = atan(dir.z, dir.x) / (2.0 * PI) + 0.5;
    float v = 0.5 - asin(clamp(dir.y, -1.0, 1.0)) / PI;

    fragColor = vec4(texture(uSource, vec2(u, v)).rgb, 1.0);
}
