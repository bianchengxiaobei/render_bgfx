#pragma once

#include "core/bounds.h"
#include "core/math.h"
#include "render/culling/draw_list.h"

#include <array>
#include <cstdint>
#include <vector>

namespace hbg::render {

struct VegetationTile {
    uint64_t key = 0;
    Bounds bounds;
    std::vector<uint32_t> itemIndices;
};

void buildVegetationTiles(const std::vector<DrawItem>& items, float tileSize,
                          std::vector<VegetationTile>& tiles);

std::vector<uint32_t> collectVisibleVegetation(const std::vector<DrawItem>& items,
                                               const std::vector<VegetationTile>& tiles,
                                               const Vec3& cameraPosition,
                                               const std::array<Vec4, 6>& frustumPlanes,
                                               float maxDistance);

} // namespace hbg::render
