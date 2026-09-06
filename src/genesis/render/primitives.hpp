/*
 * Genesis Render - procedural geometry (cube for sky/IBL, ground, sphere).
 */
#pragma once

#include "resource/mesh_data.hpp"

namespace Genesis {

// Unit cube centered at origin, corners at +/-1 (used for skybox/IBL capture).
void buildCube(MeshData& out);
// Axis-aligned box with half extents.
void buildBox(MeshData& out, const Vec3& halfExtents);
// Subdivided UV sphere.
void buildSphere(MeshData& out, int stacks = 24, int slices = 32);
// Horizontal plane in XZ.
void buildPlane(MeshData& out, float size = 10.0f);

} // namespace Genesis
