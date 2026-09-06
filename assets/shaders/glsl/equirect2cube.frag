#version 450
layout(location = 0) in vec3 vDir;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 12) uniform sampler2D equirectMap;

const float PI = 3.14159265359;

void main()
{
    vec3 d = normalize(vDir);
    float u = atan(d.z, d.x) / (2.0 * PI) + 0.5;
    float v = asin(clamp(d.y, -1.0, 1.0)) / PI + 0.5;
    vec3 color = texture(equirectMap, vec2(u, v)).rgb;
    outColor = vec4(color, 1.0); // linear HDR
}
