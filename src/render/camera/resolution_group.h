#pragma once

#include "core/math.h"

#include <cstdint>

namespace hbg::render {

enum class ResolutionGroup : uint8_t {
    BeforeDynamicResUpscale,
    AfterDynamicResUpscale,
};

struct ViewResolution {
    uint16_t targetWidth = 0;
    uint16_t targetHeight = 0;
    uint16_t renderWidth = 0;
    uint16_t renderHeight = 0;
    float scale = 1.0f;

    uint16_t width(ResolutionGroup group) const;
    uint16_t height(ResolutionGroup group) const;
    Vec4 screenSize(ResolutionGroup group) const;
    Vec4 dynamicResolutionScale() const;
    Vec4 rtScale() const;
};

ViewResolution makeViewResolution(uint16_t targetWidth, uint16_t targetHeight, float scale);
const char* resolutionGroupName(ResolutionGroup group);

} // namespace hbg::render
