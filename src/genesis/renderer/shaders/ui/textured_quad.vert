/*
 * Genesis Render - generic textured/colored quad vertex shader.
 *
 * Used by the CommandBuffer free-draw path (2D fills/lines, 3D quads).
 *
 * Vertex layout (expanded, per quad):
 *   location 0 : vec3 a_position (pre-projected NDC for 2D, world space for 3D)
 *   location 1 : vec2 a_uv
 *   location 2 : vec4 a_color
 *
 * u_quad : set 0 binding 0 UBO
 *   mat4 u_view_proj (3D only)
 *   vec4 u_params : x = is_2d (1.0 => clip space passthrough)
 */
#include "common/genesis_shader.h"

GS_UBO(0, 0, QuadParams)
{
    mat4 u_view_proj;
    vec4 u_params;
};

GS_IN(0, vec3, a_position);
GS_IN(1, vec2, a_uv);
GS_IN(2, vec4, a_color);

GS_OUT(0, vec2, v_uv);
GS_OUT(1, vec4, v_color);

void main()
{
    v_uv = a_uv;
    v_color = a_color;
    if (u_params.x > 0.5)
        gl_Position = vec4(a_position.xy, 0.0, 1.0);
    else
        gl_Position = u_view_proj * vec4(a_position, 1.0);
}
