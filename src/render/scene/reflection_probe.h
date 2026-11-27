#pragma once

#include "core/math.h"

#include <array>
#include <string>
#include <vector>

namespace hbg::render {

constexpr std::size_t kMaxReflectionProbes = 4;
constexpr std::size_t kMaxActiveReflectionProbes = 2;

struct ReflectionProbe {
    std::string name;
    int image = -1;
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f};
    Vec3 boxExtents{1.0f, 1.0f, 1.0f};
    float blendDistance = 0.1f;
    float intensity = 1.0f;
    bool enabled = true;
};

Mat4 probeRotation(const Vec3& rotationDegrees);
Mat4 probeToWorld(const ReflectionProbe& probe);
Mat4 probeWorldToProbe(const ReflectionProbe& probe);
float probeInfluenceWeight(const ReflectionProbe& probe, const Vec3& worldPosition);
bool probeContainsWorld(const ReflectionProbe& probe, const Vec3& worldPosition);
std::array<int, static_cast<std::size_t>(kMaxActiveReflectionProbes)>
selectReflectionProbes(const std::vector<ReflectionProbe>& probes, const Vec3& cameraPosition);

} // namespace hbg::render
