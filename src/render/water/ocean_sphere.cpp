#include "render/water/ocean_sphere.h"

#include <algorithm>
#include <cmath>

namespace hbg::render {

OceanSphereHit intersectOceanSphere(const Vec3& cameraPosition, const Vec3& rayDirection, float radius,
                                    float seaLevel) {
    OceanSphereHit hit;
    const float rayLength = length(rayDirection);
    if (rayLength > 1.0e-8f) {
        hit.direction = rayDirection * (1.0f / rayLength);
    }

    const float altitude = cameraPosition.y - seaLevel;
    if (radius <= 0.0f || altitude <= 0.0f) {
        return hit;
    }

    const float altitudeRatio = altitude / radius;
    const float onePlus = 1.0f + altitudeRatio;
    const float horizonDown = std::sqrt(std::max(altitudeRatio * (2.0f + altitudeRatio), 0.0f)) / onePlus;
    if (hit.direction.y > -horizonDown) {
        Vec3 horizontal(hit.direction.x, 0.0f, hit.direction.z);
        const float horizontalLength = length(horizontal);
        horizontal = horizontalLength > 1.0e-8f ? horizontal * (1.0f / horizontalLength) : Vec3(0.0f, 0.0f, -1.0f);
        hit.direction = Vec3(horizontal.x / onePlus, -horizonDown, horizontal.z / onePlus);
        hit.clamped = true;
    }

    const float b = hit.direction.y * onePlus;
    const float c = altitudeRatio * (2.0f + altitudeRatio);
    const float discriminant = std::max(b * b - c, 0.0f);
    const float denominator = -b + std::sqrt(discriminant);
    if (denominator <= 1.0e-12f) {
        hit.valid = false;
        return hit;
    }

    hit.distance = radius * c / denominator;
    hit.position = cameraPosition + hit.direction * hit.distance;
    hit.valid = true;
    return hit;
}

Vec2 oceanSphereSlope(const Vec3& position, const Vec3& cameraPosition, float radius, float seaLevel) {
    if (radius <= 0.0f) {
        return Vec2(0.0f, 0.0f);
    }
    const float centerY = seaLevel - radius;
    const float vertical = position.y - centerY;
    if (vertical <= 1.0e-3f) {
        return Vec2(0.0f, 0.0f);
    }
    return Vec2((position.x - cameraPosition.x) / vertical, (position.z - cameraPosition.z) / vertical);
}

} // namespace hbg::render
