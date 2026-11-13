#include "render/postprocess/ssr_settings.h"

#include <doctest/doctest.h>

using namespace hbg::render;

TEST_CASE("SSR settings defaults and sanitize") {
    SsrSettings settings;
    CHECK(settings.maxDistance == doctest::Approx(40.0f));
    CHECK(settings.maxSteps == 32);
    CHECK(settings.thickness == doctest::Approx(0.6f));
    CHECK(settings.roughnessFade == doctest::Approx(0.25f));
    CHECK(settings.edgeFade == doctest::Approx(0.1f));
    CHECK(settings.intensity == doctest::Approx(1.0f));
    CHECK(settings.enabled);

    settings.maxDistance = 0.0f;
    settings.maxSteps = 0;
    settings.thickness = -1.0f;
    settings.roughnessFade = 2.0f;
    settings.edgeFade = 1.0f;
    settings.intensity = 20.0f;
    sanitizeSsrSettings(settings);

    CHECK(settings.maxDistance == doctest::Approx(0.1f));
    CHECK(settings.maxSteps == 1);
    CHECK(settings.thickness == doctest::Approx(0.001f));
    CHECK(settings.roughnessFade == doctest::Approx(1.0f));
    CHECK(settings.edgeFade == doctest::Approx(0.5f));
    CHECK(settings.intensity == doctest::Approx(10.0f));

    settings.maxDistance = 2000.0f;
    settings.maxSteps = 99;
    settings.thickness = 20.0f;
    settings.roughnessFade = -1.0f;
    settings.edgeFade = -1.0f;
    settings.intensity = -1.0f;
    sanitizeSsrSettings(settings);

    CHECK(settings.maxDistance == doctest::Approx(1000.0f));
    CHECK(settings.maxSteps == 64);
    CHECK(settings.thickness == doctest::Approx(10.0f));
    CHECK(settings.roughnessFade == doctest::Approx(0.0f));
    CHECK(settings.edgeFade == doctest::Approx(0.0f));
    CHECK(settings.intensity == doctest::Approx(0.0f));
}
