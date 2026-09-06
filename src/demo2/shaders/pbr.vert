// PBR 顶点着色器
#include "common.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aTangent;

DECLARE_FRAME_UBO(0)
DECLARE_OBJECT_UBO(2)

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec3 vTangent;
layout(location = 4) out float vBitangentSign;

void main()
{
    vec4 worldPos = objectUBO.model * vec4(aPosition, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = normalize((objectUBO.normalMat * vec4(aNormal, 0.0)).xyz);
    vTangent = normalize((objectUBO.normalMat * vec4(aTangent.xyz, 0.0)).xyz);
    vBitangentSign = aTangent.w;
    vUV = aUV;
    gl_Position = frameUBO.proj * frameUBO.view * worldPos;
}
