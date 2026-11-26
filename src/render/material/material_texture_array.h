#pragma once

#include "gfx/texture.h"

#include <cstdint>

namespace hbg::render {

bool isBindlessMaterialTextureFormat(gfx::TextureFormat format);
bool isBindlessMaterialTextureCompressed(gfx::TextureFormat format);
uint32_t bindlessMaterialTextureBlockExtent(gfx::TextureFormat format);
uint32_t bindlessMaterialTextureBlockBytes(gfx::TextureFormat format);
bool bindlessMaterialTextureHeterogeneousLayout(uint32_t maxWidth, uint32_t maxHeight,
                                                gfx::TextureFormat format, uint32_t& arrayWidth,
                                                uint32_t& arrayHeight, uint32_t& offsetX,
                                                uint32_t& offsetY);
bool bindlessMaterialTextureLayeredLayout(uint32_t maxWidth, uint32_t maxHeight, uint8_t mipCount,
                                          gfx::TextureFormat format, uint32_t& arrayWidth,
                                          uint32_t& arrayHeight, uint32_t& offsetX, uint32_t& offsetY);
bool bindlessMaterialTextureMipOffset(uint32_t width, uint32_t height, uint8_t mipCount, uint32_t mip,
                                      gfx::TextureFormat format, uint32_t& offset);
bool bindlessMaterialTextureLayerBytes(uint32_t width, uint32_t height, uint8_t mipCount,
                                       gfx::TextureFormat format, uint32_t& bytes);

} // namespace hbg::render
