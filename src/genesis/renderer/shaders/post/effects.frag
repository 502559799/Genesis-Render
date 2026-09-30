/*
 * Genesis Render - fullscreen post processing / blit pass.
 *
 * One shader serves both the built-in present pass (HDR scene -> display with
 * tone mapping + the configured effect) and every CommandBuffer::blit()
 * (arbitrary render texture -> render texture / backbuffer with an effect).
 *
 * u_post_d.z selects whether the source is HDR and must be exposed, tone
 * mapped and gamma corrected (1) or is already display referred (0).
 */
#include "common/genesis_shader.h"

GS_UBO(0, 0, PostParams)
{
    vec4 u_post_a; // x = exposure, y = gamma, z = tonemap mode (0 none / 1 Reinhard / 2 ACES), w = flip uv.y
    vec4 u_post_b; // x = vignette, y = saturation, z = time (s), w = effect id
    vec4 u_post_c; // effect parameters: x = strength, y = radius / size (px), z, w = extra
    vec4 u_post_d; // xy = source texel size, z = apply tone mapping (0/1), w = unused
    vec4 u_tint;   // colour parameter (tint / edge colour)
};

GS_TEX2D(0, 1, u_scene_color);

GS_IN(0, vec2, v_uv);
GS_OUT(0, vec4, out_color);

#define FX_NONE 0
#define FX_INVERT 1
#define FX_GRAYSCALE 2
#define FX_SEPIA 3
#define FX_BLUR 4
#define FX_EDGE 5
#define FX_PIXELATE 6
#define FX_VIGNETTE 7
#define FX_SCANLINE 8
#define FX_CHROMATIC 9
#define FX_TINT 10
#define FX_POSTERIZE 11

vec3 acesFilm(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return gsSaturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Source colour -> display referred linear colour in [0,1] (before gamma).
vec3 toDisplayLinear(vec3 color)
{
    if (u_post_d.z < 0.5)
        return color;
    color *= max(u_post_a.x, 0.0);
    int mode = int(u_post_a.z + 0.5);
    if (mode == 1)
        color = color / (color + vec3(1.0));
    else if (mode == 2)
        color = acesFilm(color);
    else
        color = gsSaturate(color);
    return color;
}

vec3 fetchColor(vec2 uv)
{
    return toDisplayLinear(texture(u_scene_color, uv).rgb);
}

void main()
{
    vec2 uv = v_uv;
    if (u_post_a.w > 0.5)
        uv.y = 1.0 - uv.y;

    int effect = int(u_post_b.w + 0.5);
    vec2 texel = u_post_d.xy;
    float strength = u_post_c.x;
    vec4 source = texture(u_scene_color, uv);
    vec3 color = toDisplayLinear(source.rgb);

    if (effect == FX_INVERT)
    {
        color = mix(color, vec3(1.0) - color, gsSaturate(strength));
    }
    else if (effect == FX_GRAYSCALE)
    {
        color = mix(color, vec3(gsLuminance(color)), gsSaturate(strength));
    }
    else if (effect == FX_SEPIA)
    {
        vec3 sepia = vec3(dot(color, vec3(0.393, 0.769, 0.189)),
                          dot(color, vec3(0.349, 0.686, 0.168)),
                          dot(color, vec3(0.272, 0.534, 0.131)));
        color = mix(color, gsSaturate(sepia), gsSaturate(strength));
    }
    else if (effect == FX_BLUR)
    {
        // Separable-equivalent 9x9 Gaussian approximated with 25 bilinear taps.
        float radius = max(u_post_c.y, 0.5);
        vec3 sum = vec3(0.0);
        float wsum = 0.0;
        for (int y = -2; y <= 2; ++y)
        {
            for (int x = -2; x <= 2; ++x)
            {
                float w = exp(-0.5 * float(x * x + y * y) / 2.0);
                sum += fetchColor(uv + vec2(float(x), float(y)) * texel * radius) * w;
                wsum += w;
            }
        }
        color = mix(color, sum / wsum, gsSaturate(strength));
    }
    else if (effect == FX_EDGE)
    {
        float r = max(u_post_c.y, 1.0);
        float tl = gsLuminance(fetchColor(uv + vec2(-1.0, -1.0) * texel * r));
        float tc = gsLuminance(fetchColor(uv + vec2(0.0, -1.0) * texel * r));
        float tr = gsLuminance(fetchColor(uv + vec2(1.0, -1.0) * texel * r));
        float ml = gsLuminance(fetchColor(uv + vec2(-1.0, 0.0) * texel * r));
        float mr = gsLuminance(fetchColor(uv + vec2(1.0, 0.0) * texel * r));
        float bl = gsLuminance(fetchColor(uv + vec2(-1.0, 1.0) * texel * r));
        float bc = gsLuminance(fetchColor(uv + vec2(0.0, 1.0) * texel * r));
        float br = gsLuminance(fetchColor(uv + vec2(1.0, 1.0) * texel * r));
        float gx = -tl - 2.0 * ml - bl + tr + 2.0 * mr + br;
        float gy = -tl - 2.0 * tc - tr + bl + 2.0 * bc + br;
        float edge = gsSaturate(length(vec2(gx, gy)) * max(strength, 0.0));
        color = mix(color * 0.25, u_tint.rgb, edge);
    }
    else if (effect == FX_PIXELATE)
    {
        vec2 cell = texel * max(u_post_c.y, 1.0);
        vec2 snapped = (floor(uv / cell) + 0.5) * cell;
        color = fetchColor(snapped);
    }
    else if (effect == FX_VIGNETTE)
    {
        vec2 d = v_uv - 0.5;
        color *= gsSaturate(1.0 - dot(d, d) * 2.5 * max(strength, 0.0));
    }
    else if (effect == FX_SCANLINE)
    {
        float lines = max(u_post_c.y, 1.0);
        float row = floor(gl_FragCoord.y / lines);
        float darken = mod(row, 2.0) < 1.0 ? 1.0 - gsSaturate(strength) : 1.0;
        color *= darken;
    }
    else if (effect == FX_CHROMATIC)
    {
        vec2 dir = (v_uv - 0.5) * texel * max(u_post_c.y, 1.0) * 8.0;
        color = vec3(fetchColor(uv + dir).r, color.g, fetchColor(uv - dir).b);
    }
    else if (effect == FX_TINT)
    {
        color = mix(color, color * u_tint.rgb, gsSaturate(strength));
    }
    else if (effect == FX_POSTERIZE)
    {
        float levels = max(u_post_c.y, 2.0);
        color = floor(color * levels + 0.5) / levels;
    }

    // Built-in grading (present pass): saturation + vignette.
    float sat = max(u_post_b.y, 0.0);
    if (abs(sat - 1.0) > 1e-4)
        color = mix(vec3(gsLuminance(color)), color, sat);
    float vignette = max(u_post_b.x, 0.0);
    if (vignette > 1e-4)
    {
        vec2 d = v_uv - 0.5;
        color *= gsSaturate(1.0 - dot(d, d) * vignette * 2.0);
    }

    if (u_post_d.z > 0.5)
    {
        float gamma = max(u_post_a.y, 0.1);
        color = pow(max(color, vec3(0.0)), vec3(1.0 / gamma));
        out_color = vec4(gsSaturate(color), 1.0);
    }
    else
    {
        out_color = vec4(max(color, vec3(0.0)), source.a);
    }
}
