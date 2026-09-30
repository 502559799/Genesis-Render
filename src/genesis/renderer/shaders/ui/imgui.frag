/*
 * Genesis Render - Dear ImGui renderer, fragment stage.
 */
#include "common/genesis_shader.h"

GS_TEX2D(0, 0, u_ui_texture);

GS_IN(0, vec2, v_uv);
GS_IN(1, vec4, v_color);

GS_OUT(0, vec4, out_color);

void main()
{
    out_color = v_color * texture(u_ui_texture, v_uv);
}
