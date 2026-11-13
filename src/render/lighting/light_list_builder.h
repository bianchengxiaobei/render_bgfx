#pragma once

#include "core/math.h"
#include "render/lighting/gpu_light_data.h"

#include <cstdint>

namespace hbg::render {

class LightListBuilder {
  public:
    void build(const GpuLightData& lights, const Mat4& viewProj, const Mat4& view, float nearClip, float farClip,
               uint16_t width, uint16_t height, uint32_t capacity = kLightListDefaultCapacity);
    void clear();

    const GpuLightListData& data() const { return m_data; }
    const GpuLightClusterData& clusterData() const { return m_clusterData; }

    uint32_t tileLightCount(uint16_t tileX, uint16_t tileY) const;
    uint32_t tileLightIndex(uint16_t tileX, uint16_t tileY, uint32_t index) const;
    uint32_t clusterLightCount(uint16_t tileX, uint16_t tileY, uint32_t slice) const;
    uint32_t clusterLightIndex(uint16_t tileX, uint16_t tileY, uint32_t slice, uint32_t index) const;

    static uint16_t tileCount(uint16_t dimension, uint16_t tileSize);
    static uint16_t textureWidthFor(uint16_t renderWidth, uint32_t capacity = kLightListDefaultCapacity);
    static uint16_t textureHeightFor(uint16_t renderHeight);
    static uint16_t bigTileTextureWidthFor(uint16_t renderWidth, uint32_t capacity = kLightListDefaultCapacity);
    static uint16_t bigTileTextureHeightFor(uint16_t renderHeight);
    static uint16_t clusterTextureWidthFor(uint16_t renderWidth);
    static uint16_t clusterTextureHeightFor(uint16_t renderHeight);
    static int clusterSliceForDepth(float linearDepth, float nearClip, float farClip);
    static float clusterLogBase();
    static bool projectLightScreenBounds(const Mat4& viewProj, const Mat4& view, const Vec3& position, float range,
                                         float nearClip, float farClip, uint16_t width, uint16_t height,
                                         LightScreenBounds& bounds);

  private:
    GpuLightListData m_data;
    GpuLightClusterData m_clusterData;
};

} // namespace hbg::render
