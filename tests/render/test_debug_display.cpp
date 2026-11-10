#include "render/debug/debug_display.h"

#include <doctest/doctest.h>

#include <string>

using namespace hbg::render;

TEST_CASE("debug display settings default to lit rendering") {
    const DebugDisplaySettings settings;
    CHECK(settings.shadows);
    CHECK_FALSE(settings.wireframe);
    CHECK_FALSE(settings.freezeTime);
    CHECK(settings.showPanel);
    CHECK(settings.renderScale == doctest::Approx(1.0f));
    CHECK(settings.fullscreen == DebugDisplayMode::None);
}

TEST_CASE("debug display mode table is stable") {
    REQUIRE(debugDisplayModeCount() == 11);
    CHECK(debugDisplayModeAt(0) == DebugDisplayMode::None);
    CHECK(debugDisplayModeAt(1) == DebugDisplayMode::LightData);
    CHECK(debugDisplayModeAt(2) == DebugDisplayMode::Normal);
    CHECK(debugDisplayModeAt(3) == DebugDisplayMode::MotionVectors);
    CHECK(debugDisplayModeAt(4) == DebugDisplayMode::Depth);
    CHECK(debugDisplayModeAt(5) == DebugDisplayMode::DepthPyramid);
    CHECK(debugDisplayModeAt(6) == DebugDisplayMode::LightCluster);
    CHECK(debugDisplayModeAt(7) == DebugDisplayMode::ShadowAtlasDirectional);
    CHECK(debugDisplayModeAt(8) == DebugDisplayMode::ShadowAtlasPunctual);
    CHECK(debugDisplayModeAt(9) == DebugDisplayMode::Albedo);
    CHECK(debugDisplayModeAt(10) == DebugDisplayMode::Roughness);
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::None)) == "Off");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::LightData)) == "Light Data Texture");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::Normal)) == "Normal Buffer");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::MotionVectors)) == "Motion Vectors");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::Depth)) == "Depth");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::DepthPyramid)) == "Depth Pyramid");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::LightCluster)) == "Light Cluster (Tiles)");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::ShadowAtlasDirectional)) == "Directional Shadow Atlas");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::ShadowAtlasPunctual)) == "Punctual Shadow Atlas");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::Albedo)) == "Albedo");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::Roughness)) == "Roughness");
    CHECK(debugDisplayModeAt(999) == DebugDisplayMode::None);
}
