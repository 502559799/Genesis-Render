/*
 * Genesis Render - generic textured/colored quad fragment shader.
 */
#include "common/genesis_shader.h"

GS_TEX2D(0, 1, u_quad_texture);

GS_IN(0, vec2, v_uv);
GS_IN(1, vec4, v_color);

GS_OUT(0, vec4, out_color);

void main()
{
    vec4 tex = texture(u_quad_texture, v_uv);
    out_color = vec4(tex.rgb * v_color.rgb, tex.a * v_color.a);
}
