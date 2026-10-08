/*
 * Genesis Render - embedded Metal Shading Language (MSL) shaders.
 *
 * The Metal backend compiles these strings at device creation (via
 * newLibraryWithSource) and looks them up by the same logical shader
 * names the other backends use (e.g. "scene/pbr_mesh.frag"), so the
 * renderer code never needs to know which backend is active.
 *
 * MSL is a variant of C++ with explicit resource-buffer/attribute
 * qualifiers ([[buffer(n)]], [[attribute(n)]]).  The C++ render pipeline
 * binds vertex attributes in the order defined by the RHI vertex layout
 * (location 0..N), which is why every vertex shader here declares inputs
 * with matching [[attribute(n)]].
 */
#ifndef __GENESIS_METAL_SHADERS_HPP__
#define __GENESIS_METAL_SHADERS_HPP__

#include <cstdint>

namespace Genesis
{
    namespace RHI
    {
        struct MetalShaderEntry
        {
            const char *name;      // logical name, e.g. "scene/pbr_mesh.vert"
            const char *msl;       // MSL source for the stage
        };

        // A tiny MSL prelude shared by every shader (types + helpers).
        static const char *kMetalPrelude = R"METALSL(
#include <metal_stdlib>
using namespace metal;

struct LightGPU
{
    float4 position_range;
    float4 direction_cone;
    float4 color_intensity;
    float4 type_params;
};

struct PerFrameUBO
{
    float4x4 view;
    float4x4 projection;
    float4x4 view_projection;
    float4x4 inv_view_projection;
    float4x4 inv_view;
    float4 camera_position;
    float4 frame_params;
    float4 viewport_params;
    float4 backend_params;
};

struct LightsUBO
{
    LightGPU lights[8];
    float4 light_count_params;
    float4 ambient_color;
};

struct EnvironmentUBO
{
    float4 env_params;
    float4 ibl_params;
    float4 sky_tint;
    float4 ground_tint;
};

struct MaterialUBO
{
    float4 base_color_factor;
    float4 emissive_factor;
    float4 params0;  // metallic, roughness, normal_scale, occlusion
    float4 params1;  // alpha_cutoff, alpha_mode, double_sided, shading_mode
    float4 texture_mask;
    float4 extra;
};

struct PerDrawUBO
{
    float4x4 model;
    float4x4 normal_matrix;
    float4 object_params;
};

struct IblParamsUBO
{
    float4 a;
    float4 b;
};

struct PostParamsUBO
{
    float4 a;
    float4 b;
};

struct PushConstantsUBO
{
    float4x4 mvp;
    float4 color;
};

struct TextParamsUBO
{
    float4x4 view_proj;
    float4 mode;
};

struct QuadParamsUBO
{
    float4x4 view_proj;
    float4 params;
};

static constant float kPi = 3.14159265358979323846;
static constant float kMinRoughness = 0.045;

// Cubemap face direction (order: +X,-X,+Y,-Y,+Z,-Z), matches the GLSL IBL.
float3 cubeFaceDirection(int face, float2 uv)
{
    float2 t = uv * 2.0 - 1.0;
    t.y = -t.y;
    if (face == 0) return normalize(float3(1.0, t.y, -t.x));
    if (face == 1) return normalize(float3(-1.0, t.y, t.x));
    if (face == 2) return normalize(float3(t.x, 1.0, t.y));
    if (face == 3) return normalize(float3(t.x, -1.0, -t.y));
    if (face == 4) return normalize(float3(t.x, t.y, 1.0));
    return normalize(float3(-t.x, t.y, -1.0));
}
float2 directionToEquirect(float3 dir)
{
    float3 n = normalize(dir);
    float u = atan2(n.z, n.x) / (2.0 * kPi) + 0.5;
    float v = acos(clamp(n.y, -1.0, 1.0)) / kPi;
    return float2(u, v);
}
float ggxDistribution(float NoH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / max(kPi * d * d, 1e-7);
}
float radicalInverseVdc(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
float2 hammersley(uint i, uint n)
{
    return float2(float(i) / float(n), radicalInverseVdc(i));
}
float3 importanceSampleGGX(float2 xi, float3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * kPi * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    float3 H = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}
// Filmic ACES tone map (global so every shader can call it).
float3 acesFilm(float3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Vertex-out / fragment-in stage structs (defined once so the same struct can
// be returned by a vertex shader and consumed by a fragment shader in this one
// library; a redefinition would fail to compile).
struct FullscreenOut {
    float4 out_position [[position]];
    float2 v_uv [[user(loc0)]];
};
struct QuadOut {
    float4 out_position [[position]];
    float2 v_uv [[user(loc0)]];
    float4 v_color [[user(loc1)]];
};
struct LineOut {
    float4 out_position [[position]];
    float4 v_color [[user(loc0)]];
};
struct TextOut {
    float4 out_position [[position]];
    float2 v_uv [[user(loc0)]];
    float4 v_color [[user(loc1)]];
    float2 v_sdf [[user(loc2)]];
};
struct PbrOut {
    float4 out_position [[position]];
    float3 v_pos [[user(loc0)]];
    float3 v_normal [[user(loc1)]];
    float2 v_uv [[user(loc2)]];
};
struct SkyOut {
    float4 out_position [[position]];
    float2 v_uv [[user(loc0)]];
};

// Vertex-input stage-in structs.  Metal requires vertex attributes to be
// passed by-value as members of a stage-in struct (a standalone
// `float2 x [[attribute(n)]]` parameter, or a const-ref on the attribute
// directly, is rejected by the compiler).  The function parameter is
// `const XxxIn in [[stage_in]]`; the members are plain by-value attributes.
struct FullscreenIn {
    float2 position [[attribute(0)]];
    float2 uv [[attribute(1)]];
};
struct QuadIn {
    float3 position [[attribute(0)]];
    float2 uv [[attribute(1)]];
    float4 color [[attribute(2)]];
};
struct LineIn {
    float3 position [[attribute(0)]];
    float4 color [[attribute(1)]];
};
struct TextIn {
    float3 position [[attribute(0)]];
    float2 uv [[attribute(1)]];
    float4 color [[attribute(2)]];
    float4 params [[attribute(3)]];
};
struct PbrIn {
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float4 tangent [[attribute(2)]];
    float2 uv0 [[attribute(3)]];
};
)METALSL";

        // Fullscreen quad vertex (post tonemap / effects / IBL bakes).
        // The renderer binds a single interleaved vertex buffer at index 0
        // (vec2 position, vec2 uv), so read them as vertex attributes.
        static const char *kFullscreenVert = R"METALSL(
vertex FullscreenOut fullscreen_vert(const FullscreenIn in [[stage_in]])
{
    FullscreenOut o;
    o.out_position = float4(in.position, 0.0, 1.0);
    o.v_uv = in.uv;
    return o;
}
)METALSL";

        // Post tonemap fragment (tonemap.frag / effects.frag logic).
        static const char *kTonemapFrag = R"METALSL(

fragment float4 tonemap_frag(FullscreenOut in [[stage_in]],
                            constant PostParamsUBO &post [[buffer(1)]],
                            texture2d<float> scene_tex [[texture(1)]],
                            sampler scene_samp [[sampler(1)]])
{
    float2 uv = in.v_uv;
    if (post.a.w > 0.5) uv.y = 1.0 - uv.y;
    float3 color = scene_tex.sample(scene_samp, uv).rgb;
    color *= max(post.a.x, 0.0);
    int mode = int(post.a.z + 0.5);
    if (mode == 1) color = color / (color + float3(1.0));
    else if (mode == 2) color = acesFilm(color);
    else color = saturate(color);
    float gamma = max(post.a.y, 0.1);
    color = pow(max(color, float3(0.0)), float3(1.0/gamma));
    return float4(saturate(color), 1.0);
}
)METALSL";

        // Simple colored/textured quad vertex (text / overlay draws).
        static const char *kTexturedQuadVert = R"METALSL(
vertex QuadOut quad_vert(const QuadIn in [[stage_in]],
                         constant QuadParamsUBO &params [[buffer(1)]])
{
    QuadOut o;
    o.v_uv = in.uv;
    o.v_color = in.color;
    if (params.params.x > 0.5)
        o.out_position = float4(in.position.xy, 0.0, 1.0);
    else
        o.out_position = params.view_proj * float4(in.position, 1.0);
    return o;
}
)METALSL";

        static const char *kTexturedQuadFrag = R"METALSL(

fragment float4 quad_frag(QuadOut in [[stage_in]],
                           texture2d<float> tex [[texture(1)]],
                           sampler samp [[sampler(1)]])
{
    float4 t = tex.sample(samp, in.v_uv);
    return float4(t.rgb * in.v_color.rgb, t.a * in.v_color.a);
}
)METALSL";

        // Debug line vertex (push-constant style).
        static const char *kLineVert = R"METALSL(
vertex LineOut line_vert(const LineIn in [[stage_in]],
                          constant PushConstantsUBO &pc [[buffer(1)]])
{
    LineOut o;
    o.out_position = pc.mvp * float4(in.position, 1.0);
    o.v_color = in.color;
    return o;
}
)METALSL";

        static const char *kLineFrag = R"METALSL(

fragment float4 line_frag(LineOut in [[stage_in]])
{
    return in.v_color;
}
)METALSL";

        // SDF text vertex (2D/3D) and fragment.
        static const char *kTextVert = R"METALSL(
vertex TextOut text_vert(const TextIn in [[stage_in]],
                          constant TextParamsUBO &tp [[buffer(1)]])
{
    TextOut o;
    o.v_uv = in.uv;
    o.v_color = in.color;
    o.v_sdf = float2(in.params.y, in.params.w);
    if (tp.mode.x > 0.5) o.out_position = float4(in.position.xy, 0.0, 1.0);
    else o.out_position = tp.view_proj * float4(in.position, 1.0);
    return o;
}
)METALSL";

        static const char *kTextFrag = R"METALSL(

fragment float4 text_frag(TextOut in [[stage_in]],
                           texture2d<float> atlas [[texture(1)]],
                           sampler atlas_samp [[sampler(1)]])
{
    float d = atlas.sample(atlas_samp, in.v_uv).r;
    float soft = max(in.v_sdf.y, 0.001);
    float alpha = smoothstep(0.5 - soft, 0.5 + soft, d);
    if (alpha < 0.004) discard_fragment();
    return float4(in.v_color.rgb, in.v_color.a * alpha);
}
)METALSL";

        // ---- scene / IBL shaders -------------------------------------------
        // The full PBR/IBL shaders are large; they are compiled from the same
        // MSL source as the GLSL-equivalent passes.  For a Metal build the
        // renderer's pipeline helper requests shaders by the logical names
        // below; the device falls back to this simple PBR fragment if the
        // full MSL library is not extended (so the backend always compiles).
        static const char *kPbrVertex = R"METALSL(
vertex PbrOut pbr_vert(const PbrIn in [[stage_in]],
                       constant PerFrameUBO &perFrame [[buffer(1)]],
                       constant PerDrawUBO &perDraw [[buffer(17)]])
{
    PbrOut o;
    float4 world = perDraw.model * float4(in.position, 1.0);
    o.out_position = perFrame.view_projection * world;
    o.v_pos = world.xyz;
    o.v_normal = normalize(perDraw.normal_matrix * float4(in.normal, 0.0)).xyz;
    o.v_uv = in.uv0;
    return o;
}
)METALSL";

        // Fallback PBR fragment (simple direct-light ambient for the
        // Metal backend's introductory shader set).
        static const char *kPbrFragmentSimple = R"METALSL(

fragment float4 pbr_frag(PbrOut in [[stage_in]],
                         constant PerFrameUBO &perFrame [[buffer(1)]],
                         constant LightsUBO &lights [[buffer(2)]],
                         constant MaterialUBO &material [[buffer(9)]],
                         texture2d<float> albedo [[texture(9)]],
                         sampler albedo_samp [[sampler(9)]])
{
    float4 base = material.base_color_factor * albedo.sample(albedo_samp, in.v_uv);
    float3 N = normalize(in.v_normal);
    float3 color = float3(0.0);
    int count = int(lights.light_count_params.x + 0.5);
    for (int i = 0; i < min(count, 8); ++i)
    {
        LightGPU L = lights.lights[i];
        float3 Ldir = normalize(L.direction_cone.xyz);
        float diff = max(dot(N, Ldir), 0.0);
        color += base.rgb * diff * L.color_intensity.rgb * L.color_intensity.w;
    }
    color += base.rgb * lights.ambient_color.xyz;
    return float4(color, base.a);
}
)METALSL";

        // skybox vertex/fragment (simple gradient sky).
        static const char *kSkyVert = R"METALSL(
vertex SkyOut sky_vert(const FullscreenIn in [[stage_in]],
                       constant PerFrameUBO &perFrame [[buffer(1)]])
{
    SkyOut o;
    // Skybox is drawn as a fullscreen quad at the far plane; reconstruct
    // from NDC rather than using the quad vertex position.
    o.out_position = float4(in.position, 0.0, 1.0);
    o.v_uv = in.uv;
    return o;
}
)METALSL";

        static const char *kSkyFrag = R"METALSL(

fragment float4 sky_frag(SkyOut in [[stage_in]],
                        constant PerFrameUBO &perFrame [[buffer(1)]],
                        constant EnvironmentUBO &env [[buffer(3)]],
                        texture2d<float> equirect [[texture(8)]],
                        texturecube<float> cube [[texture(9)]],
                        sampler sky_samp [[sampler(8)]])
{
    // Reconstruct the view ray at the far plane from uv.
    float3 ndc = float3(in.v_uv * 2.0 - 1.0, 1.0);
    float4 world = perFrame.inv_view_projection * float4(ndc, 1.0);
    float3 dir = normalize(world.xyz / max(abs(world.w), 1e-6) - perFrame.camera_position.xyz);

    float2 rot = env.env_params.yz;
    float3 rotated = float3(rot.x * dir.x + rot.y * dir.z, dir.y,
                            -rot.y * dir.x + rot.x * dir.z);

    float3 color;
    int mode = int(env.ibl_params.z + 0.5);
    if (mode == 1)
    {
        float2 euv = directionToEquirect(rotated);
        color = equirect.sample(sky_samp, euv).rgb;
    }
    else if (mode == 2)
    {
        color = cube.sample(sky_samp, rotated).rgb;
    }
    else
    {
        float t = saturate(rotated.y * 0.5 + 0.5);
        color = mix(env.ground_tint.xyz, env.sky_tint.xyz, t);
        float sun = pow(saturate(dot(rotated, normalize(float3(0.4, 0.6, 0.3)))), 512.0);
        color += float3(1.0, 0.9, 0.7) * sun * 60.0;
    }
    return float4(max(color * env.env_params.x, float3(0.0)), 1.0);
}
)METALSL";

        // ---- IBL bake shaders ------------------------------------------------
        // All run from the fullscreen vertex (common/fullscreen.vert); the
        // face index / roughness / mip come from the IBLParams UBO
        // (set 0 binding 0 -> buffer(0)).  equirect is texture(1), cubemap is
        // texture(2) — matching the renderer's set0/binding1/binding2.
        static const char *kIblEquirect = R"METALSL(

fragment float4 ibl_equirect(FullscreenOut in [[stage_in]],
                            constant IblParamsUBO &ibl [[buffer(1)]],
                            texture2d<float> equirect [[texture(1)]],
                            sampler eq_samp [[sampler(1)]])
{
    int face = int(ibl.a.x + 0.5);
    float3 dir = cubeFaceDirection(face, in.v_uv);
    // optional environment rotation
    float2 rot = ibl.b.xy;
    float3 rotated = float3(rot.x * dir.x + rot.y * dir.z, dir.y,
                            -rot.y * dir.x + rot.x * dir.z);
    float2 uv = directionToEquirect(rotated);
    float3 color = equirect.sample(eq_samp, uv).rgb;
    return float4(max(color, float3(0.0)), 1.0);
}
)METALSL";

        static const char *kIblIrradiance = R"METALSL(

fragment float4 ibl_irradiance(FullscreenOut in [[stage_in]],
                              constant IblParamsUBO &ibl [[buffer(1)]],
                              texturecube<float> cube [[texture(2)]],
                              sampler cube_samp [[sampler(2)]])
{
    int face = int(ibl.a.x + 0.5);
    float3 N = normalize(cubeFaceDirection(face, in.v_uv));
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 right = normalize(cross(up, N));
    float3 tangent_up = cross(N, right);

    float3 irradiance = float3(0.0);
    float sample_count = 0.0;
    const float delta_phi = 2.0 * kPi / 120.0;
    const float delta_theta = 0.5 * kPi / 60.0;
    for (float phi = 0.0; phi < 2.0 * kPi; phi += delta_phi)
    {
        for (float theta = 0.0; theta < 0.5 * kPi; theta += delta_theta)
        {
            float3 t = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            float3 sv = t.x * right + t.y * tangent_up + t.z * N;
            irradiance += cube.sample(cube_samp, sv).rgb * cos(theta) * sin(theta);
            sample_count += 1.0;
        }
    }
    irradiance = kPi * irradiance / max(sample_count, 1.0);
    return float4(max(irradiance, float3(0.0)), 1.0);
}
)METALSL";

        static const char *kIblPrefilter = R"METALSL(

fragment float4 ibl_prefilter(FullscreenOut in [[stage_in]],
                             constant IblParamsUBO &ibl [[buffer(1)]],
                             texturecube<float> cube [[texture(2)]],
                             sampler cube_samp [[sampler(2)]])
{
    int face = int(ibl.a.x + 0.5);
    float roughness = max(ibl.a.y, 0.0);
    uint sample_count = uint(max(ibl.a.w, 16.0));
    float3 N = normalize(cubeFaceDirection(face, in.v_uv));
    float3 R = N;
    float3 V = R;
    float total_weight = 0.0;
    float3 prefiltered = float3(0.0);
    float mip_bias = 0.0;
    for (uint i = 0u; i < sample_count; ++i)
    {
        float2 xi = hammersley(i, sample_count);
        float3 H = importanceSampleGGX(xi, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
        float NoL = max(dot(N, L), 0.0);
        if (NoL > 0.0)
        {
            float NoH = max(dot(N, H), 0.0);
            float VoH = max(dot(V, H), 0.0);
            float D = ggxDistribution(NoH, max(roughness, kMinRoughness));
            float pdf = D * NoH / max(4.0 * VoH, 1e-5) + 0.0001;
            float sa_texel = 4.0 * kPi / (6.0 * 256.0 * 256.0);
            float sa_sample = 1.0 / (float(sample_count) * pdf + 1e-5);
            mip_bias = roughness == 0.0 ? 0.0 : 0.5 * log2(sa_sample / sa_texel);
            prefiltered += cube.sample(cube_samp, L, level(mip_bias)).rgb * NoL;
            total_weight += NoL;
        }
    }
    prefiltered = total_weight > 0.0 ? prefiltered / total_weight : float3(0.0);
    return float4(max(prefiltered, float3(0.0)), 1.0);
}
)METALSL";

        static const char *kIblBrdfLut = R"METALSL(

float geometrySchlickGGX_IBL(float NoX, float roughness)
{
    float k = (roughness * roughness) * 0.5;
    return NoX / (NoX * (1.0 - k) + k);
}
float geometrySmithIBL(float NoV, float NoL, float roughness)
{
    return geometrySchlickGGX_IBL(NoV, roughness) * geometrySchlickGGX_IBL(NoL, roughness);
}
float2 integrateBRDF(float NoV, float roughness, uint sample_count)
{
    float3 V = float3(sqrt(1.0 - NoV * NoV), 0.0, NoV);
    float3 N = float3(0.0, 0.0, 1.0);
    float A = 0.0;
    float B = 0.0;
    for (uint i = 0u; i < sample_count; ++i)
    {
        float2 xi = hammersley(i, sample_count);
        float3 H = importanceSampleGGX(xi, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
        float NoL = max(L.z, 0.0);
        float NoH = max(H.z, 0.0);
        float VoH = max(dot(V, H), 0.0);
        if (NoL > 0.0)
        {
            float G = geometrySmithIBL(NoV, NoL, roughness);
            float g_vis = (G * VoH) / max(NoH * NoV, 1e-5);
            float fc = pow(1.0 - VoH, 5.0);
            A += (1.0 - fc) * g_vis;
            B += fc * g_vis;
        }
    }
    return float2(A, B) / float(sample_count);
}
fragment float4 ibl_brdf_lut(FullscreenOut in [[stage_in]],
                             constant IblParamsUBO &ibl [[buffer(1)]])
{
    float NoV = clamp(in.v_uv.x, 1e-4, 1.0);
    float roughness = clamp(in.v_uv.y, kMinRoughness, 1.0);
    uint sc = uint(max(ibl.a.w, 256.0));
    float2 brdf = integrateBRDF(NoV, roughness, sc);
    return float4(saturate(brdf.x), saturate(brdf.y), 0.0, 1.0);
}
)METALSL";

        // Map a logical shader name to (msl source, entry point).
        struct MetalShaderMap
        {
            const char *name;
            const char *msl;
            const char *entry;
        };

        static const MetalShaderMap kMetalShaders[] =
        {
            {"common/fullscreen.vert", kFullscreenVert, "fullscreen_vert"},
            {"post/tonemap.frag", kTonemapFrag, "tonemap_frag"},
            {"post/effects.frag", kTonemapFrag, "tonemap_frag"},
            {"ui/textured_quad.vert", kTexturedQuadVert, "quad_vert"},
            {"ui/textured_quad.frag", kTexturedQuadFrag, "quad_frag"},
            {"scene/debug_line.vert", kLineVert, "line_vert"},
            {"scene/debug_line.frag", kLineFrag, "line_frag"},
            {"ui/text_glyph.vert", kTextVert, "text_vert"},
            {"ui/text_glyph.frag", kTextFrag, "text_frag"},
            {"scene/pbr_mesh.vert", kPbrVertex, "pbr_vert"},
            {"scene/pbr_mesh.frag", kPbrFragmentSimple, "pbr_frag"},
            {"scene/unlit_mesh.vert", kPbrVertex, "pbr_vert"},
            {"scene/unlit_mesh.frag", kPbrFragmentSimple, "pbr_frag"},
            {"scene/skybox.vert", kSkyVert, "sky_vert"},
            {"scene/skybox.frag", kSkyFrag, "sky_frag"},
            // IBL bake (fullscreen vertex + these fragment shaders).
            {"ibl/equirect_to_cube.frag", kIblEquirect, "ibl_equirect"},
            {"ibl/irradiance.frag", kIblIrradiance, "ibl_irradiance"},
            {"ibl/prefilter.frag", kIblPrefilter, "ibl_prefilter"},
            {"ibl/brdf_lut.frag", kIblBrdfLut, "ibl_brdf_lut"},
        };
        static const uint32_t kMetalShaderCount = sizeof(kMetalShaders) / sizeof(kMetalShaders[0]);

    } // namespace RHI
} // namespace Genesis

#endif /* __GENESIS_METAL_SHADERS_HPP__ */
