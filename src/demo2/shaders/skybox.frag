// 天空盒片元着色器
#include "common.glsl"

layout(location = 0) in vec3 vDirection;

DECLARE_FRAME_UBO(0)

LAYOUT_SET(1, 0) uniform samplerCube uEnvMap;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec3 dir = normalize(vDirection);
    vec3 color = texture(uEnvMap, dir).rgb;
    color = acesTonemap(color * frameUBO.lightColor_exposure.w);
    color = gammaCorrect(color);
    fragColor = vec4(color, 1.0);
}
