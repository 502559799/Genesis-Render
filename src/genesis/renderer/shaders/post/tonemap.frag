/*
 * Genesis Render - post processing: exposure, tone mapping, gamma
 *
 * PostParams (set 0 binding 0):
 *   u_post_a = (exposure, gamma, tonemap mode, flip uv.y)
 *   u_post_b = (vignette strength, saturation, time, unused)
 * Scene colour is sampled from set 0 binding 1.
 *
 * Tone map modes: 0 = none (clamp), 1 = Reinhard, 2 = ACES filmic (Narkowicz)
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

    // Exposure (linear space, before tone mapping)
    color *= max(u_post_a.x, 0.0);

    int mode = int(u_post_a.z + 0.5);
    if (mode == 1)
        color = color / (color + vec3(1.0));
    else if (mode == 2)
        color = acesFilm(color);
    else
        color = gsSaturate(color);

    // Saturation
    float sat = max(u_post_b.y, 0.0);
    if (abs(sat - 1.0) > 1e-4)
    {
        float luma = gsLuminance(color);
        color = mix(vec3(luma), color, sat);
    }

    // Vignette
    float vignette = max(u_post_b.x, 0.0);
    if (vignette > 1e-4)
    {
        vec2 d = uv - 0.5;
        float f = gsSaturate(1.0 - dot(d, d) * vignette * 2.0);
        color *= f;
    }

    // Gamma encode
    float gamma = max(u_post_a.y, 0.1);
    color = pow(max(color, vec3(0.0)), vec3(1.0 / gamma));

    out_color = vec4(gsSaturate(color), 1.0);
}
