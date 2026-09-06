/*
 * Genesis Render - procedural mesh generation
 *
 * Small utilities used by the demo and by the verification suite so that tests
 * never depend on downloaded assets.  Everything produces the engine's
 * canonical interleaved Vertex layout with correct normals, tangents and UVs.
 */
#ifndef __GENESIS_PROCEDURAL_MESH_HPP__
#define __GENESIS_PROCEDURAL_MESH_HPP__

#include "resource/resource_types.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace Genesis
{
    namespace Procedural
    {
        // Ground plane on the XZ plane, centred at the origin, +Y normal.
        MeshData makePlane(float width = 10.0f, float depth = 10.0f, uint32_t subdivisions = 1);

        // Axis aligned box centred at the origin (24 vertices, 12 triangles).
        MeshData makeBox(float size_x = 1.0f, float size_y = 1.0f, float size_z = 1.0f);

        // UV sphere centred at the origin.
        MeshData makeSphere(float radius = 0.5f, uint32_t segments = 32, uint32_t rings = 16);

        // Torus around the Y axis.
        MeshData makeTorus(float major_radius = 1.0f, float minor_radius = 0.3f,
                           uint32_t major_segments = 48, uint32_t minor_segments = 24);

        // A "material preview" grid: N spheres in a row, roughness/metallic
        // ramped across them.  Handy for eyeballing the PBR response and for
        // pixel tests that need a known layout.
        ModelData makeMaterialGrid(uint32_t columns = 5, uint32_t rows = 5, float spacing = 1.1f,
                                   float radius = 0.45f);

        // Wraps a single mesh into a model with one material.
        std::shared_ptr<ModelData> wrapMesh(const MeshData &mesh, const MaterialData &material,
                                            const std::string &name = "procedural");

    } // namespace Procedural
} // namespace Genesis

#endif /* __GENESIS_PROCEDURAL_MESH_HPP__ */
