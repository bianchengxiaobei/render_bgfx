#include "render/postprocess/ssr_settings.h"

#include <algorithm>

namespace hbg::render {

void sanitizeSsrSettings(SsrSettings& settings) {
    settings.maxDistance = std::clamp(settings.maxDistance, 0.1f, 1000.0f);
    settings.maxSteps = std::clamp(settings.maxSteps, 1, 64);
    settings.thickness = std::clamp(settings.thickness, 0.001f, 10.0f);
    settings.roughnessFade = std::clamp(settings.roughnessFade, 0.0f, 1.0f);
    settings.edgeFade = std::clamp(settings.edgeFade, 0.0f, 0.5f);
    settings.intensity = std::clamp(settings.intensity, 0.0f, 10.0f);
    settings.temporalWeight = std::clamp(settings.temporalWeight, 0.0f, 1.0f);
}

} // namespace hbg::render
