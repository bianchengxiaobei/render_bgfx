#pragma once

#include "core/math.h"
#include "render/lighting/gpu_light_data.h"

#include <cstdint>

namespace hbg::render {

class LightListBuilder {
  public:
    void build(const GpuLightData& lights, const Mat4& viewProj, uint16_t width, uint16_t height);
    void clear();

    const GpuLightListData& data() const { return m_data; }

    uint32_t tileLightCount(uint16_t tileX, uint16_t tileY) const;
    uint32_t tileLightIndex(uint16_t tileX, uint16_t tileY, uint32_t index) const;

    static uint16_t tileCount(uint16_t dimension, uint16_t tileSize);
    static uint16_t textureWidthFor(uint16_t renderWidth);
    static uint16_t textureHeightFor(uint16_t renderHeight);

  private:
    GpuLightListData m_data;
};

} // namespace hbg::render
