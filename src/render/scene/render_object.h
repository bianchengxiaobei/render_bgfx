#pragma once

#include "core/bounds.h"
#include "core/math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hbg::render {

struct RenderObject {
    std::string name;
    int nodeIndex = -1;
    int skinIndex = -1;
    uint32_t meshIndex = UINT32_MAX;
    Mat4 world = Mat4::identity();
    Mat4 prevWorld = Mat4::identity();
    Bounds worldBounds;
    int sortingLayer = 0;
    int sortingOrder = 0;
    int rendererPriority = 0;
    uint16_t lightLayerMask = 0xffffu;
    uint8_t decalLayerMask = 0x7fu;
    uint32_t lightmapAtlas = UINT32_MAX;
    Vec4 lightmapST{0.0f, 0.0f, 0.0f, 0.0f};
    Vec2 lightmapSize{0.0f, 0.0f};
    std::vector<float> morphWeights;

    bool staticShadowCaster = true;

    bool hasLightmap() const { return lightmapAtlas != UINT32_MAX; }
};

} // namespace hbg::render
