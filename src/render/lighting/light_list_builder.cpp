#include "render/lighting/light_list_builder.h"

#include "core/bounds.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace hbg::render {
namespace {

constexpr float kClusterLogBase = 1.0404f;

struct ScreenRect {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minY = 0.0f;
    float maxY = 0.0f;
};

bool overlaps(const ScreenRect& rect, float minX, float maxX, float minY, float maxY) {
    return rect.maxX > minX && rect.minX < maxX && rect.maxY > minY && rect.minY < maxY;
}

Vec4 project(const Mat4& viewProj, const Vec3& point) {
    return Vec4(viewProj[0] * point.x + viewProj[4] * point.y + viewProj[8] * point.z + viewProj[12],
                viewProj[1] * point.x + viewProj[5] * point.y + viewProj[9] * point.z + viewProj[13],
                viewProj[2] * point.x + viewProj[6] * point.y + viewProj[10] * point.z + viewProj[14],
                viewProj[3] * point.x + viewProj[7] * point.y + viewProj[11] * point.z + viewProj[15]);
}

bool projectLightBounds(const Mat4& viewProj, const Vec3& position, float radius, float width, float height,
                        ScreenRect& rect) {
    const float extent = std::max(radius, 0.001f);
    const Bounds bounds{{position.x - extent, position.y - extent, position.z - extent},
                        {position.x + extent, position.y + extent, position.z + extent}};

    float minX = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();
    bool anyBehind = false;
    bool anyInFront = false;

    for (uint32_t corner = 0; corner < 8; ++corner) {
        const Vec3 point((corner & 1) != 0 ? bounds.max.x : bounds.min.x,
                         (corner & 2) != 0 ? bounds.max.y : bounds.min.y,
                         (corner & 4) != 0 ? bounds.max.z : bounds.min.z);
        const Vec4 clip = project(viewProj, point);
        if (clip.w <= 0.0f) {
            anyBehind = true;
            continue;
        }
        anyInFront = true;

        const float ndcX = clip.x / clip.w;
        const float ndcY = clip.y / clip.w;
        const float pixelX = (ndcX * 0.5f + 0.5f) * width;
        const float pixelY = (ndcY * 0.5f + 0.5f) * height;
        minX = std::min(minX, pixelX);
        maxX = std::max(maxX, pixelX);
        minY = std::min(minY, pixelY);
        maxY = std::max(maxY, pixelY);
    }

    if (!anyInFront) {
        return false;
    }
    if (anyBehind) {
        rect = ScreenRect{0.0f, width, 0.0f, height};
        return width > 0.0f && height > 0.0f;
    }
    if (maxX <= 0.0f || minX >= width || maxY <= 0.0f || minY >= height) {
        return false;
    }

    rect.minX = std::clamp(minX, 0.0f, width);
    rect.maxX = std::clamp(maxX, 0.0f, width);
    rect.minY = std::clamp(minY, 0.0f, height);
    rect.maxY = std::clamp(maxY, 0.0f, height);
    return rect.maxX > rect.minX && rect.maxY > rect.minY;
}

uint32_t lightType(const GpuLightData& lights, uint32_t lightIndex) {
    return static_cast<uint32_t>(lights.texels[lightIndex * kGpuLightTexels + 3].w + 0.5f) & 3u;
}

Vec3 lightPosition(const GpuLightData& lights, uint32_t lightIndex) {
    const Vec4& positionRange = lights.texels[lightIndex * kGpuLightTexels];
    return Vec3(positionRange.x, positionRange.y, positionRange.z);
}

float lightRange(const GpuLightData& lights, uint32_t lightIndex) {
    return std::max(lights.texels[lightIndex * kGpuLightTexels].w, 0.0f);
}

float viewDepth(const Mat4& view, const Vec3& position) {
    return -(view[2] * position.x + view[6] * position.y + view[10] * position.z + view[14]);
}

} // namespace

uint16_t LightListBuilder::tileCount(uint16_t dimension, uint16_t tileSize) {
    if (dimension == 0 || tileSize == 0) {
        return 0;
    }
    return static_cast<uint16_t>((static_cast<uint32_t>(dimension) + tileSize - 1u) / tileSize);
}

uint16_t LightListBuilder::textureWidthFor(uint16_t renderWidth, uint32_t capacity) {
    const uint32_t width =
        static_cast<uint32_t>(tileCount(renderWidth, kLightTileSize)) * lightListStrideFor(capacity);
    return width <= std::numeric_limits<uint16_t>::max() ? static_cast<uint16_t>(width) : 0;
}

uint16_t LightListBuilder::textureHeightFor(uint16_t renderHeight) {
    return tileCount(renderHeight, kLightTileSize);
}

uint16_t LightListBuilder::bigTileTextureWidthFor(uint16_t renderWidth, uint32_t capacity) {
    const uint32_t width =
        static_cast<uint32_t>(tileCount(renderWidth, kLightBigTileSize)) * lightListStrideFor(capacity);
    return width <= std::numeric_limits<uint16_t>::max() ? static_cast<uint16_t>(width) : 0;
}

uint16_t LightListBuilder::bigTileTextureHeightFor(uint16_t renderHeight) {
    return tileCount(renderHeight, kLightBigTileSize);
}

uint16_t LightListBuilder::clusterTextureWidthFor(uint16_t renderWidth) {
    const uint32_t width =
        static_cast<uint32_t>(tileCount(renderWidth, kClusterTileSize)) * kLightClusterStride;
    return width <= std::numeric_limits<uint16_t>::max() ? static_cast<uint16_t>(width) : 0;
}

uint16_t LightListBuilder::clusterTextureHeightFor(uint16_t renderHeight) {
    return static_cast<uint16_t>(tileCount(renderHeight, kClusterTileSize) * kClusterSliceCount);
}

int LightListBuilder::clusterSliceForDepth(float linearDepth, float nearClip, float farClip) {
    const float nearPlane = std::max(nearClip, 0.0001f);
    const float farPlane = std::max(farClip, nearPlane + 0.0001f);
    const float range = farPlane - nearPlane;
    const float rangeFittedDistance = std::clamp((linearDepth - nearPlane) / range, 0.0f, 1.0f);
    const float clusterCount = static_cast<float>(kClusterSliceCount);
    const float scale = std::pow(kClusterLogBase, clusterCount);
    const float slice = std::log1p(rangeFittedDistance * (scale - 1.0f)) / std::log(kClusterLogBase);
    return std::clamp(static_cast<int>(std::floor(slice)), 0, static_cast<int>(kClusterSliceCount) - 1);
}

float LightListBuilder::clusterLogBase() {
    return kClusterLogBase;
}

bool LightListBuilder::projectLightScreenBounds(const Mat4& viewProj, const Mat4& view, const Vec3& position,
                                                float range, float nearClip, float farClip, uint16_t width,
                                                uint16_t height, LightScreenBounds& bounds) {
    bounds = {};
    ScreenRect rect;
    if (!projectLightBounds(viewProj, position, range, static_cast<float>(width), static_cast<float>(height), rect)) {
        return false;
    }

    const float depthRange = std::max(range, 0.0f);
    const float centerDepth = viewDepth(view, position);
    bounds.min = Vec4(rect.minX, rect.minY, std::max(centerDepth - depthRange, nearClip),
                      std::min(centerDepth + depthRange, farClip));
    bounds.max = Vec4(rect.maxX, rect.maxY, 1.0f, 0.0f);
    return true;
}

void LightListBuilder::clear() {
    m_data = {};
    m_clusterData = {};
}

void LightListBuilder::build(const GpuLightData& lights, const Mat4& viewProj, const Mat4& view, float nearClip,
                             float farClip, uint16_t width, uint16_t height, uint32_t capacity) {
    clear();
    if (width == 0 || height == 0 || lights.lightCount == 0) {
        return;
    }

    const uint32_t lightCapacity = sanitizeLightListCapacity(capacity);
    const uint32_t listStride = lightListStrideFor(lightCapacity);
    const uint16_t tileCountX = tileCount(width, kLightTileSize);
    const uint16_t tileCountY = tileCount(height, kLightTileSize);
    const uint16_t bigTileCountX = tileCount(width, kLightBigTileSize);
    const uint16_t bigTileCountY = tileCount(height, kLightBigTileSize);
    const uint16_t textureWidth = textureWidthFor(width, lightCapacity);
    if (tileCountX == 0 || tileCountY == 0 || textureWidth == 0) {
        return;
    }

    std::array<ScreenRect, kMaxGpuLights> lightRects{};
    std::array<bool, kMaxGpuLights> lightVisible{};
    for (uint32_t index = 0; index < lights.punctualCount; ++index) {
        const uint32_t lightIndex = lights.directionalCount + index;
        if (lightIndex >= lights.lightCount || lightIndex >= lightCapacity ||
            lightIndex >= kMaxGpuLights || lightType(lights, lightIndex) == 0) {
            continue;
        }

        lightVisible[lightIndex] =
            projectLightBounds(viewProj, lightPosition(lights, lightIndex), lightRange(lights, lightIndex),
                               static_cast<float>(width), static_cast<float>(height), lightRects[lightIndex]);
    }

    const uint32_t bigTileCount = static_cast<uint32_t>(bigTileCountX) * bigTileCountY;
    std::vector<std::vector<uint32_t>> bigLists(bigTileCount);
    for (uint16_t bigY = 0; bigY < bigTileCountY; ++bigY) {
        const float tileMinY = static_cast<float>(bigY) * kLightBigTileSize;
        const float tileMaxY = std::min(tileMinY + kLightBigTileSize, static_cast<float>(height));
        for (uint16_t bigX = 0; bigX < bigTileCountX; ++bigX) {
            const float tileMinX = static_cast<float>(bigX) * kLightBigTileSize;
            const float tileMaxX = std::min(tileMinX + kLightBigTileSize, static_cast<float>(width));
            std::vector<uint32_t>& list = bigLists[static_cast<std::size_t>(bigY) * bigTileCountX + bigX];
            for (uint32_t lightIndex = lights.directionalCount;
                 lightIndex < lights.lightCount && lightIndex < lightCapacity; ++lightIndex) {
                if (list.size() >= lightCapacity) {
                    break;
                }
                if (lightVisible[lightIndex] &&
                    overlaps(lightRects[lightIndex], tileMinX, tileMaxX, tileMinY, tileMaxY)) {
                    list.push_back(lightIndex);
                }
            }
            m_data.maxLightsPerBigTile = std::max(m_data.maxLightsPerBigTile, static_cast<uint32_t>(list.size()));
        }
    }

    m_data.texels.assign(static_cast<std::size_t>(textureWidth) * tileCountY, 0u);
    for (uint16_t tileY = 0; tileY < tileCountY; ++tileY) {
        const float tileMinY = static_cast<float>(tileY) * kLightTileSize;
        const float tileMaxY = std::min(tileMinY + kLightTileSize, static_cast<float>(height));
        for (uint16_t tileX = 0; tileX < tileCountX; ++tileX) {
            const float tileMinX = static_cast<float>(tileX) * kLightTileSize;
            const float tileMaxX = std::min(tileMinX + kLightTileSize, static_cast<float>(width));
            const std::vector<uint32_t>& bigList =
                bigLists[static_cast<std::size_t>(tileY / (kLightBigTileSize / kLightTileSize)) * bigTileCountX +
                         tileX / (kLightBigTileSize / kLightTileSize)];
            const std::size_t row = static_cast<std::size_t>(tileY) * textureWidth + tileX * listStride;
            uint32_t count = 0;
            for (uint32_t lightIndex : bigList) {
                if (count >= lightCapacity) {
                    break;
                }
                if (lightIndex < lightCapacity && lightVisible[lightIndex] &&
                    overlaps(lightRects[lightIndex], tileMinX, tileMaxX, tileMinY, tileMaxY)) {
                    m_data.texels[row + 1 + count] = lightIndex;
                    ++count;
                }
            }
            m_data.texels[row] = count;
            m_data.maxLightsPerTile = std::max(m_data.maxLightsPerTile, count);
        }
    }

    m_data.tileCountX = tileCountX;
    m_data.tileCountY = tileCountY;
    m_data.bigTileCountX = bigTileCountX;
    m_data.bigTileCountY = bigTileCountY;
    m_data.width = textureWidth;
    m_data.height = tileCountY;
    m_data.capacity = lightCapacity;
    m_data.listStride = listStride;
    m_data.enabled = true;

    const uint16_t clusterTileCountX = tileCount(width, kClusterTileSize);
    const uint16_t clusterTileCountY = tileCount(height, kClusterTileSize);
    const uint16_t clusterWidth = clusterTextureWidthFor(width);
    const uint16_t clusterHeight = clusterTextureHeightFor(height);
    if (clusterTileCountX == 0 || clusterTileCountY == 0 || clusterWidth == 0 || clusterHeight == 0) {
        return;
    }

    const std::size_t clusterCount =
        static_cast<std::size_t>(clusterTileCountX) * clusterTileCountY * kClusterSliceCount;
    std::vector<uint8_t> overflow(clusterCount, 0u);
    m_clusterData.texels.assign(static_cast<std::size_t>(clusterWidth) * clusterHeight, 0u);

    for (uint32_t lightIndex = lights.directionalCount;
         lightIndex < lights.lightCount && lightIndex < lightCapacity && lightIndex < lightVisible.size();
         ++lightIndex) {
        if (!lightVisible[lightIndex] || lightType(lights, lightIndex) == 0) {
            continue;
        }

        const ScreenRect& rect = lightRects[lightIndex];
        const float range = lightRange(lights, lightIndex);
        const float centerDepth = viewDepth(view, lightPosition(lights, lightIndex));
        const float minDepth = std::max(centerDepth - range, nearClip);
        const float maxDepth = std::min(centerDepth + range, farClip);
        if (maxDepth < minDepth) {
            continue;
        }

        const int sliceMin = clusterSliceForDepth(minDepth, nearClip, farClip);
        const int sliceMax = clusterSliceForDepth(maxDepth, nearClip, farClip);
        const float clusterTileSize = static_cast<float>(kClusterTileSize);
        const int minTileX = std::clamp(static_cast<int>(std::floor(rect.minX / clusterTileSize)), 0,
                                        static_cast<int>(clusterTileCountX) - 1);
        const int maxTileX = std::clamp(static_cast<int>(std::ceil(rect.maxX / clusterTileSize)) - 1, 0,
                                        static_cast<int>(clusterTileCountX) - 1);
        const int minTileY = std::clamp(static_cast<int>(std::floor(rect.minY / clusterTileSize)), 0,
                                        static_cast<int>(clusterTileCountY) - 1);
        const int maxTileY = std::clamp(static_cast<int>(std::ceil(rect.maxY / clusterTileSize)) - 1, 0,
                                        static_cast<int>(clusterTileCountY) - 1);
        for (int tileY = minTileY; tileY <= maxTileY; ++tileY) {
            for (int tileX = minTileX; tileX <= maxTileX; ++tileX) {
                for (int slice = sliceMin; slice <= sliceMax; ++slice) {
                    const std::size_t clusterIndex =
                        (static_cast<std::size_t>(tileY) * clusterTileCountX + tileX) * kClusterSliceCount +
                        static_cast<std::size_t>(slice);
                    const std::size_t row = static_cast<std::size_t>(tileY * kClusterSliceCount + slice) * clusterWidth;
                    const std::size_t column = static_cast<std::size_t>(tileX) * kLightClusterStride;
                    const uint32_t count = m_clusterData.texels[row + column];
                    if (count >= kLightClusterMaxLights) {
                        overflow[clusterIndex] = 1u;
                        continue;
                    }
                    m_clusterData.texels[row + column + 1u + count] = lightIndex;
                    m_clusterData.texels[row + column] = count + 1u;
                    m_clusterData.maxLightsPerCluster =
                        std::max(m_clusterData.maxLightsPerCluster, count + 1u);
                }
            }
        }
    }

    for (uint8_t value : overflow) {
        m_clusterData.overflowClusters += value != 0u ? 1u : 0u;
    }
    m_clusterData.tileCountX = clusterTileCountX;
    m_clusterData.tileCountY = clusterTileCountY;
    m_clusterData.width = clusterWidth;
    m_clusterData.height = clusterHeight;
    m_clusterData.listStride = kLightClusterStride;
    m_clusterData.enabled = true;
}

uint32_t LightListBuilder::tileLightCount(uint16_t tileX, uint16_t tileY) const {
    if (!m_data.enabled || tileX >= m_data.tileCountX || tileY >= m_data.tileCountY) {
        return 0;
    }
    const std::size_t row = static_cast<std::size_t>(tileY) * m_data.width + tileX * m_data.listStride;
    return row < m_data.texels.size() ? m_data.texels[row] : 0;
}

uint32_t LightListBuilder::tileLightIndex(uint16_t tileX, uint16_t tileY, uint32_t index) const {
    if (index >= tileLightCount(tileX, tileY)) {
        return 0;
    }
    const std::size_t row = static_cast<std::size_t>(tileY) * m_data.width + tileX * m_data.listStride;
    return m_data.texels[row + 1 + index];
}

uint32_t LightListBuilder::clusterLightCount(uint16_t tileX, uint16_t tileY, uint32_t slice) const {
    if (!m_clusterData.enabled || tileX >= m_clusterData.tileCountX || tileY >= m_clusterData.tileCountY ||
        slice >= kClusterSliceCount) {
        return 0;
    }
    const std::size_t row = static_cast<std::size_t>(tileY * kClusterSliceCount + slice) * m_clusterData.width;
    const std::size_t column = static_cast<std::size_t>(tileX) * kLightClusterStride;
    return m_clusterData.texels[row + column];
}

uint32_t LightListBuilder::clusterLightIndex(uint16_t tileX, uint16_t tileY, uint32_t slice, uint32_t index) const {
    if (index >= clusterLightCount(tileX, tileY, slice)) {
        return 0;
    }
    const std::size_t row = static_cast<std::size_t>(tileY * kClusterSliceCount + slice) * m_clusterData.width;
    const std::size_t column = static_cast<std::size_t>(tileX) * kLightClusterStride;
    return m_clusterData.texels[row + column + 1u + index];
}

} // namespace hbg::render
