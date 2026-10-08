/*
 * Genesis Render - programmable post processing.
 *
 * Same as the tonemap pass (exposure / tonemap / gamma) but with an extra
 * user-controlled effect branch, demonstrating a programmable pipeline:
 *   mode 0 = none (identical to tonemap), 1 = colour invert,
 *   2 = scanlines, 3 = vignette boost.
 *
 * PostParams (set 0 binding 0):
 *   u_post_a = (exposure, gamma, tonemap mode, flip uv.y)
 *   u_post_b = (vignette, saturation, time, effect mode)
 * Scene colour sampled from set 0 binding 1.
 */
#include "common/genesis_shader.h"

GS_UBO(0, 0, PostParams)
{
    vec4 u_post_a;
    vec4 u_post_b;
};
GS_TEX2D(0, 1, u_scene_color);

GS_IN(0, vec2, v_uv);
GS_OUT(0, vec4, out_color);

vec3 acesFilm(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return gsSaturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

void main()
{
    vec2 uv = v_uv;
    if (u_post_a.w > 0.5)
        uv.y = 1.0 - uv.y;

    vec3 color = texture(u_scene_color, uv).rgb;
    color *= max(u_post_a.x, 0.0);

    int tonemap_mode = int(u_post_a.z + 0.5);
    if (tonemap_mode == 1)
        color = color / (color + vec3(1.0));
    else if (tonemap_mode == 2)
        color = acesFilm(color);
    else
        color = gsSaturate(color);

    // ---- programmable effect branch (u_post_b.w) -----------------------
    int effect = int(u_post_b.w + 0.5);
    if (effect == 1)
    {
        // Invert
        color = vec3(1.0) - color;
    }
    else if (effect == 2)
    {
        // Scanlines: darken every other ~2nd pixel row after gamma
        float row = floor(gl_FragCoord.y / 2.0);
        float darken = mod(row, 2.0) < 1.0 ? 0.75 : 1.0;
        color *= darken;
    }
    else if (effect == 3)
    {
        // Vignette boost
        vec2 d = v_uv - 0.5;
        color *= gsSaturate(1.0 - dot(d, d) * 1.6);
    }

    // Saturation
    float sat = max(u_post_b.y, 0.0);
    if (abs(sat - 1.0) > 1e-4)
    {
        float luma = gsLuminance(color);
        color = mix(vec3(luma), color, sat);
    }

    // Gamma encode
    float gamma = max(u_post_a.y, 0.1);
    color = pow(max(color, vec3(0.0)), vec3(1.0 / gamma));

    out_color = vec4(gsSaturate(color), 1.0);
}
