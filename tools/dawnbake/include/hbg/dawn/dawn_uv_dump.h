#pragma once

#include "hbg/dawn/dawn_atlas.h"

#include "asset/mesh.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hbg::dawn {

struct LightmapUvDumpObject {
    std::string name;
    const asset::Mesh* mesh = nullptr;
    uint32_t atlasIndex = 0;
    LightmapRect rect;
};

bool writeLightmapUvDump(const std::filesystem::path& path,
                         const std::vector<LightmapUvDumpObject>& objects,
                         const std::vector<LightmapAtlasPlan>& atlases, uint32_t imageSize = 512);

} // namespace hbg::dawn
