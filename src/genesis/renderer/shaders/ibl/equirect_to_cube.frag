/*
 * Genesis Render - IBL bake: equirectangular HDR -> cubemap
 *
 * One draw per cubemap face (the face index comes from IBLParams).  The result
 * is the raw environment radiance in a cubemap, which is then convolved into
 * the irradiance and prefiltered specular maps.
 */
#include "common/genesis_shader.h"
#include "common/pbr.h"

// set 0 binding 0 : UBL IBLParams
//   u_ibl_a = (face index, roughness, mip level, sample count)
//   u_ibl_b = (cos(env rotation), sin(env rotation), unused, unused)
GS_UBO(0, 0, IBLParams)
{
    vec4 u_ibl_a;
    vec4 u_ibl_b;
};
GS_TEX2D(0, 1, u_src_equirect);

GS_IN(0, vec2, v_uv);
GS_OUT(0, vec4, out_color);

void main()
{
    int face = int(u_ibl_a.x + 0.5);
    vec3 dir = gsCubeFaceDirection(face, v_uv);

    // Optional environment rotation so the sun can be aimed at the scene.
    vec2 rot = u_ibl_b.xy;
    vec3 rotated = vec3(rot.x * dir.x + rot.y * dir.z, dir.y, -rot.y * dir.x + rot.x * dir.z);

    vec2 uv = gsDirectionToEquirectUV(rotated);
    vec3 color = texture(u_src_equirect, uv).rgb;
    out_color = vec4(max(color, vec3(0.0)), 1.0);
}
