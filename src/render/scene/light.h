#pragma once

#include "core/math.h"

#include <cstdint>
#include <string>

namespace hbg::render {

enum class LightType : uint8_t { Directional, Point, Spot };

enum class ShadowUpdateMode : uint8_t {
    EveryFrame = 0,
    OnEnable = 1,
    OnDemand = 2,
};

struct LightShadowSettings {
    float dimmer = 1.0f;
    float fadeDistance = 10000.0f;
    float nearPlane = 0.1f;
    uint32_t resolution = 0;
    Vec3 tint{0.0f, 0.0f, 0.0f};
    bool penumbraTint = false;
    ShadowUpdateMode updateMode = ShadowUpdateMode::EveryFrame;
    bool contactShadows = true;
    float softness = 0.5f;
    uint32_t blockerSamples = 16;
    uint32_t filterSamples = 24;
};

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
    bool shadowConfigured = false;
    LightShadowSettings shadow;
};

} // namespace hbg::render
