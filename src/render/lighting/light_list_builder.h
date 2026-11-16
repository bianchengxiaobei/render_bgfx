#pragma once

#include "core/math.h"
#include "render/lighting/gpu_light_data.h"

#include <cstdint>

namespace hbg::render {

struct LightBounds {
    Vec3 position{};
    Vec3 forward{0.0f, 0.0f, 1.0f};
    Vec3 right{1.0f, 0.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float range = 0.0f;
    float halfWidth = 0.0f;
    float halfHeight = 0.0f;
    GpuLightType type = GpuLightType::Point;
};

class LightListBuilder {
  public:
    void build(const GpuLightData& lights, const Mat4& viewProj, const Mat4& view, float nearClip, float farClip,
               uint16_t width, uint16_t height, uint32_t capacity = kLightListDefaultCapacity);
    void clear();

    const GpuLightListData& data() const { return m_data; }
    const GpuLightClusterData& clusterData() const { return m_clusterData; }

    uint32_t tileLightCount(uint16_t tileX, uint16_t tileY,
                            uint32_t category = static_cast<uint32_t>(LightCategory::Punctual)) const;
    uint32_t tileLightIndex(uint16_t tileX, uint16_t tileY, uint32_t index,
                            uint32_t category = static_cast<uint32_t>(LightCategory::Punctual)) const;
    uint32_t tileFeatureFlags(uint16_t tileX, uint16_t tileY) const;
    uint32_t clusterOffset(uint16_t tileX, uint16_t tileY, uint32_t slice,
                           uint32_t category = static_cast<uint32_t>(LightCategory::Punctual)) const;
    uint32_t clusterLightCount(uint16_t tileX, uint16_t tileY, uint32_t slice,
                               uint32_t category = static_cast<uint32_t>(LightCategory::Punctual)) const;
    uint32_t clusterLightIndex(uint16_t tileX, uint16_t tileY, uint32_t slice, uint32_t index,
                               uint32_t category = static_cast<uint32_t>(LightCategory::Punctual)) const;
    uint32_t clusterTruncated(uint16_t tileX, uint16_t tileY, uint32_t slice,
                              uint32_t category = static_cast<uint32_t>(LightCategory::Punctual)) const;

    static uint16_t tileCount(uint16_t dimension, uint16_t tileSize);
    static uint16_t textureWidthFor(uint16_t renderWidth, uint32_t capacity = kLightListDefaultCapacity);
    static uint16_t textureHeightFor(uint16_t renderHeight);
    static uint16_t bigTileTextureWidthFor(uint16_t renderWidth, uint32_t capacity = kLightListDefaultCapacity);
    static uint16_t bigTileTextureHeightFor(uint16_t renderHeight);
    static uint16_t clusterOffsetTextureWidthFor(uint16_t renderWidth);
    static uint16_t clusterOffsetTextureHeightFor(uint16_t renderHeight);
    static uint16_t clusterIndexTextureWidthFor(uint32_t indexCount);
    static uint16_t clusterIndexTextureHeightFor(uint32_t indexCount);
    static uint32_t clusterCapacityFor(uint16_t renderWidth, uint16_t renderHeight, uint32_t capacity);
    static uint32_t clusterIndexCountFor(uint16_t renderWidth, uint16_t renderHeight, uint32_t capacity);
    static bool textureFits(uint16_t renderWidth, uint16_t renderHeight, uint32_t capacity, uint32_t maxTextureSize);
    static uint32_t textureCapacityFor(uint16_t renderWidth, uint16_t renderHeight, uint32_t requestedCapacity,
                                       uint32_t maxTextureSize);
    static int clusterSliceForDepth(float linearDepth, float nearClip, float farClip);
    static float clusterLogBase();
    static bool projectLightScreenBounds(const Mat4& viewProj, const Mat4& view, const Vec3& position, float range,
                                         float nearClip, float farClip, uint16_t width, uint16_t height,
                                         LightScreenBounds& bounds);
    static bool projectLightScreenBounds(const Mat4& viewProj, const Mat4& view, const LightBounds& light,
                                         float nearClip, float farClip, uint16_t width, uint16_t height,
                                         LightScreenBounds& bounds);

  private:
    GpuLightListData m_data;
    GpuLightClusterData m_clusterData;
};

} // namespace hbg::render
