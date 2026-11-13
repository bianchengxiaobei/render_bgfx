#pragma once

#include <array>
#include <cstdint>

namespace hbg::render {

struct SsgiSettings {
    float intensity = 1.0f;
    float maxDistance = 30.0f;
    int maxSteps = 8;
    int rayCount = 32;
    float falloff = 1.0f;
    float thickness = 0.5f;
    float ambientBias = 0.5f;
    bool halfResolution = true;
    bool enabled = true;
};

void sanitizeSsgiSettings(SsgiSettings& settings);
std::array<uint16_t, 2> ssgiResolution(uint16_t renderWidth, uint16_t renderHeight, bool halfResolution);

} // namespace hbg::render
