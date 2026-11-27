#pragma once

#include "core/math.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace hbg::render {

enum class ExposureMode : uint8_t {
    Fixed,
    Automatic,
    Histogram,
};

enum class TonemappingMode : uint8_t {
    None,
    Neutral,
    ACES,
};

enum class MeteringMode : uint8_t {
    Average,
    CenterWeighted,
};

enum class AntialiasingMode : uint8_t {
    None,
    Taa,
    Fxaa,
};

struct BloomSettings {
    bool enabled = false;
    float threshold = 0.0f;
    float intensity = 0.0f;
    float scatter = 0.7f;
    Vec3 tint{1.0f, 1.0f, 1.0f};
    bool highQualityPrefiltering = false;
    bool highQualityFiltering = true;
    uint32_t resolution = 2;

    bool active() const { return enabled && intensity > 0.0f; }
    friend bool operator==(const BloomSettings&, const BloomSettings&) = default;
};

struct BloomMipSize {
    uint16_t width = 1;
    uint16_t height = 1;

    friend constexpr bool operator==(const BloomMipSize&, const BloomMipSize&) = default;
};

struct ColorGradingSettings {
    bool enabled = false;
    float postExposure = 0.0f;
    float contrast = 0.0f;
    Vec3 colorFilter{1.0f, 1.0f, 1.0f};
    float hueShift = 0.0f;
    float saturation = 0.0f;
    float temperature = 0.0f;
    float tint = 0.0f;
    Vec3 channelMixerRed{1.0f, 0.0f, 0.0f};
    Vec3 channelMixerGreen{0.0f, 1.0f, 0.0f};
    Vec3 channelMixerBlue{0.0f, 0.0f, 1.0f};
    Vec3 lift{0.0f, 0.0f, 0.0f};
    Vec3 gamma{1.0f, 1.0f, 1.0f};
    Vec3 gain{1.0f, 1.0f, 1.0f};
    Vec3 shadows{1.0f, 1.0f, 1.0f};
    Vec3 midtones{1.0f, 1.0f, 1.0f};
    Vec3 highlights{1.0f, 1.0f, 1.0f};
    Vec4 shadowsMidtonesHighlightsLimits{0.0f, 0.5f, 0.5f, 1.0f};
    Vec3 splitShadows{0.5f, 0.5f, 0.5f};
    Vec3 splitHighlights{0.5f, 0.5f, 0.5f};
    float splitBalance = 0.0f;

    friend bool operator==(const ColorGradingSettings&, const ColorGradingSettings&) = default;
};

struct VignetteSettings {
    Vec2 center{0.5f, 0.5f};
    float intensity = 0.0f;
    float smoothness = 0.2f;
    float roundness = 1.0f;
    bool rounded = false;
    Vec3 color{0.0f, 0.0f, 0.0f};
    float opacity = 1.0f;

    bool active() const { return intensity > 0.0f; }
    friend bool operator==(const VignetteSettings&, const VignetteSettings&) = default;
};

struct TaaSettings {
    bool enabled = false;
    uint32_t quality = 3;
    float motionVectorRejection = 0.1f;
    float sharpening = 0.0f;
    float jitterScale = 1.0f;

    friend bool operator==(const TaaSettings&, const TaaSettings&) = default;
};

struct DepthOfFieldSettings {
    bool enabled = false;
    float focusDistance = 10.0f;
    float nearFocusStart = 0.0f;
    float nearFocusEnd = 4.0f;
    float farFocusStart = 10.0f;
    float farFocusEnd = 20.0f;
    float nearMaxBlur = 4.0f;
    float farMaxBlur = 8.0f;
    uint32_t resolution = 2;
    bool highQuality = false;

    bool active() const {
        return enabled && ((nearMaxBlur > 0.0f && nearFocusEnd > nearFocusStart) ||
                           (farMaxBlur > 0.0f && farFocusEnd > farFocusStart));
    }
    friend bool operator==(const DepthOfFieldSettings&, const DepthOfFieldSettings&) = default;
};

struct MotionBlurSettings {
    bool enabled = false;
    float intensity = 0.5f;
    float maximumVelocity = 200.0f;
    float minimumVelocity = 2.0f;
    bool cameraMotionBlur = true;
    uint32_t sampleCount = 8;
    float depthComparisonExtent = 1.0f;

    bool active() const { return enabled && intensity > 0.0f && maximumVelocity > 0.0f && sampleCount >= 2u; }
    friend bool operator==(const MotionBlurSettings&, const MotionBlurSettings&) = default;
};

struct FxaaSettings {
    bool enabled = false;
};

struct PostProcessSettings {
    ExposureMode exposureMode = ExposureMode::Fixed;
    float fixedEv100 = -1.2630344f;
    float compensation = 0.0f;
    float limitMin = -1.0f;
    float limitMax = 14.0f;
    float debugExposure = 0.0f;
    TonemappingMode tonemapping = TonemappingMode::ACES;

    MeteringMode metering = MeteringMode::CenterWeighted;
    float adaptationSpeedDarkToLight = 1.0f;
    float adaptationSpeedLightToDark = 1.0f;
    float histogramMinPercentile = 0.40f;
    float histogramMaxPercentile = 0.90f;
    Vec3 proceduralCenter{0.5f, 0.5f, 0.0f};
    Vec3 proceduralRadii{0.5f, 0.5f, 0.0f};
    float proceduralSoftness = 0.5f;

    BloomSettings bloom;
    ColorGradingSettings colorGrading;
    VignetteSettings vignette;
    TaaSettings taa;
    DepthOfFieldSettings depthOfField;
    MotionBlurSettings motionBlur;
    FxaaSettings fxaa;

    bool needsUber() const { return bloom.active() || colorGrading.enabled || vignette.active() || fxaa.enabled; }

    bool hasAdvancedEffects() const {
        return bloom.active() || needsUber() || taa.enabled || depthOfField.active() || motionBlur.active() ||
               fxaa.enabled;
    }
};

constexpr float kLensAttenuation = 0.65f;
constexpr float kLightMeterCalibrationConstant = 12.5f;
constexpr uint32_t kExposureHistogramBins = 128;
constexpr uint32_t kExposureHistogramWeightScale = 2048;
constexpr uint32_t kMaxBloomMips = 8;
constexpr float kTaaMotionRejectionPixels = 50.0f;

float lensImperfectionExposureScale();
float taaBaseHistoryWeight(uint32_t quality);
Vec2 taaReprojectUv(const Vec2& uv, const Vec2& motion, bool flipY);
Vec3 taaClampHistory(const Vec3& history, const Vec3& minimum, const Vec3& maximum);
float taaDepthWeight(float currentDepth, float historyDepth);
float taaLumaWeight(float currentLuma, float historyLuma);
float taaHistoryWeight(float baseWeight, float depthWeight, float lumaWeight, float motionPixels,
                       float motionVectorRejection);
float dofNormalizedCoc(float linearEyeDepth, const DepthOfFieldSettings& settings);
float dofCocPixels(float linearEyeDepth, const DepthOfFieldSettings& settings, uint16_t renderHeight);
float dofResolutionScale(uint32_t resolution);
float dofBlurScale(uint16_t renderHeight);
uint32_t dofGatherTapCount(bool highQuality);
float motionBlurScreenMagnitude(uint16_t width, uint16_t height);
float motionBlurVelocityPixels(const Vec2& motionNdc, uint16_t width, uint16_t height);
Vec2 motionBlurClampVelocity(const Vec2& motionNdc, const MotionBlurSettings& settings, uint16_t width,
                             uint16_t height);
uint32_t motionBlurSampleCount(float velocityPixels, uint32_t maximumSamples);
Vec2 motionBlurDirectionUv(const Vec2& motionNdc, bool flipY);
Vec2 motionBlurSampleUv(const Vec2& centerUv, const Vec2& motionNdc, float offset, bool flipY);
float motionBlurDepthWeight(float centerDepth, float sampleDepth, float depthComparisonExtent);
float bloomThresholdLinear(float thresholdGamma);
Vec4 bloomThresholdCurve(float thresholdGamma);
Vec3 quadraticBloomThreshold(const Vec3& color, float thresholdLinear);
float bloomCompositeIntensity(float intensity);
Vec3 normalizedBloomTint(const Vec3& tint);
float bloomScatterParam(float scatter);
uint32_t bloomMipCount(uint16_t renderWidth, uint16_t renderHeight, uint32_t resolution);
BloomMipSize bloomMipSize(uint16_t renderWidth, uint16_t renderHeight, uint32_t resolution, uint32_t mip);
float convertEv100ToExposure(float ev100);
float convertExposureToEv100(float exposure);
float computeEv100FromAverageLuminance(float averageLuminance);
float computeAutoExposureEv100(float averageLuminance, const PostProcessSettings& settings);
float computeAdaptedExposureEv100(float averageLuminance, float previousEv100, bool historyValid, float deltaSeconds,
                                  const PostProcessSettings& settings);
float fixedExposureMultiplier(const PostProcessSettings& settings);
uint32_t packExposureHistogramWeight(float weight);
float unpackExposureHistogramWeight(uint32_t packed);
uint32_t exposureHistogramBinFromEv100(float ev100, float limitMin, float limitMax);
float exposureHistogramEv100FromBin(uint32_t bin, float limitMin, float limitMax);
float computeExposureHistogramEv100(const std::array<uint32_t, kExposureHistogramBins>& bins, float minPercentile,
                                    float maxPercentile, float limitMin, float limitMax);
float adaptExposureEv100(float previousEv100, float targetEv100, float speedDarkToLight, float speedLightToDark,
                         float deltaSeconds);
void sanitizePostProcessSettings(PostProcessSettings& settings);

AntialiasingMode resolveAntialiasingMode(const PostProcessSettings& settings);
const char* exposureModeName(ExposureMode mode);
const char* tonemappingModeName(TonemappingMode mode);
const char* meteringModeName(MeteringMode mode);
const char* antialiasingModeName(AntialiasingMode mode);
std::size_t exposureModeCount();
std::size_t tonemappingModeCount();
std::size_t meteringModeCount();
std::size_t antialiasingModeCount();
ExposureMode exposureModeAt(std::size_t index);
TonemappingMode tonemappingModeAt(std::size_t index);
MeteringMode meteringModeAt(std::size_t index);
AntialiasingMode antialiasingModeAt(std::size_t index);

} // namespace hbg::render
