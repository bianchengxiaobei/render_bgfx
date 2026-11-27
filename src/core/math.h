#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <type_traits>

namespace hbg {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float xValue, float yValue) : x(xValue), y(yValue) {}

    friend constexpr bool operator==(const Vec2&, const Vec2&) = default;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float xValue, float yValue, float zValue) : x(xValue), y(yValue), z(zValue) {}

    friend constexpr bool operator==(const Vec3&, const Vec3&) = default;
};

inline Vec3 operator+(const Vec3& lhs, const Vec3& rhs) {
    return Vec3(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z);
}

inline Vec3 operator-(const Vec3& lhs, const Vec3& rhs) {
    return Vec3(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z);
}

inline Vec3 operator*(const Vec3& value, float scalar) {
    return Vec3(value.x * scalar, value.y * scalar, value.z * scalar);
}

inline float dot(const Vec3& lhs, const Vec3& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

inline Vec3 cross(const Vec3& lhs, const Vec3& rhs) {
    return Vec3(lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z,
                lhs.x * rhs.y - lhs.y * rhs.x);
}

inline float length(const Vec3& value) {
    return std::sqrt(dot(value, value));
}

inline Vec3 normalize(const Vec3& value) {
    const float magnitude = length(value);
    return magnitude > 1.0e-8f ? value * (1.0f / magnitude) : Vec3(0.0f, 0.0f, 1.0f);
}

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    constexpr Vec4() = default;
    constexpr Vec4(float xValue, float yValue, float zValue, float wValue)
        : x(xValue), y(yValue), z(zValue), w(wValue) {}

    friend constexpr bool operator==(const Vec4&, const Vec4&) = default;
};

struct Mat3 {
    std::array<float, 9> values{};

    constexpr Mat3() = default;
    explicit constexpr Mat3(const std::array<float, 9>& data) : values(data) {}

    float* data() { return values.data(); }
    const float* data() const { return values.data(); }

    float& operator[](std::size_t index) { return values[index]; }
    const float& operator[](std::size_t index) const { return values[index]; }
};

struct Mat4 {
    std::array<float, 16> values{};

    constexpr Mat4() = default;
    explicit constexpr Mat4(const std::array<float, 16>& data) : values(data) {}

    static constexpr Mat4 identity() {
        Mat4 matrix{};
        matrix.values[0] = 1.0f;
        matrix.values[5] = 1.0f;
        matrix.values[10] = 1.0f;
        matrix.values[15] = 1.0f;
        return matrix;
    }

    float* data() { return values.data(); }
    const float* data() const { return values.data(); }

    float& operator[](std::size_t index) { return values[index]; }
    const float& operator[](std::size_t index) const { return values[index]; }
};

inline Mat4 fromColumnMajor(const float* values) {
    Mat4 matrix{};
    for (std::size_t index = 0; index < 16; ++index) {
        matrix.values[index] = values[index];
    }
    return matrix;
}

inline Mat4 operator*(const Mat4& lhs, const Mat4& rhs) {
    Mat4 result{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            float value = 0.0f;
            for (std::size_t k = 0; k < 4; ++k) {
                value += lhs.values[k * 4 + row] * rhs.values[column * 4 + k];
            }
            result.values[column * 4 + row] = value;
        }
    }
    return result;
}

inline Vec3 transformPoint(const Mat4& matrix, const Vec3& point) {
    return Vec3(
        matrix.values[0] * point.x + matrix.values[4] * point.y + matrix.values[8] * point.z + matrix.values[12],
        matrix.values[1] * point.x + matrix.values[5] * point.y + matrix.values[9] * point.z + matrix.values[13],
        matrix.values[2] * point.x + matrix.values[6] * point.y + matrix.values[10] * point.z + matrix.values[14]);
}

inline Vec3 transformDirection(const Mat4& matrix, const Vec3& direction) {
    return Vec3(matrix.values[0] * direction.x + matrix.values[4] * direction.y + matrix.values[8] * direction.z,
                matrix.values[1] * direction.x + matrix.values[5] * direction.y + matrix.values[9] * direction.z,
                matrix.values[2] * direction.x + matrix.values[6] * direction.y + matrix.values[10] * direction.z);
}

inline Mat4 inverse(const Mat4& matrix) {
    const float* m = matrix.values.data();
    float inv[16];

    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
             m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
             m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
             m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
              m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
             m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
             m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
             m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
              m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
             m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
             m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
              m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
              m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    const float determinant = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::abs(determinant) < 1.0e-8f) {
        return Mat4::identity();
    }

    const float inverseDeterminant = 1.0f / determinant;
    Mat4 result{};
    for (std::size_t index = 0; index < 16; ++index) {
        result.values[index] = inv[index] * inverseDeterminant;
    }
    return result;
}

inline Mat4 lookAtRH(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 forward = normalize(target - eye);
    const Vec3 right = normalize(cross(forward, up));
    const Vec3 cameraUp = cross(right, forward);

    Mat4 view = Mat4::identity();
    view.values[0] = right.x;
    view.values[1] = cameraUp.x;
    view.values[2] = -forward.x;
    view.values[4] = right.y;
    view.values[5] = cameraUp.y;
    view.values[6] = -forward.y;
    view.values[8] = right.z;
    view.values[9] = cameraUp.z;
    view.values[10] = -forward.z;
    view.values[12] = -dot(right, eye);
    view.values[13] = -dot(cameraUp, eye);
    view.values[14] = dot(forward, eye);
    return view;
}

inline Mat4 perspectiveRH(float fovY, float aspect, float nearPlane, float farPlane, bool homogeneousDepth) {
    Mat4 projection{};
    const float yScale = 1.0f / std::tan(fovY * 0.5f);
    const float xScale = yScale / aspect;
    const float depthRange = nearPlane - farPlane;
    projection.values[0] = xScale;
    projection.values[5] = yScale;
    projection.values[10] = farPlane / depthRange;
    projection.values[11] = -1.0f;
    projection.values[14] = (nearPlane * farPlane) / depthRange;
    if (homogeneousDepth) {
        projection.values[10] = (farPlane + nearPlane) / depthRange;
        projection.values[14] = (2.0f * nearPlane * farPlane) / depthRange;
    }
    return projection;
}

inline Mat4 orthographicRH(float left, float right, float bottom, float top, float nearPlane, float farPlane,
                           bool homogeneousDepth) {
    Mat4 projection = Mat4::identity();
    projection.values[0] = 2.0f / (right - left);
    projection.values[5] = 2.0f / (top - bottom);
    projection.values[10] = 1.0f / (nearPlane - farPlane);
    projection.values[12] = (left + right) / (left - right);
    projection.values[13] = (top + bottom) / (bottom - top);
    projection.values[14] = nearPlane / (nearPlane - farPlane);
    if (homogeneousDepth) {
        projection.values[10] = 2.0f / (nearPlane - farPlane);
        projection.values[14] = (farPlane + nearPlane) / (nearPlane - farPlane);
    }
    return projection;
}

static_assert(std::is_standard_layout_v<Vec2>);
static_assert(std::is_standard_layout_v<Vec3>);
static_assert(std::is_standard_layout_v<Vec4>);
static_assert(std::is_standard_layout_v<Mat3>);
static_assert(std::is_standard_layout_v<Mat4>);
static_assert(sizeof(Vec2) == 8);
static_assert(sizeof(Vec3) == 12);
static_assert(sizeof(Vec4) == 16);
static_assert(sizeof(Mat3) == 36);
static_assert(sizeof(Mat4) == 64);

} // namespace hbg
