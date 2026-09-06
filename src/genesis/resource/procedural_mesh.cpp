/*
 * Genesis Render - procedural mesh generation implementation
 */
#include "resource/procedural_mesh.hpp"
#include "foundation_layer/genesis_log.hpp"

#include <cmath>

namespace Genesis
{
    namespace Procedural
    {
        namespace
        {
            void setVec3(float *dst, const Math::Vec3 &v)
            {
                dst[0] = v.x; dst[1] = v.y; dst[2] = v.z;
            }
            void setVec2(float *dst, const Math::Vec2 &v)
            {
                dst[0] = v.x; dst[1] = v.y;
            }

            Vertex makeVertex(const Math::Vec3 &p, const Math::Vec3 &n, const Math::Vec2 &uv)
            {
                Vertex v;
                setVec3(v.position, p);
                setVec3(v.normal, n);
                setVec2(v.uv0, uv);
                setVec2(v.uv1, uv);
                // tangent = X of the local frame, bitangent implied by n x t * w
                Math::Vec3 t = std::fabs(n.y) < 0.99f ? Math::normalize(Math::cross(Math::Vec3(0, 1, 0), n))
                                                      : Math::Vec3(1, 0, 0);
                Math::Vec3 b = Math::cross(n, t);
                (void)b;
                setVec3(v.tangent, t);
                v.tangent[3] = 1.0f;
                v.weights[0] = 1.0f;
                return v;
            }
        } // namespace

        MeshData makePlane(float width, float depth, uint32_t subdivisions)
        {
            MeshData mesh;
            mesh.name = "plane";
            subdivisions = std::max(1u, subdivisions);
            uint32_t cols = subdivisions + 1;
            uint32_t rows = subdivisions + 1;

            mesh.vertices.reserve((size_t)cols * rows);
            for (uint32_t z = 0; z < rows; ++z)
            {
                for (uint32_t x = 0; x < cols; ++x)
                {
                    float u = (float)x / (float)subdivisions;
                    float v = (float)z / (float)subdivisions;
                    Math::Vec3 p((u - 0.5f) * width, 0.0f, (v - 0.5f) * depth);
                    mesh.vertices.push_back(makeVertex(p, Math::Vec3(0, 1, 0), Math::Vec2(u, v)));
                }
            }
            for (uint32_t z = 0; z < subdivisions; ++z)
            {
                for (uint32_t x = 0; x < subdivisions; ++x)
                {
                    uint32_t i0 = z * cols + x;
                    uint32_t i1 = i0 + 1;
                    uint32_t i2 = i0 + cols;
                    uint32_t i3 = i2 + 1;
                    // Wind so the +Y normal faces the viewer with CCW front faces.
                    mesh.indices.insert(mesh.indices.end(), {i0, i2, i1, i1, i2, i3});
                }
            }
            mesh.has_normals = true;
            mesh.computeTangents();
            mesh.computeBounds();
            return mesh;
        }

        MeshData makeBox(float sx, float sy, float sz)
        {
            MeshData mesh;
            mesh.name = "box";
            const Math::Vec3 h(sx * 0.5f, sy * 0.5f, sz * 0.5f);

            struct Face
            {
                Math::Vec3 n;
                Math::Vec3 u;
                Math::Vec3 v;
            };
            const Face faces[6] = {
                {Math::Vec3(0, 0, 1), Math::Vec3(1, 0, 0), Math::Vec3(0, 1, 0)},   // +Z
                {Math::Vec3(0, 0, -1), Math::Vec3(-1, 0, 0), Math::Vec3(0, 1, 0)},  // -Z
                {Math::Vec3(1, 0, 0), Math::Vec3(0, 0, -1), Math::Vec3(0, 1, 0)},   // +X
                {Math::Vec3(-1, 0, 0), Math::Vec3(0, 0, 1), Math::Vec3(0, 1, 0)},   // -X
                {Math::Vec3(0, 1, 0), Math::Vec3(1, 0, 0), Math::Vec3(0, 0, -1)},   // +Y
                {Math::Vec3(0, -1, 0), Math::Vec3(1, 0, 0), Math::Vec3(0, 0, 1)},   // -Y
            };

            for (const Face &f : faces)
            {
                uint32_t base = (uint32_t)mesh.vertices.size();
                Math::Vec3 center = f.n * Math::Vec3(std::fabs(f.n.x) > 0.5f ? h.x : 0.0f,
                                                     std::fabs(f.n.y) > 0.5f ? h.y : 0.0f,
                                                     std::fabs(f.n.z) > 0.5f ? h.z : 0.0f);
                float fu = std::fabs(f.u.x) > 0.5f ? h.x : (std::fabs(f.u.y) > 0.5f ? h.y : h.z);
                float fv = std::fabs(f.v.x) > 0.5f ? h.x : (std::fabs(f.v.y) > 0.5f ? h.y : h.z);
                Math::Vec2 uvs[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                Math::Vec3 corners[4] = {
                    center - f.u * fu - f.v * fv,
                    center + f.u * fu - f.v * fv,
                    center + f.u * fu + f.v * fv,
                    center - f.u * fu + f.v * fv,
                };
                for (int i = 0; i < 4; ++i)
                {
                    Vertex v = makeVertex(corners[i], f.n, uvs[i]);
                    setVec3(v.tangent, f.u);
                    v.tangent[3] = 1.0f;
                    mesh.vertices.push_back(v);
                }
                mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            }
            mesh.has_normals = true;
            mesh.has_tangents = true;
            mesh.computeBounds();
            return mesh;
        }

        MeshData makeSphere(float radius, uint32_t segments, uint32_t rings)
        {
            MeshData mesh;
            mesh.name = "sphere";
            segments = std::max(3u, segments);
            rings = std::max(2u, rings);

            for (uint32_t y = 0; y <= rings; ++y)
            {
                float v = (float)y / (float)rings;
                float theta = v * Math::PI;
                for (uint32_t x = 0; x <= segments; ++x)
                {
                    float u = (float)x / (float)segments;
                    float phi = u * Math::TWO_PI;
                    Math::Vec3 dir(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
                    Vertex vertex = makeVertex(dir * radius, dir, Math::Vec2(u, v));
                    // Tangent along +u
                    Math::Vec3 t(-std::sin(phi), 0.0f, std::cos(phi));
                    if (Math::lengthSquared(t) < 1e-6f) t = Math::Vec3(1, 0, 0);
                    setVec3(vertex.tangent, Math::normalize(t));
                    vertex.tangent[3] = -1.0f; // bitangent = cross(n,t)*w points towards -theta
                    mesh.vertices.push_back(vertex);
                }
            }
            uint32_t stride = segments + 1;
            for (uint32_t y = 0; y < rings; ++y)
            {
                for (uint32_t x = 0; x < segments; ++x)
                {
                    uint32_t i0 = y * stride + x;
                    uint32_t i1 = i0 + 1;
                    uint32_t i2 = i0 + stride;
                    uint32_t i3 = i2 + 1;
                    // CCW when seen from outside (the engine's front face).
                    if (y != 0) mesh.indices.insert(mesh.indices.end(), {i0, i1, i2});
                    if (y != rings - 1) mesh.indices.insert(mesh.indices.end(), {i1, i3, i2});
                }
            }
            mesh.has_normals = true;
            mesh.has_tangents = true;
            mesh.computeBounds();
            return mesh;
        }

        MeshData makeTorus(float major_radius, float minor_radius, uint32_t major_segments, uint32_t minor_segments)
        {
            MeshData mesh;
            mesh.name = "torus";
            major_segments = std::max(3u, major_segments);
            minor_segments = std::max(3u, minor_segments);

            for (uint32_t i = 0; i <= major_segments; ++i)
            {
                float u = (float)i / (float)major_segments;
                float phi = u * Math::TWO_PI;
                for (uint32_t j = 0; j <= minor_segments; ++j)
                {
                    float v = (float)j / (float)minor_segments;
                    float theta = v * Math::TWO_PI;

                    Math::Vec3 center(std::cos(phi) * major_radius, 0.0f, std::sin(phi) * major_radius);
                    Math::Vec3 radial(std::cos(phi), 0.0f, std::sin(phi));
                    Math::Vec3 tube_axis = radial * std::cos(theta) + Math::Vec3(0, 1, 0) * std::sin(theta);
                    Math::Vec3 p = center + tube_axis * minor_radius;

                    Vertex vertex = makeVertex(p, Math::normalize(tube_axis), Math::Vec2(u, v));
                    Math::Vec3 t(-std::sin(phi), 0.0f, std::cos(phi));
                    setVec3(vertex.tangent, Math::normalize(t));
                    vertex.tangent[3] = 1.0f;
                    mesh.vertices.push_back(vertex);
                }
            }
            uint32_t stride = minor_segments + 1;
            for (uint32_t i = 0; i < major_segments; ++i)
            {
                for (uint32_t j = 0; j < minor_segments; ++j)
                {
                    uint32_t i0 = i * stride + j;
                    uint32_t i1 = i0 + 1;
                    uint32_t i2 = i0 + stride;
                    uint32_t i3 = i2 + 1;
                    // CCW when seen from outside the tube.
                    mesh.indices.insert(mesh.indices.end(), {i0, i1, i2, i1, i3, i2});
                }
            }
            mesh.has_normals = true;
            mesh.has_tangents = true;
            mesh.computeBounds();
            return mesh;
        }

        std::shared_ptr<ModelData> wrapMesh(const MeshData &mesh, const MaterialData &material, const std::string &name)
        {
            auto model = std::make_shared<ModelData>();
            model->name = name;
            model->importer = "procedural";
            MeshData m = mesh;
            m.material_index = 0;
            model->meshes.push_back(m);
            model->materials.push_back(material);

            NodeData node;
            node.name = name;
            node.local_transform = Math::Mat4::identity();
            node.meshes.push_back(0);
            model->nodes.push_back(node);
            model->root_nodes.push_back(0);
            model->computeBounds();
            return model;
        }

        ModelData makeMaterialGrid(uint32_t columns, uint32_t rows, float spacing, float radius)
        {
            ModelData model;
            model.name = "material_grid";
            model.importer = "procedural";
            columns = std::max(1u, columns);
            rows = std::max(1u, rows);

            MeshData sphere = makeSphere(radius, 32, 16);

            for (uint32_t y = 0; y < rows; ++y)
            {
                for (uint32_t x = 0; x < columns; ++x)
                {
                    MeshData m = sphere;
                    m.name = "sphere_" + std::to_string(x) + "_" + std::to_string(y);
                    m.material_index = (int32_t)model.materials.size();
                    model.meshes.push_back(m);

                    MaterialData mat;
                    mat.name = m.name + "_mat";
                    mat.metallic_factor = columns > 1 ? (float)x / (float)(columns - 1) : 0.0f;
                    mat.roughness_factor = rows > 1 ? std::max(0.05f, 1.0f - (float)y / (float)(rows - 1)) : 0.5f;
                    mat.base_color_factor = Math::Color(0.95f, 0.55f, 0.30f, 1.0f);
                    model.materials.push_back(mat);

                    NodeData node;
                    node.name = m.name;
                    float px = ((float)x - (float)(columns - 1) * 0.5f) * spacing;
                    float py = ((float)y - (float)(rows - 1) * 0.5f) * spacing;
                    node.local_transform = Math::Mat4::translate(Math::Vec3(px, py, 0.0f));
                    node.meshes.push_back((int32_t)(model.meshes.size() - 1));
                    model.nodes.push_back(node);
                    model.root_nodes.push_back((int32_t)(model.nodes.size() - 1));
                }
            }
            model.computeBounds();
            return model;
        }

    } // namespace Procedural
} // namespace Genesis
