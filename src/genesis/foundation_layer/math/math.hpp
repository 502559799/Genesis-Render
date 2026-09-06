/*
 * Genesis Render - Foundation Layer / Math
 *
 * Header-only math library used by the whole engine.
 *
 * Conventions (chosen to match GLSL / SPIR-V and std140 UBO layout):
 *   - Matrices are COLUMN-MAJOR, stored as float m[16], m[col * 4 + row].
 *   - Vectors are column vectors: v' = M * v  (operator*(Mat4, Vec4)).
 *   - Right-handed coordinate system, +Y up, -Z forward (OpenGL style).
 *   - Perspective uses a [0, 1] clip-Z range for Vulkan and [-1, 1] for GL,
 *     selected via Mat4::perspective(..., bool zero_to_one_depth).
 *   - Angles are in radians unless a function name says Deg.
 */
#ifndef __GENESIS_MATH_HPP__
#define __GENESIS_MATH_HPP__

#include <cmath>
#include <cstring>
#include <algorithm>
#include <cstdint>

namespace Genesis
{
    namespace Math
    {
        static constexpr float PI = 3.14159265358979323846f;
        static constexpr float TWO_PI = 6.28318530717958647692f;
        static constexpr float HALF_PI = 1.57079632679489661923f;
        static constexpr float EPSILON = 1e-6f;

        inline float toRadians(float deg) { return deg * (PI / 180.0f); }
        inline float toDegrees(float rad) { return rad * (180.0f / PI); }
        inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
        inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
        inline float saturate(float v) { return clampf(v, 0.0f, 1.0f); }

        //------------------------------------------------------------------
        // Vec2
        //------------------------------------------------------------------
        struct Vec2
        {
            float x = 0.0f, y = 0.0f;

            Vec2() {}
            Vec2(float s) : x(s), y(s) {}
            Vec2(float x_, float y_) : x(x_), y(y_) {}

            float &operator[](int i) { return (&x)[i]; }
            float operator[](int i) const { return (&x)[i]; }

            Vec2 operator+(const Vec2 &v) const { return Vec2(x + v.x, y + v.y); }
            Vec2 operator-(const Vec2 &v) const { return Vec2(x - v.x, y - v.y); }
            Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
            Vec2 operator/(float s) const { float inv = 1.0f / s; return Vec2(x * inv, y * inv); }
            Vec2 operator-() const { return Vec2(-x, -y); }
            Vec2 &operator+=(const Vec2 &v) { x += v.x; y += v.y; return *this; }
            Vec2 &operator-=(const Vec2 &v) { x -= v.x; y -= v.y; return *this; }
            Vec2 &operator*=(float s) { x *= s; y *= s; return *this; }
            bool operator==(const Vec2 &v) const { return x == v.x && y == v.y; }
        };
        inline Vec2 operator*(float s, const Vec2 &v) { return v * s; }
        inline float dot(const Vec2 &a, const Vec2 &b) { return a.x * b.x + a.y * b.y; }
        inline float length(const Vec2 &v) { return std::sqrt(dot(v, v)); }
        inline Vec2 normalize(const Vec2 &v) { float l = length(v); return l > EPSILON ? v / l : Vec2(0.0f); }

        //------------------------------------------------------------------
        // Vec3
        //------------------------------------------------------------------
        struct Vec3
        {
            float x = 0.0f, y = 0.0f, z = 0.0f;

            Vec3() {}
            Vec3(float s) : x(s), y(s), z(s) {}
            Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
            // Convenience: construct from a float[3] (decays to const float*).
            explicit Vec3(const float *v) : x(v[0]), y(v[1]), z(v[2]) {}

            float &operator[](int i) { return (&x)[i]; }
            float operator[](int i) const { return (&x)[i]; }

            Vec3 operator+(const Vec3 &v) const { return Vec3(x + v.x, y + v.y, z + v.z); }
            Vec3 operator-(const Vec3 &v) const { return Vec3(x - v.x, y - v.y, z - v.z); }
            Vec3 operator*(const Vec3 &v) const { return Vec3(x * v.x, y * v.y, z * v.z); }
            Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
            Vec3 operator/(float s) const { float inv = 1.0f / s; return Vec3(x * inv, y * inv, z * inv); }
            Vec3 operator-() const { return Vec3(-x, -y, -z); }
            Vec3 &operator+=(const Vec3 &v) { x += v.x; y += v.y; z += v.z; return *this; }
            Vec3 &operator-=(const Vec3 &v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
            Vec3 &operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
            Vec3 &operator*=(const Vec3 &v) { x *= v.x; y *= v.y; z *= v.z; return *this; }
            bool operator==(const Vec3 &v) const { return x == v.x && y == v.y && z == v.z; }
            bool operator!=(const Vec3 &v) const { return !(*this == v); }
        };
        inline Vec3 operator*(float s, const Vec3 &v) { return v * s; }
        inline float dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
        inline Vec3 cross(const Vec3 &a, const Vec3 &b)
        {
            return Vec3(a.y * b.z - a.z * b.y,
                        a.z * b.x - a.x * b.z,
                        a.x * b.y - a.y * b.x);
        }
        inline float lengthSquared(const Vec3 &v) { return dot(v, v); }
        inline float length(const Vec3 &v) { return std::sqrt(dot(v, v)); }
        inline float distance(const Vec3 &a, const Vec3 &b) { return length(a - b); }
        inline Vec3 normalize(const Vec3 &v)
        {
            float l = length(v);
            return l > EPSILON ? v / l : Vec3(0.0f);
        }
        inline Vec3 min(const Vec3 &a, const Vec3 &b) { return Vec3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)); }
        inline Vec3 max(const Vec3 &a, const Vec3 &b) { return Vec3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)); }
        inline Vec3 abs(const Vec3 &a) { return Vec3(std::fabs(a.x), std::fabs(a.y), std::fabs(a.z)); }
        inline Vec3 lerp(const Vec3 &a, const Vec3 &b, float t) { return a + (b - a) * t; }
        inline Vec3 reflect(const Vec3 &i, const Vec3 &n) { return i - 2.0f * dot(i, n) * n; }
        inline Vec3 refract(const Vec3 &i, const Vec3 &n, float eta)
        {
            float cosi = dot(-i, n);
            float k = 1.0f - eta * eta * (1.0f - cosi * cosi);
            return k < 0.0f ? Vec3(0.0f) : eta * i + (eta * cosi - std::sqrt(k)) * n;
        }

        //------------------------------------------------------------------
        // Vec4
        //------------------------------------------------------------------
        struct Vec4
        {
            float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;

            Vec4() {}
            Vec4(float s) : x(s), y(s), z(s), w(s) {}
            Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
            Vec4(const Vec3 &v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
            explicit Vec4(const float *v) : x(v[0]), y(v[1]), z(v[2]), w(v[3]) {}

            float &operator[](int i) { return (&x)[i]; }
            float operator[](int i) const { return (&x)[i]; }

            Vec3 xyz() const { return Vec3(x, y, z); }

            Vec4 operator+(const Vec4 &v) const { return Vec4(x + v.x, y + v.y, z + v.z, w + v.w); }
            Vec4 operator-(const Vec4 &v) const { return Vec4(x - v.x, y - v.y, z - v.z, w - v.w); }
            Vec4 operator*(float s) const { return Vec4(x * s, y * s, z * s, w * s); }
            Vec4 operator/(float s) const { float inv = 1.0f / s; return Vec4(x * inv, y * inv, z * inv, w * inv); }
            Vec4 operator-() const { return Vec4(-x, -y, -z, -w); }
            Vec4 &operator+=(const Vec4 &v) { x += v.x; y += v.y; z += v.z; w += v.w; return *this; }
            Vec4 &operator*=(float s) { x *= s; y *= s; z *= s; w *= s; return *this; }
            bool operator==(const Vec4 &v) const { return x == v.x && y == v.y && z == v.z && w == v.w; }
        };
        inline Vec4 operator*(float s, const Vec4 &v) { return v * s; }
        inline float dot(const Vec4 &a, const Vec4 &b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
        inline float length(const Vec4 &v) { return std::sqrt(dot(v, v)); }
        inline Vec4 normalize(const Vec4 &v) { float l = length(v); return l > EPSILON ? v / l : Vec4(0.0f); }

        //------------------------------------------------------------------
        // Quat  (x, y, z, w) - Hamilton, right-handed
        //------------------------------------------------------------------
        struct Quat
        {
            float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

            Quat() {}
            Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

            static Quat identity() { return Quat(0.0f, 0.0f, 0.0f, 1.0f); }

            static Quat fromAxisAngle(const Vec3 &axis, float angle)
            {
                Vec3 a = normalize(axis);
                float h = angle * 0.5f;
                float s = std::sin(h);
                return Quat(a.x * s, a.y * s, a.z * s, std::cos(h));
            }

            // Euler angles in radians, applied in Y-X-Z (yaw, pitch, roll) order,
            // which is the usual FPS/scene-editor convention.
            static Quat fromEulerYXZ(float yaw, float pitch, float roll)
            {
                Quat qy = fromAxisAngle(Vec3(0, 1, 0), yaw);
                Quat qx = fromAxisAngle(Vec3(1, 0, 0), pitch);
                Quat qz = fromAxisAngle(Vec3(0, 0, 1), roll);
                return qy * qx * qz;
            }

            static Quat fromEuler(const Vec3 &euler) { return fromEulerYXZ(euler.y, euler.x, euler.z); }

            Quat operator*(const Quat &q) const
            {
                return Quat(w * q.x + x * q.w + y * q.z - z * q.y,
                            w * q.y - x * q.z + y * q.w + z * q.x,
                            w * q.z + x * q.y - y * q.x + z * q.w,
                            w * q.w - x * q.x - y * q.y - z * q.z);
            }

            Quat conjugate() const { return Quat(-x, -y, -z, w); }

            float lengthSquared() const { return x * x + y * y + z * z + w * w; }
            float length() const { return std::sqrt(lengthSquared()); }

            Quat normalized() const
            {
                float l = length();
                if (l < EPSILON) return identity();
                float inv = 1.0f / l;
                return Quat(x * inv, y * inv, z * inv, w * inv);
            }

            Quat inverse() const
            {
                float ls = lengthSquared();
                if (ls < EPSILON) return identity();
                float inv = 1.0f / ls;
                return Quat(-x * inv, -y * inv, -z * inv, w * inv);
            }

            Vec3 rotate(const Vec3 &v) const
            {
                // q * v * q^-1, optimised
                Vec3 qv(x, y, z);
                Vec3 t = 2.0f * cross(qv, v);
                return v + w * t + cross(qv, t);
            }

            static Quat slerp(const Quat &a, const Quat &b, float t)
            {
                float cosTheta = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
                Quat bb = b;
                if (cosTheta < 0.0f)
                {
                    cosTheta = -cosTheta;
                    bb = Quat(-b.x, -b.y, -b.z, -b.w);
                }
                if (cosTheta > 0.9995f)
                {
                    return Quat(a.x + (bb.x - a.x) * t, a.y + (bb.y - a.y) * t,
                                a.z + (bb.z - a.z) * t, a.w + (bb.w - a.w) * t)
                        .normalized();
                }
                float theta = std::acos(clampf(cosTheta, -1.0f, 1.0f));
                float sinTheta = std::sin(theta);
                float wa = std::sin((1.0f - t) * theta) / sinTheta;
                float wb = std::sin(t * theta) / sinTheta;
                return Quat(wa * a.x + wb * bb.x, wa * a.y + wb * bb.y,
                            wa * a.z + wb * bb.z, wa * a.w + wb * bb.w);
            }

            // look-rotation: -Z forward (right-handed), +Y up
            static Quat lookRotation(const Vec3 &forward, const Vec3 &up)
            {
                Vec3 f = normalize(-forward); // our convention: camera looks down -Z
                Vec3 r = normalize(cross(up, f));
                if (dot(r, r) < EPSILON) return identity();
                Vec3 u = cross(f, r);

                float m00 = r.x, m01 = u.x, m02 = f.x;
                float m10 = r.y, m11 = u.y, m12 = f.y;
                float m20 = r.z, m21 = u.z, m22 = f.z;

                float trace = m00 + m11 + m22;
                Quat q;
                if (trace > 0.0f)
                {
                    float s = std::sqrt(trace + 1.0f) * 2.0f;
                    q = Quat((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s);
                }
                else if (m00 > m11 && m00 > m22)
                {
                    float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
                    q = Quat(0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
                }
                else if (m11 > m22)
                {
                    float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
                    q = Quat((m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s);
                }
                else
                {
                    float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
                    q = Quat((m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s);
                }
                return q.normalized();
            }
        };

        //------------------------------------------------------------------
        // Mat4 - column major
        //------------------------------------------------------------------
        struct Mat4
        {
            float m[16];

            // Default constructs to the identity matrix.  NOTE: this must not
            // call identity(), because identity() itself default constructs a
            // Mat4 - that recursion overflowed the stack.
            Mat4()
            {
                std::memset(m, 0, sizeof(m));
                m[0] = m[5] = m[10] = m[15] = 1.0f;
            }
            explicit Mat4(const float *src) { std::memcpy(m, src, sizeof(m)); }
            Mat4(float m00, float m01, float m02, float m03,
                 float m10, float m11, float m12, float m13,
                 float m20, float m21, float m22, float m23,
                 float m30, float m31, float m32, float m33)
            {
                // arguments are given ROW major, storage is column major
                m[0] = m00; m[4] = m01; m[8] = m02; m[12] = m03;
                m[1] = m10; m[5] = m11; m[9] = m12; m[13] = m13;
                m[2] = m20; m[6] = m21; m[10] = m22; m[14] = m23;
                m[3] = m30; m[7] = m31; m[11] = m32; m[15] = m33;
            }

            float &at(int row, int col) { return m[col * 4 + row]; }
            float at(int row, int col) const { return m[col * 4 + row]; }

            static Mat4 identity() { return Mat4(); }

            static Mat4 zero()
            {
                Mat4 r;
                std::memset(r.m, 0, sizeof(r.m));
                return r;
            }

            Vec4 column(int c) const { return Vec4(m[c * 4 + 0], m[c * 4 + 1], m[c * 4 + 2], m[c * 4 + 3]); }
            Vec3 translation() const { return Vec3(m[12], m[13], m[14]); }
            Vec3 xAxis() const { return Vec3(m[0], m[1], m[2]); }
            Vec3 yAxis() const { return Vec3(m[4], m[5], m[6]); }
            Vec3 zAxis() const { return Vec3(m[8], m[9], m[10]); }

            Mat4 operator*(const Mat4 &b) const
            {
                const Mat4 &a = *this;
                Mat4 r = zero();
                for (int c = 0; c < 4; ++c)
                {
                    for (int row = 0; row < 4; ++row)
                    {
                        float sum = 0.0f;
                        for (int k = 0; k < 4; ++k)
                            sum += a.m[k * 4 + row] * b.m[c * 4 + k];
                        r.m[c * 4 + row] = sum;
                    }
                }
                return r;
            }

            Vec4 operator*(const Vec4 &v) const
            {
                return Vec4(m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w,
                            m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
                            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
                            m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w);
            }

            Vec3 transformPoint(const Vec3 &p) const
            {
                Vec4 r = (*this) * Vec4(p, 1.0f);
                if (std::fabs(r.w) < EPSILON) return r.xyz();
                return r.xyz() / r.w;
            }

            Vec3 transformDirection(const Vec3 &d) const
            {
                return Vec3(m[0] * d.x + m[4] * d.y + m[8] * d.z,
                            m[1] * d.x + m[5] * d.y + m[9] * d.z,
                            m[2] * d.x + m[6] * d.y + m[10] * d.z);
            }

            Mat4 transposed() const
            {
                Mat4 r;
                for (int c = 0; c < 4; ++c)
                    for (int row = 0; row < 4; ++row)
                        r.m[c * 4 + row] = m[row * 4 + c];
                return r;
            }

            // General 4x4 inverse (cofactor / adjugate method).
            Mat4 inversed() const
            {
                const float *mm = m;
                float inv[16];
                inv[0] = mm[5] * mm[10] * mm[15] - mm[5] * mm[11] * mm[14] - mm[9] * mm[6] * mm[15] +
                         mm[9] * mm[7] * mm[14] + mm[13] * mm[6] * mm[11] - mm[13] * mm[7] * mm[10];
                inv[4] = -mm[4] * mm[10] * mm[15] + mm[4] * mm[11] * mm[14] + mm[8] * mm[6] * mm[15] -
                         mm[8] * mm[7] * mm[14] - mm[12] * mm[6] * mm[11] + mm[12] * mm[7] * mm[10];
                inv[8] = mm[4] * mm[9] * mm[15] - mm[4] * mm[11] * mm[13] - mm[8] * mm[5] * mm[15] +
                         mm[8] * mm[7] * mm[13] + mm[12] * mm[5] * mm[11] - mm[12] * mm[7] * mm[9];
                inv[12] = -mm[4] * mm[9] * mm[14] + mm[4] * mm[10] * mm[13] + mm[8] * mm[5] * mm[14] -
                          mm[8] * mm[6] * mm[13] - mm[12] * mm[5] * mm[10] + mm[12] * mm[6] * mm[9];
                inv[1] = -mm[1] * mm[10] * mm[15] + mm[1] * mm[11] * mm[14] + mm[9] * mm[2] * mm[15] -
                         mm[9] * mm[3] * mm[14] - mm[13] * mm[2] * mm[11] + mm[13] * mm[3] * mm[10];
                inv[5] = mm[0] * mm[10] * mm[15] - mm[0] * mm[11] * mm[14] - mm[8] * mm[2] * mm[15] +
                         mm[8] * mm[3] * mm[14] + mm[12] * mm[2] * mm[11] - mm[12] * mm[3] * mm[10];
                inv[9] = -mm[0] * mm[9] * mm[15] + mm[0] * mm[11] * mm[13] + mm[8] * mm[1] * mm[15] -
                         mm[8] * mm[3] * mm[13] - mm[12] * mm[1] * mm[11] + mm[12] * mm[3] * mm[9];
                inv[13] = mm[0] * mm[9] * mm[14] - mm[0] * mm[10] * mm[13] - mm[8] * mm[1] * mm[14] +
                          mm[8] * mm[2] * mm[13] + mm[12] * mm[1] * mm[10] - mm[12] * mm[2] * mm[9];
                inv[2] = mm[1] * mm[6] * mm[15] - mm[1] * mm[7] * mm[14] - mm[5] * mm[2] * mm[15] +
                         mm[5] * mm[3] * mm[14] + mm[13] * mm[2] * mm[7] - mm[13] * mm[3] * mm[6];
                inv[6] = -mm[0] * mm[6] * mm[15] + mm[0] * mm[7] * mm[14] + mm[4] * mm[2] * mm[15] -
                         mm[4] * mm[3] * mm[14] - mm[12] * mm[2] * mm[7] + mm[12] * mm[3] * mm[6];
                inv[10] = mm[0] * mm[5] * mm[15] - mm[0] * mm[7] * mm[13] - mm[4] * mm[1] * mm[15] +
                          mm[4] * mm[3] * mm[13] + mm[12] * mm[1] * mm[7] - mm[12] * mm[3] * mm[5];
                inv[14] = -mm[0] * mm[5] * mm[14] + mm[0] * mm[6] * mm[13] + mm[4] * mm[1] * mm[14] -
                          mm[4] * mm[2] * mm[13] - mm[12] * mm[1] * mm[6] + mm[12] * mm[2] * mm[5];
                inv[3] = -mm[1] * mm[6] * mm[11] + mm[1] * mm[7] * mm[10] + mm[5] * mm[2] * mm[11] -
                         mm[5] * mm[3] * mm[10] - mm[9] * mm[2] * mm[7] + mm[9] * mm[3] * mm[6];
                inv[7] = mm[0] * mm[6] * mm[11] - mm[0] * mm[7] * mm[10] - mm[4] * mm[2] * mm[11] +
                         mm[4] * mm[3] * mm[10] + mm[8] * mm[2] * mm[7] - mm[8] * mm[3] * mm[6];
                inv[11] = -mm[0] * mm[5] * mm[11] + mm[0] * mm[7] * mm[9] + mm[4] * mm[1] * mm[11] -
                          mm[4] * mm[3] * mm[9] - mm[8] * mm[1] * mm[7] + mm[8] * mm[3] * mm[5];
                inv[15] = mm[0] * mm[5] * mm[10] - mm[0] * mm[6] * mm[9] - mm[4] * mm[1] * mm[10] +
                          mm[4] * mm[2] * mm[9] + mm[8] * mm[1] * mm[6] - mm[8] * mm[2] * mm[5];

                float det = mm[0] * inv[0] + mm[1] * inv[4] + mm[2] * inv[8] + mm[3] * inv[12];
                if (std::fabs(det) < 1e-12f)
                    return Mat4::identity();
                float invDet = 1.0f / det;
                Mat4 r;
                for (int i = 0; i < 16; ++i) r.m[i] = inv[i] * invDet;
                return r;
            }

            static Mat4 translate(const Vec3 &t)
            {
                Mat4 r = identity();
                r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
                return r;
            }

            static Mat4 scale(const Vec3 &s)
            {
                Mat4 r = identity();
                r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
                return r;
            }

            static Mat4 fromQuat(const Quat &q)
            {
                float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
                float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
                float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
                Mat4 r = identity();
                r.m[0] = 1.0f - 2.0f * (yy + zz);
                r.m[1] = 2.0f * (xy + wz);
                r.m[2] = 2.0f * (xz - wy);
                r.m[4] = 2.0f * (xy - wz);
                r.m[5] = 1.0f - 2.0f * (xx + zz);
                r.m[6] = 2.0f * (yz + wx);
                r.m[8] = 2.0f * (xz + wy);
                r.m[9] = 2.0f * (yz - wx);
                r.m[10] = 1.0f - 2.0f * (xx + yy);
                return r;
            }

            // TRS composition: M = T * R * S
            static Mat4 compose(const Vec3 &t, const Quat &r, const Vec3 &s)
            {
                Mat4 rm = fromQuat(r);
                Mat4 out = rm;
                // scale columns
                out.m[0] *= s.x; out.m[1] *= s.x; out.m[2] *= s.x;
                out.m[4] *= s.y; out.m[5] *= s.y; out.m[6] *= s.y;
                out.m[8] *= s.z; out.m[9] *= s.z; out.m[10] *= s.z;
                out.m[12] = t.x; out.m[13] = t.y; out.m[14] = t.z;
                out.m[3] = out.m[7] = out.m[11] = 0.0f;
                out.m[15] = 1.0f;
                return out;
            }

            static Mat4 lookAt(const Vec3 &eye, const Vec3 &center, const Vec3 &up)
            {
                Vec3 f = normalize(center - eye);
                Vec3 s = normalize(cross(f, up));
                Vec3 u = cross(s, f);
                Mat4 r = identity();
                r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
                r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
                r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
                r.m[12] = -dot(s, eye);
                r.m[13] = -dot(u, eye);
                r.m[14] = dot(f, eye);
                return r;
            }

            // zero_to_one_depth = true  -> Vulkan clip space [0,1]
            // zero_to_one_depth = false -> OpenGL clip space [-1,1]
            static Mat4 perspective(float fovyRadians, float aspect, float zNear, float zFar, bool zero_to_one_depth)
            {
                float tanHalf = std::tan(fovyRadians * 0.5f);
                Mat4 r = zero();
                r.m[0] = 1.0f / (aspect * tanHalf);
                r.m[5] = 1.0f / tanHalf;
                r.m[15] = 0.0f;
                if (zero_to_one_depth)
                {
                    r.m[10] = zFar / (zNear - zFar);
                    r.m[11] = -1.0f;
                    r.m[14] = (zFar * zNear) / (zNear - zFar);
                }
                else
                {
                    r.m[10] = (zFar + zNear) / (zNear - zFar);
                    r.m[11] = -1.0f;
                    r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
                }
                return r;
            }

            static Mat4 ortho(float left, float right, float bottom, float top, float zNear, float zFar, bool zero_to_one_depth)
            {
                Mat4 r = identity();
                r.m[0] = 2.0f / (right - left);
                r.m[5] = 2.0f / (top - bottom);
                r.m[12] = -(right + left) / (right - left);
                r.m[13] = -(top + bottom) / (top - bottom);
                if (zero_to_one_depth)
                {
                    r.m[10] = 1.0f / (zNear - zFar);
                    r.m[14] = zNear / (zNear - zFar);
                }
                else
                {
                    r.m[10] = 2.0f / (zNear - zFar);
                    r.m[14] = (zFar + zNear) / (zNear - zFar);
                }
                return r;
            }

            // Normal matrix = transpose(inverse(upper-left 3x3)) packed in a Mat4.
            Mat4 normalMatrix() const
            {
                Mat4 inv = inversed();
                Mat4 r = identity();
                for (int c = 0; c < 3; ++c)
                    for (int row = 0; row < 3; ++row)
                        r.m[c * 4 + row] = inv.m[row * 4 + c];
                return r;
            }
        };

        //------------------------------------------------------------------
        // Transform
        //------------------------------------------------------------------
        struct Transform
        {
            Vec3 position = Vec3(0.0f);
            Quat rotation = Quat::identity();
            Vec3 scale = Vec3(1.0f);

            Mat4 toMatrix() const { return Mat4::compose(position, rotation, scale); }

            static Transform fromMatrix(const Mat4 &m)
            {
                Transform t;
                t.position = m.translation();
                Vec3 sx(m.xAxis()), sy(m.yAxis()), sz(m.zAxis());
                t.scale = Vec3(length(sx), length(sy), length(sz));
                if (t.scale.x > EPSILON && t.scale.y > EPSILON && t.scale.z > EPSILON)
                {
                    Mat4 rot = m;
                    rot.m[0] /= t.scale.x; rot.m[1] /= t.scale.x; rot.m[2] /= t.scale.x;
                    rot.m[4] /= t.scale.y; rot.m[5] /= t.scale.y; rot.m[6] /= t.scale.y;
                    rot.m[8] /= t.scale.z; rot.m[9] /= t.scale.z; rot.m[10] /= t.scale.z;
                    t.rotation = quatFromRotationMatrix(rot);
                }
                return t;
            }

            static Quat quatFromRotationMatrix(const Mat4 &m)
            {
                float trace = m.m[0] + m.m[5] + m.m[10];
                Quat q;
                if (trace > 0.0f)
                {
                    float s = std::sqrt(trace + 1.0f) * 2.0f;
                    q = Quat((m.m[6] - m.m[9]) / s, (m.m[8] - m.m[2]) / s, (m.m[1] - m.m[4]) / s, 0.25f * s);
                }
                else if (m.m[0] > m.m[5] && m.m[0] > m.m[10])
                {
                    float s = std::sqrt(1.0f + m.m[0] - m.m[5] - m.m[10]) * 2.0f;
                    q = Quat(0.25f * s, (m.m[4] + m.m[1]) / s, (m.m[8] + m.m[2]) / s, (m.m[6] - m.m[9]) / s);
                }
                else if (m.m[5] > m.m[10])
                {
                    float s = std::sqrt(1.0f + m.m[5] - m.m[0] - m.m[10]) * 2.0f;
                    q = Quat((m.m[4] + m.m[1]) / s, 0.25f * s, (m.m[9] + m.m[6]) / s, (m.m[8] - m.m[2]) / s);
                }
                else
                {
                    float s = std::sqrt(1.0f + m.m[10] - m.m[0] - m.m[5]) * 2.0f;
                    q = Quat((m.m[8] + m.m[2]) / s, (m.m[9] + m.m[6]) / s, 0.25f * s, (m.m[1] - m.m[4]) / s);
                }
                return q.normalized();
            }
        };

        //------------------------------------------------------------------
        // Color
        //------------------------------------------------------------------
        struct Color
        {
            float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

            Color() {}
            Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}

            static Color white() { return Color(1, 1, 1, 1); }
            static Color black() { return Color(0, 0, 0, 1); }
            static Color red() { return Color(1, 0, 0, 1); }
            static Color green() { return Color(0, 1, 0, 1); }
            static Color blue() { return Color(0, 0, 1, 1); }
            static Color gray() { return Color(0.5f, 0.5f, 0.5f, 1); }

            Vec3 rgb() const { return Vec3(r, g, b); }
            uint32_t toRGBA8() const
            {
                auto c = [](float v) { return (uint32_t)(clampf(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
                return (c(r) << 0) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
            }

            // sRGB (non-linear, 0..1) -> linear
            static float srgbToLinear(float c)
            {
                return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
            }
            static float linearToSrgb(float c)
            {
                return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
            }
            Color toLinear() const { return Color(srgbToLinear(r), srgbToLinear(g), srgbToLinear(b), a); }
            Color toSrgb() const { return Color(linearToSrgb(r), linearToSrgb(g), linearToSrgb(b), a); }
        };

        //------------------------------------------------------------------
        // AABB
        //------------------------------------------------------------------
        struct AABB
        {
            Vec3 minPoint = Vec3(0.0f);
            Vec3 maxPoint = Vec3(0.0f);
            bool valid = false;

            void expand(const Vec3 &p)
            {
                if (!valid)
                {
                    minPoint = maxPoint = p;
                    valid = true;
                }
                else
                {
                    minPoint = min(minPoint, p);
                    maxPoint = max(maxPoint, p);
                }
            }

            void expand(const AABB &o)
            {
                if (!o.valid) return;
                expand(o.minPoint);
                expand(o.maxPoint);
            }

            Vec3 center() const { return (minPoint + maxPoint) * 0.5f; }
            Vec3 extent() const { return (maxPoint - minPoint) * 0.5f; }
            Vec3 size() const { return maxPoint - minPoint; }
            float radius() const { return length(extent()); }

            AABB transformed(const Mat4 &m) const
            {
                if (!valid) return *this;
                // transform the 8 corners (cheap and robust)
                AABB out;
                for (int i = 0; i < 8; ++i)
                {
                    Vec3 p((i & 1) ? maxPoint.x : minPoint.x,
                           (i & 2) ? maxPoint.y : minPoint.y,
                           (i & 4) ? maxPoint.z : minPoint.z);
                    out.expand(m.transformPoint(p));
                }
                return out;
            }
        };

        //------------------------------------------------------------------
        // Frustum (for coarse culling), extracted from a view-projection matrix
        //------------------------------------------------------------------
        struct Frustum
        {
            // planes: (nx, ny, nz, d) with n . p + d >= 0 inside
            Vec4 planes[6]; // left, right, bottom, top, near, far

            static Frustum fromViewProjection(const Mat4 &vp)
            {
                Frustum f;
                const float *m = vp.m;
                // row vectors of the matrix
                auto row = [&](int r) { return Vec4(m[0 * 4 + r], m[1 * 4 + r], m[2 * 4 + r], m[3 * 4 + r]); };
                Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
                f.planes[0] = r3 + r0;
                f.planes[1] = r3 - r0;
                f.planes[2] = r3 + r1;
                f.planes[3] = r3 - r1;
                f.planes[4] = r3 + r2;
                f.planes[5] = r3 - r2;
                for (int i = 0; i < 6; ++i)
                {
                    float l = std::sqrt(f.planes[i].x * f.planes[i].x + f.planes[i].y * f.planes[i].y + f.planes[i].z * f.planes[i].z);
                    if (l > EPSILON) f.planes[i] = f.planes[i] / l;
                }
                return f;
            }

            bool intersectsSphere(const Vec3 &c, float r) const
            {
                for (int i = 0; i < 6; ++i)
                {
                    float d = planes[i].x * c.x + planes[i].y * c.y + planes[i].z * c.z + planes[i].w;
                    if (d < -r) return false;
                }
                return true;
            }

            bool intersectsAABB(const AABB &box) const
            {
                if (!box.valid) return false;
                Vec3 c = box.center();
                Vec3 e = box.extent();
                for (int i = 0; i < 6; ++i)
                {
                    const Vec4 &p = planes[i];
                    float r = e.x * std::fabs(p.x) + e.y * std::fabs(p.y) + e.z * std::fabs(p.z);
                    float d = p.x * c.x + p.y * c.y + p.z * c.z + p.w;
                    if (d < -r) return false;
                }
                return true;
            }
        };

        //------------------------------------------------------------------
        // Ray (used by UI picking / editor selection)
        //------------------------------------------------------------------
        struct Ray
        {
            Vec3 origin;
            Vec3 direction = Vec3(0, 0, -1);

            bool intersectAABB(const AABB &box, float &tOut) const
            {
                if (!box.valid) return false;
                float tmin = 0.0f, tmax = 1e30f;
                for (int i = 0; i < 3; ++i)
                {
                    float o = origin[i], d = direction[i];
                    float lo = box.minPoint[i], hi = box.maxPoint[i];
                    if (std::fabs(d) < 1e-8f)
                    {
                        if (o < lo || o > hi) return false;
                    }
                    else
                    {
                        float t1 = (lo - o) / d, t2 = (hi - o) / d;
                        if (t1 > t2) std::swap(t1, t2);
                        tmin = std::max(tmin, t1);
                        tmax = std::min(tmax, t2);
                        if (tmin > tmax) return false;
                    }
                }
                tOut = tmin;
                return true;
            }

            bool intersectSphere(const Vec3 &c, float r, float &tOut) const
            {
                Vec3 oc = origin - c;
                float b = dot(oc, direction);
                float cc = dot(oc, oc) - r * r;
                float disc = b * b - cc;
                if (disc < 0.0f) return false;
                float s = std::sqrt(disc);
                float t = -b - s;
                if (t < 0.0f) t = -b + s;
                if (t < 0.0f) return false;
                tOut = t;
                return true;
            }
        };

    } // namespace Math

    // Convenient aliases at namespace scope
    using Math::Vec2;
    using Math::Vec3;
    using Math::Vec4;
    using Math::Mat4;
    using Math::Quat;
    using Math::Color;
    using Math::AABB;
    using Math::Transform;

} // namespace Genesis

#endif /* __GENESIS_MATH_HPP__ */
