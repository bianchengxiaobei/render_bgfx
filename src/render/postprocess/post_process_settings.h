#pragma once

#include <cstddef>
#include <cstdint>

namespace hbg::render {

enum class ExposureMode : uint8_t {
    Fixed,
    Automatic,
};

enum class TonemappingMode : uint8_t {
    None,
    Neutral,
    ACES,
};

struct PostProcessSettings {
    ExposureMode exposureMode = ExposureMode::Fixed;
    float fixedEv100 = -1.2630344f;
    float compensation = 0.0f;
    float limitMin = -1.0f;
    float limitMax = 14.0f;
    float debugExposure = 0.0f;
    TonemappingMode tonemapping = TonemappingMode::ACES;
};

constexpr float kLensAttenuation = 0.65f;
constexpr float kLightMeterCalibrationConstant = 12.5f;

float lensImperfectionExposureScale();
float convertEv100ToExposure(float ev100);
float convertExposureToEv100(float exposure);
float computeEv100FromAverageLuminance(float averageLuminance);
float computeAutoExposureEv100(float averageLuminance, const PostProcessSettings& settings);
float fixedExposureMultiplier(const PostProcessSettings& settings);
void sanitizePostProcessSettings(PostProcessSettings& settings);

const char* exposureModeName(ExposureMode mode);
const char* tonemappingModeName(TonemappingMode mode);
std::size_t exposureModeCount();
std::size_t tonemappingModeCount();
ExposureMode exposureModeAt(std::size_t index);
TonemappingMode tonemappingModeAt(std::size_t index);

} // namespace hbg::render
