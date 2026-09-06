/*
 * Genesis Render - debug line / gizmo shader (vertex)
 *
 * Everything the pass needs travels through push constants:
 *   mat4 u_pc_mvp, vec4 u_pc_color   (80 bytes)
 * which keeps the RHI push-constant path exercised on both backends.
 */
#include "common/genesis_shader.h"

GS_IN(0, vec3, a_position);
GS_IN(1, vec4, a_color);

GS_OUT(0, vec4, v_color);

GS_PUSH(PushConstants)
{
    mat4 u_pc_mvp;
    vec4 u_pc_color;
};

void main()
{
    // A per-vertex colour of (0,0,0,0) means "use the push constant colour".
    float use_vertex = step(0.0001, a_color.x + a_color.y + a_color.z + a_color.w);
    v_color = mix(u_pc_color, a_color, use_vertex);
    gl_Position = u_pc_mvp * vec4(a_position, 1.0);
}
