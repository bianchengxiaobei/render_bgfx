#pragma once

#include "core/math.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace hbg::render {

struct AtmosphereSettings {
    bool enabled = false;
    bool aerialPerspective = true;
    bool usePrecomputedScattering = true;
    float planetRadius = 6360000.0f;
    float atmosphereRadius = 6420000.0f;
    float rayleighScaleHeight = 8000.0f;
    float mieScaleHeight = 1200.0f;
    Vec3 betaRayleigh{5.8e-6f, 1.35e-5f, 3.31e-5f};
    Vec3 betaMieScattering{4.0e-6f, 4.0e-6f, 4.0e-6f};
    float mieExtinctionScale = 1.0f / 0.9f;
    float miePhaseG = 0.8f;
    float sunIntensity = 10.0f;
    float averageGroundReflectance = 0.1f;
    uint32_t multipleScatteringIterations = 3;
    uint32_t skyStepCount = 16;
    uint32_t aerialStepCount = 8;

    bool active() const;
};

struct AtmosphereHit {
    bool valid = false;
    bool ground = false;
    float distance = 0.0f;
    Vec3 position{0.0f, 0.0f, 0.0f};
};

struct AtmosphereLutDimensions {
    uint32_t transmittanceWidth = 256;
    uint32_t transmittanceHeight = 64;
    uint32_t skyWidth = 64;
    uint32_t skyHeight = 16;
    uint32_t radiusSlices = 32;
    uint32_t muSlices = 128;
    uint32_t muSunSlices = 32;
    uint32_t nuSlices = 8;
};

struct AtmosphereLut {
    AtmosphereLutDimensions dimensions{};
    float planetRadius = 6360000.0f;
    float atmosphereRadius = 6420000.0f;
    std::vector<float> transmittance;
    std::vector<float> irradiance;
    std::vector<float> inscatter;
    std::vector<float> multipleScattering;
    AtmosphereLutDimensions multipleScatteringDimensions{};
    uint32_t multipleScatteringIterations = 0;

    bool valid() const;
};

bool buildAtmosphereLut(const AtmosphereSettings& settings, AtmosphereLut& lut,
                        const AtmosphereLutDimensions& dimensions = {},
                        std::string* error = nullptr);
Vec3 sampleAtmosphereLutTransmittance(const AtmosphereLut& lut, float radius, float mu);
Vec3 sampleAtmosphereLutIrradiance(const AtmosphereLut& lut, float radius, float muSun);
Vec4 sampleAtmosphereLutInscatter(const AtmosphereLut& lut, float radius, float mu, float muSun, float nu);
Vec4 sampleAtmosphereLutMultipleScattering(const AtmosphereLut& lut, float radius, float mu, float muSun,
                                           float nu);
constexpr uint32_t kAtmosphereSkyMapWidth = 32;
constexpr uint32_t kAtmosphereSkyMapHeight = 16;
constexpr uint32_t kAtmosphereSkyMapMipCount = 2;
constexpr std::array<uint32_t, kAtmosphereSkyMapMipCount> kAtmosphereSkyMapMipOffsets = {
    0, 512,
};
constexpr uint32_t kAtmosphereSkyMapMipTexels = 640;
static_assert(kAtmosphereSkyMapMipOffsets.back() + 128u == kAtmosphereSkyMapMipTexels);
using AtmosphereSkyMap = std::array<Vec4, kAtmosphereSkyMapMipTexels>;

constexpr uint32_t kAtmosphereSkyMapLayerCount = 29;
constexpr uint32_t kAtmosphereSkyMapArrayMipCount = 6;
constexpr std::array<uint32_t, kAtmosphereSkyMapArrayMipCount> kAtmosphereSkyMapArrayMipOffsets = {
    0, 512, 640, 672, 680, 682,
};
constexpr uint32_t kAtmosphereSkyMapLayerTexels = 683;
static_assert(kAtmosphereSkyMapArrayMipOffsets.back() + 1u == kAtmosphereSkyMapLayerTexels);
using AtmosphereSkyMapArray = std::vector<Vec4>;

// Per-pixel sky irradiance table used by the ocean shading path. The table
// mirrors the original sunRadianceAndSkyIrradiance() lookup of skyIrradiance(r,
// muS), with the direct sun term removed so it remains sky-only.
constexpr uint32_t kAtmosphereSkyEWidth = 32;
constexpr uint32_t kAtmosphereSkyEHeight = 8;
constexpr uint32_t kAtmosphereSkyETexels = kAtmosphereSkyEWidth * kAtmosphereSkyEHeight;
using AtmosphereSkyE = std::array<Vec4, kAtmosphereSkyETexels>;

std::array<Vec4, 7> buildAtmosphereSkySh9(const AtmosphereLut& lut, const AtmosphereSettings& settings,
                                          const Vec3& sunDirection);
Vec3 evaluateAtmosphereSkySh9(const std::array<Vec4, 7>& coefficients, const Vec3& direction);
AtmosphereSkyMap buildAtmosphereSkyMap(const AtmosphereLut& lut, const AtmosphereSettings& settings,
                                       const Vec3& sunDirection, float cameraAltitude);
AtmosphereSkyMapArray buildAtmosphereSkyMapArray(const AtmosphereLut& lut, const AtmosphereSettings& settings,
                                                 float cameraAltitude);
AtmosphereSkyE buildAtmosphereSkyE(const AtmosphereLut& lut, const AtmosphereSettings& settings);

void sanitizeAtmosphereSettings(AtmosphereSettings& settings);
bool parseAtmosphereSettings(std::string_view json, AtmosphereSettings& settings, std::string& error);
bool loadAtmosphereSettings(const std::string& path, AtmosphereSettings& settings, std::string& error);

Vec3 atmosphereCameraPosition(const AtmosphereSettings& settings, const Vec3& localCameraPosition);
float atmosphereRayLimit(const AtmosphereSettings& settings, float radius, float mu);
AtmosphereHit intersectAtmosphere(const AtmosphereSettings& settings, const Vec3& cameraPosition,
                                  const Vec3& direction);
Vec3 atmosphereTransmittance(const AtmosphereSettings& settings, float radius, float mu, float distance);
Vec3 atmosphereSunTransmittance(const AtmosphereSettings& settings, float radius, float muSun);
Vec3 atmosphereSkyRadiance(const AtmosphereSettings& settings, const Vec3& cameraPosition,
                           const Vec3& viewDirection, const Vec3& sunDirection, uint32_t stepCount,
                           Vec3* transmittance = nullptr);
Vec3 atmosphereAerialPerspective(const AtmosphereSettings& settings, const Vec3& cameraPosition,
                                 const Vec3& worldPosition, const Vec3& sunDirection, uint32_t stepCount,
                                 Vec3* transmittance = nullptr);

} // namespace hbg::render


