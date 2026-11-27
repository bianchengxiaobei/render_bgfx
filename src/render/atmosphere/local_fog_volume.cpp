#include "render/atmosphere/local_fog_volume.h"

#include <algorithm>
#include <cmath>

namespace hbg::render {
namespace {

constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;
constexpr float kMinFadeLength = 1.0e-4f;
constexpr float kMinDistanceFadeLength = 0.00001526f;

float finiteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

Vec3 finiteOr(const Vec3& value, const Vec3& fallback) {
    return Vec3(finiteOr(value.x, fallback.x), finiteOr(value.y, fallback.y), finiteOr(value.z, fallback.z));
}

Mat4 rotationDegrees(const Vec3& rotationDegrees) {
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

float remap10(float value, float rcpLength, float endTimesRcpLength) {
    return std::clamp(endTimesRcpLength - value * rcpLength, 0.0f, 1.0f);
}

float remap01(float value, float rcpLength) {
    return std::clamp(value * rcpLength, 0.0f, 1.0f);
}

float applyFalloff(float fade, LocalFogFalloff falloff) {
    return falloff == LocalFogFalloff::Exponential ? std::pow(std::max(fade, 0.0f), 2.2f) : fade;
}

} // namespace

float LocalFogVolume::extinction() const {
    return 1.0f / std::max(meanFreePath, 0.05f);
}

Vec3 LocalFogVolume::scattering() const {
    return albedo * extinction();
}

float LocalFogVolume::distanceFadeLength() const {
    return std::max(distanceFadeEnd - distanceFadeStart, kMinDistanceFadeLength);
}

float LocalFogVolume::endTimesRcpDistanceFadeLength() const {
    return distanceFadeEnd / distanceFadeLength();
}

Mat4 LocalFogVolume::rotationMatrix() const {
    return rotationDegrees(rotation);
}

void sanitizeLocalFogVolume(LocalFogVolume& volume) {
    volume.center = finiteOr(volume.center, Vec3(0.0f, 0.0f, 0.0f));
    volume.rotation = finiteOr(volume.rotation, Vec3(0.0f, 0.0f, 0.0f));
    volume.size = finiteOr(volume.size, Vec3(1.0f, 1.0f, 1.0f));
    volume.size = Vec3(std::max(std::abs(volume.size.x), 1.0e-3f), std::max(std::abs(volume.size.y), 1.0e-3f),
                       std::max(std::abs(volume.size.z), 1.0e-3f));
    volume.albedo = finiteOr(volume.albedo, Vec3(1.0f, 1.0f, 1.0f));
    volume.albedo = Vec3(std::clamp(volume.albedo.x, 0.0f, 4.0f), std::clamp(volume.albedo.y, 0.0f, 4.0f),
                         std::clamp(volume.albedo.z, 0.0f, 4.0f));
    volume.meanFreePath = std::clamp(finiteOr(volume.meanFreePath, 10.0f), 0.05f, 1000000.0f);
    volume.anisotropy = std::clamp(finiteOr(volume.anisotropy, 0.0f), -0.99f, 0.99f);
    volume.positiveFade = finiteOr(volume.positiveFade, Vec3(0.1f, 0.1f, 0.1f));
    volume.positiveFade = Vec3(std::max(volume.positiveFade.x, 0.0f), std::max(volume.positiveFade.y, 0.0f),
                               std::max(volume.positiveFade.z, 0.0f));
    volume.negativeFade = finiteOr(volume.negativeFade, Vec3(0.1f, 0.1f, 0.1f));
    volume.negativeFade = Vec3(std::max(volume.negativeFade.x, 0.0f), std::max(volume.negativeFade.y, 0.0f),
                               std::max(volume.negativeFade.z, 0.0f));
    volume.distanceFadeStart = std::max(finiteOr(volume.distanceFadeStart, 10000.0f), 0.0f);
    volume.distanceFadeEnd = std::max(finiteOr(volume.distanceFadeEnd, volume.distanceFadeStart),
                                      volume.distanceFadeStart);
    if (volume.distanceFadeEnd <= volume.distanceFadeStart) {
        volume.distanceFadeEnd = volume.distanceFadeStart + kMinDistanceFadeLength;
    }
    volume.maskTiling = finiteOr(volume.maskTiling, Vec3(1.0f, 1.0f, 1.0f));
    volume.maskTiling = Vec3(std::max(std::abs(volume.maskTiling.x), 0.001f),
                             std::max(std::abs(volume.maskTiling.y), 0.001f),
                             std::max(std::abs(volume.maskTiling.z), 0.001f));
    volume.maskScroll = finiteOr(volume.maskScroll, Vec3(0.0f, 0.0f, 0.0f));
    volume.maskIntensity = std::clamp(finiteOr(volume.maskIntensity, 1.0f), 0.0f, 1.0f);
    if (volume.maskImage < 0) {
        volume.maskIntensity = 0.0f;
    }
}

Vec3 localFogNormalizedPosition(const LocalFogVolume& volume, const Vec3& worldPosition) {
    const Mat4 rotation = volume.rotationMatrix();
    const Vec3 right(rotation[0], rotation[1], rotation[2]);
    const Vec3 up(rotation[4], rotation[5], rotation[6]);
    const Vec3 forward(rotation[8], rotation[9], rotation[10]);
    const Vec3 offset = worldPosition - volume.center;
    const Vec3 halfSize(std::max(volume.size.x * 0.5f, kMinFadeLength),
                        std::max(volume.size.y * 0.5f, kMinFadeLength),
                        std::max(volume.size.z * 0.5f, kMinFadeLength));
    return Vec3(dot(offset, right) / halfSize.x, dot(offset, up) / halfSize.y,
                dot(offset, forward) / halfSize.z);
}

float localFogFalloff(const LocalFogVolume& volume, const Vec3& worldPosition, float cameraDistance) {
    const Vec3 local = localFogNormalizedPosition(volume, worldPosition);
    const bool inside = volume.shape == LocalFogShape::Sphere
                            ? length(local) <= 1.0f
                            : std::abs(local.x) <= 1.0f && std::abs(local.y) <= 1.0f && std::abs(local.z) <= 1.0f;
    if (!inside) {
        return 0.0f;
    }

    float fade = 0.0f;
    if (volume.shape == LocalFogShape::Sphere) {
        const float radius = length(local);
        const float edge = std::clamp(volume.positiveFade.x, kMinFadeLength, 1.0f);
        fade = std::clamp((1.0f - radius) / edge, 0.0f, 1.0f);
        fade = applyFalloff(fade, volume.falloff);
        if (volume.invertFade) {
            fade = 1.0f - fade;
        }
    } else {
        const Vec3 coord = local * 0.5f + Vec3(0.5f, 0.5f, 0.5f);
        const Vec3 rcpPositive(1.0f / std::max(volume.positiveFade.x, kMinFadeLength),
                               1.0f / std::max(volume.positiveFade.y, kMinFadeLength),
                               1.0f / std::max(volume.positiveFade.z, kMinFadeLength));
        const Vec3 rcpNegative(1.0f / std::max(volume.negativeFade.x, kMinFadeLength),
                               1.0f / std::max(volume.negativeFade.y, kMinFadeLength),
                               1.0f / std::max(volume.negativeFade.z, kMinFadeLength));
        const Vec3 positiveFade(remap10(coord.x, rcpPositive.x, rcpPositive.x),
                                remap10(coord.y, rcpPositive.y, rcpPositive.y),
                                remap10(coord.z, rcpPositive.z, rcpPositive.z));
        const Vec3 negativeFade(remap01(coord.x, rcpNegative.x), remap01(coord.y, rcpNegative.y),
                                remap01(coord.z, rcpNegative.z));
        fade = positiveFade.x * positiveFade.y * positiveFade.z * negativeFade.x * negativeFade.y *
               negativeFade.z;
        fade = applyFalloff(fade, volume.falloff);
        if (volume.invertFade) {
            fade = 1.0f - fade;
        }
    }

    const float distanceFade =
        remap10(cameraDistance, 1.0f / volume.distanceFadeLength(), volume.endTimesRcpDistanceFadeLength());
    return std::clamp(fade * distanceFade, 0.0f, 1.0f);
}

const char* localFogShapeName(LocalFogShape shape) {
    return shape == LocalFogShape::Sphere ? "sphere" : "box";
}

const char* localFogFalloffName(LocalFogFalloff falloff) {
    return falloff == LocalFogFalloff::Exponential ? "exponential" : "linear";
}

const char* localFogMaskPlaneName(LocalFogMaskPlane plane) {
    switch (plane) {
    case LocalFogMaskPlane::XY:
        return "xy";
    case LocalFogMaskPlane::YZ:
        return "yz";
    case LocalFogMaskPlane::XZ:
    default:
        return "xz";
    }
}

} // namespace hbg::render
