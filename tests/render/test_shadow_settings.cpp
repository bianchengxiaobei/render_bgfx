#include "render/shadows/shadow_settings.h"

#include <doctest/doctest.h>

#include <string>

using namespace hbg::render;
using hbg::Vec3;

TEST_CASE("shadow settings defaults and filter mapping") {
    const ShadowSettings settings;
    CHECK(settings.maxDistance == doctest::Approx(200.0f));
    CHECK(settings.cascadeCount == 4);
    CHECK(settings.cascadeSplits[0] == doctest::Approx(0.10f));
    CHECK(settings.cascadeSplits[3] == doctest::Approx(1.0f));
    CHECK(settings.cascadeFade == doctest::Approx(0.10f));
    CHECK(settings.directionalFilter == ShadowFilter::Pcf7x7);
    CHECK(settings.punctualFilter == ShadowFilter::Pcf5x5);
    CHECK(settings.areaFilter == ShadowFilter::Pcss);
    CHECK(settings.directionalResolution == 1024);
    CHECK(settings.pointResolution == 256);
    CHECK(settings.spotResolution == 512);
    CHECK(settings.areaResolution == kDefaultAreaShadowResolution);
    CHECK(settings.atlasSize == kDefaultShadowAtlasSize);
    CHECK(settings.areaAtlasSize == kDefaultAreaShadowAtlasSize);
    CHECK(settings.maxRequests == kDefaultShadowRequests);
    CHECK(kMaxShadowRequests == 128);
    CHECK(kDefaultShadowRequests == 64);
    CHECK(settings.normalBias == doctest::Approx(0.2f));
    CHECK(settings.slopeBias == doctest::Approx(0.5f));
    CHECK(settings.shadowNearPlane == doctest::Approx(0.1f));
    CHECK(settings.softness == doctest::Approx(0.5f));
    CHECK(settings.blockerSamples == 16);
    CHECK(settings.filterSamples == 24);
    CHECK(settings.minFilterSize == doctest::Approx(0.01f));
    CHECK_FALSE(settings.microShadows);
    CHECK(settings.microShadowOpacity == doctest::Approx(1.0f));
    CHECK(settings.shadowDimmer == doctest::Approx(1.0f));
    CHECK(settings.shadowFadeDistance == doctest::Approx(10000.0f));
    CHECK(settings.shadowTint == Vec3(0.0f, 0.0f, 0.0f));
    CHECK(shadowFilterTapMode(ShadowFilter::Off) == 0);
    CHECK(shadowFilterTapMode(ShadowFilter::Pcf3x3) == 1);
    CHECK(shadowFilterTapMode(ShadowFilter::Pcf5x5) == 2);
    CHECK(shadowFilterTapMode(ShadowFilter::Pcf7x7) == 3);
    CHECK(shadowFilterTapMode(ShadowFilter::Pcss) == 4);
}

TEST_CASE("shadow settings parse JSON and sanitize cascade splits") {
    const std::string json = R"({
        "maxDistance": 75.0,
        "cascadeCount": 2,
        "cascadeSplits": [0.2, 0.6, 0.8, 1.0],
        "cascadeFade": 0.2,
        "directionalFilter": "pcf5x5",
        "punctualFilter": "high",
        "areaFilter": "pcf5x5",
        "directionalResolution": 512,
        "pointResolution": 256,
        "spotResolution": 1024,
        "areaResolution": 1024,
        "atlasSize": 4096,
        "areaAtlasSize": 2048,
        "maxRequests": 48,
        "normalBias": 0.4,
        "slopeBias": 1.5,
        "shadowNearPlane": 0.25,
        "softness": 0.75,
        "blockerSamples": 20,
        "filterSamples": 32,
        "minFilterSize": 0.05,
        "microShadows": { "enabled": true, "opacity": 0.6 },
        "shadowDimmer": 0.65,
        "shadowFadeDistance": 55.0,
        "shadowTint": [0.2, 0.3, 0.4],
        "cachedPunctualShadows": true,
        "cachedAreaShadows": true
    })";

    ShadowSettings settings;
    std::string error;
    REQUIRE(parseShadowSettings(json, settings, error));
    CHECK(settings.maxDistance == doctest::Approx(75.0f));
    CHECK(settings.cascadeCount == 2);
    CHECK(settings.cascadeSplits[0] == doctest::Approx(0.2f));
    CHECK(settings.cascadeSplits[1] == doctest::Approx(1.0f));
    CHECK(settings.cascadeSplits[2] == doctest::Approx(1.0f));
    CHECK(settings.cascadeFade == doctest::Approx(0.2f));
    CHECK(settings.directionalFilter == ShadowFilter::Pcf5x5);
    CHECK(settings.punctualFilter == ShadowFilter::Pcss);
    CHECK(settings.areaFilter == ShadowFilter::Pcf5x5);
    CHECK(settings.areaResolution == 1024);
    CHECK(settings.atlasSize == 4096);
    CHECK(settings.areaAtlasSize == 2048);
    CHECK(settings.maxRequests == 48);
    CHECK(settings.slopeBias == doctest::Approx(1.5f));
    CHECK(settings.shadowNearPlane == doctest::Approx(0.25f));
    CHECK(settings.softness == doctest::Approx(0.75f));
    CHECK(settings.blockerSamples == 20);
    CHECK(settings.filterSamples == 32);
    CHECK(settings.minFilterSize == doctest::Approx(0.05f));
    CHECK(settings.microShadows);
    CHECK(settings.microShadowOpacity == doctest::Approx(0.6f));
    CHECK(settings.cachedPunctualShadows);
    CHECK(settings.cachedAreaShadows);
    CHECK(settings.shadowDimmer == doctest::Approx(0.65f));
    CHECK(settings.shadowFadeDistance == doctest::Approx(55.0f));
    CHECK(settings.shadowTint == Vec3(0.2f, 0.3f, 0.4f));
}

TEST_CASE("shadow settings reject malformed values and sanitize ranges") {
    ShadowSettings settings;
    std::string error;
    CHECK(parseShadowSettings(R"({"cascadeSplits": [0.5]})", settings, error));
    CHECK(settings.cascadeCount == 4);
    CHECK(settings.cascadeSplits[0] == doctest::Approx(0.10f));
    CHECK_FALSE(parseShadowSettings(R"({"cascadeSplits": []})", settings, error));
    CHECK_FALSE(parseShadowSettings(R"({"directionalFilter": "bogus"})", settings, error));
    CHECK(parseShadowSettings(R"({"cascadeSplits": [0.4, 0.2]})", settings, error));
    CHECK(settings.cascadeSplits[0] == doctest::Approx(0.10f));

    REQUIRE(parseShadowSettings(
        R"({"cascadeCount": 9, "directionalResolution": 16, "pointResolution": 0,
            "spotResolution": 8192, "areaResolution": 128, "atlasSize": 12, "areaAtlasSize": 12,
            "maxRequests": 500, "normalBias": -1, "slopeBias": 99, "shadowNearPlane": 0,
            "softness": 9, "blockerSamples": 0, "filterSamples": 100, "minFilterSize": 2,
            "cascadeFade": -1, "microShadows": {"enabled": true, "opacity": 2}})",
        settings, error));
    CHECK(settings.cascadeCount == 4);
    CHECK(settings.directionalResolution == 1024);
    CHECK(settings.pointResolution == 256);
    CHECK(settings.spotResolution == 512);
    CHECK(settings.areaResolution == 512);
    CHECK(settings.atlasSize == 2048);
    CHECK(settings.areaAtlasSize == 1024);
    CHECK(settings.maxRequests == kMaxShadowRequests);
    CHECK(settings.normalBias == doctest::Approx(0.0f));
    CHECK(settings.slopeBias == doctest::Approx(10.0f));
    CHECK(settings.shadowNearPlane == doctest::Approx(0.01f));
    CHECK(settings.softness == doctest::Approx(4.0f));
    CHECK(settings.blockerSamples == 1);
    CHECK(settings.filterSamples == 64);
    CHECK(settings.minFilterSize == doctest::Approx(1.0f));
    CHECK(settings.cascadeFade == doctest::Approx(0.0f));
    CHECK(settings.microShadows);
    CHECK(settings.microShadowOpacity == doctest::Approx(1.0f));
}

TEST_CASE("shadow fade scale and bias follow the distance border") {
    float scale = 0.0f;
    float bias = 0.0f;
    computeShadowFade(100.0f, 0.1f, scale, bias);
    CHECK(scale > 0.0f);
    CHECK(bias < 0.0f);
    CHECK(scale * 90.0f * 90.0f + bias == doctest::Approx(0.0f));
    CHECK(scale * 100.0f * 100.0f + bias == doctest::Approx(1.0f));

    computeShadowFade(100.0f, 0.0f, scale, bias);
    CHECK(scale == doctest::Approx(1000.0f));
    CHECK(bias == doctest::Approx(-10000000.0f));
}

TEST_CASE("shadow settings round trip through JSON") {
    ShadowSettings source;
    source.maxDistance = 123.0f;
    source.cascadeCount = 3;
    source.cascadeSplits = {0.1f, 0.4f, 1.0f, 1.0f};
    source.directionalFilter = ShadowFilter::Pcf7x7;
    source.punctualFilter = ShadowFilter::Off;
    source.areaFilter = ShadowFilter::Pcf5x5;
    source.pointResolution = 256;
    source.spotResolution = 2048;
    source.areaResolution = 1024;
    source.atlasSize = 1024;
    source.areaAtlasSize = 2048;
    source.maxRequests = 12;
    source.cascadeFade = 0.25f;
    source.slopeBias = 1.25f;
    source.shadowNearPlane = 0.2f;
    source.softness = 0.8f;
    source.blockerSamples = 12;
    source.filterSamples = 20;
    source.minFilterSize = 0.03f;
    source.microShadows = true;
    source.microShadowOpacity = 0.7f;
    source.shadowDimmer = 0.55f;
    source.shadowFadeDistance = 75.0f;
    source.shadowTint = Vec3(0.11f, 0.22f, 0.33f);
    source.cachedPunctualShadows = true;
    source.cachedAreaShadows = true;

    ShadowSettings parsed;
    std::string error;
    REQUIRE(parseShadowSettings(shadowSettingsToJson(source), parsed, error));
    CHECK(parsed.maxDistance == doctest::Approx(source.maxDistance));
    CHECK(parsed.cascadeCount == source.cascadeCount);
    CHECK(parsed.cascadeFade == doctest::Approx(source.cascadeFade));
    CHECK(parsed.directionalFilter == source.directionalFilter);
    CHECK(parsed.punctualFilter == source.punctualFilter);
    CHECK(parsed.areaFilter == source.areaFilter);
    CHECK(parsed.pointResolution == source.pointResolution);
    CHECK(parsed.spotResolution == source.spotResolution);
    CHECK(parsed.areaResolution == source.areaResolution);
    CHECK(parsed.atlasSize == source.atlasSize);
    CHECK(parsed.areaAtlasSize == source.areaAtlasSize);
    CHECK(parsed.maxRequests == source.maxRequests);
    CHECK(parsed.slopeBias == doctest::Approx(source.slopeBias));
    CHECK(parsed.shadowNearPlane == doctest::Approx(source.shadowNearPlane));
    CHECK(parsed.softness == doctest::Approx(source.softness));
    CHECK(parsed.blockerSamples == source.blockerSamples);
    CHECK(parsed.filterSamples == source.filterSamples);
    CHECK(parsed.minFilterSize == doctest::Approx(source.minFilterSize));
    CHECK(parsed.microShadows == source.microShadows);
    CHECK(parsed.microShadowOpacity == doctest::Approx(source.microShadowOpacity));
    CHECK(parsed.shadowDimmer == doctest::Approx(source.shadowDimmer));
    CHECK(parsed.shadowFadeDistance == doctest::Approx(source.shadowFadeDistance));
    CHECK(parsed.shadowTint == source.shadowTint);
    CHECK(parsed.cachedPunctualShadows);
    CHECK(parsed.cachedAreaShadows);
}
