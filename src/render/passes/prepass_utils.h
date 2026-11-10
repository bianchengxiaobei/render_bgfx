#pragma once

#include <cstdint>

namespace hbg::render {

uint8_t depthPyramidMipCount(uint16_t width, uint16_t height);
uint16_t depthPyramidMipDimension(uint16_t size, uint8_t mip);

} // namespace hbg::render
