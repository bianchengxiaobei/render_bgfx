#include "render/postprocess/post_process_settings.h"

#include <algorithm>
#include <cmath>

namespace hbg::render {
namespace {

constexpr ExposureMode kExposureModes[] = {
    ExposureMode::Fixed,
    ExposureMode::Automatic,
};

constexpr TonemappingMode kTonemappingModes[] = {
    TonemappingMode::None,
    TonemappingMode::Neutral,
    TonemappingMode::ACES,
};

constexpr const char* kExposureModeNames[] = {
    "Fixed",
    "Automatic",
};

constexpr const char* kTonemappingModeNames[] = {
    "None",
    "Neutral",
    "ACES",
};

static_assert(sizeof(kExposureModes) / sizeof(kExposureModes[0]) ==
              sizeof(kExposureModeNames) / sizeof(kExposureModeNames[0]));
static_assert(sizeof(kTonemappingModes) / sizeof(kTonemappingModes[0]) ==
              sizeof(kTonemappingModeNames) / sizeof(kTonemappingModeNames[0]));

float finiteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

} // namespace

float lensImperfectionExposureScale() {
    return 78.0f / (100.0f * kLensAttenuation);
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

void sanitizePostProcessSettings(PostProcessSettings& settings) {
    settings.fixedEv100 = std::clamp(finiteOr(settings.fixedEv100, -1.2630344f), -20.0f, 20.0f);
    settings.compensation = std::clamp(finiteOr(settings.compensation, 0.0f), -20.0f, 20.0f);
    settings.debugExposure = std::clamp(finiteOr(settings.debugExposure, 0.0f), -20.0f, 20.0f);
    settings.limitMin = std::clamp(finiteOr(settings.limitMin, -1.0f), -20.0f, 30.0f);
    settings.limitMax = std::clamp(finiteOr(settings.limitMax, 14.0f), -20.0f, 30.0f);
    if (settings.limitMin > settings.limitMax) {
        std::swap(settings.limitMin, settings.limitMax);
    }
}

const char* exposureModeName(ExposureMode mode) {
    return mode == ExposureMode::Automatic ? kExposureModeNames[1] : kExposureModeNames[0];
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

std::size_t exposureModeCount() {
    return sizeof(kExposureModes) / sizeof(kExposureModes[0]);
}

std::size_t tonemappingModeCount() {
    return sizeof(kTonemappingModes) / sizeof(kTonemappingModes[0]);
}

ExposureMode exposureModeAt(std::size_t index) {
    return index < exposureModeCount() ? kExposureModes[index] : ExposureMode::Fixed;
}

TonemappingMode tonemappingModeAt(std::size_t index) {
    return index < tonemappingModeCount() ? kTonemappingModes[index] : TonemappingMode::None;
}

} // namespace hbg::render
