#include "render/scene/reflection_probe.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace hbg::render {
namespace {

constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;
constexpr float kMinimumExtent = 1.0e-4f;
constexpr float kMinimumBlend = 1.0e-4f;

} // namespace

Mat4 probeRotation(const Vec3& rotationDegrees) {
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

Mat4 probeToWorld(const ReflectionProbe& probe) {
    Mat4 matrix = probeRotation(probe.rotation);
    matrix[12] = probe.position.x;
    matrix[13] = probe.position.y;
    matrix[14] = probe.position.z;
    return matrix;
}

Mat4 probeWorldToProbe(const ReflectionProbe& probe) {
    return inverse(probeToWorld(probe));
}

float probeInfluenceWeight(const ReflectionProbe& probe, const Vec3& worldPosition) {
    const Vec3 local = transformPoint(probeWorldToProbe(probe), worldPosition);
    const Vec3 extents(std::max(std::abs(probe.boxExtents.x), kMinimumExtent),
                       std::max(std::abs(probe.boxExtents.y), kMinimumExtent),
                       std::max(std::abs(probe.boxExtents.z), kMinimumExtent));
    const Vec3 distanceToFace(extents.x - std::abs(local.x), extents.y - std::abs(local.y),
                              extents.z - std::abs(local.z));
    const float faceDistance = std::min(distanceToFace.x, std::min(distanceToFace.y, distanceToFace.z));
    if (faceDistance <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(faceDistance / std::max(probe.blendDistance, kMinimumBlend), 0.0f, 1.0f);
}

bool probeContainsWorld(const ReflectionProbe& probe, const Vec3& worldPosition) {
    return probeInfluenceWeight(probe, worldPosition) > 0.0f;
}

std::array<int, kMaxActiveReflectionProbes>
selectReflectionProbes(const std::vector<ReflectionProbe>& probes, const Vec3& cameraPosition) {
    std::array<int, kMaxActiveReflectionProbes> selected{-1, -1};
    std::vector<std::pair<float, int>> candidates;
    candidates.reserve(std::min(probes.size(), kMaxReflectionProbes));

    for (std::size_t index = 0; index < probes.size() && index < kMaxReflectionProbes; ++index) {
        const ReflectionProbe& probe = probes[index];
        if (!probe.enabled || probe.image < 0 || !probeContainsWorld(probe, cameraPosition)) {
            continue;
        }
        const Vec3 local = transformPoint(probeWorldToProbe(probe), cameraPosition);
        candidates.emplace_back(dot(local, local), static_cast<int>(index));
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.first != rhs.first) {
            return lhs.first < rhs.first;
        }
        return lhs.second < rhs.second;
    });

    for (std::size_t index = 0; index < candidates.size() && index < selected.size(); ++index) {
        selected[index] = candidates[index].second;
    }
    return selected;
}

} // namespace hbg::render
