#include "render/camera/dynamic_resolution.h"

#include <doctest/doctest.h>

using namespace hbg::render;

namespace {

DynamicResolutionSettings makeSettings() {
    DynamicResolutionSettings settings;
    settings.enabled = true;
    settings.minScale = 0.5f;
    settings.maxScale = 1.0f;
    settings.targetFrameMs = 1000.0f / 60.0f;
    settings.scaleStep = 0.05f;
    settings.evaluationFrames = 2;
    return settings;
}

} // namespace

TEST_CASE("DynamicResolutionController stays idle when disabled") {
    DynamicResolutionController controller;
    DynamicResolutionSettings settings = makeSettings();
    settings.enabled = false;
    controller.configure(settings);
    controller.reset(0.8f);

    CHECK_FALSE(controller.update(0.1).has_value());
    CHECK_FALSE(controller.update(0.1).has_value());
    CHECK(controller.currentScale() == doctest::Approx(0.8f));
    CHECK(controller.sampleCount() == 0);
    CHECK(controller.adjustmentCount() == 0);
}

TEST_CASE("DynamicResolutionController lowers scale for slow frames") {
    DynamicResolutionController controller;
    controller.configure(makeSettings());
    controller.reset(0.8f);

    CHECK_FALSE(controller.update(0.05).has_value());
    const auto first = controller.update(0.05);
    REQUIRE(first.has_value());
    CHECK(*first == doctest::Approx(0.75f));
    CHECK(controller.averageFrameMs() == doctest::Approx(50.0f));

    std::optional<float> lastScale;
    for (int index = 0; index < 12; ++index) {
        const auto next = controller.update(0.05);
        if (next.has_value()) {
            lastScale = next;
        }
    }
    REQUIRE(lastScale.has_value());
    CHECK(*lastScale == doctest::Approx(0.5f));
    CHECK(controller.currentScale() == doctest::Approx(0.5f));
    CHECK(controller.adjustmentCount() == 6u);
}

TEST_CASE("DynamicResolutionController raises scale for fast frames") {
    DynamicResolutionController controller;
    controller.configure(makeSettings());
    controller.reset(0.5f);

    controller.update(0.005);
    const auto raised = controller.update(0.005);
    REQUIRE(raised.has_value());
    CHECK(*raised == doctest::Approx(0.55f));
    CHECK(controller.currentScale() == doctest::Approx(0.55f));
}

TEST_CASE("DynamicResolutionController keeps scale inside the dead band") {
    DynamicResolutionController controller;
    controller.configure(makeSettings());
    controller.reset(0.8f);

    controller.update(1.0 / 60.0);
    CHECK_FALSE(controller.update(1.0 / 60.0).has_value());
    CHECK(controller.currentScale() == doctest::Approx(0.8f));
}

TEST_CASE("DynamicResolutionController clamps and sanitizes settings") {
    DynamicResolutionSettings settings = makeSettings();
    settings.minScale = 1.2f;
    settings.maxScale = 0.2f;
    settings.scaleStep = 0.0f;
    settings.evaluationFrames = 0;
    settings.targetFrameMs = 0.0f;
    DynamicResolutionController controller;
    controller.configure(settings);

    CHECK(controller.settings().minScale == doctest::Approx(0.2f));
    CHECK(controller.settings().maxScale == doctest::Approx(1.0f));
    CHECK(controller.settings().scaleStep > 0.0f);
    CHECK(controller.settings().evaluationFrames == 1u);
    CHECK(controller.settings().targetFrameMs > 0.0f);
    CHECK(controller.currentScale() == doctest::Approx(1.0f));
}
