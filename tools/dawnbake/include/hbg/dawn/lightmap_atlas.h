#pragma once

#include "hbg/dawn/dawn_protocol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hbg::dawn {

struct LightmapAtlasPlacement {
    uint32_t atlasIndex = 0;
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct LightmapAtlasResult {
    const Lightmap2DOutput* output = nullptr;
    LightmapAtlasPlacement placement;
};

struct LightmapAtlasImage {
    uint32_t size = 0;
    std::array<std::vector<uint16_t>, 3> sh;
    std::vector<uint8_t> mapped;
};

struct LightmapAtlasImageSet {
    std::vector<LightmapAtlasImage> atlases;
    std::size_t texelsCopied = 0;
};

struct LightmapAtlasOptions {
    uint32_t dilationIterations = 4;
};

Float4 lightmapAtlasTexel(const Lightmap2DSample& sample, std::size_t channel);

bool buildLightmapAtlasImages(const SceneInfo& scene, const BakingJobInputs& jobs,
                              const std::vector<LightmapAtlasResult>& results, const std::vector<uint32_t>& atlasSizes,
                              LightmapAtlasImageSet* images, std::string* error,
                              const LightmapAtlasOptions& options = {});

} // namespace hbg::dawn
