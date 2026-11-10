#include "app/backend.h"

#include <doctest/doctest.h>

using namespace hbg;

TEST_CASE("parseBackend accepts aliases") {
    CHECK(parseBackend("d3d11") == bgfx::RendererType::Direct3D11);
    CHECK(parseBackend("DX11") == bgfx::RendererType::Direct3D11);
    CHECK(parseBackend("Vulkan") == bgfx::RendererType::Vulkan);
    CHECK(parseBackend("vk") == bgfx::RendererType::Vulkan);
    CHECK(parseBackend("auto") == bgfx::RendererType::Count);
}

TEST_CASE("parseBackend rejects unknown backends") {
    CHECK_FALSE(parseBackend("bogus").has_value());
    CHECK_FALSE(parseBackend("dx").has_value());
}

TEST_CASE("parseCommandLine uses defaults") {
    const char* argv[] = {"hbg"};
    const CommandLineResult result = parseCommandLine(1, argv);

    CHECK(result.ok);
    CHECK_FALSE(result.help);
    CHECK(result.options.backend == bgfx::RendererType::Count);
    CHECK(result.options.width == 1280);
    CHECK(result.options.height == 720);
    CHECK(result.options.vsync);
    CHECK_FALSE(result.options.verbose);
    CHECK_FALSE(result.options.bgfxDebug);
    CHECK_FALSE(result.options.gfxTest);
    CHECK_FALSE(result.options.debugLightData);
    CHECK(result.options.debugDisplayMode == render::DebugDisplayMode::None);
    CHECK_FALSE(result.options.cameraOrbit);
    CHECK_FALSE(result.options.debugWireframe);
    CHECK_FALSE(result.options.disableShadows);
    CHECK(result.options.debugUi);
    CHECK_FALSE(result.options.framesSpecified);
    CHECK(result.options.assetTestPath.empty());
    CHECK(result.options.scenePath.empty());
    CHECK(result.options.renderTestPath.empty());
    CHECK(result.options.screenshotPath.empty());
    CHECK(result.options.pipelineSettingsPath.empty());
    CHECK(result.options.frameSettingsPath.empty());
    CHECK(result.options.skyPath.empty());
    CHECK(result.options.skyIntensity == doctest::Approx(1.0f));
    CHECK(result.options.skyRotation == doctest::Approx(0.0f));
    CHECK(result.options.frames == 3);
    CHECK(result.options.renderScale == doctest::Approx(1.0f));
    CHECK(result.options.postProcess.exposureMode == render::ExposureMode::Fixed);
    CHECK(result.options.postProcess.fixedEv100 == doctest::Approx(-1.2630344f));
    CHECK(result.options.postProcess.limitMin == doctest::Approx(-1.0f));
    CHECK(result.options.postProcess.limitMax == doctest::Approx(14.0f));
    CHECK(result.options.postProcess.tonemapping == render::TonemappingMode::ACES);
}

TEST_CASE("parseCommandLine parses options") {
    const char* argv[] = {
        "hbg",          "--backend=Vulkan",     "--width=1920",        "--height=1080",      "--no-vsync",
        "--verbose",    "--bgfx-debug",         "--gfx-test",          "--scene=a.gltf",     "--render-test=b.gltf",
        "--frames=7",   "--screenshot=out.png", "--render-scale=0.5",  "--debug-light-data", "--debug-wireframe",
        "--no-shadows", "--sky=sky.hdr",        "--sky-intensity=2.5", "--sky-rotation=45"};
    const CommandLineResult result = parseCommandLine(19, argv);

    CHECK(result.ok);
    CHECK_FALSE(result.help);
    CHECK(result.options.backend == bgfx::RendererType::Vulkan);
    CHECK(result.options.width == 1920);
    CHECK(result.options.height == 1080);
    CHECK_FALSE(result.options.vsync);
    CHECK(result.options.verbose);
    CHECK(result.options.bgfxDebug);
    CHECK(result.options.gfxTest);
    CHECK(result.options.scenePath == "a.gltf");
    CHECK(result.options.renderTestPath == "b.gltf");
    CHECK(result.options.frames == 7);
    CHECK(result.options.screenshotPath == "out.png");
    CHECK(result.options.renderScale == doctest::Approx(0.5f));
    CHECK(result.options.debugLightData);
    CHECK(result.options.debugDisplayMode == render::DebugDisplayMode::LightData);
    CHECK(result.options.debugWireframe);
    CHECK(result.options.disableShadows);
    CHECK(result.options.framesSpecified);
    CHECK(result.options.skyPath == "sky.hdr");
    CHECK(result.options.skyIntensity == doctest::Approx(2.5f));
    CHECK(result.options.skyRotation == doctest::Approx(45.0f));
}

TEST_CASE("parseCommandLine parses final output settings") {
    const char* argv[] = {"hbg",
                          "--exposure-mode=auto",
                          "--exposure-ev100=-2.5",
                          "--exposure-compensation=1.5",
                          "--exposure-limit-min=-4",
                          "--exposure-limit-max=9",
                          "--tonemap=neutral",
                          "--debug-exposure=0.5"};
    const CommandLineResult result = parseCommandLine(8, argv);

    CHECK(result.ok);
    CHECK(result.options.postProcess.exposureMode == render::ExposureMode::Automatic);
    CHECK(result.options.postProcess.fixedEv100 == doctest::Approx(-2.5f));
    CHECK(result.options.postProcess.compensation == doctest::Approx(1.5f));
    CHECK(result.options.postProcess.limitMin == doctest::Approx(-4.0f));
    CHECK(result.options.postProcess.limitMax == doctest::Approx(9.0f));
    CHECK(result.options.postProcess.tonemapping == render::TonemappingMode::Neutral);
    CHECK(result.options.postProcess.debugExposure == doctest::Approx(0.5f));
}

TEST_CASE("parseCommandLine parses prepass debug views and camera orbit") {
    const char* argv[] = {"hbg", "--debug-view=normal", "--camera-orbit"};
    const CommandLineResult result = parseCommandLine(3, argv);
    CHECK(result.ok);
    CHECK(result.options.debugDisplayMode == render::DebugDisplayMode::Normal);
    CHECK(result.options.cameraOrbit);

    const char* invalid[] = {"hbg", "--debug-view=bogus"};
    CHECK_FALSE(parseCommandLine(2, invalid).ok);
}

TEST_CASE("parseCommandLine parses M1.10 debug views") {
    const auto parse = [](const char* value) {
        const char* argv[] = {"hbg", value};
        return parseCommandLine(2, argv);
    };

    CHECK(parse("--debug-view=depth").options.debugDisplayMode == render::DebugDisplayMode::Depth);
    CHECK(parse("--debug-view=depth-pyramid").options.debugDisplayMode == render::DebugDisplayMode::DepthPyramid);
    CHECK(parse("--debug-view=cluster").options.debugDisplayMode == render::DebugDisplayMode::LightCluster);
    CHECK(parse("--debug-view=directional-shadow-atlas").options.debugDisplayMode ==
          render::DebugDisplayMode::ShadowAtlasDirectional);
    CHECK(parse("--debug-view=punctual-shadow-atlas").options.debugDisplayMode ==
          render::DebugDisplayMode::ShadowAtlasPunctual);
    CHECK(parse("--debug-view=albedo").options.debugDisplayMode == render::DebugDisplayMode::Albedo);
    CHECK(parse("--debug-view=roughness").options.debugDisplayMode == render::DebugDisplayMode::Roughness);
    CHECK_FALSE(parse("--debug-view=clusterbogus").ok);
}

TEST_CASE("parseCommandLine can disable the debug UI") {
    const char* argv[] = {"hbg", "--no-debug-ui"};
    const CommandLineResult result = parseCommandLine(2, argv);

    CHECK(result.ok);
    CHECK_FALSE(result.options.debugUi);
}

TEST_CASE("parseCommandLine parses settings paths") {
    const char* argv[] = {"hbg", "--pipeline-settings=config/pipeline.json",
                          "--frame-settings=config/frame_settings.json"};
    const CommandLineResult result = parseCommandLine(3, argv);

    CHECK(result.ok);
    CHECK(result.options.pipelineSettingsPath == "config/pipeline.json");
    CHECK(result.options.frameSettingsPath == "config/frame_settings.json");
}

TEST_CASE("parseCommandLine reports help") {
    const char* argv[] = {"hbg", "--help"};
    const CommandLineResult result = parseCommandLine(2, argv);

    CHECK(result.ok);
    CHECK(result.help);
}

TEST_CASE("parseCommandLine rejects invalid values") {
    {
        const char* argv[] = {"hbg", "--backend=bogus"};
        CHECK_FALSE(parseCommandLine(2, argv).ok);
    }
    {
        const char* argv[] = {"hbg", "--width=0"};
        CHECK_FALSE(parseCommandLine(2, argv).ok);
    }
    {
        const char* argv[] = {"hbg", "--unknown"};
        CHECK_FALSE(parseCommandLine(2, argv).ok);
    }
    {
        const char* argv[] = {"hbg", "--render-scale=0"};
        CHECK_FALSE(parseCommandLine(2, argv).ok);
    }
    {
        const char* argv[] = {"hbg", "--render-scale=1.5"};
        CHECK_FALSE(parseCommandLine(2, argv).ok);
    }
    {
        const char* argv[] = {"hbg", "--sky-intensity=-1"};
        CHECK_FALSE(parseCommandLine(2, argv).ok);
    }
    {
        const char* argv[] = {"hbg", "--sky-rotation=nan"};
        CHECK_FALSE(parseCommandLine(2, argv).ok);
    }
    {
        const char* argv[] = {"hbg", "--exposure-mode=bogus"};
        CHECK_FALSE(parseCommandLine(2, argv).ok);
    }
    {
        const char* argv[] = {"hbg", "--tonemap=bogus"};
        CHECK_FALSE(parseCommandLine(2, argv).ok);
    }
    {
        const char* argv[] = {"hbg", "--exposure-limit-min=100"};
        CHECK_FALSE(parseCommandLine(2, argv).ok);
    }
    {
        const char* argv[] = {"hbg", "--debug-exposure=nan"};
        CHECK_FALSE(parseCommandLine(2, argv).ok);
    }
}
