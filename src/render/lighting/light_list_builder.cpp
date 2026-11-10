#include "render/lighting/light_list_builder.h"

#include "core/bounds.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace hbg::render {
namespace {

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

} // namespace

uint16_t LightListBuilder::tileCount(uint16_t dimension, uint16_t tileSize) {
    if (dimension == 0 || tileSize == 0) {
        return 0;
    }
    return static_cast<uint16_t>((static_cast<uint32_t>(dimension) + tileSize - 1u) / tileSize);
}

uint16_t LightListBuilder::textureWidthFor(uint16_t renderWidth) {
    const uint32_t width = static_cast<uint32_t>(tileCount(renderWidth, kLightTileSize)) * kLightListStride;
    return width <= std::numeric_limits<uint16_t>::max() ? static_cast<uint16_t>(width) : 0;
}

uint16_t LightListBuilder::textureHeightFor(uint16_t renderHeight) {
    return tileCount(renderHeight, kLightTileSize);
}

void LightListBuilder::clear() {
    m_data = {};
}

void LightListBuilder::build(const GpuLightData& lights, const Mat4& viewProj, uint16_t width, uint16_t height) {
    clear();
    if (width == 0 || height == 0 || lights.lightCount == 0) {
        return;
    }

    const uint16_t tileCountX = tileCount(width, kLightTileSize);
    const uint16_t tileCountY = tileCount(height, kLightTileSize);
    const uint16_t bigTileCountX = tileCount(width, kLightBigTileSize);
    const uint16_t bigTileCountY = tileCount(height, kLightBigTileSize);
    const uint16_t textureWidth = textureWidthFor(width);
    if (tileCountX == 0 || tileCountY == 0 || textureWidth == 0) {
        return;
    }

    std::array<ScreenRect, kMaxGpuLights> lightRects{};
    std::array<bool, kMaxGpuLights> lightVisible{};
    for (uint32_t index = 0; index < lights.punctualCount; ++index) {
        const uint32_t lightIndex = lights.directionalCount + index;
        if (lightIndex >= lights.lightCount || lightIndex >= kMaxGpuLights || lightType(lights, lightIndex) == 0) {
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
                 lightIndex < lights.lightCount && lightIndex < kMaxGpuLights; ++lightIndex) {
                if (list.size() >= kLightListMaxLights) {
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
            const std::size_t row = static_cast<std::size_t>(tileY) * textureWidth + tileX * kLightListStride;
            uint32_t count = 0;
            for (uint32_t lightIndex : bigList) {
                if (count >= kLightListMaxLights) {
                    break;
                }
                if (lightIndex < kMaxGpuLights && lightVisible[lightIndex] &&
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
    m_data.enabled = true;
}

uint32_t LightListBuilder::tileLightCount(uint16_t tileX, uint16_t tileY) const {
    if (!m_data.enabled || tileX >= m_data.tileCountX || tileY >= m_data.tileCountY) {
        return 0;
    }
    const std::size_t row = static_cast<std::size_t>(tileY) * m_data.width + tileX * kLightListStride;
    return row < m_data.texels.size() ? m_data.texels[row] : 0;
}

uint32_t LightListBuilder::tileLightIndex(uint16_t tileX, uint16_t tileY, uint32_t index) const {
    if (index >= tileLightCount(tileX, tileY)) {
        return 0;
    }
    const std::size_t row = static_cast<std::size_t>(tileY) * m_data.width + tileX * kLightListStride;
    return m_data.texels[row + 1 + index];
}

} // namespace hbg::render
