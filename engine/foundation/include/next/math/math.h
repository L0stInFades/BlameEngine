#pragma once

#include <cmath>
#include <cstring>

namespace Next {

// Simple 3D Vector
struct Vec3 {
    float x, y, z;

    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& v) const {
        return Vec3(x + v.x, y + v.y, z + v.z);
    }

    Vec3 operator-(const Vec3& v) const {
        return Vec3(x - v.x, y - v.y, z - v.z);
    }

    Vec3 operator*(float s) const {
        return Vec3(x * s, y * s, z * s);
    }

    Vec3 operator/(float s) const {
        float inv = 1.0f / s;
        return Vec3(x * inv, y * inv, z * inv);
    }

    float Dot(const Vec3& v) const {
        return x * v.x + y * v.y + z * v.z;
    }

    Vec3 Cross(const Vec3& v) const {
        return Vec3(
            y * v.z - z * v.y,
            z * v.x - x * v.z,
            x * v.y - y * v.x
        );
    }

    float Length() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    Vec3 Normalize() const {
        float len = Length();
        if (len > 0.0001f) {
            return Vec3(x / len, y / len, z / len);
        }
        return Vec3();
    }
};

// 4x4 Matrix (column-major)
struct Mat4 {
    float m[16];

    Mat4() {
        memset(m, 0, sizeof(m));
        m[0] = m[5] = m[10] = m[15] = 1.0f;  // Identity
    }

    Mat4(float* data) {
        memcpy(m, data, sizeof(m));
    }

    float& operator()(int row, int col) {
        return m[col * 4 + row];
    }

    const float& operator()(int row, int col) const {
        return m[col * 4 + row];
    }

    Mat4 operator*(const Mat4& other) const {
        Mat4 result;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0;
                for (int k = 0; k < 4; ++k) {
                    sum += (*this)(row, k) * other(k, col);
                }
                result(row, col) = sum;
            }
        }
        return result;
    }

    static Mat4 Identity() {
        return Mat4();
    }

    static Mat4 RotateX(float angle) {
        Mat4 result;
        float c = std::cos(angle);
        float s = std::sin(angle);
        result(1, 1) = c;
        result(1, 2) = -s;
        result(2, 1) = s;
        result(2, 2) = c;
        return result;
    }

    static Mat4 RotateY(float angle) {
        Mat4 result;
        float c = std::cos(angle);
        float s = std::sin(angle);
        result(0, 0) = c;
        result(0, 2) = s;
        result(2, 0) = -s;
        result(2, 2) = c;
        return result;
    }

    static Mat4 RotateZ(float angle) {
        Mat4 result;
        float c = std::cos(angle);
        float s = std::sin(angle);
        result(0, 0) = c;
        result(0, 1) = -s;
        result(1, 0) = s;
        result(1, 1) = c;
        return result;
    }

    static Mat4 Translation(float x, float y, float z) {
        Mat4 result;
        result(0, 3) = x;
        result(1, 3) = y;
        result(2, 3) = z;
        return result;
    }

    static Mat4 Scale(float x, float y, float z) {
        Mat4 result;
        result(0, 0) = x;
        result(1, 1) = y;
        result(2, 2) = z;
        return result;
    }

    static Mat4 Perspective(float fov, float aspect, float nearZ, float farZ) {
        Mat4 result;
        memset(result.m, 0, sizeof(result.m));

        float tanHalfFov = std::tan(fov / 2.0f);
        result(0, 0) = 1.0f / (aspect * tanHalfFov);
        result(1, 1) = 1.0f / tanHalfFov;
        result(2, 2) = farZ / (farZ - nearZ);
        result(2, 3) = 1.0f;
        result(3, 2) = -(farZ * nearZ) / (farZ - nearZ);
        result(3, 3) = 0.0f;

        return result;
    }

    static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 zAxis = (target - eye).Normalize();
        Vec3 xAxis = up.Cross(zAxis).Normalize();
        Vec3 yAxis = zAxis.Cross(xAxis);

        Mat4 result;
        result(0, 0) = xAxis.x;
        result(0, 1) = yAxis.x;
        result(0, 2) = zAxis.x;
        result(1, 0) = xAxis.y;
        result(1, 1) = yAxis.y;
        result(1, 2) = zAxis.y;
        result(2, 0) = xAxis.z;
        result(2, 1) = yAxis.z;
        result(2, 2) = zAxis.z;
        result(0, 3) = -xAxis.Dot(eye);
        result(1, 3) = -yAxis.Dot(eye);
        result(2, 3) = -zAxis.Dot(eye);
        result(3, 3) = 1.0f;

        return result;
    }

    // Transpose for upload to GPU
    Mat4 Transpose() const {
        Mat4 result;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                result(row, col) = (*this)(col, row);
            }
        }
        return result;
    }
};

// ===== Extended Math Types for CP6 =====

// 2D Vector (for texture coordinates, input curves, etc.)
struct Vec2 {
    float x, y;

    Vec2() : x(0.0f), y(0.0f) {}
    Vec2(float x, float y) : x(x), y(y) {}
    Vec2(float v) : x(v), y(v) {}  // Scalar broadcast

    Vec2 operator+(const Vec2& v) const {
        return Vec2(x + v.x, y + v.y);
    }

    Vec2 operator-(const Vec2& v) const {
        return Vec2(x - v.x, y - v.y);
    }

    Vec2 operator*(float s) const {
        return Vec2(x * s, y * s);
    }

    Vec2 operator/(float s) const {
        return Vec2(x / s, y / s);
    }

    float Dot(const Vec2& v) const {
        return x * v.x + y * v.y;
    }

    float Length() const {
        return std::sqrt(x * x + y * y);
    }

    Vec2 Normalize() const {
        float len = Length();
        if (len > 0.0001f) {
            return Vec2(x / len, y / len);
        }
        return Vec2();
    }

    // Conversion from Vec3 (take x and y)
    static Vec2 FromVec3(const Vec3& v) {
        return Vec2(v.x, v.y);
    }
};

// 4D Vector (for homogeneous coordinates, colors, etc.)
struct Vec4 {
    float x, y, z, w;

    Vec4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    Vec4(float v) : x(v), y(v), z(v), w(v) {}  // Scalar broadcast
    Vec4(const Vec3& v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}  // Promote Vec3

    Vec4 operator+(const Vec4& v) const {
        return Vec4(x + v.x, y + v.y, z + v.z, w + v.w);
    }

    Vec4 operator-(const Vec4& v) const {
        return Vec4(x - v.x, y - v.y, z - v.z, w - v.w);
    }

    Vec4 operator*(float s) const {
        return Vec4(x * s, y * s, z * s, w * s);
    }

    float Dot(const Vec4& v) const {
        return x * v.x + y * v.y + z * v.z + w * v.w;
    }

    // Conversion to Vec3 (divide by w)
    Vec3 ToVec3() const {
        if (std::abs(w) > 0.0001f) {
            return Vec3(x / w, y / w, z / w);
        }
        return Vec3(x, y, z);
    }
};

// Quaternion (for rotations, camera orientation, etc.)
struct Quaternion {
    float x, y, z, w;

    Quaternion() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
    Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

    // Identity quaternion
    static Quaternion Identity() {
        return Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    }

    // Create from axis-angle
    static Quaternion FromAxisAngle(const Vec3& axis, float angle) {
        Vec3 n = axis.Normalize();
        float halfAngle = angle * 0.5f;
        float s = std::sin(halfAngle);
        return Quaternion(n.x * s, n.y * s, n.z * s, std::cos(halfAngle));
    }

    // Create from Euler angles (pitch, yaw, roll)
    static Quaternion FromEulerAngles(float pitch, float yaw, float roll) {
        Quaternion q = Quaternion::FromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), roll);  // Roll
        Quaternion qy = Quaternion::FromAxisAngle(Vec3(0.0f, 1.0f, 0.0f), yaw);   // Yaw
        Quaternion qx = Quaternion::FromAxisAngle(Vec3(1.0f, 0.0f, 0.0f), pitch); // Pitch
        return qy * qx * q;
    }

    // Quaternion multiplication
    Quaternion operator*(const Quaternion& q) const {
        return Quaternion(
            w * q.x + x * q.w + y * q.z - z * q.y,
            w * q.y - x * q.z + y * q.w + z * q.x,
            w * q.z + x * q.y - y * q.x + z * q.w,
            w * q.w - x * q.x - y * q.y - z * q.z
        );
    }

    // Rotate a vector
    Vec3 Rotate(const Vec3& v) const {
        // Convert quaternion to 3x3 rotation matrix and apply
        // This is a simplified implementation
        Vec3 qv(x, y, z);
        Vec3 uv = qv.Cross(v);
        Vec3 uuv = qv.Cross(uv);
        return v + (uv * (2.0f * w) + uuv * 2.0f);
    }

    // Get conjugate
    Quaternion Conjugate() const {
        return Quaternion(-x, -y, -z, w);
    }

    // Normalize
    Quaternion Normalize() const {
        float len = std::sqrt(x * x + y * y + z * z + w * w);
        if (len > 0.0001f) {
            return Quaternion(x / len, y / len, z / len, w / len);
        }
        return Quaternion::Identity();
    }

    // Convert to 4x4 matrix (for rendering)
    Mat4 ToMatrix4() const {
        Mat4 result;
        float xx = x * x;
        float yy = y * y;
        float zz = z * z;
        float xy = x * y;
        float xz = x * z;
        float yz = y * z;
        float wx = w * x;
        float wy = w * y;
        float wz = w * z;

        result(0, 0) = 1.0f - 2.0f * (yy + zz);
        result(0, 1) = 2.0f * (xy + wz);
        result(0, 2) = 2.0f * (xz - wy);
        result(0, 3) = 0.0f;

        result(1, 0) = 2.0f * (xy - wz);
        result(1, 1) = 1.0f - 2.0f * (xx + zz);
        result(1, 2) = 2.0f * (yz + wx);
        result(1, 3) = 0.0f;

        result(2, 0) = 2.0f * (xz + wy);
        result(2, 1) = 2.0f * (yz - wx);
        result(2, 2) = 1.0f - 2.0f * (xx + yy);
        result(2, 3) = 0.0f;

        result(3, 0) = 0.0f;
        result(3, 1) = 0.0f;
        result(3, 2) = 0.0f;
        result(3, 3) = 1.0f;

        return result;
    }
};

} // namespace Next
