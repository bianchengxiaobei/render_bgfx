#include "render/lighting/cookie_atlas.h"

#include <algorithm>
#include <cstddef>

namespace hbg::render {

CookieAtlas::CookieAtlas() {
    m_texels.fill(Vec4(1.0f, 1.0f, 1.0f, 1.0f));
}

void CookieAtlas::beginFrame() {
    m_tileCount = 0;
}

bool CookieAtlas::allocate(const Light& light, uint32_t* tileIndex) {
    if (light.cookieMode == LightCookieMode::None || m_tileCount >= kCookieAtlasTileCapacity) {
        return false;
    }

    const uint32_t tile = m_tileCount++;
    const uint32_t tileX = tile % kCookieAtlasTilesX;
    const uint32_t tileY = tile / kCookieAtlasTilesX;
    const uint32_t originX = tileX * kCookieTileSize;
    const uint32_t originY = tileY * kCookieTileSize;

    for (uint32_t y = 0; y < kCookieTileSize; ++y) {
        for (uint32_t x = 0; x < kCookieTileSize; ++x) {
            const float sample = light.cookieSamples[y * kLightCookieSize + x];
            m_texels[(originY + y) * kCookieAtlasWidth + originX + x] =
                Vec4(sample, sample, sample, 1.0f);
        }
    }

    if (tileIndex != nullptr) {
        *tileIndex = tile;
    }
    return true;
}

void CookieAtlas::copyTo(std::vector<Vec4>& destination, uint32_t destinationWidth, uint32_t destinationX,
                         uint32_t destinationY) const {
    const std::size_t requiredSize =
        static_cast<std::size_t>(destinationY + kCookieAtlasHeight) * destinationWidth;
    if (destinationWidth < destinationX + kCookieAtlasWidth || destination.size() < requiredSize) {
        return;
    }

    for (uint32_t row = 0; row < kCookieAtlasHeight; ++row) {
        const auto source = m_texels.begin() + static_cast<std::size_t>(row) * kCookieAtlasWidth;
        auto target = destination.begin() +
                      static_cast<std::size_t>(destinationY + row) * destinationWidth + destinationX;
        std::copy_n(source, kCookieAtlasWidth, target);
    }
}

} // namespace hbg::render
