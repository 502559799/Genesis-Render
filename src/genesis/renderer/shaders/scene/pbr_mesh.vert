/*
 * Genesis Render - PBR mesh pass (vertex)
 */
#include "common/genesis_shader.h"
#include "common/ubo_layout.h"

GS_IN(0, vec3, a_position);
GS_IN(1, vec3, a_normal);
GS_IN(2, vec4, a_tangent);
GS_IN(3, vec2, a_uv0);
GS_IN(4, vec2, a_uv1);
GS_IN(5, vec4, a_joints);
GS_IN(6, vec4, a_weights);

GS_OUT(0, vec3, v_world_position);
GS_OUT(1, vec3, v_normal);
GS_OUT(2, vec4, v_tangent);
GS_OUT(3, vec2, v_uv0);
GS_OUT(4, vec2, v_uv1);

void main()
{
    vec4 world_position = u_model * vec4(a_position, 1.0);
    v_world_position = world_position.xyz;

    mat3 normal_matrix = mat3(u_normal_matrix);
    v_normal = normalize(normal_matrix * a_normal);
    vec3 tangent = normalize(normal_matrix * a_tangent.xyz);
    // Re-orthogonalise against the (possibly non-uniformly scaled) normal.
    tangent = normalize(tangent - v_normal * dot(v_normal, tangent));
    v_tangent = vec4(tangent, a_tangent.w);

    vec2 uv_scale = vec2(u_material_extra.y, u_material_extra.z);
    if (dot(uv_scale, uv_scale) < 1e-6)
        uv_scale = vec2(1.0);
    v_uv0 = a_uv0 * uv_scale + u_object_params.zw;
    v_uv1 = a_uv1;

    gl_Position = u_view_projection * world_position;
}
