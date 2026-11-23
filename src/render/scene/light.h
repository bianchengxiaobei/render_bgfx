#pragma once

#include "core/math.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hbg::render {

enum class LightType : uint8_t { Directional, Point, Spot, Area };
enum class LightCookieMode : uint8_t { None, Clamp, Repeat };
enum class LightCookieType : uint8_t { None, Spot2D, PointOctahedral };
enum class LightAreaShape : uint8_t { Rectangle, Tube, Disc };

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
    float evsmExponent = 15.0f;
    float evsmLightLeakBias = 0.0f;
    float evsmVarianceBias = 1.0e-5f;
    uint32_t evsmBlurPasses = 0;
};

struct Light {
    std::string name;
    int nodeIndex = -1;
    LightType type = LightType::Point;
    Vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
    float innerConeAngle = 0.0f;
    float outerConeAngle = 0.78539816339f;
    LightAreaShape areaShape = LightAreaShape::Rectangle;
    float areaWidth = 1.0f;
    float areaHeight = 1.0f;
    float areaShadowCone = 120.0f;
    Mat4 world = Mat4::identity();
    bool castShadows = false;
    bool shadowConfigured = false;
    uint16_t lightLayerMask = 0xffffu;
    LightCookieMode cookieMode = LightCookieMode::None;
    LightCookieType cookieType = LightCookieType::None;
    Vec2 cookieScale{1.0f, 1.0f};
    Vec2 cookieOffset{0.0f, 0.0f};
    std::array<float, 64> cookieSamples{};
    std::vector<float> cookieNative;
    uint32_t cookieNativeWidth = 0;
    uint32_t cookieNativeHeight = 0;
    std::array<float, 8> iesSamples{};
    float iesCut = 0.0f;
    bool hasIes = false;
    LightShadowSettings shadow;
};

} // namespace hbg::render
