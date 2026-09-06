/*
 * Genesis Render - Engine
 * procgen.cpp : procedural meshes + procedural equirect environment.
 */
#include "model.hpp"
#include <cmath>

namespace Genesis
{
using Mesh = CPUModel::Mesh;

static void computeTangents(Mesh &m)
{
    for (auto &v : m.vertices)
        v.tangent = Vec4(1, 0, 0, 1);
    std::vector<Vec3> tan1(m.vertices.size(), Vec3(0)), tan2(m.vertices.size(), Vec3(0));
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3)
    {
        ModelVertex &v0 = m.vertices[m.indices[t]];
        ModelVertex &v1 = m.vertices[m.indices[t + 1]];
        ModelVertex &v2 = m.vertices[m.indices[t + 2]];
        Vec3 p0 = v0.position, p1 = v1.position, p2 = v2.position;
        Vec2 w0 = v0.uv, w1 = v1.uv, w2 = v2.uv;
        Vec3 e1 = p1 - p0, e2 = p2 - p0;
        float du1 = w1.x - w0.x, dv1 = w1.y - w0.y;
        float du2 = w2.x - w0.x, dv2 = w2.y - w0.y;
        float d = du1 * dv2 - du2 * dv1;
        if (std::fabs(d) < 1e-8f) continue;
        float f = 1.0f / d;
        Vec3 s{(dv2 * e1.x - dv1 * e2.x) * f, (dv2 * e1.y - dv1 * e2.y) * f,
               (dv2 * e1.z - dv1 * e2.z) * f};
        Vec3 tt{(-du2 * e1.x + du1 * e2.x) * f, (-du2 * e1.y + du1 * e2.y) * f,
                (-du2 * e1.z + du1 * e2.z) * f};
        tan1[m.indices[t]] += s; tan1[m.indices[t + 1]] += s; tan1[m.indices[t + 2]] += s;
        tan2[m.indices[t]] += tt; tan2[m.indices[t + 1]] += tt; tan2[m.indices[t + 2]] += tt;
    }
    for (size_t i = 0; i < m.vertices.size(); ++i)
    {
        Vec3 n = m.vertices[i].normal;
        Vec3 t = tan1[i];
        Vec3 orth = normalize(t - n * dot(n, t));
        float w = (dot(cross(n, t), tan2[i]) < 0) ? -1.f : 1.f;
        m.vertices[i].tangent = Vec4(orth, w);
    }
}

static CPUModel single(Mesh m, const char *name)
{
    CPUModel model;
    SubMesh sm{0, (uint32_t)m.indices.size(), 0, name};
    m.submeshes.push_back(sm);
    computeTangents(m);
    model.meshes.push_back(std::move(m));
    model.materials.push_back(CPUMaterial{});
    model.materials[0].name = "default";
    for (auto &v : model.meshes[0].vertices)
        model.bounds.addPoint(v.position);
    return model;
}

CPUModel procgen::makePlane(float size, int)
{
    Mesh m;
    float h = size * 0.5f;
    Vec3 pos[4] = {{-h, 0, -h}, {h, 0, -h}, {h, 0, h}, {-h, 0, h}};
    Vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 4; ++i)
    {
        ModelVertex v;
        v.position = pos[i]; v.normal = {0, 1, 0}; v.uv = uv[i]; v.tangent = {1, 0, 0, 1};
        m.vertices.push_back(v);
    }
    m.indices = {0, 2, 1, 0, 3, 2};
    return single(std::move(m), "plane");
}

CPUModel procgen::makeCube(float size)
{
    Mesh m;
    float h = size * 0.5f;
    struct Face { Vec3 n, t; int a[4]; };
    Vec3 P[8] = {{-h,-h,-h},{h,-h,-h},{h,h,-h},{-h,h,-h},{-h,-h,h},{h,-h,h},{h,h,h},{-h,h,h}};
    Vec2 UV[4] = {{0,0},{1,0},{1,1},{0,1}};
    Face faces[6] = {
        {{0,0,-1},{1,0,0},{0,1,2,3}}, // back
        {{0,0,1},{-1,0,0},{4,7,6,5}}, // front
        {{-1,0,0},{0,0,1},{0,3,7,4}}, // left
        {{1,0,0},{0,0,-1},{1,5,6,2}}, // right
        {{0,1,0},{1,0,0},{3,2,6,7}},  // top
        {{0,-1,0},{1,0,0},{0,4,5,1}}  // bottom
    };
    for (auto &f : faces)
    {
        uint32_t base = (uint32_t)m.vertices.size();
        for (int k = 0; k < 4; ++k)
        {
            ModelVertex v;
            v.position = P[f.a[k]]; v.normal = f.n; v.uv = UV[k];
            v.tangent = Vec4(f.t, 1);
            m.vertices.push_back(v);
        }
        m.indices.insert(m.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
    }
    return single(std::move(m), "cube");
}

CPUModel procgen::makeSphere(float r, int slices, int stacks)
{
    Mesh m;
    for (int y = 0; y <= stacks; ++y)
    {
        float v = (float)y / stacks;
        float phi = v * kPi;
        for (int x = 0; x <= slices; ++x)
        {
            float u = (float)x / slices;
            float theta = u * kTwoPi;
            Vec3 n{std::sin(phi) * std::cos(theta), std::cos(phi),
                   std::sin(phi) * std::sin(theta)};
            ModelVertex vx;
            vx.position = n * r; vx.normal = n;
            vx.uv = {u, 1.f - v};
            vx.tangent = Vec4(1, 0, 0, 1);
            m.vertices.push_back(vx);
        }
    }
    uint32_t stride = slices + 1;
    for (int y = 0; y < stacks; ++y)
        for (int x = 0; x < slices; ++x)
        {
            uint32_t a = y * stride + x, b = a + stride;
            m.indices.insert(m.indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    return single(std::move(m), "sphere");
}

std::vector<float> procgen::makeProceduralEnvironment(uint32_t w, uint32_t h)
{
    // Gradient sky + warm sun + dark ground, linear HDR.
    std::vector<float> px((size_t)w * h * 4);
    Vec3 sunDir = normalize(Vec3(-0.5f, 0.45f, -0.65f));
    for (uint32_t y = 0; y < h; ++y)
    {
        float v = (float)y / (h - 1);
        float pitch = (0.5f - v) * kPi; // +y up
        float cp = std::cos(pitch), sp = std::sin(pitch);
        for (uint32_t x = 0; x < w; ++x)
        {
            float u = (float)x / w;
            float yaw = u * kTwoPi;
            Vec3 d{cp * std::sin(yaw), sp, cp * std::cos(yaw)};
            Vec3 col;
            float hgt = std::max(0.f, d.y);
            // sky gradient: warm horizon -> blue zenith
            Vec3 horizon = Vec3(0.85f, 0.72f, 0.58f) * 0.9f;
            Vec3 zenith = Vec3(0.25f, 0.45f, 0.8f) * 1.1f;
            col = lerp(horizon, zenith, std::pow(hgt, 0.55f));
            // sun
            float s = std::max(0.f, dot(d, sunDir));
            col += Vec3(1.0f, 0.85f, 0.6f) * std::pow(s, 800.f) * 25.f;  // disk
            col += Vec3(1.0f, 0.75f, 0.5f) * std::pow(s, 8.f) * 0.6f;    // glow
            // ground
            if (d.y < 0) col = Vec3(0.18f, 0.17f, 0.16f) * (0.5f + 0.5f * (-d.y));
            size_t i = ((size_t)y * w + x) * 4;
            px[i] = col.x; px[i + 1] = col.y; px[i + 2] = col.z; px[i + 3] = 1.f;
        }
    }
    return px;
}
}
