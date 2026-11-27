#pragma once

#include "render/lighting/gpu_light_data.h"
#include "render/scene/light.h"

#include <cstdint>
#include <vector>

namespace hbg::render {

class CookieAtlas {
  public:
    CookieAtlas();

    void configure(uint32_t size, bool native);
    void beginFrame(uint64_t signature = 0);
    uint32_t requiredTileSize(const Light& light) const;
    bool allocate(const Light& light, uint32_t* tileIndex = nullptr, Vec4* scaleOffset = nullptr);

    uint32_t tileCount() const { return m_tileCount; }
    uint32_t nativeTileCount() const { return m_nativeTileCount; }
    uint32_t size() const { return m_size; }
    uint32_t mipCount() const { return m_mipCount; }
    uint32_t padding() const { return m_padding; }
    bool native() const { return m_native; }
    bool dirty() const { return m_dirty; }
    uint64_t contentHash() const { return m_contentHash; }
    void clearDirty() { m_dirty = false; }

    const std::vector<uint8_t>& mipData(uint32_t mip) const;

  private:
    bool allocateTile(uint32_t tileSize, uint32_t payloadWidth, uint32_t payloadHeight,
                      const std::vector<float>& payload, uint32_t* tileIndex, Vec4* scaleOffset);
    void writeMips(uint32_t originX, uint32_t originY, uint32_t tileSize,
                   const std::vector<uint8_t>& baseTile);

    uint32_t m_size = kCookieAtlasDefaultSize;
    uint32_t m_mipCount = 1;
    uint32_t m_padding = kCookieAtlasPadding;
    bool m_native = true;
    bool m_dirty = true;
    uint64_t m_contentHash = 0;
    uint32_t m_tileCount = 0;
    uint32_t m_nativeTileCount = 0;
    uint32_t m_cursorX = 0;
    uint32_t m_cursorY = 0;
    uint32_t m_rowHeight = 0;
    std::vector<std::vector<uint8_t>> m_mips;
};

} // namespace hbg::render
