#include "render/pipeline/frame_settings.h"
#include "render/lighting/gpu_light_data.h"
#include "render/pipeline/pipeline_settings.h"

#include <doctest/doctest.h>

#include <array>
#include <string>

using namespace hbg;
using namespace hbg::render;

TEST_CASE("FrameSettings field bits match HDRP") {
    CHECK(static_cast<int>(FrameSettingsField::LitShaderMode) == 0);
    CHECK(static_cast<int>(FrameSettingsField::MSAAMode) == 4);
    CHECK(static_cast<int>(FrameSettingsField::OpaqueObjects) == 2);
    CHECK(static_cast<int>(FrameSettingsField::TransparentObjects) == 3);
    CHECK(static_cast<int>(FrameSettingsField::BigTilePrepass) == 121);
    CHECK(static_cast<int>(FrameSettingsField::AdaptiveProbeVolume) == 127);
    CHECK(static_cast<int>(FrameSettingsField::None) == -1);

    const auto fields = frameSettingsFieldInfos();
    CHECK(fields.size() == 96);
    std::array<bool, BitSet128::kBitCount> seen{};
    for (const FrameSettingsFieldInfo& info : fields) {
        const int index = static_cast<int>(info.field);
        REQUIRE(index >= 0);
        REQUIRE(index < static_cast<int>(BitSet128::kBitCount));
        CHECK_FALSE(seen[static_cast<std::size_t>(index)]);
        seen[static_cast<std::size_t>(index)] = true;
        CHECK(frameSettingsFieldName(info.field) == std::string(info.name));
        FrameSettingsField parsed = FrameSettingsField::None;
        CHECK(parseFrameSettingsField(info.name, parsed));
        CHECK(parsed == info.field);
    }
}

TEST_CASE("BitSet128 supports the operations used by FrameSettings") {
    BitSet128 bits;
    CHECK(bits.none());
    bits.set(0);
    bits.set(127);
    CHECK(bits.test(0));
    CHECK(bits.test(127));
    CHECK_FALSE(bits.test(64));
    CHECK(bits.count() == 2);

    const BitSet128 other = BitSet128(0x2, 0x8000000000000000ull);
    CHECK((bits & other).test(127));
    CHECK((bits | other).test(1));
    CHECK((~BitSet128{}).count() == 128);
    bits.reset(0);
    CHECK_FALSE(bits.test(0));
}

TEST_CASE("pipeline defaults enable the validated GPU light-list buffer path") {
    const PipelineSettings pipeline = PipelineSettings::defaults();
    CHECK(pipeline.supportGpuLightList);
    CHECK(pipeline.supportGpuLightListBuffers);
    CHECK(pipeline.supportLightListFeatureFlags);
    CHECK(pipeline.lightListCapacity == kLightListDefaultCapacity);
    CHECK_FALSE(pipeline.supportGpuLightListIndirect);
    CHECK(pipeline.supportGpuSkinning);
    CHECK(pipeline.supportDepthPyramidCompute);
    CHECK_FALSE(pipeline.supportGpuDrivenGeometry);
    CHECK_FALSE(pipeline.supportGpuShadowCulling);
    CHECK_FALSE(pipeline.supportGpuShadowIndirect);
    CHECK_FALSE(pipeline.supportAsyncGpuLightList);
}

TEST_CASE("FrameSettings defaults follow HDRP render types") {
    const FrameSettings camera = FrameSettings::defaults(FrameSettingsRenderType::Camera);
    CHECK(camera.isEnabled(FrameSettingsField::ShadowMaps));
    CHECK(camera.isEnabled(FrameSettingsField::OpaqueObjects));
    CHECK(camera.isEnabled(FrameSettingsField::TransparentObjects));
    CHECK(camera.isEnabled(FrameSettingsField::Postprocess));
    CHECK(camera.isEnabled(FrameSettingsField::MotionVectors));
    CHECK(camera.isEnabled(FrameSettingsField::SSAO));
    CHECK(camera.isEnabled(FrameSettingsField::SSR));
    CHECK(camera.isEnabled(FrameSettingsField::SSGI));
    CHECK(camera.litShaderMode() == LitShaderMode::Deferred);
    CHECK(camera.msaaMode == MSAAMode::None);
    CHECK(camera.lodBias == 1.0f);

    const FrameSettings realtime = FrameSettings::defaults(FrameSettingsRenderType::RealtimeReflection);
    CHECK(realtime.isEnabled(FrameSettingsField::ShadowMaps));
    CHECK_FALSE(realtime.isEnabled(FrameSettingsField::MotionVectors));
    CHECK(realtime.isEnabled(FrameSettingsField::OpaqueObjects));

    const FrameSettings baked = FrameSettings::defaults(FrameSettingsRenderType::CustomOrBakedReflection);
    CHECK(baked.isEnabled(FrameSettingsField::ShadowMaps));
    CHECK(baked.isEnabled(FrameSettingsField::OpaqueObjects));
}

TEST_CASE("FrameSettings override mask copies only requested fields") {
    FrameSettings base = FrameSettings::defaults(FrameSettingsRenderType::Camera);
    FrameSettings overriding = base;
    overriding.setEnabled(FrameSettingsField::SSR, false);
    overriding.lodBias = 2.5f;

    FrameSettingsOverrideMask mask;
    mask.setOverridden(FrameSettingsField::SSR);
    mask.setOverridden(FrameSettingsField::LODBias);
    base.overrideWith(overriding, mask);

    CHECK_FALSE(base.isEnabled(FrameSettingsField::SSR));
    CHECK(base.lodBias == 2.5f);
    CHECK(base.isEnabled(FrameSettingsField::SSAO));
}

TEST_CASE("FrameSettings sanitize gates unsupported pipeline features") {
    PipelineSettings pipeline = PipelineSettings::defaults();
    pipeline.supportedLitShaderMode = SupportedLitShaderMode::ForwardOnly;
    pipeline.supportShadows = false;
    pipeline.supportSSR = false;
    pipeline.supportSSAO = false;
    pipeline.supportSSGI = false;
    pipeline.supportMotionVectors = false;

    FrameSettings settings = FrameSettings::defaults(FrameSettingsRenderType::Camera);
    settings.setLitShaderMode(LitShaderMode::Deferred);
    settings.setEnabled(FrameSettingsField::SSR, true);
    settings.setEnabled(FrameSettingsField::SSAO, true);
    settings.setEnabled(FrameSettingsField::SSGI, true);
    settings.setEnabled(FrameSettingsField::MotionVectors, true);
    settings.sanitize(pipeline);

    CHECK(settings.litShaderMode() == LitShaderMode::Forward);
    CHECK_FALSE(settings.isEnabled(FrameSettingsField::ShadowMaps));
    CHECK_FALSE(settings.isEnabled(FrameSettingsField::SSR));
    CHECK_FALSE(settings.isEnabled(FrameSettingsField::SSAO));
    CHECK_FALSE(settings.isEnabled(FrameSettingsField::SSGI));
    CHECK_FALSE(settings.isEnabled(FrameSettingsField::MotionVectors));
    CHECK_FALSE(settings.isEnabled(FrameSettingsField::ClearGBuffers));
    CHECK(settings.isEnabled(FrameSettingsField::OpaqueObjects));
    CHECK(settings.isEnabled(FrameSettingsField::Postprocess));
}

TEST_CASE("FrameSettings sanitize applies reflection and MSAA rules") {
    PipelineSettings pipeline = PipelineSettings::defaults();
    pipeline.supportedLitShaderMode = SupportedLitShaderMode::Both;
    pipeline.supportSSR = true;
    pipeline.supportSSAO = true;
    pipeline.supportScreenSpaceShadows = true;
    pipeline.msaaSampleCount = MSAASamples::MSAA4X;

    FrameSettings reflection = FrameSettings::defaults(FrameSettingsRenderType::Camera);
    reflection.setLitShaderMode(LitShaderMode::Forward);
    reflection.sanitize(pipeline, true, false, false, true);
    CHECK_FALSE(reflection.isEnabled(FrameSettingsField::Postprocess));
    CHECK_FALSE(reflection.isEnabled(FrameSettingsField::SSR));
    CHECK_FALSE(reflection.isEnabled(FrameSettingsField::SSAO));
    CHECK_FALSE(reflection.isEnabled(FrameSettingsField::ExposureControl));
    CHECK(reflection.isEnabled(FrameSettingsField::ShadowMaps));

    FrameSettings planarReflection = FrameSettings::defaults(FrameSettingsRenderType::Camera);
    planarReflection.setLitShaderMode(LitShaderMode::Forward);
    planarReflection.sanitize(pipeline, true, true, false, true);
    CHECK(planarReflection.isEnabled(FrameSettingsField::SSR));
    CHECK(planarReflection.isEnabled(FrameSettingsField::SSAO));
    CHECK(planarReflection.isEnabled(FrameSettingsField::ExposureControl));
    CHECK_FALSE(planarReflection.isEnabled(FrameSettingsField::Postprocess));

    FrameSettings msaa = FrameSettings::defaults(FrameSettingsRenderType::Camera);
    msaa.setLitShaderMode(LitShaderMode::Forward);
    msaa.msaaMode = MSAAMode::FromHDRPAsset;
    msaa.setEnabled(FrameSettingsField::ScreenSpaceShadows, true);
    msaa.sanitize(pipeline);
    CHECK(msaa.resolveMsaaSamples(pipeline) == MSAASamples::MSAA4X);
    CHECK_FALSE(msaa.isEnabled(FrameSettingsField::ScreenSpaceShadows));
    CHECK_FALSE(msaa.isEnabled(FrameSettingsField::FPTLForForwardOpaque));

    msaa.setLitShaderMode(LitShaderMode::Deferred);
    msaa.sanitize(pipeline);
    CHECK(msaa.msaaMode == MSAAMode::None);
}

TEST_CASE("FrameSettings keeps deferred when the pipeline supports both modes") {
    PipelineSettings pipeline = PipelineSettings::defaults();
    pipeline.supportedLitShaderMode = SupportedLitShaderMode::Both;
    pipeline.supportSSAO = true;
    pipeline.supportSSR = true;

    FrameSettings settings = FrameSettings::defaults(FrameSettingsRenderType::Camera);
    settings.setLitShaderMode(LitShaderMode::Deferred);
    settings.sanitize(pipeline);

    CHECK(settings.litShaderMode() == LitShaderMode::Deferred);
    CHECK(settings.isEnabled(FrameSettingsField::SSAO));
    CHECK(settings.isEnabled(FrameSettingsField::SSR));
}

TEST_CASE("Pipeline light-list capacity uses supported tiers") {
    CHECK(sanitizeLightListCapacity(0) == 32);
    CHECK(sanitizeLightListCapacity(31) == 32);
    CHECK(sanitizeLightListCapacity(32) == 32);
    CHECK(sanitizeLightListCapacity(33) == 32);
    CHECK(sanitizeLightListCapacity(63) == 32);
    CHECK(sanitizeLightListCapacity(64) == 64);
    CHECK(sanitizeLightListCapacity(65) == 64);
    CHECK(sanitizeLightListCapacity(127) == 64);
    CHECK(sanitizeLightListCapacity(128) == 128);
    CHECK(sanitizeLightListCapacity(129) == 128);
    CHECK(isValidLightListCapacity(32));
    CHECK(isValidLightListCapacity(64));
    CHECK(isValidLightListCapacity(128));
    CHECK_FALSE(isValidLightListCapacity(33));
    CHECK(lightListDwordStrideFor(32) == 17);
    CHECK(lightListDwordStrideFor(64) == 33);
    CHECK(lightListDwordStrideFor(128) == 65);
    CHECK(lightListStrideFor(32) == 17);
    CHECK(lightListStrideFor(64) == 33);
    CHECK(lightListStrideFor(128) == 65);
    CHECK(kLightListStrideMax == 65);

    PipelineSettings settings;
    std::string error;
    REQUIRE(parsePipelineSettings(R"({"lightListCapacity":64})", settings, error));
    CHECK(settings.lightListCapacity == 64);
    PipelineSettings roundTrip;
    REQUIRE(parsePipelineSettings(pipelineSettingsToJson(settings), roundTrip, error));
    CHECK(roundTrip.lightListCapacity == 64);
    REQUIRE(parsePipelineSettings(R"({"lightListCapacity":129})", settings, error));
    CHECK(settings.lightListCapacity == 128);
    REQUIRE(parsePipelineSettings(R"({"lightListCapacity":33})", settings, error));
    CHECK(settings.lightListCapacity == 32);
    CHECK_FALSE(parsePipelineSettings(R"({"lightListCapacity":"64"})", settings, error));

    REQUIRE(parsePipelineSettings(R"({"supportLightListFeatureFlags":false})", settings, error));
    CHECK_FALSE(settings.supportLightListFeatureFlags);
    REQUIRE(parsePipelineSettings(pipelineSettingsToJson(settings), roundTrip, error));
    CHECK_FALSE(roundTrip.supportLightListFeatureFlags);
    CHECK_FALSE(parsePipelineSettings(R"({"supportLightListFeatureFlags":"false"})", settings, error));

    REQUIRE(parsePipelineSettings(R"({"supportGpuSkinning":false})", settings, error));
    CHECK_FALSE(settings.supportGpuSkinning);
    REQUIRE(parsePipelineSettings(pipelineSettingsToJson(settings), roundTrip, error));
    CHECK_FALSE(roundTrip.supportGpuSkinning);
    CHECK_FALSE(parsePipelineSettings(R"({"supportGpuSkinning":"false"})", settings, error));

    REQUIRE(parsePipelineSettings(R"({"supportDepthPyramidCompute":false})", settings, error));
    CHECK_FALSE(settings.supportDepthPyramidCompute);
    REQUIRE(parsePipelineSettings(pipelineSettingsToJson(settings), roundTrip, error));
    CHECK_FALSE(roundTrip.supportDepthPyramidCompute);
    CHECK_FALSE(parsePipelineSettings(R"({"supportDepthPyramidCompute":"false"})", settings, error));

    REQUIRE(parsePipelineSettings(R"({"supportGpuDrivenGeometry":true})", settings, error));
    CHECK(settings.supportGpuDrivenGeometry);
    REQUIRE(parsePipelineSettings(pipelineSettingsToJson(settings), roundTrip, error));
    CHECK(roundTrip.supportGpuDrivenGeometry);
    CHECK_FALSE(parsePipelineSettings(R"({"supportGpuDrivenGeometry":"true"})", settings, error));

    REQUIRE(parsePipelineSettings(R"({"supportTransparentRefraction":true})", settings, error));
    CHECK(settings.supportTransparentRefraction);
    REQUIRE(parsePipelineSettings(pipelineSettingsToJson(settings), roundTrip, error));
    CHECK(roundTrip.supportTransparentRefraction);
    CHECK_FALSE(parsePipelineSettings(R"({"supportTransparentRefraction":"true"})", settings, error));

    REQUIRE(parsePipelineSettings(R"({"supportRefractionDepthRejection":false})", settings, error));
    CHECK_FALSE(settings.supportRefractionDepthRejection);
    REQUIRE(parsePipelineSettings(pipelineSettingsToJson(settings), roundTrip, error));
    CHECK_FALSE(roundTrip.supportRefractionDepthRejection);
    CHECK_FALSE(parsePipelineSettings(R"({"supportRefractionDepthRejection":"false"})", settings, error));

    REQUIRE(parsePipelineSettings(R"({"supportRefractionRoughness":true})", settings, error));
    CHECK(settings.supportRefractionRoughness);
    REQUIRE(parsePipelineSettings(pipelineSettingsToJson(settings), roundTrip, error));
    CHECK(roundTrip.supportRefractionRoughness);
    CHECK_FALSE(parsePipelineSettings(R"({"supportRefractionRoughness":"true"})", settings, error));
}

TEST_CASE("PipelineSettings JSON parses and round-trips") {
    PipelineSettings settings;
    std::string error;
    REQUIRE(parsePipelineSettings(
        R"({"supportedLitShaderMode":"both","colorBufferFormat":"RGBA16F","depthBufferFormat":"D32F","msaaSampleCount":"4x","supportShadows":false,"supportGpuLightList":true,"supportSSR":true,"supportSSAO":true,"supportSSGI":true,"supportMotionVectors":true})",
        settings, error));
    CHECK(settings.supportedLitShaderMode == SupportedLitShaderMode::Both);
    CHECK(settings.colorBufferFormat == ColorBufferFormat::R16G16B16A16F);
    CHECK(settings.depthBufferFormat == DepthBufferFormat::D32F);
    CHECK(settings.msaaSampleCount == MSAASamples::MSAA4X);
    CHECK_FALSE(settings.supportShadows);
    CHECK(settings.supportGpuLightList);
    CHECK(settings.supportSSR);
    CHECK(settings.supportSSAO);
    CHECK(settings.supportSSGI);
    CHECK(settings.supportMotionVectors);

    PipelineSettings roundTrip;
    const std::string serialized = pipelineSettingsToJson(settings);
    REQUIRE(parsePipelineSettings(serialized, roundTrip, error));
    CHECK(pipelineSettingsToJson(roundTrip) == serialized);

    CHECK_FALSE(parsePipelineSettings(R"({"unknownKey":true})", settings, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("FrameSettings JSON parses partial fields and round-trips") {
    FrameSettings settings;
    std::string error;
    REQUIRE(parseFrameSettings(
        R"({"litShaderMode":"forward","fields":{"ShadowMaps":false,"OpaqueObjects":false,"SSR":false},"lodBias":1.5,"msaaMode":"4x","materialQuality":"medium"})",
        settings, error));
    CHECK_FALSE(settings.isEnabled(FrameSettingsField::ShadowMaps));
    CHECK_FALSE(settings.isEnabled(FrameSettingsField::OpaqueObjects));
    CHECK_FALSE(settings.isEnabled(FrameSettingsField::SSR));
    CHECK(settings.isEnabled(FrameSettingsField::TransparentObjects));
    CHECK(settings.litShaderMode() == LitShaderMode::Forward);
    CHECK(settings.msaaMode == MSAAMode::MSAA4X);
    CHECK(settings.lodBias == 1.5f);
    CHECK(settings.materialQuality == MaterialQuality::Medium);

    FrameSettings roundTrip;
    const std::string serialized = frameSettingsToJson(settings);
    REQUIRE(parseFrameSettings(serialized, roundTrip, error));
    CHECK(roundTrip == settings);

    CHECK_FALSE(parseFrameSettings(R"({"fields":{"NotAField":true}})", settings, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("Shipped settings files load") {
    PipelineSettings pipeline;
    FrameSettings frame;
    std::string error;
    REQUIRE(loadPipelineSettings(std::string(HBG_PROJECT_ROOT) + "/config/pipeline.json", pipeline, error));
    REQUIRE(loadFrameSettings(std::string(HBG_PROJECT_ROOT) + "/config/frame_settings.json", frame, error));
    CHECK(pipeline.supportShadows);
    CHECK(pipeline.supportFXAA);
    CHECK(pipeline.supportGpuSkinning);
    CHECK(pipeline.supportDepthPyramidCompute);
    CHECK_FALSE(pipeline.supportGpuDrivenGeometry);
    CHECK(frame.isEnabled(FrameSettingsField::ShadowMaps));
}

TEST_CASE("FrameSettings sanitize gates volumetric fog") {
    PipelineSettings pipeline = PipelineSettings::defaults();
    pipeline.supportVolumetrics = false;
    FrameSettings settings = FrameSettings::defaults(FrameSettingsRenderType::Camera);
    settings.setEnabled(FrameSettingsField::AtmosphericScattering, true);
    settings.setEnabled(FrameSettingsField::Volumetrics, true);
    settings.sanitize(pipeline);
    CHECK_FALSE(settings.isEnabled(FrameSettingsField::Volumetrics));

    pipeline.supportVolumetrics = true;
    settings.setEnabled(FrameSettingsField::Volumetrics, true);
    settings.sanitize(pipeline);
    CHECK(settings.isEnabled(FrameSettingsField::Volumetrics));

    settings.setEnabled(FrameSettingsField::AtmosphericScattering, false);
    settings.sanitize(pipeline);
    CHECK_FALSE(settings.isEnabled(FrameSettingsField::Volumetrics));
}

TEST_CASE("pipeline settings parse and serialize supportVFX") {
    render::PipelineSettings settings;
    std::string error;
    REQUIRE(render::parsePipelineSettings(R"({"supportVFX":true})", settings, error));
    CHECK(settings.supportVFX);
    const std::string serialized = render::pipelineSettingsToJson(settings);
    render::PipelineSettings roundTrip;
    REQUIRE(render::parsePipelineSettings(serialized, roundTrip, error));
    CHECK(roundTrip.supportVFX);
    CHECK_FALSE(render::parsePipelineSettings(R"({"supportVFX":"true"})", settings, error));
    REQUIRE(render::parsePipelineSettings(R"({"supportVFXInstancing":true})", settings, error));
    CHECK(settings.supportVFXInstancing);
    const std::string instancingSerialized = render::pipelineSettingsToJson(settings);
    render::PipelineSettings instancingRoundTrip;
    REQUIRE(render::parsePipelineSettings(instancingSerialized, instancingRoundTrip, error));
    CHECK(instancingRoundTrip.supportVFXInstancing);
    CHECK_FALSE(render::parsePipelineSettings(R"({"supportVFXInstancing":"true"})", settings, error));
    REQUIRE(render::parsePipelineSettings(R"({"supportVFXTransparentSort":true})", settings, error));
    CHECK(settings.supportVFXTransparentSort);
    const std::string transparentSortSerialized = render::pipelineSettingsToJson(settings);
    render::PipelineSettings transparentSortRoundTrip;
    REQUIRE(render::parsePipelineSettings(transparentSortSerialized, transparentSortRoundTrip, error));
    CHECK(transparentSortRoundTrip.supportVFXTransparentSort);
    CHECK_FALSE(render::parsePipelineSettings(R"({"supportVFXTransparentSort":"true"})", settings, error));
    REQUIRE(render::parsePipelineSettings(R"({"vfxInstanceBatchSize":2})", settings, error));
    CHECK(settings.vfxInstanceBatchSize == 2u);
    const std::string batchSerialized = render::pipelineSettingsToJson(settings);
    render::PipelineSettings batchRoundTrip;
    REQUIRE(render::parsePipelineSettings(batchSerialized, batchRoundTrip, error));
    CHECK(batchRoundTrip.vfxInstanceBatchSize == 2u);
    CHECK_FALSE(render::parsePipelineSettings(R"({"vfxInstanceBatchSize":65536})", settings, error));
    CHECK_FALSE(render::parsePipelineSettings(R"({"vfxInstanceBatchSize":"2"})", settings, error));
}
