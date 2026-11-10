#pragma once

#include "core/math.h"

#include <string>

namespace hbg::render {

enum class LightType : uint8_t { Directional, Point, Spot };

struct Light {
    std::string name;
    LightType type = LightType::Point;
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
    float innerConeAngle = 0.0f;
    float outerConeAngle = 0.78539816339f;
    Mat4 world = Mat4::identity();
    bool castShadows = false;
};

} // namespace hbg::render
