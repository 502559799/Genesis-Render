/*
 * Genesis Render - shared fullscreen quad vertex shader.
 *
 * Used by the skybox, every IBL bake pass and the post-processing passes.
 * Vertex layout: location 0 = vec2 clip space position (-1..1),
 *                location 1 = vec2 uv (0..1, origin top-left).
 */
#include "common/genesis_shader.h"

GS_IN(0, vec2, a_position);
GS_IN(1, vec2, a_uv);

GS_OUT(0, vec2, v_uv);

void main()
{
    v_uv = a_uv;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
