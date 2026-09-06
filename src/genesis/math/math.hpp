/*
 * Genesis Render - Engine
 * math.hpp : minimal header-only linear algebra library (vec2/3/4, mat4, quaternion).
 * Column-major mat4, binary compatible with GLSL / std140 GPU uniform blocks.
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace Genesis
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.0f * kPi;
    constexpr float kHalfPi = 0.5f * kPi;
    constexpr float kDeg2Rad = kPi / 180.0f;
    constexpr float kRad2Deg = 180.0f / kPi;
    constexpr float kEpsilon = 1e-6f;

    inline float radians(float deg) { return deg * kDeg2Rad; }
    inline float degrees(float rad) { return rad * kRad2Deg; }

    // ------------------------------------------------------------------ vec2
    struct Vec2
    {
        float x{0.0f}, y{0.0f};
        Vec2() = default;
        Vec2(float s) : x(s), y(s) {}
        Vec2(float x_, float y_) : x(x_), y(y_) {}
        float &operator[](int i) { return (&x)[i]; }
        float operator[](int i) const { return (&x)[i]; }
    };

    inline Vec2 operator+(const Vec2 &a, const Vec2 &b) { return {a.x + b.x, a.y + b.y}; }
    inline Vec2 operator-(const Vec2 &a, const Vec2 &b) { return {a.x - b.x, a.y - b.y}; }
    inline Vec2 operator*(const Vec2 &a, float s) { return {a.x * s, a.y * s}; }

    // ------------------------------------------------------------------ vec3
    struct Vec3
    {
        float x{0.0f}, y{0.0f}, z{0.0f};
        Vec3() = default;
        Vec3(float s) : x(s), y(s), z(s) {}
        Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
        float &operator[](int i) { return (&x)[i]; }
        float operator[](int i) const { return (&x)[i]; }
    };

    inline Vec3 operator+(const Vec3 &a, const Vec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    inline Vec3 operator-(const Vec3 &a, const Vec3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    inline Vec3 operator-(const Vec3 &a) { return {-a.x, -a.y, -a.z}; }
    inline Vec3 operator*(const Vec3 &a, float s) { return {a.x * s, a.y * s, a.z * s}; }
    inline Vec3 operator*(float s, const Vec3 &a) { return a * s; }
    inline Vec3 operator*(const Vec3 &a, const Vec3 &b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
    inline Vec3 operator/(const Vec3 &a, float s) { return a * (1.0f / s); }
    inline Vec3 &operator+=(Vec3 &a, const Vec3 &b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
    inline Vec3 &operator-=(Vec3 &a, const Vec3 &b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
    inline Vec3 &operator*=(Vec3 &a, float s) { a.x *= s; a.y *= s; a.z *= s; return a; }

    inline float dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline Vec3 cross(const Vec3 &a, const Vec3 &b)
    {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    inline float length(const Vec3 &a) { return std::sqrt(dot(a, a)); }
    inline float lengthSquared(const Vec3 &a) { return dot(a, a); }
    inline Vec3 normalize(const Vec3 &a)
    {
        float l = length(a);
        return l > kEpsilon ? a / l : Vec3(0.0f);
    }
    inline Vec3 lerp(const Vec3 &a, const Vec3 &b, float t) { return a + (b - a) * t; }
    inline Vec3 reflect(const Vec3 &i, const Vec3 &n) { return i - n * (2.0f * dot(i, n)); }
    inline Vec3 minV(const Vec3 &a, const Vec3 &b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
    inline Vec3 maxV(const Vec3 &a, const Vec3 &b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

    // ------------------------------------------------------------------ vec4
    struct alignas(16) Vec4
    {
        float x{0.0f}, y{0.0f}, z{0.0f}, w{0.0f};
        Vec4() = default;
        Vec4(float s) : x(s), y(s), z(s), w(s) {}
        Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
        Vec4(const Vec3 &v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
        float &operator[](int i) { return (&x)[i]; }
        float operator[](int i) const { return (&x)[i]; }
        Vec3 xyz() const { return {x, y, z}; }
    };

    inline Vec4 operator+(const Vec4 &a, const Vec4 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
    inline Vec4 operator*(const Vec4 &a, float s) { return {a.x * s, a.y * s, a.z * s, a.w * s}; }

    inline float dot4(const Vec4 &a, const Vec4 &b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    // ------------------------------------------------------------------ mat4
    // Column-major: m[col][row], exactly 16 floats -> matches GLSL mat4 / std140.
    struct alignas(16) Mat4
    {
        float m[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1};

        Mat4() = default;
        static Mat4 identity() { return Mat4{}; }
        static Mat4 fromArray(const float *p)
        {
            Mat4 r;
            std::memcpy(r.m, p, 16 * sizeof(float));
            return r;
        }
        float *operator[](int col) { return &m[col * 4]; }
        const float *operator[](int col) const { return &m[col * 4]; }

        const float *data() const { return m; }
        float *data() { return m; }
    };

    inline Mat4 operator*(const Mat4 &a, const Mat4 &b)
    {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
        {
            for (int row = 0; row < 4; ++row)
            {
                float s = 0.0f;
                for (int k = 0; k < 4; ++k)
                    s += a.m[k * 4 + row] * b.m[c * 4 + k];
                r.m[c * 4 + row] = s;
            }
        }
        return r;
    }

    inline Vec4 operator*(const Mat4 &a, const Vec4 &v)
    {
        Vec4 r;
        for (int row = 0; row < 4; ++row)
        {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k)
                s += a.m[k * 4 + row] * v[k];
            r[row] = s;
        }
        return r;
    }

    Mat4 translate(const Vec3 &t);
    Mat4 scale(const Vec3 &s);
    Mat4 rotateX(float rad);
    Mat4 rotateY(float rad);
    Mat4 rotateZ(float rad);
    Mat4 rotateAxis(const Vec3 &axis, float rad);
    Mat4 lookAt(const Vec3 &eye, const Vec3 &center, const Vec3 &up);
    Mat4 perspective(float fovyRad, float aspect, float zNear, float zFar, bool zeroToOneDepth, bool flipY);
    Mat4 ortho(float left, float right, float bottom, float top, float zNear, float zFar, bool zeroToOneDepth);
    Mat4 inverse(const Mat4 &a);
    Mat4 transpose(const Mat4 &a);
    Mat4 normalMat3(const Mat4 &model);
    Vec3 transformPoint(const Mat4 &m, const Vec3 &p);
    Vec3 transformDirection(const Mat4 &m, const Vec3 &d);

    // ------------------------------------------------------------------ quat
    struct Quat
    {
        float x{0.0f}, y{0.0f}, z{0.0f}, w{1.0f};
        Quat() = default;
        Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
        static Quat identity() { return Quat{0, 0, 0, 1}; }
    };

    Quat quatFromAxisAngle(const Vec3 &axis, float rad);
    Quat quatFromEuler(float pitchX, float yawY, float rollZ);
    Quat quatNormalize(const Quat &q);
    Quat quatMul(const Quat &a, const Quat &b);
    Vec3 quatRotate(const Quat &q, const Vec3 &v);
    Mat4 quatToMat4(const Quat &q);

    // ------------------------------------------------------------------ AABB
    struct AABB
    {
        Vec3 min{1e30f};
        Vec3 max{-1e30f};
        void reset()
        {
            min = Vec3(1e30f);
            max = Vec3(-1e30f);
        }
        void addPoint(const Vec3 &p)
        {
            min = minV(min, p);
            max = maxV(max, p);
        }
        Vec3 center() const { return (min + max) * 0.5f; }
        Vec3 extent() const { return max - min; }
        bool valid() const { return min.x <= max.x; }
    };

    // Convenience color helpers (linear / sRGB)
    inline float srgbToLinear(float c)
    {
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
    inline float linearToSrgb(float c)
    {
        return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    }

    // --------------------------------------------------------------------- //
    //                          inline implementations                        //
    // --------------------------------------------------------------------- //

    inline Mat4 translate(const Vec3 &t)
    {
        Mat4 r;
        r.m[12] = t.x;
        r.m[13] = t.y;
        r.m[14] = t.z;
        return r;
    }

    inline Mat4 scale(const Vec3 &s)
    {
        Mat4 r;
        r.m[0] = s.x;
        r.m[5] = s.y;
        r.m[10] = s.z;
        r.m[15] = 1.0f;
        return r;
    }

    inline Mat4 rotateX(float rad)
    {
        Mat4 r;
        float c = std::cos(rad), s = std::sin(rad);
        r.m[5] = c;
        r.m[6] = s;
        r.m[9] = -s;
        r.m[10] = c;
        return r;
    }

    inline Mat4 rotateY(float rad)
    {
        Mat4 r;
        float c = std::cos(rad), s = std::sin(rad);
        r.m[0] = c;
        r.m[2] = -s;
        r.m[8] = s;
        r.m[10] = c;
        return r;
    }

    inline Mat4 rotateZ(float rad)
    {
        Mat4 r;
        float c = std::cos(rad), s = std::sin(rad);
        r.m[0] = c;
        r.m[1] = s;
        r.m[4] = -s;
        r.m[5] = c;
        return r;
    }

    inline Mat4 rotateAxis(const Vec3 &axis, float rad)
    {
        Vec3 a = normalize(axis);
        float c = std::cos(rad), s = std::sin(rad), t = 1.0f - c;
        Mat4 r;
        r.m[0] = t * a.x * a.x + c;
        r.m[1] = t * a.x * a.y + s * a.z;
        r.m[2] = t * a.x * a.z - s * a.y;
        r.m[4] = t * a.x * a.y - s * a.z;
        r.m[5] = t * a.y * a.y + c;
        r.m[6] = t * a.y * a.z + s * a.x;
        r.m[8] = t * a.x * a.z + s * a.y;
        r.m[9] = t * a.y * a.z - s * a.x;
        r.m[10] = t * a.z * a.z + c;
        r.m[15] = 1.0f;
        return r;
    }

    inline Mat4 lookAt(const Vec3 &eye, const Vec3 &center, const Vec3 &up)
    {
        Vec3 f = normalize(center - eye);
        Vec3 s = normalize(cross(f, up));
        Vec3 u = cross(s, f);
        Mat4 r;
        r.m[0] = s.x;
        r.m[4] = s.y;
        r.m[8] = s.z;
        r.m[1] = u.x;
        r.m[5] = u.y;
        r.m[9] = u.z;
        r.m[2] = -f.x;
        r.m[6] = -f.y;
        r.m[10] = -f.z;
        r.m[12] = -dot(s, eye);
        r.m[13] = -dot(u, eye);
        r.m[14] = dot(f, eye);
        return r;
    }

    inline Mat4 perspective(float fovyRad, float aspect, float zNear, float zFar,
                            bool zeroToOneDepth, bool flipY)
    {
        float tanHalf = std::tan(fovyRad * 0.5f);
        Mat4 r;
        std::memset(r.m, 0, sizeof(r.m));
        r.m[0] = 1.0f / (aspect * tanHalf);
        r.m[5] = (flipY ? -1.0f : 1.0f) / tanHalf;
        if (zeroToOneDepth)
        {
            // Vulkan / Metal style depth [0, 1]
            r.m[10] = zFar / (zNear - zFar);
            r.m[11] = -1.0f;
            r.m[14] = -(zFar * zNear) / (zFar - zNear);
        }
        else
        {
            // OpenGL style depth [-1, 1]
            r.m[10] = -(zFar + zNear) / (zFar - zNear);
            r.m[11] = -1.0f;
            r.m[14] = -(2.0f * zFar * zNear) / (zFar - zNear);
        }
        r.m[15] = 0.0f;
        return r;
    }

    inline Mat4 ortho(float left, float right, float bottom, float top, float zNear, float zFar,
                      bool zeroToOneDepth)
    {
        Mat4 r;
        std::memset(r.m, 0, sizeof(r.m));
        r.m[0] = 2.0f / (right - left);
        r.m[5] = 2.0f / (top - bottom);
        if (zeroToOneDepth)
        {
            r.m[10] = -1.0f / (zFar - zNear);
            r.m[14] = -zNear / (zFar - zNear);
        }
        else
        {
            r.m[10] = -2.0f / (zFar - zNear);
            r.m[14] = -(zFar + zNear) / (zFar - zNear);
        }
        r.m[15] = 1.0f;
        return r;
    }

    inline Mat4 transpose(const Mat4 &a)
    {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row)
                r.m[c * 4 + row] = a.m[row * 4 + c];
        return r;
    }

    inline Mat4 inverse(const Mat4 &a)
    {
        const float *m = a.m;
        float inv[16];
        inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
                 m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
                 m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
                 m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[12] + m[8] * m[5] * m[14] -
                  m[8] * m[6] * m[12] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
                 m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
        inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
                 m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
        inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
                 m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
        inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[12] - m[8] * m[1] * m[14] +
                  m[8] * m[2] * m[12] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
        inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
                 m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
        inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
                 m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
        inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
                  m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
        inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[12] + m[4] * m[1] * m[14] -
                  m[4] * m[2] * m[12] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
        inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
                 m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
        inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
                 m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
        inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
                  m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
        inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
                  m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
        float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        det = 1.0f / det;
        Mat4 r;
        for (int i = 0; i < 16; ++i)
            r.m[i] = inv[i] * det;
        return r;
    }

    inline Vec3 transformPoint(const Mat4 &m, const Vec3 &p)
    {
        Vec4 v = m * Vec4(p, 1.0f);
        return v.xyz() / v.w;
    }

    inline Vec3 transformDirection(const Mat4 &m, const Vec3 &d)
    {
        Vec4 v = m * Vec4(d, 0.0f);
        return normalize(Vec3(v.x, v.y, v.z));
    }

    inline Mat4 normalMat3(const Mat4 &model)
    {
        // upper 3x3 of inverse transpose, padded to mat4 for a std140 mat4 slot
        Mat4 inv = inverse(model);
        Mat4 t = transpose(inv);
        Mat4 r;
        for (int c = 0; c < 3; ++c)
            for (int row = 0; row < 3; ++row)
                r.m[c * 4 + row] = t.m[c * 4 + row];
        return r;
    }

    // ------------------------------- quaternion
    inline Quat quatFromAxisAngle(const Vec3 &axis, float rad)
    {
        Vec3 a = normalize(axis);
        float h = rad * 0.5f;
        float s = std::sin(h);
        return Quat{a.x * s, a.y * s, a.z * s, std::cos(h)};
    }

    inline Quat quatFromEuler(float pitchX, float yawY, float rollZ)
    {
        return quatMul(quatMul(quatFromAxisAngle(Vec3(0, 0, 1), rollZ),
                               quatFromAxisAngle(Vec3(0, 1, 0), yawY)),
                       quatFromAxisAngle(Vec3(1, 0, 0), pitchX));
    }

    inline Quat quatNormalize(const Quat &q)
    {
        float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (l < kEpsilon)
            return Quat::identity();
        return Quat{q.x / l, q.y / l, q.z / l, q.w / l};
    }

    inline Quat quatMul(const Quat &a, const Quat &b)
    {
        return Quat{
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    }

    inline Vec3 quatRotate(const Quat &q, const Vec3 &v)
    {
        Vec3 u{q.x, q.y, q.z};
        float s = q.w;
        return u * (2.0f * dot(u, v)) + v * (s * s - dot(u, u)) + cross(u, v) * (2.0f * s);
    }

    inline Mat4 quatToMat4(const Quat &q)
    {
        Mat4 r;
        float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        r.m[0] = 1.0f - 2.0f * (yy + zz);
        r.m[1] = 2.0f * (xy + wz);
        r.m[2] = 2.0f * (xz - wy);
        r.m[4] = 2.0f * (xy - wz);
        r.m[5] = 1.0f - 2.0f * (xx + zz);
        r.m[6] = 2.0f * (yz + wx);
        r.m[8] = 2.0f * (xz + wy);
        r.m[9] = 2.0f * (yz - wx);
        r.m[10] = 1.0f - 2.0f * (xx + yy);
        r.m[15] = 1.0f;
        return r;
    }

    inline Mat4 composeTRS(const Vec3 &pos, const Quat &rot, const Vec3 &scl)
    {
        Mat4 r = quatToMat4(quatNormalize(rot));
        r.m[0] *= scl.x; r.m[1] *= scl.x; r.m[2] *= scl.x;
        r.m[4] *= scl.y; r.m[5] *= scl.y; r.m[6] *= scl.y;
        r.m[8] *= scl.z; r.m[9] *= scl.z; r.m[10] *= scl.z;
        r.m[12] = pos.x; r.m[13] = pos.y; r.m[14] = pos.z;
        r.m[15] = 1.0f;
        return r;
    }
}
