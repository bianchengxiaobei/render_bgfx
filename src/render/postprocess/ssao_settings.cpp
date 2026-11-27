#include "render/postprocess/ssao_settings.h"

#include <algorithm>

namespace hbg::render {

void sanitizeSsaoSettings(SsaoSettings& settings) {
    settings.radius = std::clamp(settings.radius, 0.01f, 100.0f);
    settings.intensity = std::clamp(settings.intensity, 0.0f, 4.0f);
    settings.bias = std::clamp(settings.bias, 0.0f, 10.0f);
    settings.thickness = std::clamp(settings.thickness, 0.001f, 100.0f);
    settings.falloff = std::clamp(settings.falloff, 0.0f, 1.0f);
    settings.stepCount = std::clamp(settings.stepCount, 2, 16);
    settings.stepCount &= ~1;
}

std::array<uint16_t, 2> ssaoResolution(uint16_t renderWidth, uint16_t renderHeight, bool halfResolution) {
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
