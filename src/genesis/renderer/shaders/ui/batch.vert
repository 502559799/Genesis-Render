/*
 * Genesis Render - batched primitives (2D / 3D quads, lines, images and SDF
 * text), vertex stage.
 *
 * 2D batches pass a pixel -> clip matrix (orientation of the target already
 * folded in); 3D batches pass the camera view-projection.
 */
#include "common/genesis_shader.h"

GS_UBO(0, 0, BatchParams)
{
    mat4 u_batch_view_proj;
    vec4 u_batch_params;
};

GS_IN(0, vec3, a_position);
GS_IN(1, vec2, a_uv);
GS_IN(2, vec4, a_color);
GS_IN(3, vec4, a_params);  // x = kind (0 textured, 1 SDF glyph), y = outline width, zw = unused
GS_IN(4, vec4, a_outline); // SDF outline colour

GS_OUT(0, vec2, v_uv);
GS_OUT(1, vec4, v_color);
GS_OUT(2, vec4, v_params);
GS_OUT(3, vec4, v_outline);

void main()
{
    v_uv = a_uv;
    v_color = a_color;
    v_params = a_params;
    v_outline = a_outline;
    gl_Position = u_batch_view_proj * vec4(a_position, 1.0);
}
