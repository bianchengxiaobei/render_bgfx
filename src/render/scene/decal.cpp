#include "render/scene/decal.h"

#include <algorithm>
#include <cmath>

namespace hbg::render {
namespace {

constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;

float smoothstep(float edge0, float edge1, float value) {
    const float denominator = edge1 - edge0;
    const float t = denominator > 1.0e-8f ? std::clamp((value - edge0) / denominator, 0.0f, 1.0f) : 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

Mat4 decalRotation(const Vec3& rotationDegrees) {
    const float x = rotationDegrees.x * kDegreesToRadians;
    const float y = rotationDegrees.y * kDegreesToRadians;
    const float z = rotationDegrees.z * kDegreesToRadians;
    const float cx = std::cos(x);
    const float sx = std::sin(x);
    const float cy = std::cos(y);
    const float sy = std::sin(y);
    const float cz = std::cos(z);
    const float sz = std::sin(z);

    Mat4 rotationX = Mat4::identity();
    rotationX[5] = cx;
    rotationX[6] = sx;
    rotationX[9] = -sx;
    rotationX[10] = cx;

    Mat4 rotationY = Mat4::identity();
    rotationY[0] = cy;
    rotationY[2] = -sy;
    rotationY[8] = sy;
    rotationY[10] = cy;

    Mat4 rotationZ = Mat4::identity();
    rotationZ[0] = cz;
    rotationZ[1] = sz;
    rotationZ[4] = -sz;
    rotationZ[5] = cz;

    return rotationZ * rotationY * rotationX;
}

Mat4 decalToWorld(const Decal& decal) {
    Mat4 matrix = decalRotation(decal.rotation);
    const float width = std::max(std::abs(decal.size.x), 1.0e-4f);
    const float height = std::max(std::abs(decal.size.y), 1.0e-4f);
    const float depth = std::max(std::abs(decal.size.z), 1.0e-4f);
    const std::array<float, 3> scale = {width, depth, height};
    for (std::size_t column = 0; column < 3; ++column) {
        for (std::size_t row = 0; row < 3; ++row) {
            matrix[column * 4 + row] *= scale[column];
        }
    }
    matrix[12] = decal.position.x;
    matrix[13] = decal.position.y;
    matrix[14] = decal.position.z;
    return matrix;
}

Mat4 decalWorldToDecal(const Decal& decal) {
    return inverse(decalToWorld(decal));
}

Vec3 decalForwardWorld(const Decal& decal) {
    const Mat4 matrix = decalToWorld(decal);
    return normalize(transformDirection(matrix, Vec3(0.0f, 1.0f, 0.0f)));
}

bool decalContainsWorld(const Decal& decal, const Vec3& position) {
    const Vec3 local = transformPoint(decalWorldToDecal(decal), position);
    return std::abs(local.x) <= 0.5f && std::abs(local.y) <= 0.5f && std::abs(local.z) <= 0.5f;
}

float decalAngleFade(float cosAngle, float angleDegrees) {
    if (angleDegrees <= 0.0f) {
        return 1.0f;
    }
    const float clampedAngle = std::clamp(angleDegrees, 0.0f, 180.0f);
    return smoothstep(std::cos(clampedAngle * kDegreesToRadians), 1.0f, std::abs(cosAngle));
}

} // namespace hbg::render
