/*
 * Genesis Render - skybox / environment background
 *
 * Drawn as a fullscreen quad at the far plane (depth test LEQUAL, depth write
 * off, or depth forced to 1.0) after the opaque geometry.  The world ray is
 * reconstructed from the inverse view-projection matrix so that the skybox
 * automatically respects the per-backend clip space conventions.
 *
 * Modes (u_env_params.w and the bound textures):
 *   0 - procedural gradient sky (u_sky_tint / u_ground_tint + sun disc)
 *   1 - equirectangular HDR map  (u_src_equirect bound at set 0 binding 1)
 *   2 - prefiltered cubemap      (u_prefilter_map bound at set 0 binding 4)
 */
#include "common/genesis_shader.h"
#include "common/ubo_layout.h"
#include "common/pbr.h"

GS_IN(0, vec2, v_uv);
GS_OUT(0, vec4, out_color);

// Skybox specific bindings (this pipeline uses its own small layout):
//   set 0 binding 1 : sampler2D  u_sky_equirect
//   set 0 binding 2 : samplerCube u_sky_cube
GS_TEX2D(0, 1, u_sky_equirect);
GS_TEXCUBE(0, 2, u_sky_cube);

void main()
{
    // Reconstruct the view ray for this pixel at the far plane.
    // v_uv has its origin at the top-left, which corresponds to NDC (-1,-1)
    // on both backends (row 0 of the render target).
    vec3 ndc = vec3(v_uv.x * 2.0 - 1.0, v_uv.y * 2.0 - 1.0, 1.0);
    vec4 world = u_inv_view_projection * vec4(ndc, 1.0);
    vec3 dir = normalize(world.xyz / max(abs(world.w), 1e-6) - u_camera_position.xyz);

    // Apply the environment rotation so that the sun can be moved at runtime.
    vec2 rot = u_env_params.yz;
    vec3 rotated = vec3(rot.x * dir.x + rot.y * dir.z, dir.y, -rot.y * dir.x + rot.x * dir.z);

    vec3 color;
    int mode = int(u_ibl_params.z + 0.5);
    if (mode == 1)
    {
        vec2 uv = gsDirectionToEquirectUV(rotated);
        color = texture(u_sky_equirect, uv).rgb;
    }
    else if (mode == 2)
    {
        color = textureLod(u_sky_cube, rotated, 0.0).rgb;
    }
    else
    {
        float t = gsSaturate(rotated.y * 0.5 + 0.5);
        color = mix(u_ground_tint.rgb, u_sky_tint.rgb, t);
        vec3 sun_dir = normalize(vec3(0.4, 0.6, 0.3));
        float sun = pow(gsSaturate(dot(rotated, sun_dir)), 512.0);
        float glow = pow(gsSaturate(dot(rotated, sun_dir)), 8.0);
        color += vec3(1.0, 0.9, 0.7) * sun * 60.0 + vec3(0.4, 0.35, 0.3) * glow * 0.5;
    }

    out_color = vec4(max(color * u_env_params.x, vec3(0.0)), 1.0);
    // Force the fragment to the far plane so that geometry always wins.
    gl_FragDepth = 1.0;
}
