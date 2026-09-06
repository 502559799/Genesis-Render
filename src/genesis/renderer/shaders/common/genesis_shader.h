/*
 * Genesis Render - GLSL dialect header
 *
 * One GLSL source is authored for both backends.  scripts/build_shaders.py
 * prepends `#version` and defines exactly one of GENESIS_VK / GENESIS_GLSL
 * before inlining this header.
 *
 *   Vulkan : GLSL 450, explicit set/binding qualifiers, push constants.
 *   OpenGL : GLSL 410 (the macOS ceiling), no binding qualifiers.  The GL RHI
 *            backend binds uniform blocks by NAME to the deterministic index
 *            glUniformBlockIndex(set, binding) and assigns sampler units with
 *            glTextureUnit(set, binding) via glUniform1i at pipeline build time.
 *            Push constants are emulated by a std140 block named exactly
 *            `PushConstants` bound to uniform block index 0.
 *
 * Binding budget (kept inside the GL 4.1 minimum limits:
 * GL_MAX_UNIFORM_BUFFER_BINDINGS >= 36, GL_MAX_TEXTURE_IMAGE_UNITS >= 16):
 *
 *   set 0 (scene, bound once per frame)
 *     binding 0 : UBO   PerFrame          -> GL block 1
 *     binding 1 : UBO   Lights            -> GL block 2
 *     binding 2 : UBO   Environment       -> GL block 3
 *     binding 3 : cube  u_irradiance_map  -> GL unit 3
 *     binding 4 : cube  u_prefilter_map   -> GL unit 4
 *     binding 5 : 2D    u_brdf_lut        -> GL unit 5
 *   set 1 (material, bound per draw)
 *     binding 0 : UBO   Material          -> GL block 9
 *     binding 1 : 2D    u_base_color      -> GL unit 9
 *     binding 2 : 2D    u_metallic_roughness -> GL unit 10
 *     binding 3 : 2D    u_normal          -> GL unit 11
 *     binding 4 : 2D    u_occlusion       -> GL unit 12
 *     binding 5 : 2D    u_emissive        -> GL unit 13
 *   set 2 (per draw, dynamic offset)
 *     binding 0 : UBO   PerDraw           -> GL block 17
 */
#ifndef GENESIS_SHADER_COMMON_H
#define GENESIS_SHADER_COMMON_H

// NOTE: macro parameter names must NOT collide with GLSL layout qualifier
// keywords (`set`, `binding`, `location`), otherwise the preprocessor replaces
// the keyword too and produces `layout(location = 0)` -> `layout(0 = 0)`.
#ifdef GENESIS_VK
#define GS_UBO(gs_set, gs_binding, gs_block) layout(set = gs_set, binding = gs_binding, std140) uniform gs_block
#define GS_TEX2D(gs_set, gs_binding, gs_name) layout(set = gs_set, binding = gs_binding) uniform sampler2D gs_name
#define GS_TEXCUBE(gs_set, gs_binding, gs_name) layout(set = gs_set, binding = gs_binding) uniform samplerCube gs_name
#define GS_TEX2D_ARRAY(gs_set, gs_binding, gs_name) layout(set = gs_set, binding = gs_binding) uniform sampler2DArray gs_name
#define GS_PUSH(gs_block) layout(push_constant) uniform gs_block
#define GS_IN(gs_loc, gs_type, gs_name) layout(location = gs_loc) in gs_type gs_name
#define GS_OUT(gs_loc, gs_type, gs_name) layout(location = gs_loc) out gs_type gs_name
#else
#define GS_UBO(gs_set, gs_binding, gs_block) layout(std140) uniform gs_block
#define GS_TEX2D(gs_set, gs_binding, gs_name) uniform sampler2D gs_name
#define GS_TEXCUBE(gs_set, gs_binding, gs_name) uniform samplerCube gs_name
#define GS_TEX2D_ARRAY(gs_set, gs_binding, gs_name) uniform sampler2DArray gs_name
#define GS_PUSH(gs_block) layout(std140) uniform gs_block
#define GS_IN(gs_loc, gs_type, gs_name) layout(location = gs_loc) in gs_type gs_name
#define GS_OUT(gs_loc, gs_type, gs_name) layout(location = gs_loc) out gs_type gs_name
#endif

//----------------------------------------------------------------------
// Shared constants (mirrored in C++: renderer/render_ubo.hpp)
//----------------------------------------------------------------------
#define GS_MAX_LIGHTS 8
#define GS_LIGHT_DIRECTIONAL 0
#define GS_LIGHT_POINT 1
#define GS_LIGHT_SPOT 2

#define GS_ALPHA_OPAQUE 0
#define GS_ALPHA_MASK 1
#define GS_ALPHA_BLEND 2

// Material "texture present" bitmask (MaterialUBO.texture_mask.x)
#define GS_TEX_BASE_COLOR 1
#define GS_TEX_METAL_ROUGH 2
#define GS_TEX_NORMAL 4
#define GS_TEX_OCCLUSION 8
#define GS_TEX_EMISSIVE 16

//----------------------------------------------------------------------
// Small helpers used by several shaders
//----------------------------------------------------------------------
float gsSaturate(float v) { return clamp(v, 0.0, 1.0); }
vec2 gsSaturate(vec2 v) { return clamp(v, vec2(0.0), vec2(1.0)); }
vec3 gsSaturate(vec3 v) { return clamp(v, vec3(0.0), vec3(1.0)); }

float gsLuminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// sRGB -> linear (exact piecewise curve)
vec3 gsSrgbToLinear(vec3 c)
{
    vec3 lo = c / 12.92;
    vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
    return mix(hi, lo, lessThanEqual(c, vec3(0.04045)));
}

// linear -> sRGB
vec3 gsLinearToSrgb(vec3 c)
{
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(hi, lo, lessThanEqual(c, vec3(0.0031308)));
}

// Equirectangular UV <-> direction.
// Convention (matches scripts/gen_assets.py):
//   u = atan2(dir.z, dir.x) / (2*PI) + 0.5
//   v = acos(dir.y) / PI
vec2 gsDirectionToEquirectUV(vec3 dir)
{
    vec3 d = normalize(dir);
    float u = atan(d.z, d.x) / (2.0 * 3.14159265358979323846) + 0.5;
    float v = acos(clamp(d.y, -1.0, 1.0)) / 3.14159265358979323846;
    return vec2(u, v);
}

vec3 gsEquirectUVToDirection(vec2 uv)
{
    float phi = (uv.x - 0.5) * 2.0 * 3.14159265358979323846;
    float theta = uv.y * 3.14159265358979323846;
    return vec3(cos(phi) * sin(theta), cos(theta), sin(phi) * sin(theta));
}

// Cubemap face directions used by the IBL baker.  Order matches the engine's
// layer order: +X, -X, +Y, -Y, +Z, -Z.
vec3 gsCubeFaceDirection(int face, vec2 uv)
{
    // uv in [0,1]^2 with (0,0) at the top-left of the rendered quad.
    vec2 t = uv * 2.0 - 1.0; // -> [-1,1], y up after the flip below
    t.y = -t.y;
    if (face == 0) return normalize(vec3(1.0, t.y, -t.x));   // +X
    if (face == 1) return normalize(vec3(-1.0, t.y, t.x));   // -X
    if (face == 2) return normalize(vec3(t.x, 1.0, t.y));    // +Y
    if (face == 3) return normalize(vec3(t.x, -1.0, -t.y));  // -Y
    if (face == 4) return normalize(vec3(t.x, t.y, 1.0));    // +Z
    return normalize(vec3(-t.x, t.y, -1.0));                 // -Z
}

#endif // GENESIS_SHADER_COMMON_H
