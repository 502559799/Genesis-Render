/*
 * Genesis Render - unlit mesh (debug / test shader), fragment stage.
 *
 * Outputs the push-constant colour directly, optionally modulated by a simple
 * half-lambert term so that orientation is visible in screenshots.
 */
#include "common/genesis_shader.h"
#include "common/ubo_layout.h"

GS_IN(0, vec2, v_uv0);
GS_IN(1, vec3, v_normal);

GS_OUT(0, vec4, out_color);

GS_PUSH(PushConstants)
{
    vec4 u_pc_color;
};

void main()
{
    // u_material_extra.x is unused by this shader; keep the output exact so
    // tests can assert on the framebuffer value.
    out_color = vec4(u_pc_color.rgb, u_pc_color.a);
}
