#pragma once

#include "core/math.h"

#include <cstdint>
#include <string>

namespace hbg::render {

constexpr uint32_t kMaxLocalFogVolumes = 8;
constexpr uint32_t kMaxLocalFogMasks = 4;

enum class LocalFogShape : uint8_t {
    Box,
    Sphere,
};

enum class LocalFogFalloff : uint8_t {
    Linear,
    Exponential,
};

enum class LocalFogMaskPlane : uint8_t {
    XY,
    XZ,
    YZ,
};

struct LocalFogVolume {
    std::string name;
    bool enabled = true;
    LocalFogShape shape = LocalFogShape::Box;
    Vec3 center{0.0f, 0.0f, 0.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f};
    Vec3 size{1.0f, 1.0f, 1.0f};
    Vec3 albedo{1.0f, 1.0f, 1.0f};
    float meanFreePath = 10.0f;
    float anisotropy = 0.0f;
    Vec3 positiveFade{0.1f, 0.1f, 0.1f};
    Vec3 negativeFade{0.1f, 0.1f, 0.1f};
    bool invertFade = false;
    LocalFogFalloff falloff = LocalFogFalloff::Linear;
    float distanceFadeStart = 10000.0f;
    float distanceFadeEnd = 10000.0f;
    int maskImage = -1;
    Vec3 maskTiling{1.0f, 1.0f, 1.0f};
    Vec3 maskScroll{0.0f, 0.0f, 0.0f};
    LocalFogMaskPlane maskPlane = LocalFogMaskPlane::XZ;
    float maskIntensity = 1.0f;
    bool maskInvert = false;

    float extinction() const;
    Vec3 scattering() const;
    float distanceFadeLength() const;
    float endTimesRcpDistanceFadeLength() const;
    Mat4 rotationMatrix() const;
};

void sanitizeLocalFogVolume(LocalFogVolume& volume);
Vec3 localFogNormalizedPosition(const LocalFogVolume& volume, const Vec3& worldPosition);
float localFogFalloff(const LocalFogVolume& volume, const Vec3& worldPosition, float cameraDistance);

const char* localFogShapeName(LocalFogShape shape);
const char* localFogFalloffName(LocalFogFalloff falloff);
const char* localFogMaskPlaneName(LocalFogMaskPlane plane);

} // namespace hbg::render
