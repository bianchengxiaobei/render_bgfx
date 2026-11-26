#include "render/material/material_texture_array.h"

#include <algorithm>

namespace hbg::render {

bool isBindlessMaterialTextureFormat(gfx::TextureFormat format) {
    switch (format) {
    case gfx::TextureFormat::RGBA8:
    case gfx::TextureFormat::RGBA32F:
    case gfx::TextureFormat::BC1:
    case gfx::TextureFormat::BC3:
    case gfx::TextureFormat::BC5:
    case gfx::TextureFormat::BC7:
        return true;
    default:
        return false;
    }
}

bool isBindlessMaterialTextureCompressed(gfx::TextureFormat format) {
    return format == gfx::TextureFormat::BC1 || format == gfx::TextureFormat::BC3 ||
           format == gfx::TextureFormat::BC5 || format == gfx::TextureFormat::BC7;
}

uint32_t bindlessMaterialTextureBlockExtent(gfx::TextureFormat format) {
    return isBindlessMaterialTextureCompressed(format) ? 4u : 1u;
}

uint32_t bindlessMaterialTextureBlockBytes(gfx::TextureFormat format) {
    switch (format) {
    case gfx::TextureFormat::BC1:
    case gfx::TextureFormat::BC5:
        return 8;
    case gfx::TextureFormat::BC3:
    case gfx::TextureFormat::BC7:
        return 16;
    default:
        return 0;
    }
}

namespace {

uint32_t nextPowerOfTwo(uint32_t value) {
    uint32_t result = 1u;
    while (result < value) {
        result <<= 1u;
    }
    return result;
}

} // namespace

bool bindlessMaterialTextureHeterogeneousLayout(uint32_t maxWidth, uint32_t maxHeight,
                                                gfx::TextureFormat format, uint32_t& arrayWidth,
                                                uint32_t& arrayHeight, uint32_t& offsetX,
                                                uint32_t& offsetY) {
    arrayWidth = 0;
    arrayHeight = 0;
    offsetX = 0;
    offsetY = 0;
    if (maxWidth == 0 || maxHeight == 0 || !isBindlessMaterialTextureFormat(format)) {
        return false;
    }
    const uint32_t border = bindlessMaterialTextureBlockExtent(format);
    if (maxWidth > UINT16_MAX - border * 2u || maxHeight > UINT16_MAX - border * 2u) {
        return false;
    }
    arrayWidth = nextPowerOfTwo(maxWidth + border * 2u);
    arrayHeight = nextPowerOfTwo(maxHeight + border * 2u);
    if (arrayWidth > UINT16_MAX || arrayHeight > UINT16_MAX) {
        return false;
    }
    offsetX = border;
    offsetY = border;
    return true;
}

bool bindlessMaterialTextureLayeredLayout(uint32_t maxWidth, uint32_t maxHeight, uint8_t mipCount,
                                          gfx::TextureFormat format, uint32_t& arrayWidth,
                                          uint32_t& arrayHeight, uint32_t& offsetX,
                                          uint32_t& offsetY) {
    if (mipCount <= 1) {
        return bindlessMaterialTextureHeterogeneousLayout(maxWidth, maxHeight, format, arrayWidth,
                                                          arrayHeight, offsetX, offsetY);
    }
    arrayWidth = 0;
    arrayHeight = 0;
    offsetX = 0;
    offsetY = 0;
    if (maxWidth == 0 || maxHeight == 0 || maxWidth > UINT16_MAX || maxHeight > UINT16_MAX ||
        !isBindlessMaterialTextureFormat(format)) {
        return false;
    }
    arrayWidth = nextPowerOfTwo(maxWidth);
    arrayHeight = nextPowerOfTwo(maxHeight);
    if (arrayWidth > UINT16_MAX || arrayHeight > UINT16_MAX) {
        return false;
    }
    return true;
}

bool bindlessMaterialTextureMipOffset(uint32_t width, uint32_t height, uint8_t mipCount, uint32_t mip,
                                      gfx::TextureFormat format, uint32_t& offset) {
    offset = 0;
    uint32_t layerBytes = 0;
    if (mip >= mipCount || !bindlessMaterialTextureLayerBytes(width, height, mipCount, format, layerBytes)) {
        return false;
    }
    uint32_t mipWidth = width;
    uint32_t mipHeight = height;
    for (uint32_t level = 0; level < mip; ++level) {
        bgfx::TextureInfo info{};
        bgfx::calcTextureSize(info, static_cast<uint16_t>(mipWidth), static_cast<uint16_t>(mipHeight), 1,
                              false, false, 1, gfx::toBgfx(format));
        if (info.storageSize == 0) {
            return false;
        }
        offset += info.storageSize;
        mipWidth = std::max(1u, mipWidth >> 1u);
        mipHeight = std::max(1u, mipHeight >> 1u);
    }
    return offset <= layerBytes;
}

bool bindlessMaterialTextureLayerBytes(uint32_t width, uint32_t height, uint8_t mipCount,
                                       gfx::TextureFormat format, uint32_t& bytes) {
    bytes = 0;
    if (width == 0 || height == 0 || mipCount == 0 || width > UINT16_MAX || height > UINT16_MAX ||
        !isBindlessMaterialTextureFormat(format)) {
        return false;
    }

    uint32_t mipExtent = std::max(width, height);
    uint32_t maxMips = 1;
    while (mipExtent > 1) {
        mipExtent >>= 1u;
        ++maxMips;
    }
    if (mipCount > maxMips) {
        return false;
    }

    bgfx::TextureInfo info{};
    bgfx::calcTextureSize(info, static_cast<uint16_t>(width), static_cast<uint16_t>(height), 1, false,
                          mipCount > 1, 1, gfx::toBgfx(format));
    if (info.storageSize == 0) {
        return false;
    }
    bytes = info.storageSize;
    return true;
}

} // namespace hbg::render
