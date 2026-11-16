#pragma once

#include "core/bounds.h"
#include "core/math.h"

#include <cstdint>
#include <string>

namespace hbg::render {

struct RenderObject {
    std::string name;
    uint32_t meshIndex = UINT32_MAX;
    Mat4 world = Mat4::identity();
    Mat4 prevWorld = Mat4::identity();
    Bounds worldBounds;
    int sortingLayer = 0;
    int sortingOrder = 0;
    int rendererPriority = 0;
    uint16_t lightLayerMask = 0xffffu;
};

} // namespace hbg::render
