/*
 * Genesis Render - SDF text vertex shader.
 *
 * Draws a glyph as a small quad.  Two modes share one vertex layout:
 *
 *   mode 2D (screen space) : a_world is already clip space (NDC), z = 0.
 *                            The caller pre-projects the anchor and corner.
 *   mode 3D (world space)  : a_world is a world-space quad corner; the
 *                            vertex shader applies the camera view*projection.
 *
 * Vertex layout (expanded per glyph: 4 vertices/quad, all data in the verts):
 *   location 0 : vec3  a_world   (clip-space NDC for 2D, world space for 3D)
 *   location 1 : vec2  a_uv      (glyph atlas uv)
 *   location 2 : vec4  a_color  (r,g,b,a)
 *   location 3 : vec4  a_params  (sdf scale, softness, unused, unused)
 *
 * u_text : set 0 binding 0 UBO
 *   u_a = (world view-projection matrix ... packed as four vec4)
 *   u_mode = (2D mode flag, sdf scale, sdf softness, unused)
 */
#include "common/genesis_shader.h"

GS_UBO(0, 0, TextParams)
{
    mat4 u_view_proj;
    vec4 u_mode; // x = is_2d (1.0 => world.xy is NDC), y = sdf scale, z = sdf softness
};

GS_IN(0, vec3, a_world);
GS_IN(1, vec2, a_uv);
GS_IN(2, vec4, a_color);
GS_IN(3, vec4, a_params);

GS_OUT(0, vec2, v_uv);
GS_OUT(1, vec4, v_color);
GS_OUT(2, vec2, v_sdf); // sdf scale, softness

void main()
{
    v_uv = a_uv;
    v_color = a_color;
    v_sdf = vec2(a_params.x, a_params.y);
    if (u_mode.x > 0.5)
        gl_Position = vec4(a_world.xy, 0.0, 1.0);
    else
        gl_Position = u_view_proj * vec4(a_world, 1.0);
}
