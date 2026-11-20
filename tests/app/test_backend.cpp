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
    CHECK_FALSE(result.options.vfxSpike);
    CHECK_FALSE(result.options.debugLightData);
    CHECK(result.options.debugDisplayMode == render::DebugDisplayMode::None);
    CHECK(result.options.debugLightClusterSlice == -1);
    CHECK_FALSE(result.options.disableLightCluster);
    CHECK_FALSE(result.options.gpuLightList.has_value());
    CHECK_FALSE(result.options.lightListFeatureFlags.has_value());
    CHECK_FALSE(result.options.lightListCapacity.has_value());
    CHECK_FALSE(result.options.cameraOrbit);
    CHECK_FALSE(result.options.debugWireframe);
    CHECK_FALSE(result.options.disableShadows);
    CHECK(result.options.debugUi);
    CHECK_FALSE(result.options.framesSpecified);
    CHECK_FALSE(result.options.litShaderMode.has_value());
    CHECK_FALSE(result.options.ssao.has_value());
    CHECK_FALSE(result.options.ssr.has_value());
    CHECK_FALSE(result.options.ssrTemporal.has_value());
    CHECK_FALSE(result.options.ssgi.has_value());
    CHECK_FALSE(result.options.ssgiTemporal.has_value());
    CHECK_FALSE(result.options.decals.has_value());
    CHECK_FALSE(result.options.reflectionProbes.has_value());
    CHECK_FALSE(result.options.vfx.has_value());
    CHECK_FALSE(result.options.vfxTest);
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

TEST_CASE("parseCommandLine parses the VFX spike switch") {
    const char* argv[] = {"hbg", "--vfx-spike"};
    const CommandLineResult result = parseCommandLine(2, argv);
    REQUIRE(result.ok);
    CHECK(result.options.vfxSpike);
}

TEST_CASE("parseCommandLine parses options") {
    const char* argv[] = {
        "hbg",          "--backend=Vulkan",     "--width=1920",        "--height=1080",      "--no-vsync",
        "--verbose",    "--bgfx-debug",         "--gfx-test",          "--scene=a.gltf",     "--render-test=b.gltf",
        "--frames=7",   "--screenshot=out.png", "--render-scale=0.5",  "--debug-light-data", "--debug-wireframe",
        "--no-shadows", "--sky=sky.hdr",        "--sky-intensity=2.5", "--sky-rotation=45", "--ssao-temporal"};
    const CommandLineResult result = parseCommandLine(20, argv);

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
    CHECK(result.options.ssaoTemporal.has_value());
    CHECK(*result.options.ssaoTemporal);
}

TEST_CASE("parseCommandLine parses contact shadow settings") {
    const char* argv[] = {"hbg",
                          "--contact-shadows",
                          "--contact-shadow-length=0.3",
                          "--contact-shadow-opacity=0.5",
                          "--contact-shadow-samples=16",
                          "--contact-shadow-half-res",
                          "--contact-shadow-directional-only"};
    const CommandLineResult result = parseCommandLine(7, argv);
    REQUIRE(result.ok);
    REQUIRE(result.options.shadowOverrides.contactShadows.has_value());
    CHECK(*result.options.shadowOverrides.contactShadows);
    REQUIRE(result.options.shadowOverrides.contactShadowLength.has_value());
    CHECK(*result.options.shadowOverrides.contactShadowLength == doctest::Approx(0.3f));
    REQUIRE(result.options.shadowOverrides.contactShadowOpacity.has_value());
    CHECK(*result.options.shadowOverrides.contactShadowOpacity == doctest::Approx(0.5f));
    REQUIRE(result.options.shadowOverrides.contactShadowSamples.has_value());
    CHECK(*result.options.shadowOverrides.contactShadowSamples == 16u);
    REQUIRE(result.options.shadowOverrides.contactShadowHalfResolution.has_value());
    CHECK(*result.options.shadowOverrides.contactShadowHalfResolution);
    REQUIRE(result.options.shadowOverrides.contactShadowDirectionalOnly.has_value());
    CHECK(*result.options.shadowOverrides.contactShadowDirectionalOnly);

    render::ShadowSettings settings;
    applyShadowOverrides(result.options.shadowOverrides, settings);
    render::sanitizeShadowSettings(settings);
    CHECK(settings.contactShadows.enabled);
    CHECK(settings.contactShadows.length == doctest::Approx(0.3f));
    CHECK(settings.contactShadows.opacity == doctest::Approx(0.5f));
    CHECK(settings.contactShadows.sampleCount == 16u);
    CHECK(settings.contactShadows.halfResolution);
    CHECK(settings.contactShadows.directionalOnly);

    const char* invalid[] = {"hbg", "--contact-shadow-samples=2"};
    CHECK_FALSE(parseCommandLine(2, invalid).ok);
    const char* disabled[] = {"hbg", "--contact-shadows", "--no-contact-shadows"};
    const CommandLineResult disabledResult = parseCommandLine(3, disabled);
    REQUIRE(disabledResult.ok);
    REQUIRE(disabledResult.options.shadowOverrides.contactShadows.has_value());
    CHECK_FALSE(*disabledResult.options.shadowOverrides.contactShadows);
}
TEST_CASE("parseCommandLine parses shadow settings") {
    const char* argv[] = {"hbg",
                          "--shadow-profile=config/shadows.json",
                          "--shadow-filter=pcf5x5",
                          "--shadow-distance=120",
                          "--shadow-cascades=2",
                          "--shadow-atlas-size=4096",
                          "--shadow-max-requests=48",
                          "--shadow-directional-resolution=2048",
                          "--shadow-point-resolution=512",
                          "--shadow-spot-resolution=1024",
                          "--shadow-normal-bias=0.35",
                          "--shadow-slope-bias=1.5",
                          "--shadow-near-plane=0.25",
                          "--shadow-softness=0.75",
                          "--shadow-blocker-samples=20",
                          "--shadow-filter-samples=32",
                          "--shadow-min-filter-size=0.05",
                          "--shadow-cascade-fade=0.2",
                          "--micro-shadows",
                          "--micro-shadow-opacity=0.6"};
    const CommandLineResult result = parseCommandLine(20, argv);
    REQUIRE(result.ok);
    CHECK(result.options.shadowSettingsPath == "config/shadows.json");
    REQUIRE(result.options.shadowOverrides.directionalFilter.has_value());
    CHECK(*result.options.shadowOverrides.directionalFilter == render::ShadowFilter::Pcf5x5);
    REQUIRE(result.options.shadowOverrides.punctualFilter.has_value());
    CHECK(*result.options.shadowOverrides.punctualFilter == render::ShadowFilter::Pcf5x5);
    REQUIRE(result.options.shadowOverrides.maxDistance.has_value());
    CHECK(*result.options.shadowOverrides.maxDistance == doctest::Approx(120.0f));
    REQUIRE(result.options.shadowOverrides.cascadeCount.has_value());
    CHECK(*result.options.shadowOverrides.cascadeCount == 2u);
    REQUIRE(result.options.shadowOverrides.atlasSize.has_value());
    CHECK(*result.options.shadowOverrides.atlasSize == 4096u);
    REQUIRE(result.options.shadowOverrides.maxRequests.has_value());
    CHECK(*result.options.shadowOverrides.maxRequests == 48u);
    REQUIRE(result.options.shadowOverrides.directionalResolution.has_value());
    CHECK(*result.options.shadowOverrides.directionalResolution == 2048u);
    REQUIRE(result.options.shadowOverrides.pointResolution.has_value());
    CHECK(*result.options.shadowOverrides.pointResolution == 512u);
    REQUIRE(result.options.shadowOverrides.spotResolution.has_value());
    CHECK(*result.options.shadowOverrides.spotResolution == 1024u);
    REQUIRE(result.options.shadowOverrides.normalBias.has_value());
    CHECK(*result.options.shadowOverrides.normalBias == doctest::Approx(0.35f));
    REQUIRE(result.options.shadowOverrides.slopeBias.has_value());
    CHECK(*result.options.shadowOverrides.slopeBias == doctest::Approx(1.5f));
    REQUIRE(result.options.shadowOverrides.shadowNearPlane.has_value());
    CHECK(*result.options.shadowOverrides.shadowNearPlane == doctest::Approx(0.25f));
    REQUIRE(result.options.shadowOverrides.softness.has_value());
    CHECK(*result.options.shadowOverrides.softness == doctest::Approx(0.75f));
    REQUIRE(result.options.shadowOverrides.blockerSamples.has_value());
    CHECK(*result.options.shadowOverrides.blockerSamples == 20u);
    REQUIRE(result.options.shadowOverrides.filterSamples.has_value());
    CHECK(*result.options.shadowOverrides.filterSamples == 32u);
    REQUIRE(result.options.shadowOverrides.minFilterSize.has_value());
    CHECK(*result.options.shadowOverrides.minFilterSize == doctest::Approx(0.05f));
    REQUIRE(result.options.shadowOverrides.cascadeFade.has_value());
    CHECK(*result.options.shadowOverrides.cascadeFade == doctest::Approx(0.2f));
    REQUIRE(result.options.shadowOverrides.microShadows.has_value());
    CHECK(*result.options.shadowOverrides.microShadows);
    REQUIRE(result.options.shadowOverrides.microShadowOpacity.has_value());
    CHECK(*result.options.shadowOverrides.microShadowOpacity == doctest::Approx(0.6f));

    render::ShadowSettings settings;
    applyShadowOverrides(result.options.shadowOverrides, settings);
    render::sanitizeShadowSettings(settings);
    CHECK(settings.maxDistance == doctest::Approx(120.0f));
    CHECK(settings.cascadeCount == 2u);
    CHECK(settings.atlasSize == 4096u);
    CHECK(settings.directionalFilter == render::ShadowFilter::Pcf5x5);
    CHECK(settings.slopeBias == doctest::Approx(1.5f));
    CHECK(settings.shadowNearPlane == doctest::Approx(0.25f));
    CHECK(settings.softness == doctest::Approx(0.75f));
    CHECK(settings.blockerSamples == 20u);
    CHECK(settings.filterSamples == 32u);
    CHECK(settings.minFilterSize == doctest::Approx(0.05f));
    CHECK(settings.cascadeFade == doctest::Approx(0.2f));
    CHECK(settings.microShadows);
    CHECK(settings.microShadowOpacity == doctest::Approx(0.6f));

    const char* invalidFilter[] = {"hbg", "--shadow-filter=bogus"};
    CHECK_FALSE(parseCommandLine(2, invalidFilter).ok);
    const char* invalidCascades[] = {"hbg", "--shadow-cascades=5"};
    CHECK_FALSE(parseCommandLine(2, invalidCascades).ok);
    const char* invalidAtlas[] = {"hbg", "--shadow-atlas-size=512"};
    CHECK_FALSE(parseCommandLine(2, invalidAtlas).ok);
    const char* invalidRequests[] = {"hbg", "--shadow-max-requests=129"};
    CHECK_FALSE(parseCommandLine(2, invalidRequests).ok);
    const char* validRequests[] = {"hbg", "--shadow-max-requests=128"};
    const CommandLineResult validRequestsResult = parseCommandLine(2, validRequests);
    CHECK(validRequestsResult.ok);
    REQUIRE(validRequestsResult.options.shadowOverrides.maxRequests.has_value());
    CHECK(*validRequestsResult.options.shadowOverrides.maxRequests == 128u);
    const char* invalidQuality[] = {"hbg", "--shadow-quality=bogus"};
    CHECK_FALSE(parseCommandLine(2, invalidQuality).ok);
    const char* invalidBlockers[] = {"hbg", "--shadow-blocker-samples=0"};
    CHECK_FALSE(parseCommandLine(2, invalidBlockers).ok);
    const char* invalidFilters[] = {"hbg", "--shadow-filter-samples=65"};
    CHECK_FALSE(parseCommandLine(2, invalidFilters).ok);
    const char* invalidMinFilter[] = {"hbg", "--shadow-min-filter-size=2"};
    CHECK_FALSE(parseCommandLine(2, invalidMinFilter).ok);
    const char* invalidMicro[] = {"hbg", "--micro-shadow-opacity=2"};
    CHECK_FALSE(parseCommandLine(2, invalidMicro).ok);
}

TEST_CASE("parseCommandLine parses per-light shadow defaults and cache") {
    const char* argv[] = {"hbg", "--directional-shadow-cache", "--shadow-cache", "--area-shadow-cache", "--shadow-dimmer=0.5",
                          "--shadow-fade-distance=50", "--shadow-tint=0.1,0.2,0.3"};
    const CommandLineResult result = parseCommandLine(7, argv);
    REQUIRE(result.ok);
    REQUIRE(result.options.shadowOverrides.cachedDirectionalShadows.has_value());
    CHECK(*result.options.shadowOverrides.cachedDirectionalShadows);
    REQUIRE(result.options.shadowOverrides.cachedPunctualShadows.has_value());
    CHECK(*result.options.shadowOverrides.cachedPunctualShadows);
    REQUIRE(result.options.shadowOverrides.cachedAreaShadows.has_value());
    CHECK(*result.options.shadowOverrides.cachedAreaShadows);
    REQUIRE(result.options.shadowOverrides.shadowDimmer.has_value());
    CHECK(*result.options.shadowOverrides.shadowDimmer == doctest::Approx(0.5f));
    REQUIRE(result.options.shadowOverrides.shadowFadeDistance.has_value());
    CHECK(*result.options.shadowOverrides.shadowFadeDistance == doctest::Approx(50.0f));
    REQUIRE(result.options.shadowOverrides.shadowTint.has_value());
    CHECK(*result.options.shadowOverrides.shadowTint == Vec3(0.1f, 0.2f, 0.3f));

    render::ShadowSettings settings;
    applyShadowOverrides(result.options.shadowOverrides, settings);
    render::sanitizeShadowSettings(settings);
    CHECK(settings.cachedDirectionalShadows);
    CHECK(settings.cachedPunctualShadows);
    CHECK(settings.cachedAreaShadows);
    CHECK(settings.shadowDimmer == doctest::Approx(0.5f));
    CHECK(settings.shadowFadeDistance == doctest::Approx(50.0f));
    CHECK(settings.shadowTint == Vec3(0.1f, 0.2f, 0.3f));

    const char* disabled[] = {"hbg", "--directional-shadow-cache", "--no-directional-shadow-cache", "--shadow-cache", "--no-shadow-cache", "--area-shadow-cache",
                              "--no-area-shadow-cache"};
    const CommandLineResult disabledResult = parseCommandLine(7, disabled);
    REQUIRE(disabledResult.ok);
    REQUIRE(disabledResult.options.shadowOverrides.cachedDirectionalShadows.has_value());
    CHECK_FALSE(*disabledResult.options.shadowOverrides.cachedDirectionalShadows);
    REQUIRE(disabledResult.options.shadowOverrides.cachedPunctualShadows.has_value());
    CHECK_FALSE(*disabledResult.options.shadowOverrides.cachedPunctualShadows);
    REQUIRE(disabledResult.options.shadowOverrides.cachedAreaShadows.has_value());
    CHECK_FALSE(*disabledResult.options.shadowOverrides.cachedAreaShadows);

    const char* invalidTint[] = {"hbg", "--shadow-tint=1,2"};
    CHECK_FALSE(parseCommandLine(2, invalidTint).ok);
    const char* invalidDimmer[] = {"hbg", "--shadow-dimmer=2"};
    CHECK_FALSE(parseCommandLine(2, invalidDimmer).ok);
}

TEST_CASE("parseCommandLine maps shadow quality presets") {
    const struct {
        const char* value;
        render::ShadowFilter directional;
        render::ShadowFilter punctual;
    } cases[] = {
        {"off", render::ShadowFilter::Off, render::ShadowFilter::Off},
        {"low", render::ShadowFilter::Pcf5x5, render::ShadowFilter::Pcf3x3},
        {"medium", render::ShadowFilter::Pcf7x7, render::ShadowFilter::Pcf5x5},
        {"high", render::ShadowFilter::Pcss, render::ShadowFilter::Pcss},
    };
    for (const auto& entry : cases) {
        const std::string arg = std::string("--shadow-quality=") + entry.value;
        const char* argv[] = {"hbg", arg.c_str()};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.shadowOverrides.directionalFilter.has_value());
        REQUIRE(result.options.shadowOverrides.punctualFilter.has_value());
        CHECK(*result.options.shadowOverrides.directionalFilter == entry.directional);
        CHECK(*result.options.shadowOverrides.punctualFilter == entry.punctual);
    }
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

    const char* lightmapMask[] = {"hbg", "--debug-lightmap-mask"};
    const auto lightmapMaskResult = parseCommandLine(2, lightmapMask);
    CHECK(lightmapMaskResult.ok);
    CHECK(lightmapMaskResult.options.debugLightmapMask);
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
    CHECK(parse("--debug-view=shadow-cascade").options.debugDisplayMode ==
          render::DebugDisplayMode::ShadowCascade);
    CHECK(parse("--debug-view=shadow-filter").options.debugDisplayMode ==
          render::DebugDisplayMode::ShadowFilter);
    CHECK(parse("--debug-view=lightmap").options.debugDisplayMode == render::DebugDisplayMode::Lightmap);
    CHECK(parse("--debug-view=baked-lightmap").options.debugDisplayMode ==
          render::DebugDisplayMode::Lightmap);
    CHECK(parse("--debug-view=albedo").options.debugDisplayMode == render::DebugDisplayMode::Albedo);
    CHECK(parse("--debug-view=roughness").options.debugDisplayMode == render::DebugDisplayMode::Roughness);
    CHECK(parse("--debug-view=metallic").options.debugDisplayMode == render::DebugDisplayMode::Metallic);
    CHECK(parse("--debug-view=metal").options.debugDisplayMode == render::DebugDisplayMode::Metallic);
    CHECK(parse("--debug-view=ao").options.debugDisplayMode == render::DebugDisplayMode::AmbientOcclusion);
    CHECK(parse("--debug-view=gbuffer-ao").options.debugDisplayMode ==
          render::DebugDisplayMode::AmbientOcclusion);
    CHECK(parse("--debug-view=emissive").options.debugDisplayMode == render::DebugDisplayMode::Emissive);
    CHECK_FALSE(parse("--debug-view=ao").options.ssao.has_value());
    CHECK(parse("--debug-view=ssao").options.debugDisplayMode == render::DebugDisplayMode::SSAO);
    REQUIRE(parse("--debug-view=ssao").options.ssao.has_value());
    CHECK(*parse("--debug-view=ssao").options.ssao);
    CHECK(parse("--debug-view=ambient-occlusion").options.debugDisplayMode == render::DebugDisplayMode::SSAO);
    CHECK(parse("--debug-view=ssr").options.debugDisplayMode == render::DebugDisplayMode::SSR);
    REQUIRE(parse("--debug-view=ssr").options.ssr.has_value());
    CHECK(*parse("--debug-view=ssr").options.ssr);
    CHECK(parse("--debug-view=ssgi").options.debugDisplayMode == render::DebugDisplayMode::SSGI);
    REQUIRE(parse("--debug-view=ssgi").options.ssgi.has_value());
    CHECK(*parse("--debug-view=ssgi").options.ssgi);
    CHECK(parse("--debug-view=global-illumination").options.debugDisplayMode ==
          render::DebugDisplayMode::SSGI);
    CHECK(parse("--debug-view=ssao-raw").options.debugDisplayMode == render::DebugDisplayMode::SSAORaw);
    REQUIRE(parse("--debug-view=ssao-raw").options.ssao.has_value());
    CHECK(*parse("--debug-view=ssao-raw").options.ssao);
    CHECK(parse("--debug-view=ssao-history").options.debugDisplayMode ==
          render::DebugDisplayMode::SSAOHistory);
    REQUIRE(parse("--debug-view=ssao-history").options.ssaoTemporal.has_value());
    CHECK(*parse("--debug-view=ssao-history").options.ssaoTemporal);
    CHECK(parse("--debug-view=ssr-raw").options.debugDisplayMode == render::DebugDisplayMode::SSRRaw);
    REQUIRE(parse("--debug-view=ssr-raw").options.ssr.has_value());
    CHECK(*parse("--debug-view=ssr-raw").options.ssr);
    CHECK(parse("--debug-view=ssr-history").options.debugDisplayMode ==
          render::DebugDisplayMode::SSRHistory);
    REQUIRE(parse("--debug-view=ssr-history").options.ssrTemporal.has_value());
    CHECK(*parse("--debug-view=ssr-history").options.ssrTemporal);
    CHECK(parse("--debug-view=ssgi-raw").options.debugDisplayMode == render::DebugDisplayMode::SSGIRaw);
    REQUIRE(parse("--debug-view=ssgi-raw").options.ssgi.has_value());
    CHECK(*parse("--debug-view=ssgi-raw").options.ssgi);
    CHECK(parse("--debug-view=ssgi-history").options.debugDisplayMode ==
          render::DebugDisplayMode::SSGIHistory);
    REQUIRE(parse("--debug-view=ssgi-history").options.ssgiTemporal.has_value());
    CHECK(*parse("--debug-view=ssgi-history").options.ssgiTemporal);
    CHECK(parse("--debug-view=ssao-denoised").options.debugDisplayMode == render::DebugDisplayMode::SSAO);
    CHECK(parse("--debug-view=ssr-denoised").options.debugDisplayMode == render::DebugDisplayMode::SSR);
    CHECK(parse("--debug-view=ssgi-denoised").options.debugDisplayMode == render::DebugDisplayMode::SSGI);
    CHECK(parse("--debug-view=decals").options.debugDisplayMode == render::DebugDisplayMode::Decals);
    REQUIRE(parse("--debug-view=decals").options.decals.has_value());
    CHECK(*parse("--debug-view=decals").options.decals);
    CHECK_FALSE(parse("--debug-view=clusterbogus").ok);
}

TEST_CASE("parseCommandLine parses light-cluster debug slice") {
    const char* fixed[] = {"hbg", "--debug-light-cluster-slice=12"};
    const CommandLineResult result = parseCommandLine(2, fixed);
    REQUIRE(result.ok);
    CHECK(result.options.debugLightClusterSlice == 12);

    const char* depth[] = {"hbg", "--debug-light-cluster-slice=-1"};
    CHECK(parseCommandLine(2, depth).options.debugLightClusterSlice == -1);
    const char* legacy[] = {"hbg", "--debug-light-cluster-slice=-2"};
    CHECK(parseCommandLine(2, legacy).options.debugLightClusterSlice == -2);

    const char* invalidLow[] = {"hbg", "--debug-light-cluster-slice=-3"};
    CHECK_FALSE(parseCommandLine(2, invalidLow).ok);
    const char* invalidHigh[] = {"hbg", "--debug-light-cluster-slice=32"};
    CHECK_FALSE(parseCommandLine(2, invalidHigh).ok);

    const char* disabled[] = {"hbg", "--no-light-cluster"};
    CHECK(parseCommandLine(2, disabled).options.disableLightCluster);
}

TEST_CASE("parseCommandLine parses GPU light-list switches") {
    const char* enabled[] = {"hbg", "--gpu-light-list"};
    const CommandLineResult enabledResult = parseCommandLine(2, enabled);
    REQUIRE(enabledResult.ok);
    REQUIRE(enabledResult.options.gpuLightList.has_value());
    CHECK(*enabledResult.options.gpuLightList);

    const char* disabled[] = {"hbg", "--no-gpu-light-list"};
    const CommandLineResult disabledResult = parseCommandLine(2, disabled);
    REQUIRE(disabledResult.ok);
    REQUIRE(disabledResult.options.gpuLightList.has_value());
    CHECK_FALSE(*disabledResult.options.gpuLightList);

    const char* lastWins[] = {"hbg", "--gpu-light-list", "--no-gpu-light-list"};
    const CommandLineResult lastWinsResult = parseCommandLine(3, lastWins);
    REQUIRE(lastWinsResult.ok);
    REQUIRE(lastWinsResult.options.gpuLightList.has_value());
    CHECK_FALSE(*lastWinsResult.options.gpuLightList);

    const char* noCluster[] = {"hbg", "--gpu-light-list", "--no-light-cluster"};
    const CommandLineResult noClusterResult = parseCommandLine(3, noCluster);
    REQUIRE(noClusterResult.ok);
    REQUIRE(noClusterResult.options.gpuLightList.has_value());
    CHECK(*noClusterResult.options.gpuLightList);
    CHECK(noClusterResult.options.disableLightCluster);
}

TEST_CASE("parseCommandLine parses light-list capacity") {
    const char* capacity32[] = {"hbg", "--light-capacity=32"};
    const CommandLineResult result32 = parseCommandLine(2, capacity32);
    REQUIRE(result32.ok);
    REQUIRE(result32.options.lightListCapacity.has_value());
    CHECK(*result32.options.lightListCapacity == 32u);

    const char* capacity64[] = {"hbg", "--light-capacity=64"};
    const CommandLineResult result64 = parseCommandLine(2, capacity64);
    REQUIRE(result64.ok);
    REQUIRE(result64.options.lightListCapacity.has_value());
    CHECK(*result64.options.lightListCapacity == 64u);

    const char* capacity128[] = {"hbg", "--light-capacity=128"};
    const CommandLineResult result128 = parseCommandLine(2, capacity128);
    REQUIRE(result128.ok);
    REQUIRE(result128.options.lightListCapacity.has_value());
    CHECK(*result128.options.lightListCapacity == 128u);

    const char* invalid[] = {"hbg", "--light-capacity=48"};
    CHECK_FALSE(parseCommandLine(2, invalid).ok);
}

TEST_CASE("parseCommandLine parses light-list feature flags") {
    const char* enabled[] = {"hbg", "--light-list-feature-flags"};
    const CommandLineResult enabledResult = parseCommandLine(2, enabled);
    REQUIRE(enabledResult.ok);
    REQUIRE(enabledResult.options.lightListFeatureFlags.has_value());
    CHECK(*enabledResult.options.lightListFeatureFlags);

    const char* disabled[] = {"hbg", "--no-light-list-feature-flags"};
    const CommandLineResult disabledResult = parseCommandLine(2, disabled);
    REQUIRE(disabledResult.ok);
    REQUIRE(disabledResult.options.lightListFeatureFlags.has_value());
    CHECK_FALSE(*disabledResult.options.lightListFeatureFlags);

    const char* lastWins[] = {"hbg", "--light-list-feature-flags", "--no-light-list-feature-flags"};
    const CommandLineResult lastWinsResult = parseCommandLine(3, lastWins);
    REQUIRE(lastWinsResult.ok);
    REQUIRE(lastWinsResult.options.lightListFeatureFlags.has_value());
    CHECK_FALSE(*lastWinsResult.options.lightListFeatureFlags);
}

TEST_CASE("parseCommandLine can disable the debug UI") {
    const char* argv[] = {"hbg", "--no-debug-ui"};
    const CommandLineResult result = parseCommandLine(2, argv);

    CHECK(result.ok);
    CHECK_FALSE(result.options.debugUi);
}

TEST_CASE("parseCommandLine parses lit shader mode") {
    {
        const char* argv[] = {"hbg", "--lit-shader-mode=deferred"};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.litShaderMode.has_value());
        CHECK(*result.options.litShaderMode == render::LitShaderMode::Deferred);
    }
    {
        const char* argv[] = {"hbg", "--deferred"};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.litShaderMode.has_value());
        CHECK(*result.options.litShaderMode == render::LitShaderMode::Deferred);
    }
    {
        const char* argv[] = {"hbg", "--lit-shader-mode=forward"};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.litShaderMode.has_value());
        CHECK(*result.options.litShaderMode == render::LitShaderMode::Forward);
    }
    {
        const char* argv[] = {"hbg", "--lit-shader-mode=bogus"};
        CHECK_FALSE(parseCommandLine(2, argv).ok);
    }
}

TEST_CASE("parseCommandLine parses SSAO switches") {
    {
        const char* argv[] = {"hbg", "--ssao"};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.ssao.has_value());
        CHECK(*result.options.ssao);
    }
    {
        const char* argv[] = {"hbg", "--no-ssao"};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.ssao.has_value());
        CHECK_FALSE(*result.options.ssao);
    }
}

TEST_CASE("parseCommandLine parses SSR switches") {
    {
        const char* argv[] = {"hbg", "--ssr"};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.ssr.has_value());
        CHECK(*result.options.ssr);
    }
    {
        const char* argv[] = {"hbg", "--no-ssr"};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.ssr.has_value());
        CHECK_FALSE(*result.options.ssr);
    }
}

TEST_CASE("parseCommandLine parses SSR temporal switches") {
    {
        const char* argv[] = {"hbg", "--ssr-temporal"};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.ssrTemporal.has_value());
        CHECK(*result.options.ssrTemporal);
    }
    {
        const char* argv[] = {"hbg", "--no-ssr-temporal"};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.ssrTemporal.has_value());
        CHECK_FALSE(*result.options.ssrTemporal);
    }
}

TEST_CASE("parseCommandLine parses SSGI switches") {
    {
        const char* argv[] = {"hbg", "--ssgi"};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.ssgi.has_value());
        CHECK(*result.options.ssgi);
    }
    {
        const char* argv[] = {"hbg", "--no-ssgi"};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.ssgi.has_value());
        CHECK_FALSE(*result.options.ssgi);
    }
    {
        const char* argv[] = {"hbg", "--ssgi-temporal"};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.ssgiTemporal.has_value());
        CHECK(*result.options.ssgiTemporal);
    }
    {
        const char* argv[] = {"hbg", "--no-ssgi-temporal"};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.ssgiTemporal.has_value());
        CHECK_FALSE(*result.options.ssgiTemporal);
    }
}

TEST_CASE("parseCommandLine parses decal switches") {
    {
        const char* argv[] = {"hbg", "--decals"};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.decals.has_value());
        CHECK(*result.options.decals);
    }
    {
        const char* argv[] = {"hbg", "--no-decals"};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.decals.has_value());
        CHECK_FALSE(*result.options.decals);
    }
}

TEST_CASE("parseCommandLine parses reflection probe switches") {
    {
        const char* argv[] = {"hbg", "--reflection-probes"};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.reflectionProbes.has_value());
        CHECK(*result.options.reflectionProbes);
    }
    {
        const char* argv[] = {"hbg", "--no-reflection-probes"};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.reflectionProbes.has_value());
        CHECK_FALSE(*result.options.reflectionProbes);
    }
}

TEST_CASE("parseCommandLine parses M3 post-process profile and effect switches") {
    const char* argv[] = {"hbg",
                          "--postprocess-profile=config/postprocess.json",
                          "--aa=taa",
                          "--exposure-mode=auto",
                          "--exposure-ev100=-2.0",
                          "--bloom",
                          "--no-dof",
                          "--motion-blur"};
    const CommandLineResult result = parseCommandLine(8, argv);

    REQUIRE(result.ok);
    CHECK(result.options.postProcessProfilePath == "config/postprocess.json");
    REQUIRE(result.options.postProcessOverrides.taa.has_value());
    CHECK(*result.options.postProcessOverrides.taa);
    REQUIRE(result.options.postProcessOverrides.fxaa.has_value());
    CHECK_FALSE(*result.options.postProcessOverrides.fxaa);
    REQUIRE(result.options.postProcessOverrides.exposureMode.has_value());
    CHECK(*result.options.postProcessOverrides.exposureMode == render::ExposureMode::Automatic);
    REQUIRE(result.options.postProcessOverrides.fixedEv100.has_value());
    CHECK(*result.options.postProcessOverrides.fixedEv100 == doctest::Approx(-2.0f));
    REQUIRE(result.options.postProcessOverrides.bloom.has_value());
    CHECK(*result.options.postProcessOverrides.bloom);
    REQUIRE(result.options.postProcessOverrides.depthOfField.has_value());
    CHECK_FALSE(*result.options.postProcessOverrides.depthOfField);
    REQUIRE(result.options.postProcessOverrides.motionBlur.has_value());
    CHECK(*result.options.postProcessOverrides.motionBlur);
}

TEST_CASE("parseCommandLine parses FXAA and prefers it for the FXAA debug view") {
    {
        const char* argv[] = {"hbg", "--aa=fxaa"};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.postProcessOverrides.fxaa.has_value());
        CHECK(*result.options.postProcessOverrides.fxaa);
        REQUIRE(result.options.postProcessOverrides.taa.has_value());
        CHECK_FALSE(*result.options.postProcessOverrides.taa);
        CHECK(result.options.postProcess.fxaa.enabled);
        CHECK_FALSE(result.options.postProcess.taa.enabled);
    }
    {
        const char* argv[] = {"hbg", "--debug-view=fxaa"};
        const CommandLineResult result = parseCommandLine(2, argv);
        REQUIRE(result.ok);
        CHECK(result.options.debugDisplayMode == render::DebugDisplayMode::Fxaa);
        REQUIRE(result.options.postProcessOverrides.fxaa.has_value());
        CHECK(*result.options.postProcessOverrides.fxaa);
        CHECK(result.options.postProcess.fxaa.enabled);
        CHECK_FALSE(result.options.postProcess.taa.enabled);
    }
    {
        const char* argv[] = {"hbg", "--aa=taa", "--fxaa"};
        const CommandLineResult result = parseCommandLine(3, argv);
        REQUIRE(result.ok);
        REQUIRE(result.options.postProcessOverrides.fxaa.has_value());
        CHECK(*result.options.postProcessOverrides.fxaa);
        REQUIRE(result.options.postProcessOverrides.taa.has_value());
        CHECK_FALSE(*result.options.postProcessOverrides.taa);
        CHECK(result.options.postProcess.fxaa.enabled);
        CHECK_FALSE(result.options.postProcess.taa.enabled);
    }
}

TEST_CASE("parseCommandLine parses histogram exposure and exposure debug view") {
    const char* argv[] = {"hbg", "--exposure-mode=histogram", "--debug-view=exposure"};
    const CommandLineResult result = parseCommandLine(3, argv);

    REQUIRE(result.ok);
    REQUIRE(result.options.postProcessOverrides.exposureMode.has_value());
    CHECK(*result.options.postProcessOverrides.exposureMode == render::ExposureMode::Histogram);
    CHECK(result.options.debugDisplayMode == render::DebugDisplayMode::Exposure);
}

TEST_CASE("post-process overrides apply after a profile") {
    render::PostProcessSettings settings;
    settings.bloom.enabled = true;
    settings.taa.enabled = true;
    settings.exposureMode = render::ExposureMode::Automatic;

    PostProcessOverrides overrides;
    overrides.bloom = false;
    overrides.taa = false;
    overrides.fxaa = true;
    overrides.exposureMode = render::ExposureMode::Fixed;
    overrides.motionBlur = true;
    overrides.motionBlurIntensity = 0.75f;
    overrides.motionBlurMaxVelocity = 120.0f;
    overrides.motionBlurMinVelocity = 3.0f;
    overrides.motionBlurCamera = false;
    overrides.motionBlurSamples = 16u;
    overrides.motionBlurDepthExtent = 2.0f;
    applyPostProcessOverrides(overrides, settings);

    CHECK_FALSE(settings.bloom.enabled);
    CHECK_FALSE(settings.taa.enabled);
    CHECK(settings.fxaa.enabled);
    CHECK(settings.exposureMode == render::ExposureMode::Fixed);
    CHECK(settings.motionBlur.enabled);
    CHECK(settings.motionBlur.intensity == doctest::Approx(0.75f));
    CHECK(settings.motionBlur.maximumVelocity == doctest::Approx(120.0f));
    CHECK(settings.motionBlur.minimumVelocity == doctest::Approx(3.0f));
    CHECK_FALSE(settings.motionBlur.cameraMotionBlur);
    CHECK(settings.motionBlur.sampleCount == 16u);
    CHECK(settings.motionBlur.depthComparisonExtent == doctest::Approx(2.0f));
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

TEST_CASE("parseCommandLine parses color grading and LUT debug view") {
    const char* argv[] = {"hbg", "--color-grading", "--debug-view=color-grading"};
    const CommandLineResult result = parseCommandLine(3, argv);

    REQUIRE(result.ok);
    REQUIRE(result.options.postProcessOverrides.colorGrading.has_value());
    CHECK(*result.options.postProcessOverrides.colorGrading);
    CHECK(result.options.postProcess.colorGrading.enabled);
    CHECK(result.options.debugDisplayMode == render::DebugDisplayMode::ColorGrading);

    const char* disabled[] = {"hbg", "--no-color-grading"};
    const CommandLineResult disabledResult = parseCommandLine(2, disabled);
    REQUIRE(disabledResult.ok);
    REQUIRE(disabledResult.options.postProcessOverrides.colorGrading.has_value());
    CHECK_FALSE(*disabledResult.options.postProcessOverrides.colorGrading);
}

TEST_CASE("parseCommandLine parses bloom settings and debug view") {
    const char* argv[] = {"hbg", "--bloom", "--bloom-threshold=0.8", "--bloom-intensity=0.5",
                          "--bloom-scatter=0.6", "--bloom-resolution=quarter",
                          "--bloom-high-quality-prefilter", "--no-bloom-high-quality-filter",
                          "--debug-view=bloom"};
    const CommandLineResult result = parseCommandLine(9, argv);

    REQUIRE(result.ok);
    REQUIRE(result.options.postProcessOverrides.bloom.has_value());
    CHECK(*result.options.postProcessOverrides.bloom);
    REQUIRE(result.options.postProcessOverrides.bloomThreshold.has_value());
    CHECK(*result.options.postProcessOverrides.bloomThreshold == doctest::Approx(0.8f));
    REQUIRE(result.options.postProcessOverrides.bloomIntensity.has_value());
    CHECK(*result.options.postProcessOverrides.bloomIntensity == doctest::Approx(0.5f));
    REQUIRE(result.options.postProcessOverrides.bloomScatter.has_value());
    CHECK(*result.options.postProcessOverrides.bloomScatter == doctest::Approx(0.6f));
    REQUIRE(result.options.postProcessOverrides.bloomResolution.has_value());
    CHECK(*result.options.postProcessOverrides.bloomResolution == 4u);
    REQUIRE(result.options.postProcessOverrides.bloomHighQualityPrefiltering.has_value());
    CHECK(*result.options.postProcessOverrides.bloomHighQualityPrefiltering);
    REQUIRE(result.options.postProcessOverrides.bloomHighQualityFiltering.has_value());
    CHECK_FALSE(*result.options.postProcessOverrides.bloomHighQualityFiltering);
    CHECK(result.options.debugDisplayMode == render::DebugDisplayMode::Bloom);

    const char* invalid[] = {"hbg", "--bloom-resolution=bogus"};
    CHECK_FALSE(parseCommandLine(2, invalid).ok);
}

TEST_CASE("parseCommandLine parses motion blur settings") {
    const char* argv[] = {"hbg", "--motion-blur", "--motion-blur-intensity=0.8",
                          "--motion-blur-max-velocity=120", "--motion-blur-min-velocity=3",
                          "--no-motion-blur-camera", "--motion-blur-samples=16",
                          "--motion-blur-depth-extent=2.5"};
    const CommandLineResult result = parseCommandLine(8, argv);
    REQUIRE(result.ok);
    REQUIRE(result.options.postProcessOverrides.motionBlur.has_value());
    CHECK(*result.options.postProcessOverrides.motionBlur);
    REQUIRE(result.options.postProcessOverrides.motionBlurIntensity.has_value());
    CHECK(*result.options.postProcessOverrides.motionBlurIntensity == doctest::Approx(0.8f));
    REQUIRE(result.options.postProcessOverrides.motionBlurMaxVelocity.has_value());
    CHECK(*result.options.postProcessOverrides.motionBlurMaxVelocity == doctest::Approx(120.0f));
    REQUIRE(result.options.postProcessOverrides.motionBlurMinVelocity.has_value());
    CHECK(*result.options.postProcessOverrides.motionBlurMinVelocity == doctest::Approx(3.0f));
    REQUIRE(result.options.postProcessOverrides.motionBlurCamera.has_value());
    CHECK_FALSE(*result.options.postProcessOverrides.motionBlurCamera);
    REQUIRE(result.options.postProcessOverrides.motionBlurSamples.has_value());
    CHECK(*result.options.postProcessOverrides.motionBlurSamples == 16u);
    REQUIRE(result.options.postProcessOverrides.motionBlurDepthExtent.has_value());
    CHECK(*result.options.postProcessOverrides.motionBlurDepthExtent == doctest::Approx(2.5f));

    const char* invalid[] = {"hbg", "--motion-blur-samples=1"};
    CHECK_FALSE(parseCommandLine(2, invalid).ok);
    const char* invalidVelocity[] = {"hbg", "--motion-blur-max-velocity=2000"};
    CHECK_FALSE(parseCommandLine(2, invalidVelocity).ok);
}

TEST_CASE("parseCommandLine parses DOF ranges and debug view") {
    const char* argv[] = {"hbg", "--dof", "--dof-near-start=1.5", "--dof-near-end=3.0",
                          "--dof-far-start=8.0", "--dof-far-end=16.0", "--dof-near-max-blur=2.5",
                          "--dof-far-max-blur=9.0", "--dof-resolution=quarter", "--dof-high-quality",
                          "--debug-view=dof"};
    const CommandLineResult result = parseCommandLine(11, argv);
    REQUIRE(result.ok);
    REQUIRE(result.options.postProcessOverrides.depthOfField.has_value());
    CHECK(*result.options.postProcessOverrides.depthOfField);
    REQUIRE(result.options.postProcessOverrides.dofNearFocusStart.has_value());
    CHECK(*result.options.postProcessOverrides.dofNearFocusStart == doctest::Approx(1.5f));
    REQUIRE(result.options.postProcessOverrides.dofNearFocusEnd.has_value());
    CHECK(*result.options.postProcessOverrides.dofNearFocusEnd == doctest::Approx(3.0f));
    REQUIRE(result.options.postProcessOverrides.dofFarFocusStart.has_value());
    CHECK(*result.options.postProcessOverrides.dofFarFocusStart == doctest::Approx(8.0f));
    REQUIRE(result.options.postProcessOverrides.dofFarFocusEnd.has_value());
    CHECK(*result.options.postProcessOverrides.dofFarFocusEnd == doctest::Approx(16.0f));
    REQUIRE(result.options.postProcessOverrides.dofNearMaxBlur.has_value());
    CHECK(*result.options.postProcessOverrides.dofNearMaxBlur == doctest::Approx(2.5f));
    REQUIRE(result.options.postProcessOverrides.dofFarMaxBlur.has_value());
    CHECK(*result.options.postProcessOverrides.dofFarMaxBlur == doctest::Approx(9.0f));
    REQUIRE(result.options.postProcessOverrides.dofResolution.has_value());
    CHECK(*result.options.postProcessOverrides.dofResolution == 4u);
    REQUIRE(result.options.postProcessOverrides.dofHighQuality.has_value());
    CHECK(*result.options.postProcessOverrides.dofHighQuality);
    CHECK(result.options.debugDisplayMode == render::DebugDisplayMode::Dof);

    const char* invalid[] = {"hbg", "--dof-resolution=bogus"};
    CHECK_FALSE(parseCommandLine(2, invalid).ok);
}

TEST_CASE("parseCommandLine parses the TAA history debug view") {
    const char* argv[] = {"hbg", "--debug-view=taa-history", "--taa"};
    const CommandLineResult result = parseCommandLine(3, argv);
    REQUIRE(result.ok);
    CHECK(result.options.debugDisplayMode == render::DebugDisplayMode::TaaHistory);
    REQUIRE(result.options.postProcessOverrides.taa.has_value());
    CHECK(*result.options.postProcessOverrides.taa);
}

TEST_CASE("parseCommandLine parses TAA quality and sharpen overrides") {
    const char* argv[] = {"hbg", "--taa", "--taa-quality=4", "--taa-motion-rejection=0.25",
                          "--taa-sharpening=0.5", "--taa-jitter-scale=0.75"};
    const CommandLineResult result = parseCommandLine(6, argv);
    REQUIRE(result.ok);
    REQUIRE(result.options.postProcessOverrides.taaQuality.has_value());
    CHECK(*result.options.postProcessOverrides.taaQuality == 4u);
    REQUIRE(result.options.postProcessOverrides.taaMotionRejection.has_value());
    CHECK(*result.options.postProcessOverrides.taaMotionRejection == doctest::Approx(0.25f));
    REQUIRE(result.options.postProcessOverrides.taaSharpening.has_value());
    CHECK(*result.options.postProcessOverrides.taaSharpening == doctest::Approx(0.5f));
    REQUIRE(result.options.postProcessOverrides.taaJitterScale.has_value());
    CHECK(*result.options.postProcessOverrides.taaJitterScale == doctest::Approx(0.75f));

    const char* invalid[] = {"hbg", "--taa-quality=9"};
    CHECK_FALSE(parseCommandLine(2, invalid).ok);
}

TEST_CASE("parseCommandLine parses volumetric fog options") {
    const char* argv[] = {"hbg",
                          "--volumetric-fog",
                          "--fog-profile=config/volumetric_fog.json",
                          "--fog-mean-free-path=120",
                          "--fog-base-height=2",
                          "--fog-maximum-height=40",
                          "--fog-anisotropy=0.75",
                          "--fog-depth-extent=48",
                          "--fog-steps=24",
                          "--no-fog-shadows"};
    const CommandLineResult result = parseCommandLine(10, argv);
    REQUIRE(result.ok);
    REQUIRE(result.options.volumetricFogOverrides.enabled.has_value());
    CHECK(*result.options.volumetricFogOverrides.enabled);
    CHECK(result.options.volumetricFogProfilePath == "config/volumetric_fog.json");
    REQUIRE(result.options.volumetricFogOverrides.meanFreePath.has_value());
    CHECK(*result.options.volumetricFogOverrides.meanFreePath == doctest::Approx(120.0f));
    REQUIRE(result.options.volumetricFogOverrides.baseHeight.has_value());
    CHECK(*result.options.volumetricFogOverrides.baseHeight == doctest::Approx(2.0f));
    REQUIRE(result.options.volumetricFogOverrides.maximumHeight.has_value());
    CHECK(*result.options.volumetricFogOverrides.maximumHeight == doctest::Approx(40.0f));
    REQUIRE(result.options.volumetricFogOverrides.anisotropy.has_value());
    CHECK(*result.options.volumetricFogOverrides.anisotropy == doctest::Approx(0.75f));
    REQUIRE(result.options.volumetricFogOverrides.depthExtent.has_value());
    CHECK(*result.options.volumetricFogOverrides.depthExtent == doctest::Approx(48.0f));
    REQUIRE(result.options.volumetricFogOverrides.stepCount.has_value());
    CHECK(*result.options.volumetricFogOverrides.stepCount == 24u);
    REQUIRE(result.options.volumetricFogOverrides.directionalShadows.has_value());
    CHECK_FALSE(*result.options.volumetricFogOverrides.directionalShadows);

    const char* disabled[] = {"hbg", "--no-volumetric-fog"};
    const CommandLineResult disabledResult = parseCommandLine(2, disabled);
    REQUIRE(disabledResult.ok);
    REQUIRE(disabledResult.options.volumetricFogOverrides.enabled.has_value());
    CHECK_FALSE(*disabledResult.options.volumetricFogOverrides.enabled);

    const char* invalidSteps[] = {"hbg", "--fog-steps=0"};
    CHECK_FALSE(parseCommandLine(2, invalidSteps).ok);
    const char* tooFewSteps[] = {"hbg", "--fog-steps=3"};
    CHECK_FALSE(parseCommandLine(2, tooFewSteps).ok);
    const char* invalidAnisotropy[] = {"hbg", "--fog-anisotropy=2"};
    CHECK_FALSE(parseCommandLine(2, invalidAnisotropy).ok);
}

TEST_CASE("parseCommandLine parses low-resolution temporal fog options") {
    const char* argv[] = {"hbg",
                          "--fog-resolution-scale=0.25",
                          "--fog-temporal",
                          "--fog-temporal-weight=0.5",
                          "--no-fog-jitter",
                          "--no-fog-punctual-lights",
                          "--fog-punctual-shadows",
                          "--fog-max-punctual-lights=4",
                          "--no-fog-local-volumes"};
    const CommandLineResult result = parseCommandLine(9, argv);
    REQUIRE(result.ok);
    REQUIRE(result.options.volumetricFogOverrides.resolutionScale.has_value());
    CHECK(*result.options.volumetricFogOverrides.resolutionScale == doctest::Approx(0.25f));
    REQUIRE(result.options.volumetricFogOverrides.temporal.has_value());
    CHECK(*result.options.volumetricFogOverrides.temporal);
    REQUIRE(result.options.volumetricFogOverrides.temporalHistoryWeight.has_value());
    CHECK(*result.options.volumetricFogOverrides.temporalHistoryWeight == doctest::Approx(0.5f));
    REQUIRE(result.options.volumetricFogOverrides.temporalJitter.has_value());
    CHECK_FALSE(*result.options.volumetricFogOverrides.temporalJitter);
    REQUIRE(result.options.volumetricFogOverrides.punctualLights.has_value());
    CHECK_FALSE(*result.options.volumetricFogOverrides.punctualLights);
    REQUIRE(result.options.volumetricFogOverrides.punctualShadows.has_value());
    CHECK(*result.options.volumetricFogOverrides.punctualShadows);
    REQUIRE(result.options.volumetricFogOverrides.maxPunctualLights.has_value());
    CHECK(*result.options.volumetricFogOverrides.maxPunctualLights == 4u);
    REQUIRE(result.options.volumetricFogOverrides.localVolumes.has_value());
    CHECK_FALSE(*result.options.volumetricFogOverrides.localVolumes);

    const char* invalidScale[] = {"hbg", "--fog-scale=0.1"};
    CHECK_FALSE(parseCommandLine(2, invalidScale).ok);
    const char* invalidWeight[] = {"hbg", "--fog-temporal-weight=1"};
    CHECK_FALSE(parseCommandLine(2, invalidWeight).ok);
    const char* invalidLights[] = {"hbg", "--fog-max-punctual-lights=20"};
    CHECK_FALSE(parseCommandLine(2, invalidLights).ok);
}

TEST_CASE("parseCommandLine parses VFX runtime switches") {
    const char* enabledArgv[] = {"hbg", "--vfx-test"};
    const CommandLineResult enabled = parseCommandLine(2, enabledArgv);
    REQUIRE(enabled.ok);
    REQUIRE(enabled.options.vfx.has_value());
    CHECK(*enabled.options.vfx);
    CHECK(enabled.options.vfxTest);

    const char* disabledArgv[] = {"hbg", "--vfx", "--no-vfx"};
    const CommandLineResult disabled = parseCommandLine(3, disabledArgv);
    REQUIRE(disabled.ok);
    REQUIRE(disabled.options.vfx.has_value());
    CHECK_FALSE(*disabled.options.vfx);
}
