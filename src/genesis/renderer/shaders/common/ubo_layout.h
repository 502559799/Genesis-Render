/*
 * Genesis Render - uniform block layouts shared by every shader stage.
 *
 * !!! These layouts are mirrored byte-for-byte in C++ !!!
 *     src/genesis/renderer/render_ubo.hpp
 * Any change here must be reflected there (and vice versa).
 *
 * std140 rules used: only mat4, vec4 and float arrays appear, so the C++ side
 * can use plain structs with alignas(16) and no padding surprises.
 */
#ifndef GENESIS_UBO_LAYOUT_H
#define GENESIS_UBO_LAYOUT_H

#include "common/genesis_shader.h"

//----------------------------------------------------------------------
// set 0 / binding 0 - per frame (scene) data
//----------------------------------------------------------------------
GS_UBO(0, 0, PerFrame)
{
    mat4 u_view;
    mat4 u_projection;
    mat4 u_view_projection;
    mat4 u_inv_view_projection;
    mat4 u_inv_view;
    vec4 u_camera_position;  // xyz = world space eye, w = vertical fov (radians)
    vec4 u_frame_params;     // x = time (s), y = delta time (s), z = frame index, w = exposure
    vec4 u_viewport_params;  // x = width, y = height, z = 1/width, w = 1/height
    vec4 u_backend_params;   // x = flip uv.y (GL offscreen targets), y = depth [0,1] (1) or [-1,1] (0),
                             // z = flip clip y (Vulkan), w = unused
};

//----------------------------------------------------------------------
// set 0 / binding 1 - light array
//----------------------------------------------------------------------
struct GpuLight
{
    vec4 position_range;   // xyz = world position, w = range (point/spot)
    vec4 direction_cone;   // xyz = world direction (normalised), w = unused
    vec4 color_intensity;  // rgb = linear colour, w = intensity
    vec4 type_params;      // x = type (0 dir, 1 point, 2 spot), y = cos(inner),
                           // z = cos(outer), w = enabled (0/1)
};

GS_UBO(0, 1, Lights)
{
    GpuLight u_lights[GS_MAX_LIGHTS];
    vec4 u_light_count_params;  // x = number of active lights, yzw = unused
    vec4 u_ambient_color;       // rgb = linear ambient, w = ambient intensity
};

//----------------------------------------------------------------------
// set 0 / binding 2 - environment / IBL parameters
//----------------------------------------------------------------------
GS_UBO(0, 2, Environment)
{
    vec4 u_env_params;    // x = env intensity, y = cos(rotation), z = sin(rotation), w = show skybox
    vec4 u_ibl_params;    // x = use IBL (0/1), y = prefilter mip count, z = max lod, w = unused
    vec4 u_sky_tint;      // procedural fallback sky colour (top)
    vec4 u_ground_tint;   // procedural fallback ground colour (bottom)
};

//----------------------------------------------------------------------
// set 1 / binding 0 - material
//----------------------------------------------------------------------
GS_UBO(1, 0, Material)
{
    vec4 u_base_color_factor;   // rgba, linear (already converted on the CPU)
    vec4 u_emissive_factor;     // rgb linear, a = emissive strength
    vec4 u_material_params0;    // x = metallic, y = roughness, z = normal scale, w = occlusion strength
    vec4 u_material_params1;    // x = alpha cutoff, y = alpha mode, z = double sided, w = unused
    vec4 u_texture_mask;        // x = base colour, y = metallic-roughness, z = normal, w = occlusion (0/1)
    vec4 u_material_extra;      // x = emissive texture (0/1), y = uv scale x, z = uv scale y, w = unused
};

//----------------------------------------------------------------------
// set 2 / binding 0 - per draw (dynamic offset)
//----------------------------------------------------------------------
GS_UBO(2, 0, PerDraw)
{
    mat4 u_model;
    mat4 u_normal_matrix;   // transpose(inverse(model)) upper 3x3
    vec4 u_object_params;   // x = pick id, y = wireframe (0/1), z = uv offset x, w = uv offset y
};

#endif // GENESIS_UBO_LAYOUT_H
