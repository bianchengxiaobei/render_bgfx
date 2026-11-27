#pragma once

#include "core/math.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace hbg::render {

struct VolumetricFogSettings {
    bool enabled = false;
    float meanFreePath = 400.0f;
    Vec3 albedo{1.0f, 1.0f, 1.0f};
    float baseHeight = 0.0f;
    float maximumHeight = 50.0f;
    float anisotropy = 0.0f;
    float depthExtent = 64.0f;
    uint32_t stepCount = 16;
    bool directionalShadows = true;
    float globalLightProbeDimmer = 1.0f;
    float resolutionScale = 0.5f;
    bool temporal = true;
    float temporalHistoryWeight = 0.85f;
    bool temporalJitter = true;
    bool punctualLights = true;
    bool punctualShadows = true;
    uint32_t maxPunctualLights = 8;
    bool localVolumes = true;

    bool active() const;
    float scaleHeight() const;
    float extinction() const;
    Vec3 scattering() const;
};

void sanitizeVolumetricFogSettings(VolumetricFogSettings& settings);
bool parseVolumetricFogSettings(std::string_view json, VolumetricFogSettings& settings, std::string& error);
bool loadVolumetricFogSettings(const std::string& path, VolumetricFogSettings& settings, std::string& error);

} // namespace hbg::render
