#include "render/postprocess/ssgi_settings.h"

#include <algorithm>

namespace hbg::render {

void sanitizeSsgiSettings(SsgiSettings& settings) {
    settings.intensity = std::clamp(settings.intensity, 0.0f, 8.0f);
    settings.maxDistance = std::clamp(settings.maxDistance, 0.1f, 200.0f);
    settings.maxSteps = std::clamp(settings.maxSteps, 1, 16);
    settings.rayCount = std::clamp(settings.rayCount, 1, 32);
    settings.falloff = std::clamp(settings.falloff, 0.0f, 4.0f);
    settings.thickness = std::clamp(settings.thickness, 0.001f, 10.0f);
    settings.ambientBias = std::clamp(settings.ambientBias, 0.0f, 1.0f);
}

std::array<uint16_t, 2> ssgiResolution(uint16_t renderWidth, uint16_t renderHeight, bool halfResolution) {
    if (renderWidth == 0 || renderHeight == 0) {
        return {1, 1};
    }
    if (!halfResolution) {
        return {renderWidth, renderHeight};
    }
    return {
        static_cast<uint16_t>((renderWidth + 1) / 2),
        static_cast<uint16_t>((renderHeight + 1) / 2),
    };
}

} // namespace hbg::render
