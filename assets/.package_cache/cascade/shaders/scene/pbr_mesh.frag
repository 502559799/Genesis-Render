/*
 * Genesis Render - PBR mesh pass (fragment)
 *
 * Direct light : Cook-Torrance GGX *or* Blinn-Phong (selected per material via
 *                u_material_params1.w), both with Schlick Fresnel, Smith
 *                geometry and the metallic-roughness workflow.
 * Indirect light: split-sum IBL
 *                - diffuse  : irradiance cubemap sampled with N
 *                - specular : prefiltered cubemap sampled with R at a
 *                             roughness derived mip
 *                - BRDF     : integration LUT (scale, bias)
 *                When no IBL is bound the shader falls back to a procedural
 *                gradient sky so that the scene never renders pure black.
 *
 * Colour textures (base colour / emissive) are uploaded as sRGB formats, so
 * the hardware already returns linear values here.
 */
#include "common/genesis_shader.h"
#include "common/ubo_layout.h"
#include "common/pbr.h"

GS_IN(0, vec3, v_world_position);
GS_IN(1, vec3, v_normal);
GS_IN(2, vec4, v_tangent);
GS_IN(3, vec2, v_uv0);
GS_IN(4, vec2, v_uv1);

GS_OUT(0, vec4, out_color);

GS_TEXCUBE(0, 3, u_irradiance_map);
GS_TEXCUBE(0, 4, u_prefilter_map);
GS_TEX2D(0, 5, u_brdf_lut);
GS_TEX2D(1, 1, u_base_color);
GS_TEX2D(1, 2, u_metallic_roughness);
GS_TEX2D(1, 3, u_normal);
GS_TEX2D(1, 4, u_occlusion);
GS_TEX2D(1, 5, u_emissive);

vec3 gsProceduralSky(vec3 dir)
{
    float t = gsSaturate(dir.y * 0.5 + 0.5);
    vec3 sky = mix(u_ground_tint.rgb, u_sky_tint.rgb, t);
    // Cheap sun disc so that reflections have a highlight even without IBL.
    float sun = pow(gsSaturate(dot(normalize(dir), normalize(vec3(0.4, 0.6, 0.3)))), 256.0);
    return sky * u_env_params.x + vec3(sun * 8.0);
}

void main()
{
    //------------------------------------------------------------------
    // Material gathering
    //------------------------------------------------------------------
    vec4 base_color = u_base_color_factor;
    if (u_texture_mask.x > 0.5)
        base_color *= texture(u_base_color, v_uv0);

    float metallic = u_material_params0.x;
    float roughness = u_material_params0.y;
    if (u_texture_mask.y > 0.5)
    {
        // glTF convention: G = roughness, B = metallic
        vec4 mr = texture(u_metallic_roughness, v_uv0);
        metallic *= mr.b;
        roughness *= mr.g;
    }

    float occlusion = 1.0;
    if (u_texture_mask.w > 0.5)
    {
        float ao = texture(u_occlusion, v_uv0).r;
        occlusion = mix(1.0, ao, u_material_params0.w);
    }

    vec3 emissive = u_emissive_factor.rgb * u_emissive_factor.a;
    if (u_material_extra.x > 0.5)
        emissive *= texture(u_emissive, v_uv0).rgb;

    // Alpha handling ---------------------------------------------------
    float alpha = base_color.a;
    int alpha_mode = int(u_material_params1.y + 0.5);
    if (alpha_mode == GS_ALPHA_MASK && alpha < u_material_params1.x)
        discard;

    //------------------------------------------------------------------
    // Geometry
    //------------------------------------------------------------------
    vec3 N = normalize(v_normal);
    if (u_texture_mask.z > 0.5)
    {
        vec3 T = normalize(v_tangent.xyz - N * dot(N, v_tangent.xyz));
        vec3 B = cross(N, T) * v_tangent.w;
        vec3 n_ts = texture(u_normal, v_uv0).xyz * 2.0 - 1.0;
        n_ts.xy *= u_material_params0.z;
        N = normalize(mat3(T, B, N) * n_ts);
    }
    vec3 V = normalize(u_camera_position.xyz - v_world_position);
    // Two sided materials flip the normal towards the viewer.
    if (u_material_params1.z > 0.5 && dot(N, V) < 0.0)
        N = -N;

    int shading_model = int(u_material_params1.w + 0.5);
    if (shading_model == GS_SHADING_UNLIT)
    {
        // Unlit: base colour (x texture) + emissive, no lights, no IBL.
        out_color = vec4(max(base_color.rgb * occlusion + emissive, vec3(0.0)), alpha);
        return;
    }

    PbrSurface surface = gsMakeSurface(N, V, base_color.rgb, metallic, roughness,
                                       occlusion, emissive, shading_model);

    vec3 color = vec3(0.0);

    //------------------------------------------------------------------
    // Direct lights
    //------------------------------------------------------------------
    int light_count = int(u_light_count_params.x + 0.5);
    for (int i = 0; i < GS_MAX_LIGHTS; ++i)
    {
        if (i >= light_count)
            break;
        // Index the array through constant-index branches.  Dynamically
        // indexing a struct array inside a uniform block is legal GLSL, but it
        // is a known weak spot of software rasterisers (lavapipe returned zero
        // for every field), and the switch costs nothing after unrolling.
        GpuLight light = u_lights[0];
        if (i == 1) light = u_lights[1];
        else if (i == 2) light = u_lights[2];
        else if (i == 3) light = u_lights[3];
        else if (i == 4) light = u_lights[4];
        else if (i == 5) light = u_lights[5];
        else if (i == 6) light = u_lights[6];
        else if (i == 7) light = u_lights[7];
        if (light.type_params.w < 0.5)
            continue;

        int type = int(light.type_params.x + 0.5);
        vec3 L;
        vec3 radiance = light.color_intensity.rgb * light.color_intensity.a;

        if (type == GS_LIGHT_DIRECTIONAL)
        {
            // Guard against a zero/garbage direction: normalize(vec3(0)) yields
            // NaN which would poison the whole fragment.
            vec3 light_dir = light.direction_cone.xyz;
            if (dot(light_dir, light_dir) < 1e-8)
                light_dir = vec3(0.0, -1.0, 0.0);
            L = -normalize(light_dir);
        }
        else
        {
            vec3 delta = light.position_range.xyz - v_world_position;
            float distance = max(length(delta), 1e-4);
            L = delta / distance;
            float attenuation = gsDistanceAttenuation(distance, light.position_range.w);
            if (type == GS_LIGHT_SPOT)
            {
                attenuation *= gsSpotAttenuation(light.direction_cone.xyz, delta,
                                                 light.type_params.y, light.type_params.z);
            }
            radiance *= attenuation;
        }

        color += gsEvaluateDirectLight(surface, L, radiance);
    }

    //------------------------------------------------------------------
    // Ambient term (flat, non IBL)
    //------------------------------------------------------------------
    color += u_ambient_color.rgb * u_ambient_color.w * surface.albedo * surface.ao;

    //------------------------------------------------------------------
    // Indirect light (IBL, split sum)
    //------------------------------------------------------------------
    vec3 R = reflect(-V, N);
    vec3 irradiance;
    vec3 prefiltered;
    vec2 brdf;
    float NoV = max(dot(N, V), 1e-5);

    // u_ibl_params.x = indirect lighting enabled
    // u_ibl_params.z = sky mode (>=2 means the cubemaps are baked and usable)
    if (u_ibl_params.x > 0.5)
    {
        if (u_ibl_params.z > 1.5)
        {
            irradiance = texture(u_irradiance_map, N).rgb;
            float mip_levels = max(u_ibl_params.y, 1.0);
            float lod = surface.roughness * (mip_levels - 1.0);
            prefiltered = textureLod(u_prefilter_map, R, lod).rgb;
            brdf = texture(u_brdf_lut, vec2(NoV, surface.roughness)).rg;
        }
        else
        {
            // No baked IBL yet: fall back to the analytic procedural sky so the
            // scene is never pure black while assets are loading.
            irradiance = gsProceduralSky(N);
            prefiltered = gsProceduralSky(R);
            brdf = gsEnvBRDFApprox(NoV, surface.roughness);
        }
        color += gsEvaluateIBL(surface, irradiance, prefiltered, brdf) * u_env_params.x;
    }

    //------------------------------------------------------------------
    // Emissive
    //------------------------------------------------------------------
    color += surface.emissive;

    out_color = vec4(max(color, vec3(0.0)), alpha);
}
