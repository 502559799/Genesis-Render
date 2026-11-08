/*
 * Genesis Render - resource data structure helpers
 */
#include "resource/resource_types.hpp"
#include "foundation_layer/genesis_log.hpp"

#include <cmath>
#include <cstring>
#include <unordered_map>

namespace Genesis
{
    //======================================================================
    // MeshData
    //======================================================================
    void MeshData::computeNormals()
    {
        if (vertices.empty()) return;
        for (auto &v : vertices)
        {
            v.normal[0] = v.normal[1] = 0.0f;
            v.normal[2] = 1.0f;
        }
        if (indices.empty())
        {
            // Non-indexed: accumulate per triangle of three consecutive vertices
            for (size_t i = 0; i + 2 < vertices.size(); i += 3)
            {
                Math::Vec3 a(vertices[i].position);
                Math::Vec3 b(vertices[i + 1].position);
                Math::Vec3 c(vertices[i + 2].position);
                Math::Vec3 n = Math::normalize(Math::cross(b - a, c - a));
                vertices[i].normal[0] = vertices[i + 1].normal[0] = vertices[i + 2].normal[0] = n.x;
                vertices[i].normal[1] = vertices[i + 1].normal[1] = vertices[i + 2].normal[1] = n.y;
                vertices[i].normal[2] = vertices[i + 1].normal[2] = vertices[i + 2].normal[2] = n.z;
            }
            has_normals = true;
            return;
        }

        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            Vertex &va = vertices[indices[i]];
            Vertex &vb = vertices[indices[i + 1]];
            Vertex &vc = vertices[indices[i + 2]];
            Math::Vec3 a(va.position), b(vb.position), c(vc.position);
            Math::Vec3 n = Math::cross(b - a, c - a); // area weighted
            for (Vertex *v : {&va, &vb, &vc})
            {
                v->normal[0] += n.x;
                v->normal[1] += n.y;
                v->normal[2] += n.z;
            }
        }
        for (auto &v : vertices)
        {
            Math::Vec3 n(v.normal[0], v.normal[1], v.normal[2]);
            n = Math::normalize(n);
            if (Math::lengthSquared(n) < 0.5f) n = Math::Vec3(0, 1, 0);
            v.normal[0] = n.x;
            v.normal[1] = n.y;
            v.normal[2] = n.z;
        }
        has_normals = true;
    }

    void MeshData::computeTangents()
    {
        if (vertices.empty()) return;
        std::vector<Math::Vec3> tan1(vertices.size(), Math::Vec3(0.0f));
        std::vector<Math::Vec3> tan2(vertices.size(), Math::Vec3(0.0f));

        auto accumulate = [&](size_t ia, size_t ib, size_t ic)
        {
            const Vertex &va = vertices[ia];
            const Vertex &vb = vertices[ib];
            const Vertex &vc = vertices[ic];
            Math::Vec3 p0(va.position), p1(vb.position), p2(vc.position);
            Math::Vec2 uv0(va.uv0[0], va.uv0[1]), uv1(vb.uv0[0], vb.uv0[1]), uv2(vc.uv0[0], vc.uv0[1]);
            Math::Vec3 e1 = p1 - p0, e2 = p2 - p0;
            float du1 = uv1.x - uv0.x, dv1 = uv1.y - uv0.y;
            float du2 = uv2.x - uv0.x, dv2 = uv2.y - uv0.y;
            float det = du1 * dv2 - du2 * dv1;
            float f = (std::fabs(det) > 1e-8f) ? 1.0f / det : 0.0f;
            Math::Vec3 t((dv2 * e1.x - dv1 * e2.x) * f,
                         (dv2 * e1.y - dv1 * e2.y) * f,
                         (dv2 * e1.z - dv1 * e2.z) * f);
            Math::Vec3 b((du1 * e2.x - du2 * e1.x) * f,
                         (du1 * e2.y - du2 * e1.y) * f,
                         (du1 * e2.z - du2 * e1.z) * f);
            for (size_t idx : {ia, ib, ic})
            {
                tan1[idx] += t;
                tan2[idx] += b;
            }
        };

        if (indices.empty())
        {
            for (size_t i = 0; i + 2 < vertices.size(); i += 3)
                accumulate(i, i + 1, i + 2);
        }
        else
        {
            for (size_t i = 0; i + 2 < indices.size(); i += 3)
                accumulate(indices[i], indices[i + 1], indices[i + 2]);
        }

        // Orthonormalise (Gram-Schmidt) and compute the handedness sign.
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            Vertex &v = vertices[i];
            Math::Vec3 n(v.normal);
            Math::Vec3 t = tan1[i];
            if (Math::lengthSquared(t) < 1e-12f)
            {
                // Fall back to an arbitrary tangent perpendicular to n
                Math::Vec3 axis = std::fabs(n.x) < 0.9f ? Math::Vec3(1, 0, 0) : Math::Vec3(0, 1, 0);
                t = Math::normalize(Math::cross(axis, n));
            }
            else
            {
                t = Math::normalize(t - n * Math::dot(n, t));
            }
            float w = (Math::lengthSquared(tan2[i]) > 1e-12f && Math::dot(Math::cross(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;
            v.tangent[0] = t.x; v.tangent[1] = t.y; v.tangent[2] = t.z;
            v.tangent[3] = w;
        }
        has_tangents = true;
    }

    void MeshData::computeBounds()
    {
        bounds = Math::AABB();
        for (const auto &v : vertices)
            bounds.expand(Math::Vec3(v.position));
    }

    //======================================================================
    // ModelData
    //======================================================================
    std::vector<ModelData::PlacedMesh> ModelData::flattenNodes() const
    {
        std::vector<PlacedMesh> out;
        if (nodes.empty())
        {
            for (size_t i = 0; i < meshes.size(); ++i)
                out.push_back(PlacedMesh{(int32_t)i, Math::Mat4::identity(), meshes[i].name});
            return out;
        }

        struct StackEntry
        {
            int32_t node;
            Math::Mat4 parent;
        };
        std::vector<StackEntry> stack;
        for (int32_t r : root_nodes) stack.push_back(StackEntry{r, Math::Mat4::identity()});
        if (stack.empty())
        {
            // No explicit roots: treat nodes without parents as roots.
            std::vector<bool> is_child(nodes.size(), false);
            for (const auto &n : nodes)
                for (int32_t c : n.children)
                    if (c >= 0 && (size_t)c < is_child.size()) is_child[c] = true;
            for (size_t i = 0; i < nodes.size(); ++i)
                if (!is_child[i]) stack.push_back(StackEntry{(int32_t)i, Math::Mat4::identity()});
            if (stack.empty() && !nodes.empty())
                stack.push_back(StackEntry{0, Math::Mat4::identity()});
        }

        size_t guard = 0;
        while (!stack.empty())
        {
            if (++guard > nodes.size() * 64 + 1024)
            {
                GEN_LOG_WARN("[Model] node traversal guard triggered (cyclic hierarchy?)");
                break;
            }
            StackEntry e = stack.back();
            stack.pop_back();
            if (e.node < 0 || (size_t)e.node >= nodes.size()) continue;
            const NodeData &n = nodes[e.node];
            Math::Mat4 world = e.parent * n.local_transform;
            for (int32_t mi : n.meshes)
            {
                if (mi >= 0 && (size_t)mi < meshes.size())
                    out.push_back(PlacedMesh{mi, world, n.name});
            }
            for (int32_t c : n.children)
                stack.push_back(StackEntry{c, world});
        }
        return out;
    }

    void ModelData::computeBounds()
    {
        bounds = Math::AABB();
        for (const auto &m : meshes) bounds.expand(m.bounds);
        auto placed = flattenNodes();
        if (!placed.empty())
        {
            bounds = Math::AABB();
            for (const auto &p : placed)
                if (p.mesh_index >= 0 && (size_t)p.mesh_index < meshes.size())
                    bounds.expand(meshes[p.mesh_index].bounds.transformed(p.transform));
        }
    }

} // namespace Genesis
