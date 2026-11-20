#pragma once

#include "hbg/dawn/lightmap_atlas.h"

#include <filesystem>
#include <string>

namespace hbg::dawn {

bool writeLightmapAtlasDds(const LightmapAtlasImageSet& images, const std::filesystem::path& directory,
                           std::string* error);

bool verifyLightmapAtlasDds(const Lightmap2DOutput& output, uint32_t atlasIndex, uint32_t targetX, uint32_t targetY,
                            uint32_t atlasSize, const std::filesystem::path& directory, std::string* error);

} // namespace hbg::dawn
