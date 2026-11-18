#include "render/postprocess/ssgi_settings.h"

#include <doctest/doctest.h>

using namespace hbg::render;

TEST_CASE("SSGI settings defaults and sanitize") {
    SsgiSettings settings;
    CHECK(settings.intensity == doctest::Approx(1.0f));
    CHECK(settings.maxDistance == doctest::Approx(30.0f));
    CHECK(settings.maxSteps == 8);
    CHECK(settings.rayCount == 32);
    CHECK(settings.falloff == doctest::Approx(1.0f));
    CHECK(settings.thickness == doctest::Approx(0.5f));
    CHECK(settings.ambientBias == doctest::Approx(0.5f));
    CHECK(settings.halfResolution);
    CHECK_FALSE(settings.temporalAccumulation);
    CHECK(settings.enabled);

    settings.intensity = -1.0f;
    settings.maxDistance = 0.0f;
    settings.maxSteps = 0;
    settings.rayCount = 0;
    settings.falloff = -1.0f;
    settings.thickness = -1.0f;
    settings.ambientBias = -1.0f;
    sanitizeSsgiSettings(settings);

    CHECK(settings.intensity == doctest::Approx(0.0f));
    CHECK(settings.maxDistance == doctest::Approx(0.1f));
    CHECK(settings.maxSteps == 1);
    CHECK(settings.rayCount == 1);
    CHECK(settings.falloff == doctest::Approx(0.0f));
    CHECK(settings.thickness == doctest::Approx(0.001f));
    CHECK(settings.ambientBias == doctest::Approx(0.0f));

    settings.intensity = 20.0f;
    settings.maxDistance = 500.0f;
    settings.maxSteps = 99;
    settings.rayCount = 99;
    settings.falloff = 10.0f;
    settings.thickness = 20.0f;
    settings.ambientBias = 2.0f;
    sanitizeSsgiSettings(settings);

    CHECK(settings.intensity == doctest::Approx(8.0f));
    CHECK(settings.maxDistance == doctest::Approx(200.0f));
    CHECK(settings.maxSteps == 16);
    CHECK(settings.rayCount == 32);
    CHECK(settings.falloff == doctest::Approx(4.0f));
    CHECK(settings.thickness == doctest::Approx(10.0f));
    CHECK(settings.ambientBias == doctest::Approx(1.0f));
}

TEST_CASE("SSGI resolution uses half resolution when requested") {
    CHECK(ssgiResolution(1920, 1080, true) == std::array<uint16_t, 2>{960, 540});
    CHECK(ssgiResolution(1921, 1081, true) == std::array<uint16_t, 2>{961, 541});
    CHECK(ssgiResolution(1920, 1080, false) == std::array<uint16_t, 2>{1920, 1080});
    CHECK(ssgiResolution(0, 1080, true) == std::array<uint16_t, 2>{1, 1});
}
