#pragma once

#include <array>
#include <cstdint>

namespace hbg::render {

struct SsaoSettings {
    float radius = 2.0f;
    float intensity = 2.0f;
    float bias = 0.01f;
    float thickness = 2.0f;
    float falloff = 1.0f;
    int stepCount = 6;
    bool halfResolution = true;
};

void sanitizeSsaoSettings(SsaoSettings& settings);
std::array<uint16_t, 2> ssaoResolution(uint16_t renderWidth, uint16_t renderHeight, bool halfResolution);

} // namespace hbg::render
