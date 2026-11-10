#include "render/pipeline/frame_settings.h"
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

TEST_CASE("FrameSettings defaults follow HDRP render types") {
    const FrameSettings camera = FrameSettings::defaults(FrameSettingsRenderType::Camera);
    CHECK(camera.isEnabled(FrameSettingsField::ShadowMaps));
    CHECK(camera.isEnabled(FrameSettingsField::OpaqueObjects));
    CHECK(camera.isEnabled(FrameSettingsField::TransparentObjects));
    CHECK(camera.isEnabled(FrameSettingsField::Postprocess));
    CHECK(camera.isEnabled(FrameSettingsField::MotionVectors));
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
    pipeline.supportMotionVectors = false;

    FrameSettings settings = FrameSettings::defaults(FrameSettingsRenderType::Camera);
    settings.setLitShaderMode(LitShaderMode::Deferred);
    settings.setEnabled(FrameSettingsField::SSR, true);
    settings.setEnabled(FrameSettingsField::MotionVectors, true);
    settings.sanitize(pipeline);

    CHECK(settings.litShaderMode() == LitShaderMode::Forward);
    CHECK_FALSE(settings.isEnabled(FrameSettingsField::ShadowMaps));
    CHECK_FALSE(settings.isEnabled(FrameSettingsField::SSR));
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

TEST_CASE("PipelineSettings JSON parses and round-trips") {
    PipelineSettings settings;
    std::string error;
    REQUIRE(parsePipelineSettings(
        R"({"supportedLitShaderMode":"both","colorBufferFormat":"RGBA16F","depthBufferFormat":"D32F","msaaSampleCount":"4x","supportShadows":false,"supportSSR":true,"supportMotionVectors":true})",
        settings, error));
    CHECK(settings.supportedLitShaderMode == SupportedLitShaderMode::Both);
    CHECK(settings.colorBufferFormat == ColorBufferFormat::R16G16B16A16F);
    CHECK(settings.depthBufferFormat == DepthBufferFormat::D32F);
    CHECK(settings.msaaSampleCount == MSAASamples::MSAA4X);
    CHECK_FALSE(settings.supportShadows);
    CHECK(settings.supportSSR);
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
    CHECK(frame.isEnabled(FrameSettingsField::ShadowMaps));
}
