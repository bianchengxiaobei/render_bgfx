#pragma once

#include "core/math.h"

#include <string>

namespace hbg::render {

struct Decal {
    std::string name;
    int albedoImage = -1;
    int normalImage = -1;
    int maskImage = -1;
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f};
    Vec3 size{1.0f, 1.0f, 1.0f};
    Vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
    float angleFade = 0.0f;
    float normalBlend = 0.0f;
    float metallic = 0.0f;
    float ambientOcclusion = 1.0f;
    float roughness = 0.5f;
    bool enabled = true;
};

Mat4 decalRotation(const Vec3& rotationDegrees);
Mat4 decalToWorld(const Decal& decal);
Mat4 decalWorldToDecal(const Decal& decal);
Vec3 decalForwardWorld(const Decal& decal);
bool decalContainsWorld(const Decal& decal, const Vec3& position);
float decalAngleFade(float cosAngle, float angleDegrees);

} // namespace hbg::render
