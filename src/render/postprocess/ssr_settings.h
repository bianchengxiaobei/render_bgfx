#pragma once

namespace hbg::render {

struct SsrSettings {
    float maxDistance = 40.0f;
    int maxSteps = 32;
    float thickness = 0.6f;
    float roughnessFade = 0.25f;
    float edgeFade = 0.1f;
    float intensity = 1.0f;
    float temporalWeight = 0.1f;
    bool temporalAccumulation = false;
    bool enabled = true;
};

void sanitizeSsrSettings(SsrSettings& settings);

} // namespace hbg::render
