#include "render/debug/debug_display.h"

#include <doctest/doctest.h>

#include <string>

using namespace hbg::render;

TEST_CASE("debug display settings default to lit rendering") {
    const DebugDisplaySettings settings;
    CHECK(settings.shadows);
    CHECK_FALSE(settings.wireframe);
    CHECK(settings.gpuLightList);
    CHECK_FALSE(settings.freezeTime);
    CHECK(settings.showPanel);
    CHECK(settings.renderScale == doctest::Approx(1.0f));
    CHECK(settings.fullscreen == DebugDisplayMode::None);
}

TEST_CASE("debug display mode table is stable") {
    REQUIRE(debugDisplayModeCount() == 27);
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
    CHECK(debugDisplayModeAt(11) == DebugDisplayMode::Metallic);
    CHECK(debugDisplayModeAt(12) == DebugDisplayMode::AmbientOcclusion);
    CHECK(debugDisplayModeAt(13) == DebugDisplayMode::Emissive);
    CHECK(debugDisplayModeAt(14) == DebugDisplayMode::SSAO);
    CHECK(debugDisplayModeAt(15) == DebugDisplayMode::SSR);
    CHECK(debugDisplayModeAt(16) == DebugDisplayMode::SSGI);
    CHECK(debugDisplayModeAt(17) == DebugDisplayMode::Decals);
    CHECK(debugDisplayModeAt(18) == DebugDisplayMode::Exposure);
    CHECK(debugDisplayModeAt(19) == DebugDisplayMode::ColorGrading);
    CHECK(debugDisplayModeAt(20) == DebugDisplayMode::Bloom);
    CHECK(debugDisplayModeAt(21) == DebugDisplayMode::TaaHistory);
    CHECK(debugDisplayModeAt(22) == DebugDisplayMode::Dof);
    CHECK(debugDisplayModeAt(23) == DebugDisplayMode::Fxaa);
    CHECK(debugDisplayModeAt(24) == DebugDisplayMode::ContactShadows);
    CHECK(debugDisplayModeAt(25) == DebugDisplayMode::ShadowCascade);
    CHECK(debugDisplayModeAt(26) == DebugDisplayMode::ShadowFilter);
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
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::Metallic)) == "Metallic");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::AmbientOcclusion)) == "Ambient Occlusion");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::Emissive)) == "Emissive");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::SSAO)) == "Screen Space Ambient Occlusion");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::SSR)) == "Screen Space Reflections");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::SSGI)) ==
          "Screen Space Global Illumination");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::Decals)) == "Decals");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::Exposure)) == "Exposure");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::ColorGrading)) == "Color Grading LUT");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::Bloom)) == "Bloom");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::TaaHistory)) == "TAA History");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::Dof)) == "Depth of Field CoC");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::Fxaa)) == "FXAA Edges");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::ContactShadows)) == "Contact Shadows");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::ShadowCascade)) == "Shadow Cascade");
    CHECK(std::string(debugDisplayModeName(DebugDisplayMode::ShadowFilter)) == "Shadow Filter");
    CHECK(debugDisplayModeAt(999) == DebugDisplayMode::None);
}
