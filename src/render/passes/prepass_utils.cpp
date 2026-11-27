#include "render/passes/prepass_utils.h"

#include <algorithm>

namespace hbg::render {

uint8_t depthPyramidMipCount(uint16_t width, uint16_t height) {
    uint32_t size = std::max<uint32_t>(width, height);
    uint8_t mipCount = 1;
    while (size > 1 && mipCount < UINT8_MAX) {
        size >>= 1;
        ++mipCount;
    }
    return mipCount;
}

uint16_t depthPyramidMipDimension(uint16_t size, uint8_t mip) {
    const uint32_t dimension = static_cast<uint32_t>(size) >> mip;
    return static_cast<uint16_t>(std::max<uint32_t>(dimension, 1));
}

} // namespace hbg::render
