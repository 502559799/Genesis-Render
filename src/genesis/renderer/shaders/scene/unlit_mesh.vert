/*
 * Genesis Render - unlit mesh (debug / test shader)
 *
 * Deliberately tiny: it exists so the test suite can verify transforms, depth
 * testing and colour output with a known value.  The colour comes through push
 * constants, which also exercises the RHI push-constant path on both backends
 * (real vkCmdPushConstants on Vulkan, an emulated std140 "PushConstants" block
 * on OpenGL).
 */
#include "common/genesis_shader.h"
#include "common/ubo_layout.h"

GS_IN(0, vec3, a_position);
GS_IN(1, vec3, a_normal);
GS_IN(3, vec2, a_uv0);

GS_OUT(0, vec2, v_uv0);
GS_OUT(1, vec3, v_normal);

void main()
{
    vec4 world = u_model * vec4(a_position, 1.0);
    v_uv0 = a_uv0;
    v_normal = normalize(mat3(u_normal_matrix) * a_normal);
    gl_Position = u_view_projection * world;
}
