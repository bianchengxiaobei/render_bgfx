#include "render/lighting/cookie_atlas.h"

#include <algorithm>
#include <bit>
#include <cstddef>

namespace hbg::render {
namespace {

uint32_t nextPowerOfTwo(uint32_t value) {
    uint32_t result = 1;
    while (result < value && result < kCookieAtlasMaxSize) {
        result <<= 1;
    }
    return result;
}

uint32_t alignUp(uint32_t value, uint32_t alignment) {
    return ((value + alignment - 1u) / alignment) * alignment;
}

uint8_t toUnorm(float value) {
    return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

void mixHash(uint64_t& hash, uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ull;
}

} // namespace

CookieAtlas::CookieAtlas() {
    configure(kCookieAtlasDefaultSize, true);
}

void CookieAtlas::configure(uint32_t size, bool native) {
    size = std::clamp(nextPowerOfTwo(size), 256u, kCookieAtlasMaxSize);
    const uint32_t mipCount = native ? 1u + static_cast<uint32_t>(std::countr_zero(size)) : 1u;
    const uint32_t padding = native ? kCookieAtlasPadding : 0u;
    if (m_size == size && m_mipCount == mipCount && m_native == native && !m_mips.empty()) {
        return;
    }

    m_size = size;
    m_native = native;
    m_mipCount = mipCount;
    m_padding = padding;
    m_mips.clear();
    m_mips.resize(m_mipCount);
    for (uint32_t mip = 0; mip < m_mipCount; ++mip) {
        const uint32_t mipSize = std::max(1u, m_size >> mip);
        m_mips[mip].assign(static_cast<std::size_t>(mipSize) * mipSize * 4u, 255u);
    }
    m_dirty = true;
}

void CookieAtlas::beginFrame(uint64_t signature) {
    m_tileCount = 0;
    m_nativeTileCount = 0;
    m_cursorX = 0;
    m_cursorY = 0;
    m_rowHeight = 0;
    m_contentHash = signature;
    mixHash(m_contentHash, m_size);
    mixHash(m_contentHash, m_native ? 1u : 0u);
    mixHash(m_contentHash, m_mipCount);
}

uint32_t CookieAtlas::requiredTileSize(const Light& light) const {
    if (light.cookieMode == LightCookieMode::None) {
        return 0;
    }
    const bool nativeCookie = m_native && light.cookieNativeWidth > 0 && light.cookieNativeHeight > 0 &&
                              light.cookieNative.size() ==
                                  static_cast<std::size_t>(light.cookieNativeWidth) * light.cookieNativeHeight;
    if (!nativeCookie) {
        return m_native ? 16u : kCookieLegacySize;
    }
    const uint32_t tileSize =
        std::max(16u, nextPowerOfTwo(std::max(light.cookieNativeWidth, light.cookieNativeHeight) + 2u * m_padding));
    return tileSize <= m_size ? tileSize : 0u;
}

bool CookieAtlas::allocate(const Light& light, uint32_t* tileIndex, Vec4* scaleOffset) {
    if (light.cookieMode == LightCookieMode::None || m_tileCount >= kCookieAtlasTileCapacity) {
        return false;
    }

    mixHash(m_contentHash, reinterpret_cast<uintptr_t>(&light));
    mixHash(m_contentHash, reinterpret_cast<uintptr_t>(light.cookieNative.data()));
    mixHash(m_contentHash, light.cookieNativeWidth);
    mixHash(m_contentHash, light.cookieNativeHeight);
    mixHash(m_contentHash, static_cast<uint64_t>(light.cookieMode));

    const bool nativeCookie = m_native && light.cookieNativeWidth > 0 && light.cookieNativeHeight > 0 &&
                              light.cookieNative.size() ==
                                  static_cast<std::size_t>(light.cookieNativeWidth) * light.cookieNativeHeight;
    if (nativeCookie) {
        const uint32_t payloadWidth = light.cookieNativeWidth;
        const uint32_t payloadHeight = light.cookieNativeHeight;
        const uint32_t tileSize =
            std::max(16u, nextPowerOfTwo(std::max(payloadWidth, payloadHeight) + 2u * m_padding));
        if (tileSize > m_size || payloadWidth + 2u * m_padding > tileSize ||
            payloadHeight + 2u * m_padding > tileSize) {
            return false;
        }
        if (!allocateTile(tileSize, payloadWidth, payloadHeight, light.cookieNative, tileIndex, scaleOffset)) {
            return false;
        }
        ++m_nativeTileCount;
        return true;
    }

    std::vector<float> payload(kCookieLegacySize * kCookieLegacySize);
    for (uint32_t y = 0; y < kCookieLegacySize; ++y) {
        for (uint32_t x = 0; x < kCookieLegacySize; ++x) {
            payload[static_cast<std::size_t>(y) * kCookieLegacySize + x] =
                light.cookieSamples[static_cast<std::size_t>(y) * kCookieLegacySize + x];
        }
    }
    const uint32_t tileSize = m_native ? 16u : kCookieLegacySize;
    return allocateTile(tileSize, kCookieLegacySize, kCookieLegacySize, payload, tileIndex, scaleOffset);
}

bool CookieAtlas::allocateTile(uint32_t tileSize, uint32_t payloadWidth, uint32_t payloadHeight,
                               const std::vector<float>& payload, uint32_t* tileIndex, Vec4* scaleOffset) {
    uint32_t x = alignUp(m_cursorX, tileSize);
    uint32_t y = m_cursorY;
    if (x + tileSize > m_size) {
        x = 0;
        y = alignUp(m_cursorY + m_rowHeight, tileSize);
        m_rowHeight = 0;
    }
    if (y + tileSize > m_size) {
        return false;
    }

    std::vector<uint8_t> baseTile(static_cast<std::size_t>(tileSize) * tileSize * 4u, 255u);
    const uint32_t padding = m_native ? m_padding : 0u;
    for (uint32_t ty = 0; ty < tileSize; ++ty) {
        for (uint32_t tx = 0; tx < tileSize; ++tx) {
            const int sourceX = std::clamp(static_cast<int>(tx) - static_cast<int>(padding), 0,
                                           static_cast<int>(payloadWidth) - 1);
            const int sourceY = std::clamp(static_cast<int>(ty) - static_cast<int>(padding), 0,
                                           static_cast<int>(payloadHeight) - 1);
            const uint8_t value =
                toUnorm(payload[static_cast<std::size_t>(sourceY) * payloadWidth + static_cast<uint32_t>(sourceX)]);
            const std::size_t offset = (static_cast<std::size_t>(ty) * tileSize + tx) * 4u;
            baseTile[offset] = value;
            baseTile[offset + 1u] = value;
            baseTile[offset + 2u] = value;
            baseTile[offset + 3u] = 255u;
        }
    }

    const uint32_t tile = m_tileCount++;
    mixHash(m_contentHash, tile);
    mixHash(m_contentHash, x);
    mixHash(m_contentHash, y);
    mixHash(m_contentHash, tileSize);
    mixHash(m_contentHash, payloadWidth);
    mixHash(m_contentHash, payloadHeight);
    writeMips(x, y, tileSize, baseTile);
    m_cursorX = x + tileSize;
    m_rowHeight = std::max(m_rowHeight, tileSize);
    m_dirty = true;

    if (tileIndex != nullptr) {
        *tileIndex = tile;
    }
    if (scaleOffset != nullptr) {
        const uint32_t paddedWidth = payloadWidth + 2u * padding;
        const uint32_t paddedHeight = payloadHeight + 2u * padding;
        *scaleOffset = Vec4(static_cast<float>(paddedWidth) / static_cast<float>(m_size),
                            static_cast<float>(paddedHeight) / static_cast<float>(m_size),
                            static_cast<float>(x) / static_cast<float>(m_size),
                            static_cast<float>(y) / static_cast<float>(m_size));
    }
    return true;
}

void CookieAtlas::writeMips(uint32_t originX, uint32_t originY, uint32_t tileSize,
                            const std::vector<uint8_t>& baseTile) {
    for (uint32_t mip = 0; mip < m_mipCount; ++mip) {
        if (tileSize < (1u << mip)) {
            break;
        }
        const uint32_t mipSize = std::max(1u, m_size >> mip);
        const uint32_t tileMipSize = std::max(1u, tileSize >> mip);
        const uint32_t originMipX = originX >> mip;
        const uint32_t originMipY = originY >> mip;
        std::vector<uint8_t>& destination = m_mips[mip];

        if (mip == 0) {
            for (uint32_t row = 0; row < tileMipSize; ++row) {
                const auto source = baseTile.begin() + static_cast<std::size_t>(row) * tileSize * 4u;
                auto target = destination.begin() +
                              (static_cast<std::size_t>(originMipY + row) * mipSize + originMipX) * 4u;
                std::copy_n(source, static_cast<std::size_t>(tileMipSize) * 4u, target);
            }
            continue;
        }

        const uint32_t previousMipSize = std::max(1u, m_size >> (mip - 1u));
        const uint32_t previousX = originX >> (mip - 1u);
        const uint32_t previousY = originY >> (mip - 1u);
        const std::vector<uint8_t>& source = m_mips[mip - 1u];
        for (uint32_t row = 0; row < tileMipSize; ++row) {
            for (uint32_t column = 0; column < tileMipSize; ++column) {
                const uint32_t sourceX0 = std::min(previousX + column * 2u, previousMipSize - 1u);
                const uint32_t sourceY0 = std::min(previousY + row * 2u, previousMipSize - 1u);
                const uint32_t sourceX1 = std::min(sourceX0 + 1u, previousMipSize - 1u);
                const uint32_t sourceY1 = std::min(sourceY0 + 1u, previousMipSize - 1u);
                uint32_t sum[4] = {0u, 0u, 0u, 0u};
                const uint32_t sampleX[2] = {sourceX0, sourceX1};
                const uint32_t sampleY[2] = {sourceY0, sourceY1};
                for (uint32_t sy = 0; sy < 2; ++sy) {
                    for (uint32_t sx = 0; sx < 2; ++sx) {
                        const std::size_t offset =
                            (static_cast<std::size_t>(sampleY[sy]) * previousMipSize + sampleX[sx]) * 4u;
                        for (uint32_t channel = 0; channel < 4; ++channel) {
                            sum[channel] += source[offset + channel];
                        }
                    }
                }
                const std::size_t targetOffset =
                    (static_cast<std::size_t>(originMipY + row) * mipSize + originMipX + column) * 4u;
                for (uint32_t channel = 0; channel < 4; ++channel) {
                    destination[targetOffset + channel] = static_cast<uint8_t>((sum[channel] + 2u) / 4u);
                }
            }
        }
    }
}

const std::vector<uint8_t>& CookieAtlas::mipData(uint32_t mip) const {
    static const std::vector<uint8_t> empty;
    return mip < m_mips.size() ? m_mips[mip] : empty;
}

} // namespace hbg::render
