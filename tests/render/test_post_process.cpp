#include "render/postprocess/color_grading_lut.h"
#include "render/postprocess/post_process_chain.h"
#include "render/postprocess/post_process_profile.h"
#include "render/postprocess/post_process_settings.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <string>

using namespace hbg;
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
    CHECK(exposureModeCount() == 3);
    CHECK(tonemappingModeCount() == 3);
    CHECK(meteringModeCount() == 2);
    CHECK(std::string(exposureModeName(ExposureMode::Fixed)) == "Fixed");
    CHECK(std::string(exposureModeName(ExposureMode::Automatic)) == "Automatic");
    CHECK(std::string(exposureModeName(ExposureMode::Histogram)) == "Histogram");
    CHECK(std::string(tonemappingModeName(TonemappingMode::None)) == "None");
    CHECK(std::string(tonemappingModeName(TonemappingMode::Neutral)) == "Neutral");
    CHECK(std::string(tonemappingModeName(TonemappingMode::ACES)) == "ACES");
    CHECK(std::string(meteringModeName(MeteringMode::CenterWeighted)) == "CenterWeighted");
}

TEST_CASE("post-process antialiasing mode prefers TAA and exposes stable names") {
    PostProcessSettings settings;
    CHECK(resolveAntialiasingMode(settings) == AntialiasingMode::None);
    CHECK(std::string(antialiasingModeName(AntialiasingMode::None)) == "Off");
    CHECK(std::string(antialiasingModeName(AntialiasingMode::Taa)) == "TAA");
    CHECK(std::string(antialiasingModeName(AntialiasingMode::Fxaa)) == "FXAA");
    CHECK(antialiasingModeCount() == 3);
    CHECK(antialiasingModeAt(0) == AntialiasingMode::None);
    CHECK(antialiasingModeAt(1) == AntialiasingMode::Taa);
    CHECK(antialiasingModeAt(2) == AntialiasingMode::Fxaa);

    settings.fxaa.enabled = true;
    CHECK(resolveAntialiasingMode(settings) == AntialiasingMode::Fxaa);
    CHECK(settings.needsUber());
    CHECK(settings.hasAdvancedEffects());

    settings.taa.enabled = true;
    sanitizePostProcessSettings(settings);
    CHECK(resolveAntialiasingMode(settings) == AntialiasingMode::Taa);
    CHECK_FALSE(settings.fxaa.enabled);
}

TEST_CASE("post-process histogram bins and weights follow HDRP range mapping") {
    CHECK(packExposureHistogramWeight(0.0f) == 0);
    CHECK(packExposureHistogramWeight(1.0f) == 2048);
    CHECK(packExposureHistogramWeight(0.5f) == 1024);
    CHECK(packExposureHistogramWeight(2.0f) == 2048);
    CHECK(unpackExposureHistogramWeight(1024) == doctest::Approx(0.5f));

    CHECK(exposureHistogramBinFromEv100(-1.0f, -1.0f, 14.0f) == 0);
    CHECK(exposureHistogramBinFromEv100(14.0f, -1.0f, 14.0f) == 127);
    CHECK(exposureHistogramBinFromEv100(6.5f, -1.0f, 14.0f) == 63);
    CHECK(exposureHistogramEv100FromBin(0, -1.0f, 14.0f) == doctest::Approx(-1.0f));
    CHECK(exposureHistogramEv100FromBin(127, -1.0f, 14.0f) == doctest::Approx(14.0f));
    CHECK(exposureHistogramEv100FromBin(63, -1.0f, 14.0f) == doctest::Approx(-1.0f + 15.0f * 63.0f / 127.0f));
}

TEST_CASE("post-process histogram percentile discards outlier bins") {
    std::array<uint32_t, kExposureHistogramBins> bins{};
    bins[0] = packExposureHistogramWeight(100.0f);
    bins[64] = packExposureHistogramWeight(100.0f);

    const float result = computeExposureHistogramEv100(bins, 0.50f, 1.0f, -1.0f, 14.0f);
    CHECK(result == doctest::Approx(exposureHistogramEv100FromBin(64, -1.0f, 14.0f)));

    std::array<uint32_t, kExposureHistogramBins> empty{};
    CHECK(computeExposureHistogramEv100(empty, 0.40f, 0.90f, -1.0f, 14.0f) == doctest::Approx(0.0f));
}

TEST_CASE("post-process exposure adaptation follows HDRP exponential decay") {
    CHECK(adaptExposureEv100(0.0f, 4.0f, 1.0f, 1.0f, 1.0f) == doctest::Approx(2.0f));
    CHECK(adaptExposureEv100(0.0f, -4.0f, 1.0f, 1.0f, 1.0f) == doctest::Approx(-2.0f));
    CHECK(adaptExposureEv100(0.0f, 4.0f, 2.0f, 1.0f, 1.0f) == doctest::Approx(3.0f));
    CHECK(adaptExposureEv100(0.0f, 4.0f, 1.0f, 1.0f, 0.0f) == doctest::Approx(0.0f));

    PostProcessSettings settings;
    settings.limitMin = -1.0f;
    settings.limitMax = 1.0f;
    CHECK(computeAdaptedExposureEv100(1.0f, 0.0f, true, 1.0f, settings) == doctest::Approx(1.0f));
    CHECK(computeAdaptedExposureEv100(1.0f, 0.0f, false, 1.0f, settings) == doctest::Approx(1.0f));
}

TEST_CASE("post-process bloom math follows HDRP threshold and composite rules") {
    CHECK(bloomCompositeIntensity(0.0f) == doctest::Approx(0.0f));
    CHECK(bloomCompositeIntensity(0.5f) == doctest::Approx(std::sqrt(2.0f) - 1.0f));
    CHECK(bloomCompositeIntensity(1.0f) == doctest::Approx(1.0f));
    CHECK(bloomThresholdLinear(0.5f) == doctest::Approx(std::pow(0.5f, 2.2f)));
    const Vec4 curve = bloomThresholdCurve(1.0f);
    CHECK(curve.x == doctest::Approx(1.0f));
    CHECK(curve.y == doctest::Approx(1.0f - (0.5f + 1.0e-5f)));
    CHECK(curve.z == doctest::Approx(1.0f + 2.0e-5f));
    CHECK(curve.w == doctest::Approx(0.25f / (0.5f + 1.0e-5f)));
    CHECK(quadraticBloomThreshold(Vec3(0.0f, 0.0f, 0.0f), 0.0f) == Vec3(0.0f, 0.0f, 0.0f));
    CHECK(quadraticBloomThreshold(Vec3(4.0f, 4.0f, 4.0f), 1.0f).x == doctest::Approx(3.0f));
    CHECK(bloomScatterParam(0.0f) == doctest::Approx(0.05f));
    CHECK(bloomScatterParam(1.0f) == doctest::Approx(0.95f));
    const Vec3 tint = normalizedBloomTint(Vec3(2.0f, 1.0f, 1.0f));
    CHECK((tint.x * 0.2126f + tint.y * 0.7152f + tint.z * 0.0722f) == doctest::Approx(1.0f));
}

TEST_CASE("post-process bloom mip count and sizes follow HDRP post-process resolution") {
    CHECK(bloomMipCount(1920, 1080, 2) == 8);
    CHECK(bloomMipCount(1920, 1080, 4) == 7);
    CHECK(bloomMipCount(640, 360, 2) == 7);
    CHECK(bloomMipCount(3840, 2160, 2) == 8);
    CHECK(bloomMipSize(1920, 1080, 2, 0) == BloomMipSize{960, 540});
    CHECK(bloomMipSize(1920, 1080, 2, 7) == BloomMipSize{8, 4});
    CHECK(bloomMipSize(1920, 1080, 4, 0) == BloomMipSize{480, 270});
    CHECK(bloomMipSize(640, 360, 4, 0) == BloomMipSize{320, 180});
    CHECK(bloomMipSize(1280, 720, 2, 0) == BloomMipSize{640, 360});
}

TEST_CASE("post-process bloom settings sanitize and activity follow HDRP") {
    PostProcessSettings settings;
    settings.bloom.enabled = true;
    settings.bloom.intensity = 0.0f;
    CHECK_FALSE(settings.bloom.active());
    CHECK_FALSE(settings.hasAdvancedEffects());
    settings.bloom.intensity = 0.5f;
    CHECK(settings.bloom.active());
    CHECK(settings.hasAdvancedEffects());

    settings.bloom.threshold = -1.0f;
    settings.bloom.intensity = 2.0f;
    settings.bloom.scatter = 2.0f;
    settings.bloom.resolution = 3;
    sanitizePostProcessSettings(settings);
    CHECK(settings.bloom.threshold == doctest::Approx(0.0f));
    CHECK(settings.bloom.intensity == doctest::Approx(1.0f));
    CHECK(settings.bloom.scatter == doctest::Approx(1.0f));
    CHECK(settings.bloom.resolution == 2);
}

TEST_CASE("post-process DOF manual CoC follows HDRP near and far ranges") {
    DepthOfFieldSettings dof;
    CHECK(dof.nearFocusStart == doctest::Approx(0.0f));
    CHECK(dof.nearFocusEnd == doctest::Approx(4.0f));
    CHECK(dof.farFocusStart == doctest::Approx(10.0f));
    CHECK(dof.farFocusEnd == doctest::Approx(20.0f));
    CHECK(dofNormalizedCoc(0.0f, dof) == doctest::Approx(-1.0f));
    CHECK(dofNormalizedCoc(2.0f, dof) == doctest::Approx(-0.5f));
    CHECK(dofNormalizedCoc(4.0f, dof) == doctest::Approx(0.0f));
    CHECK(dofNormalizedCoc(7.0f, dof) == doctest::Approx(0.0f));
    CHECK(dofNormalizedCoc(10.0f, dof) == doctest::Approx(0.0f));
    CHECK(dofNormalizedCoc(15.0f, dof) == doctest::Approx(0.5f));
    CHECK(dofNormalizedCoc(20.0f, dof) == doctest::Approx(1.0f));
    CHECK(dofNormalizedCoc(40.0f, dof) == doctest::Approx(1.0f));
    CHECK(dofNormalizedCoc(-1.0f, dof) == doctest::Approx(-1.0f));

    CHECK(dofResolutionScale(1) == doctest::Approx(1.0f));
    CHECK(dofResolutionScale(2) == doctest::Approx(0.5f));
    CHECK(dofResolutionScale(4) == doctest::Approx(0.25f));
    CHECK(dofBlurScale(1080) == doctest::Approx(1.0f));
    CHECK(dofBlurScale(720) == doctest::Approx(2.0f / 3.0f));
    CHECK(dofCocPixels(15.0f, dof, 1080) == doctest::Approx(4.0f));
    CHECK(dofCocPixels(2.0f, dof, 1080) == doctest::Approx(-2.0f));
    CHECK(dofGatherTapCount(false) == 8u);
    CHECK(dofGatherTapCount(true) == 12u);
}

TEST_CASE("post-process DOF sanitize clamps manual ranges and resolution") {
    PostProcessSettings settings;
    settings.depthOfField.enabled = true;
    settings.depthOfField.nearFocusStart = 20.0f;
    settings.depthOfField.nearFocusEnd = 4.0f;
    settings.depthOfField.farFocusStart = -5.0f;
    settings.depthOfField.farFocusEnd = 2.0f;
    settings.depthOfField.nearMaxBlur = 20.0f;
    settings.depthOfField.farMaxBlur = -1.0f;
    settings.depthOfField.resolution = 3;
    sanitizePostProcessSettings(settings);

    CHECK(settings.depthOfField.nearFocusStart == doctest::Approx(4.0f));
    CHECK(settings.depthOfField.nearFocusEnd == doctest::Approx(4.0f));
    CHECK(settings.depthOfField.farFocusStart == doctest::Approx(4.0f));
    CHECK(settings.depthOfField.farFocusEnd >= 4.0f);
    CHECK(settings.depthOfField.nearMaxBlur == doctest::Approx(8.0f));
    CHECK(settings.depthOfField.farMaxBlur == doctest::Approx(0.0f));
    CHECK(settings.depthOfField.resolution == 2u);
}

TEST_CASE("post-process DOF activity requires a visible near or far layer") {
    DepthOfFieldSettings dof;
    dof.enabled = true;
    CHECK(dof.active());
    dof.nearMaxBlur = 0.0f;
    dof.farMaxBlur = 0.0f;
    CHECK_FALSE(dof.active());

    dof.farMaxBlur = 8.0f;
    CHECK(dof.active());
    dof.enabled = false;
    CHECK_FALSE(dof.active());
}

TEST_CASE("post-process motion blur follows HDRP pixel velocity semantics") {
    MotionBlurSettings motion;
    CHECK(motion.intensity == doctest::Approx(0.5f));
    CHECK(motion.maximumVelocity == doctest::Approx(200.0f));
    CHECK(motion.minimumVelocity == doctest::Approx(2.0f));
    CHECK(motion.sampleCount == 8u);
    CHECK(motion.depthComparisonExtent == doctest::Approx(1.0f));
    CHECK_FALSE(motion.active());
    motion.enabled = true;
    CHECK(motion.active());

    CHECK(motionBlurScreenMagnitude(3, 4) == doctest::Approx(5.0f));
    CHECK(motionBlurVelocityPixels(Vec2(0.03f, 0.04f), 3, 4) == doctest::Approx(0.25f));

    motion.intensity = 1.0f;
    const Vec2 clamped = motionBlurClampVelocity(Vec2(100.0f, 0.0f), motion, 3, 4);
    CHECK(std::sqrt(clamped.x * clamped.x + clamped.y * clamped.y) == doctest::Approx(200.0f / 5.0f));

    CHECK(motionBlurSampleCount(0.0f, 8) == 0u);
    CHECK(motionBlurSampleCount(1.5f, 8) == 2u);
    CHECK(motionBlurSampleCount(7.8f, 8) == 8u);
    CHECK(motionBlurSampleCount(9.2f, 16) == 10u);
    CHECK(motionBlurSampleCount(40.0f, 8) == 8u);

    const Vec2 direction = motionBlurDirectionUv(Vec2(0.1f, 0.2f), true);
    CHECK(direction.x == doctest::Approx(0.05f));
    CHECK(direction.y == doctest::Approx(-0.1f));
    const Vec2 sample = motionBlurSampleUv(Vec2(0.5f, 0.5f), Vec2(0.1f, 0.2f), 0.5f, true);
    CHECK(sample.x == doctest::Approx(0.525f));
    CHECK(sample.y == doctest::Approx(0.45f));

    CHECK(motionBlurDepthWeight(5.0f, 5.0f, 1.0f) == doctest::Approx(0.5f));
    CHECK(motionBlurDepthWeight(5.0f, 4.0f, 1.0f) == doctest::Approx(1.0f));
    CHECK(motionBlurDepthWeight(5.0f, 6.0f, 1.0f) == doctest::Approx(0.0f));
}

TEST_CASE("post-process motion blur sanitize clamps HDRP limits") {
    PostProcessSettings settings;
    settings.motionBlur.enabled = true;
    settings.motionBlur.intensity = -1.0f;
    settings.motionBlur.maximumVelocity = 2000.0f;
    settings.motionBlur.minimumVelocity = 100.0f;
    settings.motionBlur.sampleCount = 1;
    settings.motionBlur.depthComparisonExtent = 30.0f;
    sanitizePostProcessSettings(settings);
    CHECK(settings.motionBlur.intensity == doctest::Approx(0.0f));
    CHECK(settings.motionBlur.maximumVelocity == doctest::Approx(1500.0f));
    CHECK(settings.motionBlur.minimumVelocity == doctest::Approx(64.0f));
    CHECK(settings.motionBlur.sampleCount == 2u);
    CHECK(settings.motionBlur.depthComparisonExtent == doctest::Approx(20.0f));

    settings.motionBlur.sampleCount = 15;
    sanitizePostProcessSettings(settings);
    CHECK(settings.motionBlur.sampleCount == 16u);
    CHECK_FALSE(settings.motionBlur.active());
}

TEST_CASE("post-process profile parses motion blur fields") {
    PostProcessProfile profile;
    std::string error;
    REQUIRE(parsePostProcessProfile(
        R"({"motionBlur":{"enabled":true,"intensity":0.75,"maximumVelocity":120.0,)"
        R"("minimumVelocity":3.0,"cameraMotionBlur":false,"sampleCount":16,"depthComparisonExtent":2.0}})",
        profile, error));
    CHECK(profile.settings.motionBlur.enabled);
    CHECK(profile.settings.motionBlur.intensity == doctest::Approx(0.75f));
    CHECK(profile.settings.motionBlur.maximumVelocity == doctest::Approx(120.0f));
    CHECK(profile.settings.motionBlur.minimumVelocity == doctest::Approx(3.0f));
    CHECK_FALSE(profile.settings.motionBlur.cameraMotionBlur);
    CHECK(profile.settings.motionBlur.sampleCount == 16u);
    CHECK(profile.settings.motionBlur.depthComparisonExtent == doctest::Approx(2.0f));
    CHECK(profile.settings.motionBlur.active());
}

TEST_CASE("post-process profile parses FXAA and TAA precedence") {
    PostProcessProfile profile;
    std::string error;
    REQUIRE(parsePostProcessProfile(R"({"fxaa":{"enabled":true}})", profile, error));
    CHECK(profile.settings.fxaa.enabled);
    CHECK(profile.settings.needsUber());
    CHECK(resolveAntialiasingMode(profile.settings) == AntialiasingMode::Fxaa);

    REQUIRE(parsePostProcessProfile(R"({"taa":{"enabled":true},"fxaa":{"enabled":true}})", profile, error));
    CHECK(profile.settings.taa.enabled);
    CHECK_FALSE(profile.settings.fxaa.enabled);
    CHECK(resolveAntialiasingMode(profile.settings) == AntialiasingMode::Taa);
}

TEST_CASE("post-process profile parses DOF manual ranges") {
    PostProcessProfile profile;
    std::string error;
    REQUIRE(parsePostProcessProfile(
        R"({"depthOfField":{"enabled":true,"focusDistance":6.0,"nearFocusStart":1.0,)"
        R"("nearFocusEnd":3.0,"farFocusStart":8.0,"farFocusEnd":14.0,)"
        R"("nearMaxBlur":2.0,"farMaxBlur":6.0,"resolution":4,"highQuality":true}})",
        profile, error));
    CHECK(profile.settings.depthOfField.enabled);
    CHECK(profile.settings.depthOfField.focusDistance == doctest::Approx(6.0f));
    CHECK(profile.settings.depthOfField.nearFocusStart == doctest::Approx(1.0f));
    CHECK(profile.settings.depthOfField.nearFocusEnd == doctest::Approx(3.0f));
    CHECK(profile.settings.depthOfField.farFocusStart == doctest::Approx(8.0f));
    CHECK(profile.settings.depthOfField.farFocusEnd == doctest::Approx(14.0f));
    CHECK(profile.settings.depthOfField.nearMaxBlur == doctest::Approx(2.0f));
    CHECK(profile.settings.depthOfField.farMaxBlur == doctest::Approx(6.0f));
    CHECK(profile.settings.depthOfField.resolution == 4u);
    CHECK(profile.settings.depthOfField.highQuality);
    CHECK(profile.settings.depthOfField.active());
}

TEST_CASE("post-process TAA reprojection follows motion vectors and exposure orientation") {
    CHECK(taaBaseHistoryWeight(1) == doctest::Approx(0.80f));
    CHECK(taaBaseHistoryWeight(3) == doctest::Approx(0.875f));
    CHECK(taaBaseHistoryWeight(5) == doctest::Approx(0.925f));

    const Vec2 uv(0.5f, 0.5f);
    const Vec2 motion(0.1f, 0.2f);
    const Vec2 regular = taaReprojectUv(uv, motion, false);
    CHECK(regular.x == doctest::Approx(0.4f));
    CHECK(regular.y == doctest::Approx(0.3f));

    const Vec2 flipped = taaReprojectUv(uv, motion, true);
    CHECK(flipped.x == doctest::Approx(0.4f));
    CHECK(flipped.y == doctest::Approx(0.7f));
}

TEST_CASE("post-process TAA settings sanitize quality and weights") {
    PostProcessSettings settings;
    settings.taa.quality = 9;
    settings.taa.motionVectorRejection = 2.0f;
    settings.taa.velocityRejection = 2.0f;
    settings.taa.sharpening = -1.0f;
    settings.taa.jitterScale = 2.0f;
    settings.taa.upscaleInjection = static_cast<TaaUpsampleInjection>(99);
    sanitizePostProcessSettings(settings);
    CHECK(settings.taa.quality == 5u);
    CHECK(settings.taa.motionVectorRejection == doctest::Approx(1.0f));
    CHECK(settings.taa.velocityRejection == doctest::Approx(1.0f));
    CHECK(settings.taa.sharpening == doctest::Approx(0.0f));
    CHECK(settings.taa.jitterScale == doctest::Approx(1.0f));
    CHECK(settings.taa.upscaleInjection == TaaUpsampleInjection::BeforePost);
}

TEST_CASE("post-process TAAU injection names and parsing mirror HDRP schedules") {
    CHECK(taaUpsampleInjectionCount() == 3u);
    CHECK(std::string(taaUpsampleInjectionName(TaaUpsampleInjection::BeforePost)) == "BeforePost");
    CHECK(std::string(taaUpsampleInjectionName(TaaUpsampleInjection::AfterDepthOfField)) ==
          "AfterDepthOfField");
    CHECK(std::string(taaUpsampleInjectionName(TaaUpsampleInjection::AfterPost)) == "AfterPost");

    TaaUpsampleInjection injection = TaaUpsampleInjection::AfterPost;
    CHECK(parseTaaUpsampleInjection("before-post", injection));
    CHECK(injection == TaaUpsampleInjection::BeforePost);
    CHECK(parseTaaUpsampleInjection("after-dof", injection));
    CHECK(injection == TaaUpsampleInjection::AfterDepthOfField);
    CHECK(parseTaaUpsampleInjection("afterPost", injection));
    CHECK(injection == TaaUpsampleInjection::AfterPost);
    CHECK_FALSE(parseTaaUpsampleInjection("after-depth", injection));
}

TEST_CASE("post-process profile rejects unknown TAAU injection") {
    PostProcessProfile profile;
    std::string error;
    CHECK_FALSE(parsePostProcessProfile(R"({"taa":{"injection":"after-depth"}})", profile, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("post-process TAA history uses minmax clamp and rejection weights") {
    const Vec3 clamped = taaClampHistory(Vec3(2.0f, -1.0f, 0.5f), Vec3(0.0f, 0.0f, 0.0f),
                                         Vec3(1.0f, 1.0f, 1.0f));
    CHECK(clamped == Vec3(1.0f, 0.0f, 0.5f));

    CHECK(taaDepthWeight(0.5f, 0.5f) == doctest::Approx(1.0f));
    CHECK(taaDepthWeight(0.5f, 1.0f) < 0.01f);
    CHECK(taaLumaWeight(0.5f, 0.5f) == doctest::Approx(1.0f));
    CHECK(taaLumaWeight(0.5f, 1.0f) < 1.0f);

    CHECK(taaHistoryWeight(0.875f, 1.0f, 1.0f, 0.0f, 0.0f) == doctest::Approx(0.875f));
    CHECK(taaHistoryWeight(0.875f, 1.0f, 1.0f, 50.0f, 1.0f) == doctest::Approx(0.0f));
    CHECK(taaHistoryWeight(0.875f, 1.0f, 1.0f, 50.0f, 0.0f) == doctest::Approx(0.875f));
    CHECK(taaHistoryWeight(1.0f, 1.0f, 1.0f, 0.0f, 0.0f) <= 0.98f);
}

TEST_CASE("post-process profile parses HDRP bloom quality fields") {
    PostProcessProfile profile;
    std::string error;
    REQUIRE(parsePostProcessProfile(
        R"({"bloom":{"enabled":true,"threshold":0.8,"intensity":0.5,"scatter":0.6,)"
        R"("tint":[1.0,0.9,0.8],"highQualityPrefiltering":true,"highQualityFiltering":false,"resolution":4}})",
        profile, error));
    CHECK(profile.settings.bloom.enabled);
    CHECK(profile.settings.bloom.threshold == doctest::Approx(0.8f));
    CHECK(profile.settings.bloom.intensity == doctest::Approx(0.5f));
    CHECK(profile.settings.bloom.scatter == doctest::Approx(0.6f));
    CHECK(profile.settings.bloom.highQualityPrefiltering);
    CHECK_FALSE(profile.settings.bloom.highQualityFiltering);
    CHECK(profile.settings.bloom.resolution == 4);
}

TEST_CASE("post-process profile maps legacy bloom highQuality alias") {
    PostProcessProfile profile;
    std::string error;
    REQUIRE(parsePostProcessProfile(
        R"({"bloom":{"enabled":true,"intensity":0.5,"highQuality":true}})", profile, error));
    CHECK(profile.settings.bloom.highQualityPrefiltering);
    CHECK(profile.settings.bloom.highQualityFiltering);
}

TEST_CASE("post-process profile baseline keeps M2 output path") {
    const PostProcessProfile profile = baselinePostProcessProfile();
    CHECK(profile.name == "baseline");
    CHECK(profile.settings.exposureMode == ExposureMode::Fixed);
    CHECK(profile.settings.tonemapping == TonemappingMode::ACES);
    CHECK_FALSE(profile.settings.hasAdvancedEffects());
}

TEST_CASE("post-process profile parses grouped settings") {
    PostProcessProfile profile;
    std::string error;
    REQUIRE(parsePostProcessProfile(
        R"({"name":"quality","exposure":{"mode":"auto","fixedEv100":-2.0,"metering":"average"},)"
        R"("tonemapping":"neutral","taa":{"enabled":true,"upscale":true,"injection":"afterDof","quality":4,)"
        R"("motionVectorRejection":0.2,"velocityRejection":0.35,"sharpening":0.4,"jitterScale":0.5},)"
        R"("bloom":{"enabled":true,"threshold":0.8,"intensity":0.5,"tint":[1.0,0.9,0.8]},)"
        R"("colorGrading":{"enabled":true,"saturation":12.0},)"
        R"("fxaa":{"enabled":true}})",
        profile, error));

    CHECK(profile.name == "quality");
    CHECK(profile.settings.exposureMode == ExposureMode::Automatic);
    CHECK(profile.settings.metering == MeteringMode::Average);
    CHECK(profile.settings.fixedEv100 == doctest::Approx(-2.0f));
    CHECK(profile.settings.tonemapping == TonemappingMode::Neutral);
    CHECK(profile.settings.taa.enabled);
    CHECK(profile.settings.taa.upscale);
    CHECK(profile.settings.taa.upscaleInjection == TaaUpsampleInjection::AfterDepthOfField);
    CHECK_FALSE(profile.settings.fxaa.enabled);
    CHECK(profile.settings.taa.quality == 4);
    CHECK(profile.settings.taa.motionVectorRejection == doctest::Approx(0.2f));
    CHECK(profile.settings.taa.velocityRejection == doctest::Approx(0.35f));
    CHECK(profile.settings.taa.sharpening == doctest::Approx(0.4f));
    CHECK(profile.settings.bloom.enabled);
    CHECK(profile.settings.bloom.tint.y == doctest::Approx(0.9f));
    CHECK(profile.settings.hasAdvancedEffects());
}

TEST_CASE("post-process profile parses histogram exposure settings") {
    PostProcessProfile profile;
    std::string error;
    REQUIRE(parsePostProcessProfile(
        R"({"name":"histogram","exposure":{"mode":"histogram","metering":"centerWeighted",)"
        R"("histogramMinPercentile":0.35,"histogramMaxPercentile":0.85,"limitMin":-2,"limitMax":8}})",
        profile, error));

    CHECK(profile.name == "histogram");
    CHECK(profile.settings.exposureMode == ExposureMode::Histogram);
    CHECK(profile.settings.metering == MeteringMode::CenterWeighted);
    CHECK(profile.settings.histogramMinPercentile == doctest::Approx(0.35f));
    CHECK(profile.settings.histogramMaxPercentile == doctest::Approx(0.85f));
    CHECK(profile.settings.limitMin == doctest::Approx(-2.0f));
    CHECK(profile.settings.limitMax == doctest::Approx(8.0f));
}

TEST_CASE("post-process profile rejects unknown keys") {
    PostProcessProfile profile;
    std::string error;
    CHECK_FALSE(parsePostProcessProfile(R"({"exposure":{"mode":"bogus"}})", profile, error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(parsePostProcessProfile(R"({"unknown":true})", profile, error));
}

TEST_CASE("post-process sanitize enforces TAA and FXAA exclusivity") {
    PostProcessSettings settings;
    settings.taa.enabled = true;
    settings.fxaa.enabled = true;
    sanitizePostProcessSettings(settings);
    CHECK(settings.taa.enabled);
    CHECK_FALSE(settings.fxaa.enabled);

    settings.taa.enabled = false;
    settings.fxaa.enabled = true;
    sanitizePostProcessSettings(settings);
    CHECK_FALSE(settings.taa.enabled);
    CHECK(settings.fxaa.enabled);
}

TEST_CASE("post-process history resets on first frame, size and effect changes") {
    PostProcessHistoryState history;
    CHECK_FALSE(history.valid());
    CHECK_FALSE(history.beginFrame(1920, 1080, 1.0f, 1, true));
    CHECK_FALSE(history.beginFrame(1920, 1080, 1.0f, 1, true));
    CHECK(history.valid());
    CHECK(history.frameCount() == 1);
    CHECK(history.beginFrame(1920, 1080, 1.0f, 1, true));
    CHECK(history.frameCount() == 2);

    CHECK_FALSE(history.beginFrame(960, 540, 1.0f, 1, true));
    CHECK_FALSE(history.valid());
    CHECK_FALSE(history.beginFrame(960, 540, 1.0f, 1, true));
    CHECK(history.valid());

    CHECK_FALSE(history.beginFrame(960, 540, 0.5f, 1, true));
    CHECK_FALSE(history.valid());
    CHECK_FALSE(history.beginFrame(960, 540, 0.5f, 2, true));
    CHECK(history.beginFrame(960, 540, 0.5f, 2, true) == false);
    CHECK(history.beginFrame(960, 540, 0.5f, 2, true));
}

TEST_CASE("post-process color grading defaults are identity") {
    ColorGradingSettings grading;
    CHECK(grading.splitShadows == Vec3(0.5f, 0.5f, 0.5f));
    CHECK(grading.splitHighlights == Vec3(0.5f, 0.5f, 0.5f));

    PostProcessSettings settings;
    settings.vignette.intensity = 2.0f;
    settings.vignette.smoothness = -1.0f;
    settings.vignette.center = Vec2(2.0f, -1.0f);
    sanitizePostProcessSettings(settings);
    CHECK(settings.vignette.intensity == doctest::Approx(1.0f));
    CHECK(settings.vignette.smoothness == doctest::Approx(0.01f));
    CHECK(settings.vignette.center == Vec2(1.0f, 0.0f));
}

TEST_CASE("post-process color grading LUT layout and identity sampling") {
    CHECK(kColorGradingLutSize == 64);
    CHECK(kColorGradingLutWidth == 64);
    CHECK(kColorGradingLutHeight == 4096);
    CHECK(colorGradingLutUploadBytes() == 2097152);
    CHECK(colorGradingLutStripIndex(0, 0, 0) == 0);
    CHECK(colorGradingLutStripIndex(1, 2, 3) == ((3 * 64 + 2) * 64 + 1) * 4);

    ColorGradingSettings grading;
    grading.enabled = false;
    ColorGradingLut lut;
    REQUIRE(buildColorGradingLut(grading, TonemappingMode::None, lut));
    REQUIRE(lut.valid);
    CHECK(lut.texels.size() == 64u * 4096u * 4u);

    const Vec3 logColor(8.0f / 63.0f, 12.0f / 63.0f, 4.0f / 63.0f);
    const Vec3 linear = logCToLinear(logColor);
    const Vec3 sampled = lut.sample(logColor);
    CHECK(std::abs(sampled.x - linear.x) <= 1.0f / 255.0f);
    CHECK(std::abs(sampled.y - linear.y) <= 1.0f / 255.0f);
    CHECK(std::abs(sampled.z - linear.z) <= 1.0f / 255.0f);
}

TEST_CASE("post-process color grading applies a known color filter") {
    ColorGradingSettings grading;
    grading.enabled = true;
    grading.colorFilter = Vec3(2.0f, 1.0f, 0.5f);

    const Vec3 logColor(8.0f / 63.0f, 12.0f / 63.0f, 4.0f / 63.0f);
    const Vec3 linear = logCToLinear(logColor);
    const Vec3 expected = Vec3(linear.x * 2.0f, linear.y, linear.z * 0.5f);
    const Vec3 evaluated = evaluateColorGradingLut(logColor, grading, TonemappingMode::None);
    CHECK(evaluated.x == doctest::Approx(expected.x).epsilon(0.001));
    CHECK(evaluated.y == doctest::Approx(expected.y).epsilon(0.001));
    CHECK(evaluated.z == doctest::Approx(expected.z).epsilon(0.001));

    ColorGradingLut lut;
    REQUIRE(buildColorGradingLut(grading, TonemappingMode::None, lut));
    const Vec3 sampled = lut.sample(logColor);
    CHECK(std::abs(sampled.x - expected.x) <= 1.0f / 255.0f);
    CHECK(std::abs(sampled.y - expected.y) <= 1.0f / 255.0f);
    CHECK(std::abs(sampled.z - expected.z) <= 1.0f / 255.0f);
}

TEST_CASE("post-process neutral ACES LUT matches the CPU tonemap") {
    ColorGradingSettings grading;
    grading.enabled = true;
    ColorGradingLut lut;
    REQUIRE(buildColorGradingLut(grading, TonemappingMode::ACES, lut));

    const Vec3 logColor(8.0f / 63.0f, 12.0f / 63.0f, 4.0f / 63.0f);
    const Vec3 linear = logCToLinear(logColor);
    const Vec3 expected = tonemapColor(linear, TonemappingMode::ACES);
    const Vec3 sampled = lut.sample(logColor);
    CHECK(std::abs(sampled.x - expected.x) <= 1.0f / 255.0f);
    CHECK(std::abs(sampled.y - expected.y) <= 1.0f / 255.0f);
    CHECK(std::abs(sampled.z - expected.z) <= 1.0f / 255.0f);
}

TEST_CASE("post-process profile parses color grading and vignette") {
    PostProcessProfile profile;
    std::string error;
    REQUIRE(parsePostProcessProfile(
        R"({"colorGrading":{"enabled":true,"postExposure":1.5,"contrast":20,"colorFilter":[1,0.8,0.6],)"
        R"("splitShadows":[0.4,0.5,0.6]},"vignette":{"center":[0.4,0.6],"intensity":0.4,)"
        R"("smoothness":0.3,"roundness":0.8,"rounded":true,"color":[0.1,0.2,0.3],"opacity":0.7}})",
        profile, error));

    CHECK(profile.settings.colorGrading.enabled);
    CHECK(profile.settings.colorGrading.postExposure == doctest::Approx(1.5f));
    CHECK(profile.settings.colorGrading.contrast == doctest::Approx(20.0f));
    CHECK(profile.settings.colorGrading.colorFilter.y == doctest::Approx(0.8f));
    CHECK(profile.settings.colorGrading.splitShadows.z == doctest::Approx(0.6f));
    CHECK(profile.settings.vignette.active());
    CHECK(profile.settings.vignette.center.x == doctest::Approx(0.4f));
    CHECK(profile.settings.vignette.rounded);
    CHECK(profile.settings.vignette.opacity == doctest::Approx(0.7f));
}

TEST_CASE("post-process neutral color grading is identity") {
    ColorGradingSettings grading;
    grading.enabled = true;
    const Vec3 color(0.08f, 0.35f, 0.9f);
    const Vec3 graded = colorGrade(color, grading);
    CHECK(std::abs(graded.x - color.x) <= 1.0f / 255.0f);
    CHECK(std::abs(graded.y - color.y) <= 1.0f / 255.0f);
    CHECK(std::abs(graded.z - color.z) <= 1.0f / 255.0f);
}
