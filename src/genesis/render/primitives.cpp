/*
 * Genesis Render - procedural geometry.
 */
#include "render/primitives.hpp"
#include <cmath>

namespace Genesis {

static void pushTri(MeshData& m, const Vec3& a, const Vec3& b, const Vec3& c,
                    const Vec3& n, const Vec2& uva, const Vec2& uvb, const Vec2& uvc) {
    uint32_t base = (uint32_t)m.vertices.size();
    Vertex va{a, n, Vec4{0,0,0,1}, uva};
    Vertex vb{b, n, Vec4{0,0,0,1}, uvb};
    Vertex vc{c, n, Vec4{0,0,0,1}, uvc};
    m.vertices.push_back(va); m.vertices.push_back(vb); m.vertices.push_back(vc);
    SubMesh* sm = m.submeshes.empty() ? nullptr : &m.submeshes.back();
    if (!sm || sm->indexOffset + sm->indexCount != base) {
        SubMesh s; s.indexOffset = (uint32_t)m.indices.size(); s.materialIndex = -1;
        m.submeshes.push_back(s);
        sm = &m.submeshes.back();
    }
    m.indices.push_back(base); m.indices.push_back(base + 1); m.indices.push_back(base + 2);
    sm->indexCount += 3;
}

static void addFace(MeshData& m, const Vec3& v0, const Vec3& v1, const Vec3& v2, const Vec3& v3,
                    const Vec3& n, float scale) {
    (void)scale;
    pushTri(m, v0, v1, v2, n, {0,0}, {1,0}, {1,1});
    pushTri(m, v0, v2, v3, n, {0,0}, {1,1}, {0,1});
}

static void buildBoxInternal(MeshData& out, const Vec3& h) {
    SubMesh sm; sm.indexOffset = 0; sm.materialIndex = -1;
    out.submeshes.push_back(sm);
    Vec3 px{h.x,0,0}, nx{-h.x,0,0}, py{0,h.y,0}, ny{0,-h.y,0}, pz{0,0,h.z}, nz{0,0,-h.z};
    // +X / -X
    addFace(out, px+py+pz, px+py+nz, px+ny+nz, px+ny+pz, {1,0,0}, 1);
    addFace(out, nx+py+nz, nx+py+pz, nx+ny+pz, nx+ny+nz, {-1,0,0}, 1);
    // +Y / -Y
    addFace(out, nx+py+pz, px+py+pz, px+py+nz, nx+py+nz, {0,1,0}, 1);
    addFace(out, nx+ny+nz, px+ny+nz, px+ny+pz, nx+ny+pz, {0,-1,0}, 1);
    // +Z / -Z
    addFace(out, nx+py+pz, nx+ny+pz, px+ny+pz, px+py+pz, {0,0,1}, 1);
    addFace(out, px+py+nz, px+ny+nz, nx+ny+nz, nx+py+nz, {0,0,-1}, 1);
    out.submeshes[0].indexCount = (uint32_t)out.indices.size();
}

void buildCube(MeshData& out) {
    out = MeshData{};
    buildBoxInternal(out, Vec3{1, 1, 1});
}
void buildBox(MeshData& out, const Vec3& half) {
    out = MeshData{};
    buildBoxInternal(out, half);
}

void buildPlane(MeshData& out, float size) {
    out = MeshData{};
    SubMesh sm; sm.materialIndex = -1;
    out.submeshes.push_back(sm);
    float h = size * 0.5f;
    Vec3 n{0,1,0};
    pushTri(out, {-h,0,-h}, {h,0,-h}, {h,0,h}, n, {0,0},{1,0},{1,1});
    pushTri(out, {-h,0,-h}, {h,0,h}, {-h,0,h}, n, {0,0},{1,1},{0,1});
    out.submeshes[0].indexCount = (uint32_t)out.indices.size();
}

void buildSphere(MeshData& out, int stacks, int slices) {
    out = MeshData{};
    SubMesh sm; sm.materialIndex = -1;
    out.submeshes.push_back(sm);
    auto vert = [&](float theta, float phi) {
        float x = std::sin(theta) * std::cos(phi);
        float y = std::cos(theta);
        float z = std::sin(theta) * std::sin(phi);
        Vec3 p{x, y, z}, nn = normalize(p);
        out.vertices.push_back(Vertex{p, nn, Vec4{1,0,0,1},
                                      {phi / (2.0f * kPi), 1.0f - theta / kPi}});
    };
    for (int i = 0; i <= stacks; ++i) {
        float theta = kPi * float(i) / stacks;
        for (int j = 0; j <= slices; ++j) {
            float phi = 2.0f * kPi * float(j) / slices;
            vert(theta, phi);
        }
    }
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            uint32_t a = i * (slices + 1) + j;
            out.indices.push_back(a);
            out.indices.push_back(a + slices + 1);
            out.indices.push_back(a + 1);
            out.indices.push_back(a + 1);
            out.indices.push_back(a + slices + 1);
            out.indices.push_back(a + slices + 2);
        }
    }
    out.submeshes[0].indexCount = (uint32_t)out.indices.size();
}

} // namespace Genesis
