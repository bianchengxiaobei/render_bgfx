#include "render/atmosphere/atmosphere.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <string>

using namespace hbg;
using namespace hbg::render;

TEST_CASE("atmosphere settings expose physical defaults") {
    const AtmosphereSettings settings;
    CHECK_FALSE(settings.enabled);
    CHECK(settings.aerialPerspective);
    CHECK(settings.usePrecomputedScattering);
    CHECK(settings.planetRadius == doctest::Approx(6360000.0f));
    CHECK(settings.atmosphereRadius == doctest::Approx(6420000.0f));
    CHECK(settings.rayleighScaleHeight == doctest::Approx(8000.0f));
    CHECK(settings.mieScaleHeight == doctest::Approx(1200.0f));
    CHECK(settings.betaRayleigh.x == doctest::Approx(5.8e-6f));
    CHECK(settings.betaRayleigh.y == doctest::Approx(1.35e-5f));
    CHECK(settings.betaRayleigh.z == doctest::Approx(3.31e-5f));
    CHECK(settings.betaMieScattering.x == doctest::Approx(4.0e-6f));
    CHECK(settings.miePhaseG == doctest::Approx(0.8f));
    CHECK(settings.sunIntensity == doctest::Approx(10.0f));
    CHECK(settings.averageGroundReflectance == doctest::Approx(0.1f));
    CHECK(settings.multipleScatteringIterations == 3u);
    CHECK(settings.skyStepCount == 16u);
    CHECK(settings.aerialStepCount == 8u);
    CHECK_FALSE(settings.active());

    AtmosphereSettings enabled = settings;
    enabled.enabled = true;
    CHECK(enabled.active());
}

TEST_CASE("atmosphere settings sanitize invalid values") {
    AtmosphereSettings settings;
    settings.enabled = true;
    settings.planetRadius = std::numeric_limits<float>::quiet_NaN();
    settings.atmosphereRadius = 100.0f;
    settings.rayleighScaleHeight = -1.0f;
    settings.mieScaleHeight = std::numeric_limits<float>::infinity();
    settings.betaRayleigh = Vec3(-1.0f, std::numeric_limits<float>::infinity(), 100.0f);
    settings.betaMieScattering = Vec3(-2.0f, 2.0f, 3.0f);
    settings.mieExtinctionScale = 0.0f;
    settings.miePhaseG = 2.0f;
    settings.sunIntensity = -5.0f;
    settings.averageGroundReflectance = 4.0f;
    settings.multipleScatteringIterations = 100;
    settings.skyStepCount = 0;
    settings.aerialStepCount = 100;

    sanitizeAtmosphereSettings(settings);
    CHECK(settings.planetRadius == doctest::Approx(6360000.0f));
    CHECK(settings.atmosphereRadius == doctest::Approx(6361000.0f));
    CHECK(settings.rayleighScaleHeight == doctest::Approx(8000.0f));
    CHECK(settings.mieScaleHeight == doctest::Approx(1200.0f));
    CHECK(settings.betaRayleigh.x == doctest::Approx(0.0f));
    CHECK(settings.betaRayleigh.y == doctest::Approx(1.35e-5f));
    CHECK(settings.betaRayleigh.z == doctest::Approx(1.0f));
    CHECK(settings.betaMieScattering.x == doctest::Approx(0.0f));
    CHECK(settings.betaMieScattering.y == doctest::Approx(1.0f));
    CHECK(settings.betaMieScattering.z == doctest::Approx(1.0f));
    CHECK(settings.mieExtinctionScale == doctest::Approx(1.0f));
    CHECK(settings.miePhaseG == doctest::Approx(0.99f));
    CHECK(settings.sunIntensity == doctest::Approx(0.0f));
    CHECK(settings.averageGroundReflectance == doctest::Approx(1.0f));
    CHECK(settings.multipleScatteringIterations == 8u);
    CHECK(settings.skyStepCount == 1u);
    CHECK(settings.aerialStepCount == 64u);
}

TEST_CASE("atmosphere geometry follows the spherical shell") {
    AtmosphereSettings settings;
    settings.enabled = true;

    const Vec3 localCamera(1200.0f, 800.0f, -2400.0f);
    const Vec3 camera = atmosphereCameraPosition(settings, localCamera);
    CHECK(camera.x == doctest::Approx(0.0f));
    CHECK(camera.y == doctest::Approx(settings.planetRadius + 800.0f));
    CHECK(camera.z == doctest::Approx(0.0f));

    const float down = atmosphereRayLimit(settings, camera.y, -1.0f);
    CHECK(down == doctest::Approx(800.0f).epsilon(0.001f));
    const float up = atmosphereRayLimit(settings, camera.y, 1.0f);
    CHECK(up == doctest::Approx(settings.atmosphereRadius - camera.y).epsilon(0.001f));

    const AtmosphereHit hit = intersectAtmosphere(settings, camera, Vec3(0.0f, -1.0f, 0.0f));
    REQUIRE(hit.valid);
    CHECK(hit.ground);
    CHECK(hit.position.y == doctest::Approx(settings.planetRadius).epsilon(0.001f));

    const AtmosphereHit sky = intersectAtmosphere(settings, camera, Vec3(0.0f, 1.0f, 0.0f));
    REQUIRE(sky.valid);
    CHECK_FALSE(sky.ground);
    CHECK(sky.distance > up - 1.0f);
}

TEST_CASE("atmosphere optical depth and sun transmittance are physical") {
    AtmosphereSettings settings;
    settings.enabled = true;
    const Vec3 camera = atmosphereCameraPosition(settings, Vec3(0.0f, 1000.0f, 0.0f));

    const Vec3 zenith = atmosphereTransmittance(settings, camera.y, 1.0f,
                                                settings.atmosphereRadius - camera.y);
    CHECK(zenith.x > 0.0f);
    CHECK(zenith.x <= 1.0f);
    CHECK(zenith.y <= zenith.x);
    CHECK(zenith.z <= zenith.y);

    const Vec3 ground = atmosphereSunTransmittance(settings, camera.y, -1.0f);
    CHECK(ground.x == doctest::Approx(0.0f));
    CHECK(ground.y == doctest::Approx(0.0f));
    CHECK(ground.z == doctest::Approx(0.0f));

    const Vec3 above = atmosphereSunTransmittance(settings, camera.y, 0.5f);
    CHECK(above.x > 0.0f);
    CHECK(above.x <= 1.0f);
}

TEST_CASE("atmosphere sky raymarch produces finite blue-biased radiance") {
    AtmosphereSettings settings;
    settings.enabled = true;
    const Vec3 camera = atmosphereCameraPosition(settings, Vec3(0.0f, 800.0f, 0.0f));
    const Vec3 sun = normalize(Vec3(0.5f, 0.7f, 0.4f));

    Vec3 transmittance;
    const Vec3 radiance =
        atmosphereSkyRadiance(settings, camera, Vec3(0.0f, 1.0f, 0.0f), sun, settings.skyStepCount, &transmittance);
    CHECK(std::isfinite(radiance.x));
    CHECK(std::isfinite(radiance.y));
    CHECK(std::isfinite(radiance.z));
    CHECK(radiance.x >= 0.0f);
    CHECK(radiance.y >= 0.0f);
    CHECK(radiance.z >= 0.0f);
    CHECK(radiance.z > radiance.x);
    CHECK(transmittance.x > 0.0f);
    CHECK(transmittance.x <= 1.0f);

    const Vec3 horizon = atmosphereSkyRadiance(settings, camera, normalize(Vec3(0.0f, 0.02f, -1.0f)), sun,
                                               settings.skyStepCount);
    CHECK(std::isfinite(horizon.x));
    CHECK(std::isfinite(horizon.y));
    CHECK(std::isfinite(horizon.z));
    CHECK(horizon.x >= 0.0f);
}

TEST_CASE("atmosphere aerial perspective attenuates with distance") {
    AtmosphereSettings settings;
    settings.enabled = true;
    const Vec3 camera = atmosphereCameraPosition(settings, Vec3(0.0f, 800.0f, 0.0f));
    const Vec3 sun = normalize(Vec3(0.5f, 0.7f, 0.4f));

    Vec3 nearTransmittance;
    const Vec3 nearRadiance =
        atmosphereAerialPerspective(settings, camera, camera + Vec3(0.0f, 0.0f, -100.0f), sun,
                                    settings.aerialStepCount, &nearTransmittance);
    Vec3 farTransmittance;
    const Vec3 farRadiance = atmosphereAerialPerspective(
        settings, camera, camera + Vec3(0.0f, 0.0f, -10000.0f), sun, settings.aerialStepCount,
        &farTransmittance);
    CHECK(farTransmittance.x < nearTransmittance.x);
    CHECK(farTransmittance.z < nearTransmittance.z);
    CHECK(farRadiance.z >= nearRadiance.z);
    CHECK(farRadiance.z > 0.0f);
    CHECK(farRadiance.z < 1000000.0f);
}

TEST_CASE("atmosphere settings parse JSON") {
    AtmosphereSettings settings;
    std::string error;
    REQUIRE(parseAtmosphereSettings(R"({
        "enabled": true,
        "aerialPerspective": false,
        "planetRadius": 6300000.0,
        "atmosphereRadius": 6400000.0,
        "rayleighScaleHeight": 7000.0,
        "mieScaleHeight": 1000.0,
        "betaRayleigh": [1e-6, 2e-6, 3e-6],
        "betaMieScattering": [4e-6, 5e-6, 6e-6],
        "mieExtinctionScale": 1.2,
        "miePhaseG": 0.7,
        "sunIntensity": 50.0,
        "averageGroundReflectance": 0.25,
        "multipleScatteringIterations": 2,
        "skyStepCount": 12,
        "aerialStepCount": 6
    })",
                                    settings, error));
    CHECK(settings.enabled);
    CHECK_FALSE(settings.aerialPerspective);
    CHECK(settings.planetRadius == doctest::Approx(6300000.0f));
    CHECK(settings.atmosphereRadius == doctest::Approx(6400000.0f));
    CHECK(settings.rayleighScaleHeight == doctest::Approx(7000.0f));
    CHECK(settings.betaRayleigh.z == doctest::Approx(3e-6f));
    CHECK(settings.betaMieScattering.y == doctest::Approx(5e-6f));
    CHECK(settings.mieExtinctionScale == doctest::Approx(1.2f));
    CHECK(settings.miePhaseG == doctest::Approx(0.7f));
    CHECK(settings.sunIntensity == doctest::Approx(50.0f));
    CHECK(settings.averageGroundReflectance == doctest::Approx(0.25f));
    CHECK(settings.multipleScatteringIterations == 2u);
    CHECK(settings.skyStepCount == 12u);
    CHECK(settings.aerialStepCount == 6u);

    CHECK_FALSE(parseAtmosphereSettings(R"({"enabled":"yes"})", settings, error));
    CHECK_FALSE(parseAtmosphereSettings(R"({"betaRayleigh":[1,2]})", settings, error));
    CHECK_FALSE(parseAtmosphereSettings("[1,2,3]", settings, error));
}

TEST_CASE("atmosphere precomputed LUT captures transmittance and scattering") {
    AtmosphereSettings settings;
    settings.enabled = true;
    settings.multipleScatteringIterations = 1;
    AtmosphereLutDimensions dimensions;
    dimensions.transmittanceWidth = 64;
    dimensions.transmittanceHeight = 32;
    dimensions.skyWidth = 32;
    dimensions.skyHeight = 8;
    dimensions.radiusSlices = 16;
    dimensions.muSlices = 32;
    dimensions.muSunSlices = 16;
    dimensions.nuSlices = 4;

    AtmosphereLut lut;
    std::string error;
    REQUIRE(buildAtmosphereLut(settings, lut, dimensions, &error));
    CHECK(lut.valid());
    CHECK(lut.transmittance.size() ==
          static_cast<std::size_t>(dimensions.transmittanceWidth * dimensions.transmittanceHeight * 4));
    CHECK(lut.inscatter.size() ==
          static_cast<std::size_t>(dimensions.muSunSlices * dimensions.nuSlices * dimensions.muSlices *
                                   dimensions.radiusSlices * 4));
    CHECK(lut.multipleScatteringIterations == 1u);
    CHECK(lut.multipleScatteringDimensions.radiusSlices == 16u);
    CHECK(lut.multipleScatteringDimensions.muSlices == 32u);
    CHECK(lut.multipleScatteringDimensions.muSunSlices == 16u);
    CHECK(lut.multipleScatteringDimensions.nuSlices == 4u);
    CHECK(lut.multipleScattering.size() ==
          static_cast<std::size_t>(16u * 4u * 32u * 16u * 4u));
    double multipleScatteringSum = 0.0;
    bool multipleScatteringFinite = true;
    for (float value : lut.multipleScattering) {
        multipleScatteringFinite = multipleScatteringFinite && std::isfinite(value);
        multipleScatteringSum += value;
    }
    CHECK(multipleScatteringFinite);
    CHECK(multipleScatteringSum > 0.0);

    const float radius = settings.planetRadius + 800.0f;
    const Vec3 zenith = sampleAtmosphereLutTransmittance(lut, radius, 1.0f);
    CHECK(zenith.x > 0.0f);
    CHECK(zenith.x <= 1.0f);
    CHECK(zenith.z < zenith.x);

    const Vec3 irradiance = sampleAtmosphereLutIrradiance(lut, radius, 0.5f);
    CHECK(irradiance.x > 0.0f);
    CHECK(irradiance.y > 0.0f);
    CHECK(irradiance.z > 0.0f);

    const Vec4 inscatter = sampleAtmosphereLutInscatter(lut, radius, 1.0f, 0.5f, 0.5f);
    CHECK(std::isfinite(inscatter.x));
    CHECK(std::isfinite(inscatter.y));
    CHECK(std::isfinite(inscatter.z));
    CHECK(inscatter.x >= 0.0f);
    CHECK(inscatter.z >= inscatter.x);

    const std::array<Vec4, 7> skySh =
        buildAtmosphereSkySh9(lut, settings, normalize(Vec3(0.5f, 0.7f, 0.4f)));
    CHECK(skySh[6].w == doctest::Approx(1.0f));
    bool skyShFinite = true;
    double skyShSum = 0.0;
    for (const Vec4& coefficient : skySh) {
        skyShFinite = skyShFinite && std::isfinite(coefficient.x) && std::isfinite(coefficient.y) &&
                      std::isfinite(coefficient.z) && std::isfinite(coefficient.w);
        skyShSum += static_cast<double>(coefficient.x) + coefficient.y + coefficient.z;
    }
    CHECK(skyShFinite);
    CHECK(skyShSum > 0.0);

    const Vec3 zenithRadiance = evaluateAtmosphereSkySh9(skySh, Vec3(0.0f, 1.0f, 0.0f));
    const Vec3 horizonRadiance = evaluateAtmosphereSkySh9(skySh, normalize(Vec3(0.2f, 0.05f, -1.0f)));
    CHECK(std::isfinite(zenithRadiance.x));
    CHECK(std::isfinite(zenithRadiance.y));
    CHECK(std::isfinite(zenithRadiance.z));
    CHECK(zenithRadiance.x >= 0.0f);
    CHECK(zenithRadiance.y >= 0.0f);
    CHECK(zenithRadiance.z >= 0.0f);
    CHECK(horizonRadiance.z >= 0.0f);

    const AtmosphereSkyMap skyMap =
        buildAtmosphereSkyMap(lut, settings, normalize(Vec3(0.5f, 0.7f, 0.4f)), 800.0f);
    CHECK(skyMap.size() == kAtmosphereSkyMapMipTexels);
    bool skyMapFinite = true;
    double skyMapSum = 0.0;
    double horizonMapSum = 0.0;
    for (std::size_t index = 0; index < skyMap.size(); ++index) {
        const Vec4& texel = skyMap[index];
        skyMapFinite = skyMapFinite && std::isfinite(texel.x) && std::isfinite(texel.y) &&
                       std::isfinite(texel.z);
        skyMapSum += static_cast<double>(texel.x) + texel.y + texel.z;
        if (index < kAtmosphereSkyMapWidth) {
            horizonMapSum += static_cast<double>(texel.x) + texel.y + texel.z;
        }
    }
    CHECK(skyMapFinite);
    CHECK(skyMapSum > 0.0);
    CHECK(horizonMapSum > 0.0);
    const Vec4& skyMapMip = skyMap[kAtmosphereSkyMapMipOffsets[1]];
    CHECK(std::isfinite(skyMapMip.x));
    CHECK(skyMapMip.x + skyMapMip.y + skyMapMip.z >= 0.0f);

    const AtmosphereSkyMapArray skyMapArray = buildAtmosphereSkyMapArray(lut, settings, 800.0f);
    CHECK(skyMapArray.size() ==
          static_cast<std::size_t>(kAtmosphereSkyMapLayerCount * kAtmosphereSkyMapLayerTexels));
    bool skyMapArrayFinite = true;
    double skyMapArraySum = 0.0;
    for (const Vec4& texel : skyMapArray) {
        skyMapArrayFinite = skyMapArrayFinite && std::isfinite(texel.x) && std::isfinite(texel.y) &&
                            std::isfinite(texel.z);
        skyMapArraySum += static_cast<double>(texel.x) + texel.y + texel.z;
    }
    CHECK(skyMapArrayFinite);
    CHECK(skyMapArraySum > 0.0);
    const std::size_t horizonLayerOffset =
        static_cast<std::size_t>(kAtmosphereSkyMapLayerCount - 1u) * kAtmosphereSkyMapLayerTexels;
    CHECK(skyMapArray[horizonLayerOffset].x >= 0.0f);
    CHECK(skyMapArray[horizonLayerOffset + kAtmosphereSkyMapArrayMipOffsets.back()].z >= 0.0f);

    const AtmosphereSkyE skyE = buildAtmosphereSkyE(lut, settings);
    CHECK(skyE.size() == kAtmosphereSkyETexels);
    bool skyEFinite = true;
    double skyESum = 0.0;
    for (const Vec4& texel : skyE) {
        skyEFinite = skyEFinite && std::isfinite(texel.x) && std::isfinite(texel.y) &&
                     std::isfinite(texel.z);
        skyESum += static_cast<double>(texel.x) + texel.y + texel.z;
    }
    CHECK(skyEFinite);
    CHECK(skyESum > 0.0);
    CHECK(skyE[0].x >= 0.0f);
    CHECK(skyE[kAtmosphereSkyETexels - 1].z >= 0.0f);
}

TEST_CASE("atmosphere settings can disable precomputed scattering") {
    AtmosphereSettings settings;
    std::string error;
    REQUIRE(parseAtmosphereSettings(R"({"enabled":true,"usePrecomputedScattering":false})", settings, error));
    CHECK_FALSE(settings.usePrecomputedScattering);
}
