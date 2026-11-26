#include "render/postprocess/post_process_settings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace hbg::render {
namespace {

constexpr ExposureMode kExposureModes[] = {
    ExposureMode::Fixed,
    ExposureMode::Automatic,
    ExposureMode::Histogram,
};

constexpr TonemappingMode kTonemappingModes[] = {
    TonemappingMode::None,
    TonemappingMode::Neutral,
    TonemappingMode::ACES,
};

constexpr MeteringMode kMeteringModes[] = {
    MeteringMode::Average,
    MeteringMode::CenterWeighted,
};

constexpr AntialiasingMode kAntialiasingModes[] = {
    AntialiasingMode::None,
    AntialiasingMode::Taa,
    AntialiasingMode::Fxaa,
};

constexpr TaaUpsampleInjection kTaaUpsampleInjections[] = {
    TaaUpsampleInjection::BeforePost,
    TaaUpsampleInjection::AfterDepthOfField,
    TaaUpsampleInjection::AfterPost,
};

constexpr const char* kExposureModeNames[] = {
    "Fixed",
    "Automatic",
    "Histogram",
};

constexpr const char* kTonemappingModeNames[] = {
    "None",
    "Neutral",
    "ACES",
};

constexpr const char* kMeteringModeNames[] = {
    "Average",
    "CenterWeighted",
};

constexpr const char* kAntialiasingModeNames[] = {
    "Off",
    "TAA",
    "FXAA",
};

constexpr const char* kTaaUpsampleInjectionNames[] = {
    "BeforePost",
    "AfterDepthOfField",
    "AfterPost",
};

static_assert(sizeof(kExposureModes) / sizeof(kExposureModes[0]) ==
              sizeof(kExposureModeNames) / sizeof(kExposureModeNames[0]));
static_assert(sizeof(kTonemappingModes) / sizeof(kTonemappingModes[0]) ==
              sizeof(kTonemappingModeNames) / sizeof(kTonemappingModeNames[0]));
static_assert(sizeof(kMeteringModes) / sizeof(kMeteringModes[0]) ==
              sizeof(kMeteringModeNames) / sizeof(kMeteringModeNames[0]));
static_assert(sizeof(kAntialiasingModes) / sizeof(kAntialiasingModes[0]) ==
              sizeof(kAntialiasingModeNames) / sizeof(kAntialiasingModeNames[0]));
static_assert(sizeof(kTaaUpsampleInjections) / sizeof(kTaaUpsampleInjections[0]) ==
              sizeof(kTaaUpsampleInjectionNames) / sizeof(kTaaUpsampleInjectionNames[0]));

float finiteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

Vec2 finiteOr(const Vec2& value, const Vec2& fallback) {
    return Vec2(finiteOr(value.x, fallback.x), finiteOr(value.y, fallback.y));
}

Vec2 clampVec2(const Vec2& value, float minimum, float maximum) {
    return Vec2(std::clamp(value.x, minimum, maximum), std::clamp(value.y, minimum, maximum));
}

Vec3 finiteOr(const Vec3& value, const Vec3& fallback) {
    return Vec3(finiteOr(value.x, fallback.x), finiteOr(value.y, fallback.y), finiteOr(value.z, fallback.z));
}

Vec4 finiteOr(const Vec4& value, const Vec4& fallback) {
    return Vec4(finiteOr(value.x, fallback.x), finiteOr(value.y, fallback.y), finiteOr(value.z, fallback.z),
                finiteOr(value.w, fallback.w));
}

Vec3 clampVec3(const Vec3& value, float minimum, float maximum) {
    return Vec3(std::clamp(value.x, minimum, maximum), std::clamp(value.y, minimum, maximum),
                std::clamp(value.z, minimum, maximum));
}

bool validExposureMode(ExposureMode mode) {
    return mode == ExposureMode::Fixed || mode == ExposureMode::Automatic || mode == ExposureMode::Histogram;
}

bool validTonemappingMode(TonemappingMode mode) {
    return mode == TonemappingMode::None || mode == TonemappingMode::Neutral || mode == TonemappingMode::ACES;
}

bool validMeteringMode(MeteringMode mode) {
    return mode == MeteringMode::Average || mode == MeteringMode::CenterWeighted;
}

bool validTaaUpsampleInjection(TaaUpsampleInjection injection) {
    return injection == TaaUpsampleInjection::BeforePost ||
           injection == TaaUpsampleInjection::AfterDepthOfField ||
           injection == TaaUpsampleInjection::AfterPost;
}

std::string normalizeKey(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (char ch : text) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if (std::isalnum(value) != 0) {
            result.push_back(static_cast<char>(std::tolower(value)));
        }
    }
    return result;
}

Vec4 bloomCurveFromLinear(float threshold) {
    const float clamped = std::max(finiteOr(threshold, 0.0f), 0.0f);
    const float knee = clamped * 0.5f + 1.0e-5f;
    return Vec4(clamped, clamped - knee, knee * 2.0f, 0.25f / knee);
}

} // namespace

float lensImperfectionExposureScale() {
    return 78.0f / (100.0f * kLensAttenuation);
}

float bloomThresholdLinear(float thresholdGamma) {
    return std::pow(std::clamp(finiteOr(thresholdGamma, 0.0f), 0.0f, 100.0f), 2.2f);
}

Vec4 bloomThresholdCurve(float thresholdGamma) {
    return bloomCurveFromLinear(bloomThresholdLinear(thresholdGamma));
}

Vec3 quadraticBloomThreshold(const Vec3& color, float thresholdLinear) {
    const Vec4 curve = bloomCurveFromLinear(thresholdLinear);
    const float brightness = std::max(color.x, std::max(color.y, color.z));
    const float rq = std::clamp(brightness - curve.y, 0.0f, curve.z);
    const float response = std::max(curve.w * rq * rq, brightness - curve.x) / std::max(brightness, 1.0e-4f);
    const float scale = std::max(response, 0.0f);
    return Vec3(color.x * scale, color.y * scale, color.z * scale);
}

float bloomCompositeIntensity(float intensity) {
    return std::exp2(std::clamp(finiteOr(intensity, 0.0f), 0.0f, 1.0f)) - 1.0f;
}

Vec3 normalizedBloomTint(const Vec3& tint) {
    const float luminance = 0.2126f * tint.x + 0.7152f * tint.y + 0.0722f * tint.z;
    return luminance > 0.0f ? Vec3(tint.x / luminance, tint.y / luminance, tint.z / luminance)
                            : Vec3(1.0f, 1.0f, 1.0f);
}

float bloomScatterParam(float scatter) {
    return 0.05f + 0.90f * std::clamp(finiteOr(scatter, 0.5f), 0.0f, 1.0f);
}

uint32_t bloomMipCount(uint16_t renderWidth, uint16_t renderHeight, uint32_t resolution) {
    const float maxSize = static_cast<float>(std::max(renderWidth, renderHeight));
    if (!(maxSize > 0.0f)) {
        return 1;
    }
    const int iterations = static_cast<int>(std::floor(std::log2(maxSize))) - 2 - (resolution >= 4 ? 1 : 0);
    return static_cast<uint32_t>(std::clamp(iterations, 1, static_cast<int>(kMaxBloomMips)));
}

BloomMipSize bloomMipSize(uint16_t renderWidth, uint16_t renderHeight, uint32_t resolution, uint32_t mip) {
    const bool smallViewport = renderWidth < 800 || renderHeight < 450;
    const float baseScale = (smallViewport || resolution < 4) ? 1.0f : 0.5f;
    const float scale = baseScale * std::exp2(-static_cast<float>(mip + 1));
    const auto scaled = [scale](uint16_t value) {
        return static_cast<uint16_t>(std::max(1, static_cast<int>(std::lround(static_cast<float>(value) * scale))));
    };
    return BloomMipSize{scaled(renderWidth), scaled(renderHeight)};
}

float taaBaseHistoryWeight(uint32_t quality) {
    switch (std::clamp<uint32_t>(quality, 1, 5)) {
    case 1:
        return 0.80f;
    case 2:
        return 0.84f;
    case 4:
        return 0.90f;
    case 5:
        return 0.925f;
    case 3:
    default:
        return 0.875f;
    }
}

Vec2 taaReprojectUv(const Vec2& uv, const Vec2& motion, bool flipY) {
    const float motionY = flipY ? -motion.y : motion.y;
    return Vec2(uv.x - motion.x, uv.y - motionY);
}

Vec3 taaClampHistory(const Vec3& history, const Vec3& minimum, const Vec3& maximum) {
    return Vec3(std::clamp(history.x, minimum.x, maximum.x), std::clamp(history.y, minimum.y, maximum.y),
                std::clamp(history.z, minimum.z, maximum.z));
}

float taaDepthWeight(float currentDepth, float historyDepth) {
    const float current = std::clamp(finiteOr(currentDepth, 1.0f), 0.0f, 1.0f);
    const float history = std::clamp(finiteOr(historyDepth, current), 0.0f, 1.0f);
    const float tolerance = 0.002f + current * 0.02f;
    return 1.0f - std::clamp(std::abs(current - history) / tolerance, 0.0f, 1.0f);
}

float taaLumaWeight(float currentLuma, float historyLuma) {
    const float current = std::max(finiteOr(currentLuma, 0.0f), 0.0f);
    const float history = std::max(finiteOr(historyLuma, current), 0.0f);
    const float relativeDifference = std::abs(current - history) / std::max(current, 0.01f);
    return 1.0f / (1.0f + relativeDifference * 4.0f);
}

float taaHistoryWeight(float baseWeight, float depthWeight, float lumaWeight, float motionPixels,
                       float motionVectorRejection) {
    const float base = std::clamp(finiteOr(baseWeight, 0.0f), 0.0f, 1.0f);
    const float depth = std::clamp(finiteOr(depthWeight, 1.0f), 0.0f, 1.0f);
    const float luma = std::clamp(finiteOr(lumaWeight, 1.0f), 0.0f, 1.0f);
    const float rejection = std::clamp(finiteOr(motionVectorRejection, 0.0f), 0.0f, 1.0f);
    const float motion = std::clamp(std::max(finiteOr(motionPixels, 0.0f), 0.0f) * rejection / kTaaMotionRejectionPixels,
                                    0.0f, 1.0f);
    return std::clamp(base * depth * luma * (1.0f - motion), 0.0f, 0.98f);
}

float dofNormalizedCoc(float linearEyeDepth, const DepthOfFieldSettings& settings) {
    const float depth = std::max(finiteOr(linearEyeDepth, 0.0f), 0.0f);
    const float nearEnd = std::max(finiteOr(settings.nearFocusEnd, 0.0f), 0.0f);
    const float nearStart = std::clamp(finiteOr(settings.nearFocusStart, 0.0f), 0.0f,
                                       std::max(nearEnd - 1.0e-5f, 0.0f));
    const float farStart = std::max(finiteOr(settings.farFocusStart, nearEnd), nearEnd);
    const float farEnd = std::max(finiteOr(settings.farFocusEnd, farStart + 1.0f), farStart + 1.0e-5f);

    const float nearCoc = std::clamp((depth - nearEnd) / std::min(nearStart - nearEnd, -1.0e-5f), 0.0f, 1.0f);
    const float farCoc = std::clamp((depth - farStart) / std::max(farEnd - farStart, 1.0e-5f), 0.0f, 1.0f);
    return farCoc - nearCoc;
}

float dofCocPixels(float linearEyeDepth, const DepthOfFieldSettings& settings, uint16_t renderHeight) {
    const float normalized = dofNormalizedCoc(linearEyeDepth, settings);
    const float maxBlur = normalized < 0.0f ? std::max(finiteOr(settings.nearMaxBlur, 0.0f), 0.0f)
                                            : std::max(finiteOr(settings.farMaxBlur, 0.0f), 0.0f);
    return normalized * maxBlur * dofBlurScale(renderHeight);
}

float dofResolutionScale(uint32_t resolution) {
    switch (resolution) {
    case 1:
        return 1.0f;
    case 4:
        return 0.25f;
    case 2:
    default:
        return 0.5f;
    }
}

float dofBlurScale(uint16_t renderHeight) {
    return std::max(static_cast<float>(renderHeight), 1.0f) / 1080.0f;
}

uint32_t dofGatherTapCount(bool highQuality) {
    return highQuality ? 12u : 8u;
}

float motionBlurScreenMagnitude(uint16_t width, uint16_t height) {
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    return std::sqrt(w * w + h * h);
}

float motionBlurVelocityPixels(const Vec2& motionNdc, uint16_t width, uint16_t height) {
    const Vec2 motion = finiteOr(motionNdc, Vec2(0.0f, 0.0f));
    return std::sqrt(motion.x * motion.x + motion.y * motion.y) * motionBlurScreenMagnitude(width, height);
}

Vec2 motionBlurClampVelocity(const Vec2& motionNdc, const MotionBlurSettings& settings, uint16_t width,
                             uint16_t height) {
    const float screenMagnitude = motionBlurScreenMagnitude(width, height);
    if (!(screenMagnitude > 0.0f) || !(settings.maximumVelocity > 0.0f)) {
        return Vec2(0.0f, 0.0f);
    }
    const float intensity = std::max(finiteOr(settings.intensity, 0.0f), 0.0f);
    const Vec2 input = finiteOr(motionNdc, Vec2(0.0f, 0.0f));
    Vec2 scaled(input.x * intensity, input.y * intensity);
    const float maxNdc = std::max(settings.maximumVelocity, 0.0f) / screenMagnitude;
    const float magnitude = std::sqrt(scaled.x * scaled.x + scaled.y * scaled.y);
    if (magnitude > maxNdc && magnitude > 1.0e-8f) {
        const float scale = maxNdc / magnitude;
        scaled = Vec2(scaled.x * scale, scaled.y * scale);
    }
    return scaled;
}

uint32_t motionBlurSampleCount(float velocityPixels, uint32_t maximumSamples) {
    if (!(velocityPixels > 1.0f)) {
        return 0u;
    }
    uint32_t count = static_cast<uint32_t>(std::floor(velocityPixels));
    if (count < 2u) {
        count = 2u;
    }
    if ((count & 1u) != 0u) {
        ++count;
    }
    const uint32_t limit = std::max(maximumSamples & ~1u, 2u);
    return std::min(count, limit);
}

Vec2 motionBlurDirectionUv(const Vec2& motionNdc, bool flipY) {
    const Vec2 motion = finiteOr(motionNdc, Vec2(0.0f, 0.0f));
    Vec2 direction(motion.x * 0.5f, motion.y * 0.5f);
    if (flipY) {
        direction.y = -direction.y;
    }
    return direction;
}

Vec2 motionBlurSampleUv(const Vec2& centerUv, const Vec2& motionNdc, float offset, bool flipY) {
    const Vec2 direction = motionBlurDirectionUv(motionNdc, flipY);
    const float amount = finiteOr(offset, 0.0f);
    return Vec2(centerUv.x + direction.x * amount, centerUv.y + direction.y * amount);
}

float motionBlurDepthWeight(float centerDepth, float sampleDepth, float depthComparisonExtent) {
    const float center = std::max(finiteOr(centerDepth, 0.0f), 0.0f);
    const float sample = std::max(finiteOr(sampleDepth, center), 0.0f);
    const float scale = std::max(finiteOr(depthComparisonExtent, 0.0f), 0.0f);
    return std::clamp(0.5f - scale * (sample - center), 0.0f, 1.0f);
}

float convertEv100ToExposure(float ev100) {
    if (!std::isfinite(ev100)) {
        return 1.0f;
    }
    return 1.0f / (lensImperfectionExposureScale() * std::exp2(ev100));
}

float convertExposureToEv100(float exposure) {
    if (!std::isfinite(exposure) || exposure <= 0.0f) {
        return 0.0f;
    }
    return std::log2(1.0f / (lensImperfectionExposureScale() * exposure));
}

float computeEv100FromAverageLuminance(float averageLuminance) {
    const float luminance = std::max(finiteOr(averageLuminance, 0.0f), 1.0e-4f);
    return std::log2(luminance * 100.0f / kLightMeterCalibrationConstant);
}

float computeAutoExposureEv100(float averageLuminance, const PostProcessSettings& settings) {
    const float targetEv100 =
        computeEv100FromAverageLuminance(averageLuminance) - settings.compensation - settings.debugExposure;
    return std::clamp(targetEv100, settings.limitMin, settings.limitMax);
}

float fixedExposureMultiplier(const PostProcessSettings& settings) {
    return convertEv100ToExposure(settings.fixedEv100 - settings.compensation - settings.debugExposure);
}

float adaptExposureEv100(float previousEv100, float targetEv100, float speedDarkToLight, float speedLightToDark,
                         float deltaSeconds) {
    const float previous = finiteOr(previousEv100, 0.0f);
    const float target = finiteOr(targetEv100, previous);
    const float delta = target - previous;
    const float speed = delta > 0.0f ? std::max(finiteOr(speedDarkToLight, 0.0f), 0.0f)
                                     : std::max(finiteOr(speedLightToDark, 0.0f), 0.0f);
    const float time = std::max(finiteOr(deltaSeconds, 0.0f), 0.0f);
    return previous + delta * (1.0f - std::exp2(-time * speed));
}

float computeAdaptedExposureEv100(float averageLuminance, float previousEv100, bool historyValid, float deltaSeconds,
                                  const PostProcessSettings& settings) {
    float targetEv100 =
        computeEv100FromAverageLuminance(averageLuminance) - settings.compensation - settings.debugExposure;
    if (historyValid) {
        targetEv100 = adaptExposureEv100(previousEv100, targetEv100, settings.adaptationSpeedDarkToLight,
                                         settings.adaptationSpeedLightToDark, deltaSeconds);
    }
    return std::clamp(targetEv100, settings.limitMin, settings.limitMax);
}

uint32_t packExposureHistogramWeight(float weight) {
    const float scaled =
        std::clamp(finiteOr(weight, 0.0f), 0.0f, 1.0f) * static_cast<float>(kExposureHistogramWeightScale);
    return static_cast<uint32_t>(std::lround(scaled));
}

float unpackExposureHistogramWeight(uint32_t packed) {
    return static_cast<float>(packed) / static_cast<float>(kExposureHistogramWeightScale);
}

uint32_t exposureHistogramBinFromEv100(float ev100, float limitMin, float limitMax) {
    const float minimum = finiteOr(limitMin, -1.0f);
    const float maximum = finiteOr(limitMax, 14.0f);
    const float range = maximum - minimum;
    if (!(range > 0.0f)) {
        return 0;
    }
    const float fraction = std::clamp((finiteOr(ev100, minimum) - minimum) / range, 0.0f, 1.0f);
    return static_cast<uint32_t>(fraction * static_cast<float>(kExposureHistogramBins - 1));
}

float exposureHistogramEv100FromBin(uint32_t bin, float limitMin, float limitMax) {
    const float minimum = finiteOr(limitMin, -1.0f);
    const float maximum = finiteOr(limitMax, 14.0f);
    const uint32_t clampedBin = std::min(bin, kExposureHistogramBins - 1);
    return minimum + (maximum - minimum) * static_cast<float>(clampedBin) /
                         static_cast<float>(kExposureHistogramBins - 1);
}

float computeExposureHistogramEv100(const std::array<uint32_t, kExposureHistogramBins>& bins, float minPercentile,
                                    float maxPercentile, float limitMin, float limitMax) {
    std::array<float, kExposureHistogramBins> trimmed{};
    float total = 0.0f;
    for (std::size_t index = 0; index < bins.size(); ++index) {
        trimmed[index] = unpackExposureHistogramWeight(bins[index]);
        total += trimmed[index];
    }
    if (!(total > 0.0f)) {
        return 0.0f;
    }

    const float clampedMin = std::clamp(finiteOr(minPercentile, 0.0f), 0.0f, 1.0f);
    const float clampedMax = std::clamp(finiteOr(maxPercentile, 1.0f), 0.0f, 1.0f);
    float lowTrim = total * std::min(clampedMin, clampedMax);
    float highTrim = total * (1.0f - std::max(clampedMin, clampedMax));

    for (std::size_t index = 0; index < trimmed.size() && lowTrim > 0.0f; ++index) {
        const float cut = std::min(trimmed[index], lowTrim);
        trimmed[index] -= cut;
        lowTrim -= cut;
    }
    for (std::size_t index = trimmed.size(); index > 0 && highTrim > 0.0f; --index) {
        float& value = trimmed[index - 1];
        const float cut = std::min(value, highTrim);
        value -= cut;
        highTrim -= cut;
    }

    float weightedEv = 0.0f;
    float weight = 0.0f;
    for (std::size_t index = 0; index < trimmed.size(); ++index) {
        const float ev = exposureHistogramEv100FromBin(static_cast<uint32_t>(index), limitMin, limitMax);
        weightedEv += ev * trimmed[index];
        weight += trimmed[index];
    }
    return weight > 0.0f ? weightedEv / weight : 0.0f;
}

void sanitizePostProcessSettings(PostProcessSettings& settings) {
    if (!validExposureMode(settings.exposureMode)) {
        settings.exposureMode = ExposureMode::Fixed;
    }
    if (!validTonemappingMode(settings.tonemapping)) {
        settings.tonemapping = TonemappingMode::ACES;
    }
    if (!validMeteringMode(settings.metering)) {
        settings.metering = MeteringMode::CenterWeighted;
    }

    settings.fixedEv100 = std::clamp(finiteOr(settings.fixedEv100, -1.2630344f), -20.0f, 20.0f);
    settings.compensation = std::clamp(finiteOr(settings.compensation, 0.0f), -20.0f, 20.0f);
    settings.debugExposure = std::clamp(finiteOr(settings.debugExposure, 0.0f), -20.0f, 20.0f);
    settings.limitMin = std::clamp(finiteOr(settings.limitMin, -1.0f), -20.0f, 30.0f);
    settings.limitMax = std::clamp(finiteOr(settings.limitMax, 14.0f), -20.0f, 30.0f);
    if (settings.limitMin > settings.limitMax) {
        std::swap(settings.limitMin, settings.limitMax);
    }

    settings.adaptationSpeedDarkToLight =
        std::clamp(finiteOr(settings.adaptationSpeedDarkToLight, 1.0f), 0.0f, 100.0f);
    settings.adaptationSpeedLightToDark =
        std::clamp(finiteOr(settings.adaptationSpeedLightToDark, 1.0f), 0.0f, 100.0f);
    settings.histogramMinPercentile = std::clamp(finiteOr(settings.histogramMinPercentile, 0.40f), 0.0f, 1.0f);
    settings.histogramMaxPercentile = std::clamp(finiteOr(settings.histogramMaxPercentile, 0.90f), 0.0f, 1.0f);
    if (settings.histogramMinPercentile > settings.histogramMaxPercentile) {
        std::swap(settings.histogramMinPercentile, settings.histogramMaxPercentile);
    }
    settings.proceduralCenter = clampVec3(finiteOr(settings.proceduralCenter, Vec3(0.5f, 0.5f, 0.0f)), 0.0f, 1.0f);
    settings.proceduralRadii = clampVec3(finiteOr(settings.proceduralRadii, Vec3(0.5f, 0.5f, 0.0f)), 0.0f, 1.0f);
    settings.proceduralSoftness = std::clamp(finiteOr(settings.proceduralSoftness, 0.5f), 0.0f, 1.0f);

    BloomSettings& bloom = settings.bloom;
    bloom.threshold = std::clamp(finiteOr(bloom.threshold, 0.0f), 0.0f, 100.0f);
    bloom.intensity = std::clamp(finiteOr(bloom.intensity, 0.0f), 0.0f, 1.0f);
    bloom.scatter = std::clamp(finiteOr(bloom.scatter, 0.7f), 0.0f, 1.0f);
    bloom.tint = clampVec3(finiteOr(bloom.tint, Vec3(1.0f, 1.0f, 1.0f)), 0.0f, 100.0f);
    bloom.resolution = bloom.resolution >= 4u ? 4u : 2u;

    ColorGradingSettings& grading = settings.colorGrading;
    grading.postExposure = std::clamp(finiteOr(grading.postExposure, 0.0f), -20.0f, 20.0f);
    grading.contrast = std::clamp(finiteOr(grading.contrast, 0.0f), -100.0f, 100.0f);
    grading.colorFilter = clampVec3(finiteOr(grading.colorFilter, Vec3(1.0f, 1.0f, 1.0f)), 0.0f, 100.0f);
    grading.hueShift = std::clamp(finiteOr(grading.hueShift, 0.0f), -180.0f, 180.0f);
    grading.saturation = std::clamp(finiteOr(grading.saturation, 0.0f), -100.0f, 100.0f);
    grading.temperature = std::clamp(finiteOr(grading.temperature, 0.0f), -100.0f, 100.0f);
    grading.tint = std::clamp(finiteOr(grading.tint, 0.0f), -100.0f, 100.0f);
    grading.channelMixerRed = finiteOr(grading.channelMixerRed, Vec3(1.0f, 0.0f, 0.0f));
    grading.channelMixerGreen = finiteOr(grading.channelMixerGreen, Vec3(0.0f, 1.0f, 0.0f));
    grading.channelMixerBlue = finiteOr(grading.channelMixerBlue, Vec3(0.0f, 0.0f, 1.0f));
    grading.lift = finiteOr(grading.lift, Vec3(0.0f, 0.0f, 0.0f));
    grading.gamma = clampVec3(finiteOr(grading.gamma, Vec3(1.0f, 1.0f, 1.0f)), 0.01f, 10.0f);
    grading.gain = finiteOr(grading.gain, Vec3(1.0f, 1.0f, 1.0f));
    grading.shadows = finiteOr(grading.shadows, Vec3(1.0f, 1.0f, 1.0f));
    grading.midtones = finiteOr(grading.midtones, Vec3(1.0f, 1.0f, 1.0f));
    grading.highlights = finiteOr(grading.highlights, Vec3(1.0f, 1.0f, 1.0f));
    grading.shadowsMidtonesHighlightsLimits =
        finiteOr(grading.shadowsMidtonesHighlightsLimits, Vec4(0.0f, 0.5f, 0.5f, 1.0f));
    grading.splitShadows = finiteOr(grading.splitShadows, Vec3(0.5f, 0.5f, 0.5f));
    grading.splitHighlights = finiteOr(grading.splitHighlights, Vec3(0.5f, 0.5f, 0.5f));
    grading.splitBalance = std::clamp(finiteOr(grading.splitBalance, 0.0f), -1.0f, 1.0f);

    VignetteSettings& vignette = settings.vignette;
    vignette.center = clampVec2(finiteOr(vignette.center, Vec2(0.5f, 0.5f)), 0.0f, 1.0f);
    vignette.intensity = std::clamp(finiteOr(vignette.intensity, 0.0f), 0.0f, 1.0f);
    vignette.smoothness = std::clamp(finiteOr(vignette.smoothness, 0.2f), 0.01f, 1.0f);
    vignette.roundness = std::clamp(finiteOr(vignette.roundness, 1.0f), 0.0f, 1.0f);
    vignette.color = clampVec3(finiteOr(vignette.color, Vec3(0.0f, 0.0f, 0.0f)), 0.0f, 1.0f);
    vignette.opacity = std::clamp(finiteOr(vignette.opacity, 1.0f), 0.0f, 1.0f);

    TaaSettings& taa = settings.taa;
    if (!validTaaUpsampleInjection(taa.upscaleInjection)) {
        taa.upscaleInjection = TaaUpsampleInjection::BeforePost;
    }
    taa.quality = std::clamp(taa.quality, 1u, 5u);
    taa.motionVectorRejection = std::clamp(finiteOr(taa.motionVectorRejection, 0.1f), 0.0f, 1.0f);
    taa.velocityRejection = std::clamp(finiteOr(taa.velocityRejection, 0.0f), 0.0f, 1.0f);
    taa.sharpening = std::clamp(finiteOr(taa.sharpening, 0.0f), 0.0f, 1.0f);
    taa.jitterScale = std::clamp(finiteOr(taa.jitterScale, 1.0f), 0.0f, 1.0f);

    DepthOfFieldSettings& dof = settings.depthOfField;
    dof.focusDistance = std::max(finiteOr(dof.focusDistance, 10.0f), 0.1f);
    dof.nearFocusEnd = std::max(finiteOr(dof.nearFocusEnd, 4.0f), 0.0f);
    dof.nearFocusStart = std::clamp(finiteOr(dof.nearFocusStart, 0.0f), 0.0f, dof.nearFocusEnd);
    dof.farFocusStart = std::max(finiteOr(dof.farFocusStart, 10.0f), dof.nearFocusEnd);
    dof.farFocusEnd = std::max(finiteOr(dof.farFocusEnd, 20.0f), dof.farFocusStart + 1.0e-5f);
    dof.nearMaxBlur = std::clamp(finiteOr(dof.nearMaxBlur, 4.0f), 0.0f, 8.0f);
    dof.farMaxBlur = std::clamp(finiteOr(dof.farMaxBlur, 8.0f), 0.0f, 16.0f);
    dof.resolution = dof.resolution == 1u || dof.resolution == 4u ? dof.resolution : 2u;

    MotionBlurSettings& motion = settings.motionBlur;
    motion.intensity = std::clamp(finiteOr(motion.intensity, 0.5f), 0.0f, 2.0f);
    motion.maximumVelocity = std::clamp(finiteOr(motion.maximumVelocity, 200.0f), 0.0f, 1500.0f);
    motion.minimumVelocity = std::clamp(finiteOr(motion.minimumVelocity, 2.0f), 0.0f, 64.0f);
    motion.minimumVelocity = std::min(motion.minimumVelocity, motion.maximumVelocity);
    motion.sampleCount = std::clamp(motion.sampleCount, 2u, 32u);
    if ((motion.sampleCount & 1u) != 0u) {
        motion.sampleCount = std::min(motion.sampleCount + 1u, 32u);
    }
    motion.depthComparisonExtent = std::clamp(finiteOr(motion.depthComparisonExtent, 1.0f), 0.0f, 20.0f);

    if (settings.taa.enabled) {
        settings.fxaa.enabled = false;
    } else if (settings.fxaa.enabled) {
        settings.taa.enabled = false;
    }
}

AntialiasingMode resolveAntialiasingMode(const PostProcessSettings& settings) {
    if (settings.taa.enabled) {
        return AntialiasingMode::Taa;
    }
    return settings.fxaa.enabled ? AntialiasingMode::Fxaa : AntialiasingMode::None;
}

const char* exposureModeName(ExposureMode mode) {
    switch (mode) {
    case ExposureMode::Automatic:
        return kExposureModeNames[1];
    case ExposureMode::Histogram:
        return kExposureModeNames[2];
    case ExposureMode::Fixed:
    default:
        return kExposureModeNames[0];
    }
}

const char* tonemappingModeName(TonemappingMode mode) {
    switch (mode) {
    case TonemappingMode::Neutral:
        return kTonemappingModeNames[1];
    case TonemappingMode::ACES:
        return kTonemappingModeNames[2];
    case TonemappingMode::None:
    default:
        return kTonemappingModeNames[0];
    }
}

const char* meteringModeName(MeteringMode mode) {
    return mode == MeteringMode::Average ? kMeteringModeNames[0] : kMeteringModeNames[1];
}

const char* antialiasingModeName(AntialiasingMode mode) {
    switch (mode) {
    case AntialiasingMode::Taa:
        return kAntialiasingModeNames[1];
    case AntialiasingMode::Fxaa:
        return kAntialiasingModeNames[2];
    case AntialiasingMode::None:
    default:
        return kAntialiasingModeNames[0];
    }
}

const char* taaUpsampleInjectionName(TaaUpsampleInjection injection) {
    switch (injection) {
    case TaaUpsampleInjection::AfterDepthOfField:
        return kTaaUpsampleInjectionNames[1];
    case TaaUpsampleInjection::AfterPost:
        return kTaaUpsampleInjectionNames[2];
    case TaaUpsampleInjection::BeforePost:
    default:
        return kTaaUpsampleInjectionNames[0];
    }
}

std::size_t exposureModeCount() {
    return sizeof(kExposureModes) / sizeof(kExposureModes[0]);
}

std::size_t tonemappingModeCount() {
    return sizeof(kTonemappingModes) / sizeof(kTonemappingModes[0]);
}

std::size_t meteringModeCount() {
    return sizeof(kMeteringModes) / sizeof(kMeteringModes[0]);
}

std::size_t antialiasingModeCount() {
    return sizeof(kAntialiasingModes) / sizeof(kAntialiasingModes[0]);
}

std::size_t taaUpsampleInjectionCount() {
    return sizeof(kTaaUpsampleInjections) / sizeof(kTaaUpsampleInjections[0]);
}

ExposureMode exposureModeAt(std::size_t index) {
    return index < exposureModeCount() ? kExposureModes[index] : ExposureMode::Fixed;
}

TonemappingMode tonemappingModeAt(std::size_t index) {
    return index < tonemappingModeCount() ? kTonemappingModes[index] : TonemappingMode::None;
}

MeteringMode meteringModeAt(std::size_t index) {
    return index < meteringModeCount() ? kMeteringModes[index] : MeteringMode::CenterWeighted;
}

AntialiasingMode antialiasingModeAt(std::size_t index) {
    return index < antialiasingModeCount() ? kAntialiasingModes[index] : AntialiasingMode::None;
}

TaaUpsampleInjection taaUpsampleInjectionAt(std::size_t index) {
    return index < taaUpsampleInjectionCount() ? kTaaUpsampleInjections[index]
                                               : TaaUpsampleInjection::BeforePost;
}

bool parseTaaUpsampleInjection(std::string_view text, TaaUpsampleInjection& injection) {
    const std::string key = normalizeKey(text);
    if (key == "beforepost" || key == "beforepostprocess") {
        injection = TaaUpsampleInjection::BeforePost;
        return true;
    }
    if (key == "afterdepthoffield" || key == "afterdof") {
        injection = TaaUpsampleInjection::AfterDepthOfField;
        return true;
    }
    if (key == "afterpost" || key == "afterpostprocess") {
        injection = TaaUpsampleInjection::AfterPost;
        return true;
    }
    return false;
}

} // namespace hbg::render
