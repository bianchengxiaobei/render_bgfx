#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace hbg::dawn {

struct DawnLightmapConfig {
    bool uv2VFlip = false;
    float texelsPerUnit = 6.0f;
    uint32_t maxLightmapSize = 1024;
    uint32_t atlasSize = 2048;
    uint32_t taskPackSize = 512;
    bool splitLightmaps = true;
    uint32_t maxAtlases = 2;
    uint32_t padding = 2;
    uint32_t dilationIterations = 4;
    bool directional = true;
    std::string format = "rgba16f";
    uint32_t layers = 3;
};

struct DawnLightingConfig {
    std::string colorSpace = "linear";
    float directionalLightmapScale = 1.0f;
    float lightUnitScale = 1.0f;
    std::string pointFalloff = "inverse-square";
    bool lightPowerAffectsRadiance = false;
};

struct DawnConfig {
    int version = 1;
    DawnLightmapConfig lightmap;
    DawnLightingConfig lighting;
};

bool parseDawnConfig(std::string_view json, DawnConfig& config, std::string& error);
bool loadDawnConfig(const std::filesystem::path& path, DawnConfig& config, std::string& error);

} // namespace hbg::dawn
