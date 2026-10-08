/*
 * Genesis Render - SDF text fragment shader.
 *
 * The glyph atlas stores a signed-distance field: channel R carries the
 * normalized distance, with 0.5 at the glyph edge (inside = >0.5,
 * outside = <0.5).  A single `smoothstep` around 0.5 gives crisp,
 * resolution-independent anti-aliased edges.  v_sdf.y is the edge half
 * width in atlas-uv units (controls sharpness; small values are crisp).
 *
 * set 0 / binding 1 : sampler2D u_glyph_atlas
 */
#include "common/genesis_shader.h"

GS_TEX2D(0, 1, u_glyph_atlas);

GS_IN(0, vec2, v_uv);
GS_IN(1, vec4, v_color);
GS_IN(2, vec2, v_sdf);

GS_OUT(0, vec4, out_color);

void main()
{
    float d = texture(u_glyph_atlas, v_uv).r;
    float soft = max(v_sdf.y, 0.001);
    float alpha = smoothstep(0.5 - soft, 0.5 + soft, d);
    if (alpha < 0.004) discard;
    out_color = vec4(v_color.rgb, v_color.a * alpha);
}
