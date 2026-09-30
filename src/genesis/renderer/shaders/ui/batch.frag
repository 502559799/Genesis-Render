/*
 * Genesis Render - batched primitives, fragment stage.
 *
 * kind 0: textured / solid primitive (texture * vertex colour)
 * kind 1: SDF glyph.  The atlas stores a signed distance remapped to [0,1]
 *         with 0.5 on the glyph outline.  Anti-aliasing uses the screen
 *         space derivative of the distance, so edges stay one pixel wide at
 *         any text size or camera distance; an optional outline is produced
 *         by moving the threshold outwards.
 */
#include "common/genesis_shader.h"

GS_TEX2D(0, 1, u_batch_texture);

GS_IN(0, vec2, v_uv);
GS_IN(1, vec4, v_color);
GS_IN(2, vec4, v_params);
GS_IN(3, vec4, v_outline);

GS_OUT(0, vec4, out_color);

void main()
{
    vec4 tex = texture(u_batch_texture, v_uv);
    // Derivatives must be taken in uniform control flow (before branching).
    float d = tex.r;
    float aa = max(fwidth(d) * 0.75, 1e-4);
    if (v_params.x < 0.5)
    {
        out_color = tex * v_color;
        return;
    }

    float fill = smoothstep(0.5 - aa, 0.5 + aa, d);
    float outline_width = v_params.y;
    if (outline_width > 0.0)
    {
        float edge = 0.5 - outline_width;
        float outer = smoothstep(edge - aa, edge + aa, d);
        vec3 rgb = mix(v_outline.rgb, v_color.rgb, fill);
        float a = mix(v_outline.a * outer, v_color.a, fill);
        if (a < 0.003)
            discard;
        out_color = vec4(rgb, a);
    }
    else
    {
        if (fill < 0.003)
            discard;
        out_color = vec4(v_color.rgb, v_color.a * fill);
    }
}
