#pragma once

#include "asset/mesh.h"

#include <cstdint>

namespace hbg::render {

struct WaterScreenGridDesc {
    int width = 0;
    int height = 0;
};

struct WaterScreenGridSource {
    Bounds bounds;
    float baseHeight = 0.0f;
    Vec2 uvMin{0.0f, 0.0f};
    Vec2 uvMax{1.0f, 1.0f};
    uint32_t materialIndex = 0;
};

// Rebuilds the water mesh as the screen-space grid used by Proland's
// DrawOceanFFTTask (8 px cells with a 1.25 overscan).
bool configureWaterScreenGrid(asset::Mesh& waterMesh, uint16_t viewportWidth, uint16_t viewportHeight,
                              WaterScreenGridDesc* desc = nullptr,
                              const WaterScreenGridSource* source = nullptr);

} // namespace hbg::render
