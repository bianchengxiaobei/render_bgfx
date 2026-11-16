#pragma once

#include "render/lighting/gpu_light_data.h"
#include "render/scene/light.h"

#include <array>
#include <cstdint>
#include <vector>

namespace hbg::render {

class CookieAtlas {
  public:
    CookieAtlas();

    void beginFrame();
    bool allocate(const Light& light, uint32_t* tileIndex = nullptr);
    uint32_t tileCount() const { return m_tileCount; }

    const std::array<Vec4, kCookieAtlasWidth * kCookieAtlasHeight>& texels() const { return m_texels; }
    void copyTo(std::vector<Vec4>& destination, uint32_t destinationWidth, uint32_t destinationX,
                uint32_t destinationY) const;

  private:
    std::array<Vec4, kCookieAtlasWidth * kCookieAtlasHeight> m_texels{};
    uint32_t m_tileCount = 0;
};

} // namespace hbg::render
