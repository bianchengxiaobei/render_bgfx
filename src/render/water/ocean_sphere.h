#pragma once

#include "core/math.h"

namespace hbg::render {

struct OceanSphereHit {
    bool valid = false;
    bool clamped = false;
    float distance = 0.0f;
    Vec3 direction{0.0f, 0.0f, -1.0f};
    Vec3 position{};
};

OceanSphereHit intersectOceanSphere(const Vec3& cameraPosition, const Vec3& rayDirection, float radius,
                                    float seaLevel);

Vec2 oceanSphereSlope(const Vec3& position, const Vec3& cameraPosition, float radius, float seaLevel);

} // namespace hbg::render
