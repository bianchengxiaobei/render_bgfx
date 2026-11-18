#include "render/postprocess/ssao_settings.h"

#include <doctest/doctest.h>

using namespace hbg::render;

TEST_CASE("SSAO settings sanitize values and step count") {
    SsaoSettings settings;
    CHECK(settings.radius == doctest::Approx(2.0f));
    CHECK(settings.intensity == doctest::Approx(2.0f));
    CHECK(settings.stepCount == 6);
    CHECK(settings.halfResolution);
    CHECK_FALSE(settings.temporalAccumulation);

    settings.radius = -1.0f;
    settings.intensity = 10.0f;
    settings.bias = -0.5f;
    settings.thickness = 0.0f;
    settings.falloff = 2.0f;
    settings.stepCount = 17;
    sanitizeSsaoSettings(settings);

    CHECK(settings.radius == doctest::Approx(0.01f));
    CHECK(settings.intensity == doctest::Approx(4.0f));
    CHECK(settings.bias == doctest::Approx(0.0f));
    CHECK(settings.thickness == doctest::Approx(0.001f));
    CHECK(settings.falloff == doctest::Approx(1.0f));
    CHECK(settings.stepCount == 16);

    settings.stepCount = 5;
    sanitizeSsaoSettings(settings);
    CHECK(settings.stepCount == 4);
}

TEST_CASE("SSAO half resolution uses ceil and preserves full resolution") {
    CHECK(ssaoResolution(1920, 1080, true) == std::array<uint16_t, 2>{960, 540});
    CHECK(ssaoResolution(641, 361, true) == std::array<uint16_t, 2>{321, 181});
    CHECK(ssaoResolution(1, 1, true) == std::array<uint16_t, 2>{1, 1});
    CHECK(ssaoResolution(641, 361, false) == std::array<uint16_t, 2>{641, 361});
    CHECK(ssaoResolution(0, 0, true) == std::array<uint16_t, 2>{1, 1});
}
