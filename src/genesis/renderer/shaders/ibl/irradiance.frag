/*
 * Genesis Render - IBL bake: diffuse irradiance convolution
 *
 * For every output texel (direction N) we integrate the source environment
 * over the hemisphere with a cosine weighting:
 *
 *     E(N) = 1/pi * integral( L(w) * (N . w) dw )
 *
 * which is approximated by a Riemann sum over (phi, theta) with the solid
 * angle element dOmega = sin(theta) dTheta dPhi.  The 1/pi normalisation and
 * the sin(theta) term cancel against the sample density, giving the classic
 * `sum * PI / sample_count` form used below.
 */
#include "common/genesis_shader.h"
#include "common/pbr.h"

GS_UBO(0, 0, IBLParams)
{
    vec4 u_ibl_a; // (face index, roughness, mip level, sample count)
    vec4 u_ibl_b; // (cos(env rotation), sin(env rotation), unused, unused)
};
GS_TEXCUBE(0, 2, u_src_cube);

GS_IN(0, vec2, v_uv);
GS_OUT(0, vec4, out_color);

void main()
{
    int face = int(u_ibl_a.x + 0.5);
    vec3 N = normalize(gsCubeFaceDirection(face, v_uv));

    // Build a tangent frame around N.
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    vec3 tangent_up = cross(N, right);

    vec3 irradiance = vec3(0.0);
    float sample_count = 0.0;

    const float delta_phi = 2.0 * GS_PI / 120.0;
    const float delta_theta = 0.5 * GS_PI / 60.0;

    for (float phi = 0.0; phi < 2.0 * GS_PI; phi += delta_phi)
    {
        for (float theta = 0.0; theta < 0.5 * GS_PI; theta += delta_theta)
        {
            // Spherical to cartesian (hemisphere around N).
            vec3 tangent_sample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 sample_vec = tangent_sample.x * right + tangent_sample.y * tangent_up + tangent_sample.z * N;
            irradiance += texture(u_src_cube, sample_vec).rgb * cos(theta) * sin(theta);
            sample_count += 1.0;
        }
    }

    irradiance = GS_PI * irradiance / max(sample_count, 1.0);
    out_color = vec4(max(irradiance, vec3(0.0)), 1.0);
}
