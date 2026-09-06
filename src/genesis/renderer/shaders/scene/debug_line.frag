/*
 * Genesis Render - debug line / gizmo shader (fragment)
 */
#include "common/genesis_shader.h"

GS_IN(0, vec4, v_color);
GS_OUT(0, vec4, out_color);

void main()
{
    out_color = v_color;
}
