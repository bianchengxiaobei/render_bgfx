#include "render/postprocess/post_process_settings.h"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <string>

using namespace hbg::render;

TEST_CASE("post-process exposure math follows HDRP physical camera constants") {
    CHECK(lensImperfectionExposureScale() == doctest::Approx(1.2f));
    CHECK(convertEv100ToExposure(0.0f) == doctest::Approx(1.0f / 1.2f));
    CHECK(convertExposureToEv100(1.0f) == doctest::Approx(std::log2(1.0f / 1.2f)));
    CHECK(computeEv100FromAverageLuminance(1.0f) == doctest::Approx(3.0f));
}

TEST_CASE("post-process fixed exposure converts EV100 to a linear multiplier") {
    PostProcessSettings defaults;
    CHECK(fixedExposureMultiplier(defaults) == doctest::Approx(2.0f));

    PostProcessSettings settings;
    settings.fixedEv100 = 0.0f;
    CHECK(fixedExposureMultiplier(settings) == doctest::Approx(1.0f / 1.2f));

    settings.fixedEv100 = -2.0f;
    CHECK(fixedExposureMultiplier(settings) == doctest::Approx(4.0f / 1.2f));

    settings.compensation = 1.0f;
    settings.debugExposure = 0.5f;
    CHECK(fixedExposureMultiplier(settings) == doctest::Approx(std::exp2(3.5f) / 1.2f));
}

TEST_CASE("post-process automatic exposure applies compensation and limits") {
    PostProcessSettings settings;
    settings.limitMin = -2.0f;
    settings.limitMax = 8.0f;
    settings.compensation = 1.0f;
    settings.debugExposure = 0.5f;

    CHECK(computeAutoExposureEv100(1.0f, settings) == doctest::Approx(1.5f));
    CHECK(computeAutoExposureEv100(0.001f, settings) == doctest::Approx(-2.0f));
    CHECK(computeAutoExposureEv100(1000.0f, settings) == doctest::Approx(8.0f));
}

TEST_CASE("post-process settings sanitize invalid values") {
    PostProcessSettings settings;
    settings.fixedEv100 = std::numeric_limits<float>::quiet_NaN();
    settings.compensation = std::numeric_limits<float>::infinity();
    settings.debugExposure = 100.0f;
    settings.limitMin = 10.0f;
    settings.limitMax = -5.0f;

    sanitizePostProcessSettings(settings);

    CHECK(settings.fixedEv100 == doctest::Approx(-1.2630344f));
    CHECK(settings.compensation == doctest::Approx(0.0f));
    CHECK(settings.debugExposure == doctest::Approx(20.0f));
    CHECK(settings.limitMin == doctest::Approx(-5.0f));
    CHECK(settings.limitMax == doctest::Approx(10.0f));
}

TEST_CASE("post-process mode names are stable for UI and CLI") {
    CHECK(exposureModeCount() == 2);
    CHECK(tonemappingModeCount() == 3);
    CHECK(std::string(exposureModeName(ExposureMode::Fixed)) == "Fixed");
    CHECK(std::string(exposureModeName(ExposureMode::Automatic)) == "Automatic");
    CHECK(std::string(tonemappingModeName(TonemappingMode::None)) == "None");
    CHECK(std::string(tonemappingModeName(TonemappingMode::Neutral)) == "Neutral");
    CHECK(std::string(tonemappingModeName(TonemappingMode::ACES)) == "ACES");
}
