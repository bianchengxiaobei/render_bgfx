#pragma once

#include "hbg/dawn/dawn_unwrap.h"

#include <cstdint>
#include <vector>

namespace hbg::dawn {

struct LightmapSizeSettings {
    float texelsPerUnit = 6.0f;
    uint32_t maxLightmapSize = 1024;
    bool padding = true;
};

struct LightmapRect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t size = 0;
};

struct LightmapAtlasLayout {
    uint32_t size = 0;
    std::vector<LightmapRect> rects;
};

struct LightmapPackingSettings {
    uint32_t atlasSize = 2048;
    uint32_t maxAtlases = 2;
    uint32_t padding = 2;
    float minScale = 0.5f;
};

struct LightmapPlacement {
    uint32_t atlasIndex = 0;
    LightmapRect rect;
    float scale = 1.0f;
};

struct LightmapAtlasPlan {
    uint32_t size = 0;
    float scale = 1.0f;
};

struct LightmapPackingResult {
    std::vector<LightmapAtlasPlan> atlases;
    std::vector<LightmapPlacement> placements;
};

int calculateLightmapSize(const LightmapUvLayout& layout, float scaleInLightmap, int lodIndex,
                          const LightmapSizeSettings& settings);

bool packLightmapSquares(const std::vector<uint32_t>& sizes, uint32_t atlasSize, uint32_t padding,
                         LightmapAtlasLayout& outLayout);

bool packLightmapAtlases(const std::vector<uint32_t>& sizes, const LightmapPackingSettings& settings,
                         LightmapPackingResult& outResult);

} // namespace hbg::dawn
