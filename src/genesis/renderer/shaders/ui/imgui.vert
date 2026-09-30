/*
 * Genesis Render - Dear ImGui renderer (RHI based, shared by all backends).
 */
#include "common/genesis_shader.h"

GS_PUSH(PushConstants)
{
    vec4 u_pc_scale_translate; // xy = scale, zw = translate (display pixels -> clip)
};

GS_IN(0, vec2, a_position);
GS_IN(1, vec2, a_uv);
GS_IN(2, vec4, a_color);

GS_OUT(0, vec2, v_uv);
GS_OUT(1, vec4, v_color);

void main()
{
    v_uv = a_uv;
    v_color = a_color;
    gl_Position = vec4(a_position * u_pc_scale_translate.xy + u_pc_scale_translate.zw, 0.0, 1.0);
}
