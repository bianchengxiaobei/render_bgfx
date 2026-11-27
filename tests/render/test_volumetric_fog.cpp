#include "render/atmosphere/volumetric_fog_settings.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <string>

using namespace hbg;
using namespace hbg::render;

TEST_CASE("volumetric fog settings use HDRP fog defaults") {
    const VolumetricFogSettings settings;
    CHECK_FALSE(settings.enabled);
    CHECK(settings.meanFreePath == doctest::Approx(400.0f));
    CHECK(settings.albedo.x == doctest::Approx(1.0f));
    CHECK(settings.albedo.y == doctest::Approx(1.0f));
    CHECK(settings.albedo.z == doctest::Approx(1.0f));
    CHECK(settings.baseHeight == doctest::Approx(0.0f));
    CHECK(settings.maximumHeight == doctest::Approx(50.0f));
    CHECK(settings.anisotropy == doctest::Approx(0.0f));
    CHECK(settings.depthExtent == doctest::Approx(64.0f));
    CHECK(settings.stepCount == 16u);
    CHECK(settings.directionalShadows);
    CHECK(settings.globalLightProbeDimmer == doctest::Approx(1.0f));
    CHECK(settings.resolutionScale == doctest::Approx(0.5f));
    CHECK(settings.temporal);
    CHECK(settings.temporalHistoryWeight == doctest::Approx(0.85f));
    CHECK(settings.temporalJitter);
    CHECK(settings.punctualLights);
    CHECK(settings.punctualShadows);
    CHECK(settings.maxPunctualLights == 8u);
    CHECK(settings.localVolumes);
    CHECK_FALSE(settings.active());

    VolumetricFogSettings enabled = settings;
    enabled.enabled = true;
    CHECK(enabled.active());
}

TEST_CASE("volumetric fog derived values follow HDRP height fog") {
    VolumetricFogSettings settings;
    settings.enabled = true;
    CHECK(settings.scaleHeight() == doctest::Approx(50.0f * 0.144765f));
    CHECK(settings.extinction() == doctest::Approx(1.0f / 400.0f));
    const Vec3 scattering = settings.scattering();
    CHECK(scattering.x == doctest::Approx(1.0f / 400.0f));
    CHECK(scattering.y == doctest::Approx(1.0f / 400.0f));
    CHECK(scattering.z == doctest::Approx(1.0f / 400.0f));

    settings.baseHeight = 10.0f;
    settings.maximumHeight = 30.0f;
    CHECK(settings.scaleHeight() == doctest::Approx(20.0f * 0.144765f));
}

TEST_CASE("volumetric fog settings sanitize invalid values") {
    VolumetricFogSettings settings;
    settings.enabled = true;
    settings.meanFreePath = std::numeric_limits<float>::quiet_NaN();
    settings.albedo = Vec3(std::numeric_limits<float>::infinity(), -1.0f, 6.0f);
    settings.baseHeight = 100.0f;
    settings.maximumHeight = 50.0f;
    settings.anisotropy = 2.0f;
    settings.depthExtent = -4.0f;
    settings.stepCount = 0;
    settings.globalLightProbeDimmer = 3.0f;
    settings.resolutionScale = 0.1f;
    settings.temporalHistoryWeight = 2.0f;
    settings.maxPunctualLights = 20;

    sanitizeVolumetricFogSettings(settings);
    CHECK(settings.meanFreePath == doctest::Approx(400.0f));
    CHECK(settings.albedo.x == doctest::Approx(1.0f));
    CHECK(settings.albedo.y == doctest::Approx(0.0f));
    CHECK(settings.albedo.z == doctest::Approx(4.0f));
    CHECK(settings.baseHeight == doctest::Approx(100.0f));
    CHECK(settings.maximumHeight == doctest::Approx(100.01f));
    CHECK(settings.anisotropy == doctest::Approx(0.99f));
    CHECK(settings.depthExtent == doctest::Approx(0.1f));
    CHECK(settings.stepCount == 4u);
    CHECK(settings.globalLightProbeDimmer == doctest::Approx(2.0f));
    CHECK(settings.resolutionScale == doctest::Approx(0.25f));
    CHECK(settings.temporalHistoryWeight == doctest::Approx(0.95f));
    CHECK(settings.maxPunctualLights == 16u);

    settings.stepCount = 100;
    sanitizeVolumetricFogSettings(settings);
    CHECK(settings.stepCount == 64u);
}

TEST_CASE("volumetric fog settings parse JSON") {
    VolumetricFogSettings settings;
    std::string error;
    REQUIRE(parseVolumetricFogSettings(R"({
        "enabled": true,
        "meanFreePath": 50.0,
        "albedo": [0.8, 0.7, 0.6],
        "baseHeight": 5.0,
        "maximumHeight": 75.0,
        "anisotropy": -0.25,
        "depthExtent": 32.0,
        "stepCount": 24,
        "directionalShadows": false,
        "globalLightProbeDimmer": 0.75,
        "resolutionScale": 0.25,
        "temporal": false,
        "temporalHistoryWeight": 0.5,
        "temporalJitter": false,
        "punctualLights": false,
        "punctualShadows": false,
        "maxPunctualLights": 4,
        "localVolumes": false
    })",
                                    settings, error));
    CHECK(settings.enabled);
    CHECK(settings.meanFreePath == doctest::Approx(50.0f));
    CHECK(settings.albedo.x == doctest::Approx(0.8f));
    CHECK(settings.albedo.y == doctest::Approx(0.7f));
    CHECK(settings.albedo.z == doctest::Approx(0.6f));
    CHECK(settings.baseHeight == doctest::Approx(5.0f));
    CHECK(settings.maximumHeight == doctest::Approx(75.0f));
    CHECK(settings.anisotropy == doctest::Approx(-0.25f));
    CHECK(settings.depthExtent == doctest::Approx(32.0f));
    CHECK(settings.stepCount == 24u);
    CHECK_FALSE(settings.directionalShadows);
    CHECK(settings.globalLightProbeDimmer == doctest::Approx(0.75f));
    CHECK(settings.resolutionScale == doctest::Approx(0.25f));
    CHECK_FALSE(settings.temporal);
    CHECK(settings.temporalHistoryWeight == doctest::Approx(0.5f));
    CHECK_FALSE(settings.temporalJitter);
    CHECK_FALSE(settings.punctualLights);
    CHECK_FALSE(settings.punctualShadows);
    CHECK(settings.maxPunctualLights == 4u);
    CHECK_FALSE(settings.localVolumes);

    CHECK_FALSE(parseVolumetricFogSettings(R"({"enabled":"yes"})", settings, error));
    CHECK_FALSE(parseVolumetricFogSettings(R"({"albedo":[1,1]})", settings, error));
    CHECK_FALSE(parseVolumetricFogSettings("[1,2,3]", settings, error));
}
