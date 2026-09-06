/*
 * Genesis Render - core math
 * Minimal column-major vector/matrix/quaternion math shared by every backend.
 * Matrices match OpenGL/Vulkan uniform-block layout (std140-compatible).
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace Genesis {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;
constexpr float kRad2Deg = 180.0f / kPi;

inline float radians(float deg) { return deg * kDeg2Rad; }

struct Vec2 { float x{0}, y{0}; };
struct Vec3 {
    float x{0}, y{0}, z{0};
    Vec3() = default;
    Vec3(float s) : x(s), y(s), z(s) {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};
struct Vec4 { float x{0}, y{0}, z{0}, w{0}; };

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator-(const Vec3& a) { return {-a.x, -a.y, -a.z}; }
inline Vec3 operator*(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, const Vec3& a) { return a * s; }
inline Vec3 operator*(const Vec3& a, const Vec3& b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline Vec3& operator+=(Vec3& a, const Vec3& b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline Vec3& operator-=(Vec3& a, const Vec3& b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
inline Vec3& operator*=(Vec3& a, float s) { a.x *= s; a.y *= s; a.z *= s; return a; }

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(const Vec3& a) {
    float l = length(a);
    return l > 1e-12f ? a * (1.0f / l) : Vec3{0, 0, 0};
}
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

struct Quat {
    float x{0}, y{0}, z{0}, w{1}; // identity
};
inline Quat quatFromAxisAngle(const Vec3& axis, float angleRad) {
    float h = angleRad * 0.5f;
    float s = std::sin(h);
    Vec3 n = normalize(axis);
    return {n.x * s, n.y * s, n.z * s, std::cos(h)};
}
inline Quat quatFromEuler(float pitchX, float yawY, float rollZ) {
    // Y * X * Z intrinsic
    Quat qx = quatFromAxisAngle({1, 0, 0}, pitchX);
    Quat qy = quatFromAxisAngle({0, 1, 0}, yawY);
    Quat qz = quatFromAxisAngle({0, 0, 1}, rollZ);
    auto mul = [](const Quat& a, const Quat& b) {
        return Quat{a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                    a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                    a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                    a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    };
    return mul(mul(qy, qx), qz);
}
inline Vec3 quatRotate(const Quat& q, const Vec3& v) {
    Vec3 u{q.x, q.y, q.z};
    float s = q.w;
    return 2.0f * dot(u, v) * u + (s * s - dot(u, u)) * v + 2.0f * s * cross(u, v);
}
inline Quat normalize(const Quat& q) {
    float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l < 1e-12f) return {};
    return {q.x / l, q.y / l, q.z / l, q.w / l};
}

// Quaternion -> intrinsic YXZ euler radians (matches quatFromEuler).
inline Vec3 quatToEuler(const Quat& q) {
    // yaw (Y)
    float sinp = 2.0f * (q.w * q.y - q.z * q.x);
    float pitch = std::asin(std::clamp(sinp, -1.0f, 1.0f));
    float yaw = std::atan2(2.0f * (q.w * q.x + q.y * q.z),
                           1.0f - 2.0f * (q.x * q.x + q.y * q.y));
    float roll = std::atan2(2.0f * (q.w * q.z + q.x * q.y),
                            1.0f - 2.0f * (q.y * q.y + q.z * q.z));
    return {pitch, yaw, roll};
}

// Column-major 4x4, memory layout identical to GLSL mat4 (std140).
struct Mat4 {
    float m[16];
    Mat4(float d = 1.0f) {
        for (int i = 0; i < 16; ++i) m[i] = 0.0f;
        m[0] = m[5] = m[10] = m[15] = d;
    }
    float& at(int col, int row) { return m[col * 4 + row]; }
    float at(int col, int row) const { return m[col * 4 + row]; }
};

inline Vec4 operator*(const Mat4& a, const Vec4& v) {
    return Vec4{a.m[0] * v.x + a.m[4] * v.y + a.m[8] * v.z + a.m[12] * v.w,
                a.m[1] * v.x + a.m[5] * v.y + a.m[9] * v.z + a.m[13] * v.w,
                a.m[2] * v.x + a.m[6] * v.y + a.m[10] * v.z + a.m[14] * v.w,
                a.m[3] * v.x + a.m[7] * v.y + a.m[11] * v.z + a.m[15] * v.w};
}

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r(0.0f);
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a.at(k, row) * b.at(c, k);
            r.at(c, row) = s;
        }
    return r;
}

inline Mat4 makeTranslation(const Vec3& t) {
    Mat4 r;
    r.at(3, 0) = t.x;
    r.at(3, 1) = t.y;
    r.at(3, 2) = t.z;
    return r;
}
inline Mat4 makeScale(const Vec3& s) {
    Mat4 r;
    r.at(0, 0) = s.x;
    r.at(1, 1) = s.y;
    r.at(2, 2) = s.z;
    return r;
}
inline Mat4 makeQuatRotation(const Quat& q) {
    Quat n = normalize(q);
    float xx = n.x * n.x, yy = n.y * n.y, zz = n.z * n.z;
    float xy = n.x * n.y, xz = n.x * n.z, yz = n.y * n.z;
    float wx = n.w * n.x, wy = n.w * n.y, wz = n.w * n.z;
    Mat4 r;
    // rotation rows stored column-major
    r.at(0, 0) = 1.0f - 2.0f * (yy + zz);
    r.at(1, 0) = 2.0f * (xy + wz);
    r.at(2, 0) = 2.0f * (xz - wy);
    r.at(0, 1) = 2.0f * (xy - wz);
    r.at(1, 1) = 1.0f - 2.0f * (xx + zz);
    r.at(2, 1) = 2.0f * (yz + wx);
    r.at(0, 2) = 2.0f * (xz + wy);
    r.at(1, 2) = 2.0f * (yz - wx);
    r.at(2, 2) = 1.0f - 2.0f * (xx + yy);
    return r;
}

// Build world matrix from translation / euler(radians) / scale.
inline Mat4 makeTransform(const Vec3& pos, const Vec3& eulerRad, const Vec3& scale) {
    Mat4 t = makeTranslation(pos);
    Mat4 r = makeQuatRotation(quatFromEuler(eulerRad.x, eulerRad.y, eulerRad.z));
    Mat4 s = makeScale(scale);
    return t * r * s;
}

inline Mat4 makeLookAt(const Vec3& eye, const Vec3& center, const Vec3& upHint) {
    Vec3 f = normalize(center - eye);
    Vec3 s = normalize(cross(f, upHint));
    Vec3 u = cross(s, f);
    Mat4 r;
    // rows s/u/-f, stored column-major: at(column, row)
    r.at(0, 0) = s.x;  r.at(1, 0) = s.y;  r.at(2, 0) = s.z;
    r.at(0, 1) = u.x;  r.at(1, 1) = u.y;  r.at(2, 1) = u.z;
    r.at(0, 2) = -f.x; r.at(1, 2) = -f.y; r.at(2, 2) = -f.z;
    Mat4 t = makeTranslation(-eye);
    return r * t;
}

// right-handed perspective.
// Vulkan clip space has y pointing down and z in [0,1]; OpenGL has y up, z in [-1,1].
inline Mat4 makePerspective(float fovyRad, float aspect, float zNear, float zFar,
                            bool vulkanClip) {
    float f = 1.0f / std::tan(fovyRad * 0.5f);
    Mat4 r(0.0f);
    r.at(0, 0) = f / aspect;
    r.at(1, 1) = vulkanClip ? -f : f;
    if (vulkanClip) {
        r.at(2, 2) = zFar / (zNear - zFar);
        r.at(2, 3) = -1.0f;
        r.at(3, 2) = (zNear * zFar) / (zNear - zFar);
    } else {
        r.at(2, 2) = (zFar + zNear) / (zNear - zFar);
        r.at(2, 3) = -1.0f;
        r.at(3, 2) = (2.0f * zNear * zFar) / (zNear - zFar);
    }
    return r;
}

inline Mat4 makeOrtho(float left, float right, float bottom, float top, float zNear, float zFar) {
    Mat4 r;
    r.at(0, 0) = 2.0f / (right - left);
    r.at(1, 1) = 2.0f / (top - bottom);
    r.at(2, 2) = -2.0f / (zFar - zNear);
    r.at(3, 0) = -(right + left) / (right - left);
    r.at(3, 1) = -(top + bottom) / (top - bottom);
    r.at(3, 2) = -(zFar + zNear) / (zFar - zNear);
    return r;
}

// Normal matrix: mat3(transpose(inverse(model))), padded to mat4 for std140.
inline Mat4 makeNormalMatrix(const Mat4& model) {
    // invert upper 3x3 via cofactors
    Mat4 n(0.0f);
    auto a = [&](int r, int c) { return model.at(c, r); };
    float c00 = a(1,1)*a(2,2)-a(1,2)*a(2,1);
    float c01 = a(1,2)*a(2,0)-a(1,0)*a(2,2);
    float c02 = a(1,0)*a(2,1)-a(1,1)*a(2,0);
    float c10 = a(0,2)*a(2,1)-a(0,1)*a(2,2);
    float c11 = a(0,0)*a(2,2)-a(0,2)*a(2,0);
    float c12 = a(0,1)*a(2,0)-a(0,0)*a(2,1);
    float c20 = a(0,1)*a(1,2)-a(0,2)*a(1,1);
    float c21 = a(0,2)*a(1,0)-a(0,0)*a(1,2);
    float c22 = a(0,0)*a(1,1)-a(0,1)*a(1,0);
    // det of first row
    float det = a(0,0)*c00 + a(0,1)*c01 + a(0,2)*c02;
    if (std::fabs(det) < 1e-12f) det = 1.0f;
    float inv = 1.0f / det;
    // transpose of cofactor / det
    n.at(0,0)=c00*inv; n.at(0,1)=c10*inv; n.at(0,2)=c20*inv;
    n.at(1,0)=c01*inv; n.at(1,1)=c11*inv; n.at(1,2)=c21*inv;
    n.at(2,0)=c02*inv; n.at(2,1)=c12*inv; n.at(2,2)=c22*inv;
    n.at(3,3)=1.0f;
    return n;
}

// Pixel scale for IBL convolutions.
inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

struct TRS {
    Vec3 position;
    Vec3 euler;
    Vec3 scale;
};

// Decompose an affine transform (ignores shear/skew) into T * R * S.
inline TRS decompose(const Mat4& m) {
    TRS r;
    r.position = {m.m[12], m.m[13], m.m[14]};
    Vec3 c0{m.m[0], m.m[1], m.m[2]};
    Vec3 c1{m.m[4], m.m[5], m.m[6]};
    Vec3 c2{m.m[8], m.m[9], m.m[10]};
    r.scale = {length(c0), length(c1), length(c2)};
    auto col = [&](Vec3 c, float s) -> Vec3 {
        return s > 1e-8f ? c * (1.0f / s) : Vec3{0, 0, 0};
    };
    Vec3 x = col(c0, r.scale.x), y = col(c1, r.scale.y), z = col(c2, r.scale.z);
    // build quaternion from orthonormal matrix rows
    float m00=x.x,m01=y.x,m02=z.x, m10=x.y,m11=y.y,m12=z.y, m20=x.z,m21=y.z,m22=z.z;
    float trace = m00 + m11 + m22;
    Quat q;
    if (trace > 0) {
        float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s; q.x = (m21 - m12) / s; q.y = (m02 - m20) / s; q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q.w = (m21 - m12) / s; q.x = 0.25f * s; q.y = (m01 + m10) / s; q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q.w = (m02 - m20) / s; q.x = (m01 + m10) / s; q.y = 0.25f * s; q.z = (m12 + m21) / s;
    } else {
        float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q.w = (m10 - m01) / s; q.x = (m02 + m20) / s; q.y = (m12 + m21) / s; q.z = 0.25f * s;
    }
    r.euler = quatToEuler(q);
    return r;
}

} // namespace Genesis
