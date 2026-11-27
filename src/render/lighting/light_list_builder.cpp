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
constexpr uint32_t kLegacyClusterCountMask = 0xffu;
constexpr uint32_t kLegacyClusterOverflowShift = 8u;
constexpr uint32_t kLightListCountMask = 0xffffu;
constexpr uint32_t kLightListHalfwordShift = 16u;
constexpr uint32_t kLightListHalfwordMask = 0xffffu;
constexpr uint32_t kMaxTextureUploadWidth = std::numeric_limits<uint16_t>::max() / sizeof(uint32_t);
constexpr uint32_t kClusterIndexTextureWidth = 4096u;
constexpr uint32_t kActiveLightCategoryCount = 2u;

uint32_t lightListDword(uint32_t index) {
    return (index + 1u) >> 1u;
}

uint32_t lightListShift(uint32_t index) {
    return ((index + 1u) & 1u) * kLightListHalfwordShift;
}

void writeLightListIndex(std::vector<uint32_t>& words, std::size_t row, uint32_t index, uint32_t lightIndex) {
    words[row + lightListDword(index)] |= (lightIndex & kLightListHalfwordMask) << lightListShift(index);
}

uint32_t readLightListIndex(const std::vector<uint32_t>& words, std::size_t row, uint32_t index) {
    return (words[row + lightListDword(index)] >> lightListShift(index)) & kLightListHalfwordMask;
}

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

float viewDepth(const Mat4& view, const Vec3& position);

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

struct OrientedBox {
    Vec3 center;
    Vec3 axisX;
    Vec3 axisY;
    Vec3 axisZ;
};

bool makeAreaLightBox(const LightBounds& light, OrientedBox& box) {
    const float range = std::max(light.range, 0.0f);
    const float halfWidth = std::max(light.halfWidth, 0.0f);
    const float halfHeight = std::max(light.halfHeight, 0.0f);
    if (length(light.forward) <= 1.0e-8f || length(light.right) <= 1.0e-8f ||
        length(light.up) <= 1.0e-8f) {
        return false;
    }
    const Vec3 forward = normalize(light.forward);
    const Vec3 right = normalize(light.right);
    const Vec3 up = normalize(light.up);

    if (light.type == GpuLightType::AreaRectangle) {
        const float extentX = halfWidth + range;
        const float extentY = halfHeight + range;
        const float extentZ = range * 0.5f;
        box.center = light.position + forward * extentZ;
        box.axisX = right * extentX;
        box.axisY = up * extentY;
        box.axisZ = forward * extentZ;
        return true;
    }
    if (light.type == GpuLightType::AreaTube) {
        const float extentX = halfWidth + range;
        const float extentY = range;
        const float extentZ = range;
        box.center = light.position;
        box.axisX = right * extentX;
        box.axisY = up * extentY;
        box.axisZ = forward * extentZ;
        return true;
    }
    return false;
}

bool projectOrientedBounds(const Mat4& viewProj, const Mat4& view, const OrientedBox& box, float nearClip,
                           float farClip, float width, float height, ScreenRect& rect, float& minDepth,
                           float& maxDepth) {
    float minX = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();
    float nearestDepth = std::numeric_limits<float>::max();
    float farthestDepth = -std::numeric_limits<float>::max();
    bool anyBehind = false;
    bool anyInFront = false;

    for (uint32_t corner = 0; corner < 8; ++corner) {
        const Vec3 point = box.center +
                           box.axisX * ((corner & 1u) != 0u ? 1.0f : -1.0f) +
                           box.axisY * ((corner & 2u) != 0u ? 1.0f : -1.0f) +
                           box.axisZ * ((corner & 4u) != 0u ? 1.0f : -1.0f);
        const Vec4 clip = project(viewProj, point);
        if (clip.w <= 0.0f) {
            anyBehind = true;
            continue;
        }
        anyInFront = true;

        const float depth = viewDepth(view, point);
        nearestDepth = std::min(nearestDepth, depth);
        farthestDepth = std::max(farthestDepth, depth);

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
        minDepth = nearClip;
        maxDepth = farClip;
        return width > 0.0f && height > 0.0f;
    }
    if (maxX <= 0.0f || minX >= width || maxY <= 0.0f || minY >= height) {
        return false;
    }

    rect.minX = std::clamp(minX, 0.0f, width);
    rect.maxX = std::clamp(maxX, 0.0f, width);
    rect.minY = std::clamp(minY, 0.0f, height);
    rect.maxY = std::clamp(maxY, 0.0f, height);
    minDepth = std::clamp(nearestDepth, nearClip, farClip);
    maxDepth = std::clamp(farthestDepth, nearClip, farClip);
    return rect.maxX > rect.minX && rect.maxY > rect.minY && maxDepth >= minDepth;
}

bool overlaps(const LightScreenBounds& bounds, float minX, float maxX, float minY, float maxY) {
    return bounds.max.x > minX && bounds.min.x < maxX && bounds.max.y > minY && bounds.min.y < maxY;
}

uint32_t lightType(const GpuLightData& lights, uint32_t lightIndex) {
    return static_cast<uint32_t>(lights.texels[lightIndex * kGpuLightTexels + 3].w + 0.5f) & 7u;
}

uint32_t lightCategory(const GpuLightData& lights, uint32_t lightIndex) {
    return static_cast<uint32_t>(lights.texels[lightIndex * kGpuLightTexels + 6].y + 0.5f);
}

Vec3 lightPosition(const GpuLightData& lights, uint32_t lightIndex) {
    const Vec4& positionRange = lights.texels[lightIndex * kGpuLightTexels];
    return Vec3(positionRange.x, positionRange.y, positionRange.z);
}

float lightRange(const GpuLightData& lights, uint32_t lightIndex) {
    return std::max(lights.texels[lightIndex * kGpuLightTexels].w, 0.0f);
}

uint32_t lightFeatureFlags(const GpuLightData& lights, uint32_t lightIndex) {
    return static_cast<uint32_t>(lights.texels[lightIndex * kGpuLightTexels + 6].z + 0.5f);
}

float viewDepth(const Mat4& view, const Vec3& position) {
    return -(view[2] * position.x + view[6] * position.y + view[10] * position.z + view[14]);
}

std::size_t clusterOffsetIndex(uint16_t tileX, uint16_t tileY, uint32_t slice, uint32_t category,
                               uint16_t tileCountX, uint16_t tileCountY) {
    return ((static_cast<std::size_t>(category) * kClusterSliceCount + slice) * tileCountY + tileY) *
               tileCountX +
           tileX;
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
    return static_cast<uint16_t>(tileCount(renderHeight, kLightTileSize) * kLightCategoryCount);
}

uint16_t LightListBuilder::bigTileTextureWidthFor(uint16_t renderWidth, uint32_t capacity) {
    const uint32_t width =
        static_cast<uint32_t>(tileCount(renderWidth, kLightBigTileSize)) * lightListStrideFor(capacity);
    return width <= std::numeric_limits<uint16_t>::max() ? static_cast<uint16_t>(width) : 0;
}

uint16_t LightListBuilder::bigTileTextureHeightFor(uint16_t renderHeight) {
    return static_cast<uint16_t>(tileCount(renderHeight, kLightBigTileSize) * kLightCategoryCount);
}

uint16_t LightListBuilder::clusterOffsetTextureWidthFor(uint16_t renderWidth) {
    return tileCount(renderWidth, kClusterTileSize);
}

uint16_t LightListBuilder::clusterOffsetTextureHeightFor(uint16_t renderHeight) {
    const uint32_t height = static_cast<uint32_t>(tileCount(renderHeight, kClusterTileSize)) *
                            kClusterSliceCount * kLightCategoryCount;
    return height <= std::numeric_limits<uint16_t>::max() ? static_cast<uint16_t>(height) : 0;
}

uint16_t LightListBuilder::clusterIndexTextureWidthFor(uint32_t indexCount) {
    if (indexCount == 0) {
        return 0;
    }
    return static_cast<uint16_t>(kClusterIndexTextureWidth);
}

uint16_t LightListBuilder::clusterIndexTextureHeightFor(uint32_t indexCount) {
    const uint16_t width = clusterIndexTextureWidthFor(indexCount);
    if (width == 0) {
        return 0;
    }
    const uint32_t height = (indexCount + width - 1u) / width;
    return height <= std::numeric_limits<uint16_t>::max() ? static_cast<uint16_t>(height) : 0;
}

uint32_t LightListBuilder::clusterCapacityFor(uint16_t renderWidth, uint16_t renderHeight, uint32_t capacity) {
    const uint32_t clusterCount = static_cast<uint32_t>(tileCount(renderWidth, kClusterTileSize)) *
                                  tileCount(renderHeight, kClusterTileSize) * kClusterSliceCount;
    if (clusterCount == 0) {
        return 0;
    }
    const uint32_t requested = sanitizeLightListCapacity(capacity);
    const uint32_t maxByOffset = (kClusterOffsetMask + 1u) / clusterCount;
    for (const uint32_t tier : {128u, 64u, 32u}) {
        if (tier <= requested && tier <= maxByOffset) {
            return tier;
        }
    }
    return 0;
}

uint32_t LightListBuilder::clusterIndexCountFor(uint16_t renderWidth, uint16_t renderHeight, uint32_t capacity) {
    const uint32_t clusterCount = static_cast<uint32_t>(tileCount(renderWidth, kClusterTileSize)) *
                                  tileCount(renderHeight, kClusterTileSize) * kClusterSliceCount;
    return clusterCount * clusterCapacityFor(renderWidth, renderHeight, capacity) * kLightCategoryCount;
}

bool LightListBuilder::textureFits(uint16_t renderWidth, uint16_t renderHeight, uint32_t capacity,
                                   uint32_t maxTextureSize) {
    if (maxTextureSize == 0) {
        return false;
    }
    const uint16_t fptlWidth = textureWidthFor(renderWidth, capacity);
    const uint16_t fptlHeight = textureHeightFor(renderHeight);
    const uint16_t bigTileWidth = bigTileTextureWidthFor(renderWidth, capacity);
    const uint16_t bigTileHeight = bigTileTextureHeightFor(renderHeight);
    const uint32_t clusterIndexCapacity = clusterIndexCountFor(renderWidth, renderHeight, capacity);
    const uint32_t clusterOffsetCount =
        static_cast<uint32_t>(tileCount(renderWidth, kClusterTileSize)) *
        tileCount(renderHeight, kClusterTileSize) * kClusterSliceCount * kLightCategoryCount;
    const uint16_t clusterWidth =
        clusterIndexTextureWidthFor(clusterOffsetCount + clusterIndexCapacity);
    const uint16_t clusterHeight =
        clusterIndexTextureHeightFor(clusterOffsetCount + clusterIndexCapacity);
    const uint32_t widthLimit = std::min(maxTextureSize, kMaxTextureUploadWidth);
    return fptlWidth != 0 && fptlHeight != 0 && bigTileWidth != 0 && bigTileHeight != 0 && clusterWidth != 0 &&
           clusterHeight != 0 && fptlWidth <= widthLimit && fptlHeight <= maxTextureSize &&
           bigTileWidth <= widthLimit && bigTileHeight <= maxTextureSize && clusterWidth <= widthLimit &&
           clusterHeight <= maxTextureSize;
}

uint32_t LightListBuilder::textureCapacityFor(uint16_t renderWidth, uint16_t renderHeight,
                                              uint32_t requestedCapacity, uint32_t maxTextureSize) {
    const uint32_t requested = sanitizeLightListCapacity(requestedCapacity);
    const std::array<uint32_t, 3> capacities = {128u, 64u, 32u};
    for (uint32_t capacity : capacities) {
        if (capacity <= requested && textureFits(renderWidth, renderHeight, capacity, maxTextureSize)) {
            return capacity;
        }
    }
    return 0;
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

bool LightListBuilder::projectLightScreenBounds(const Mat4& viewProj, const Mat4& view, const LightBounds& light,
                                                float nearClip, float farClip, uint16_t width, uint16_t height,
                                                LightScreenBounds& bounds) {
    bounds = {};
    const float renderWidth = static_cast<float>(width);
    const float renderHeight = static_cast<float>(height);
    ScreenRect rect;
    OrientedBox box;
    if (makeAreaLightBox(light, box)) {
        float minDepth = 0.0f;
        float maxDepth = 0.0f;
        if (!projectOrientedBounds(viewProj, view, box, nearClip, farClip, renderWidth, renderHeight, rect,
                                   minDepth, maxDepth)) {
            return false;
        }
        bounds.min = Vec4(rect.minX, rect.minY, minDepth, maxDepth);
        bounds.max = Vec4(rect.maxX, rect.maxY, 1.0f, 0.0f);
        return true;
    }
    return projectLightScreenBounds(viewProj, view, light.position, light.range, nearClip, farClip, width, height,
                                    bounds);
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

    std::array<LightScreenBounds, kMaxGpuLights> lightBounds{};
    std::array<bool, kMaxGpuLights> lightVisible{};
    for (uint32_t lightIndex = lights.directionalCount;
         lightIndex < lights.lightCount && lightIndex < lightCapacity; ++lightIndex) {
        if (lightIndex >= kMaxGpuLights || lightType(lights, lightIndex) == 0 ||
            lightCategory(lights, lightIndex) >= kLightCategoryCount) {
            continue;
        }

        const uint32_t base = lightIndex * kGpuLightTexels;
        LightBounds light{};
        light.position = lightPosition(lights, lightIndex);
        light.range = lightRange(lights, lightIndex);
        light.type = static_cast<GpuLightType>(lightType(lights, lightIndex));
        light.forward = Vec3(lights.texels[base + 2].x, lights.texels[base + 2].y, lights.texels[base + 2].z);
        light.right = Vec3(lights.texels[base + 8].x, lights.texels[base + 8].y, lights.texels[base + 8].z);
        light.up = Vec3(lights.texels[base + 9].x, lights.texels[base + 9].y, lights.texels[base + 9].z);
        light.halfWidth = lights.texels[base + 8].w;
        light.halfHeight = lights.texels[base + 9].w;
        lightVisible[lightIndex] = projectLightScreenBounds(viewProj, view, light, nearClip, farClip, width, height,
                                                            lightBounds[lightIndex]);
    }

    const uint32_t bigTileCount = static_cast<uint32_t>(bigTileCountX) * bigTileCountY;
    std::array<std::vector<std::vector<uint32_t>>, kLightCategoryCount> bigLists;
    for (auto& categoryLists : bigLists) {
        categoryLists.resize(bigTileCount);
    }
    for (uint32_t category = 0; category < kActiveLightCategoryCount; ++category) {
        for (uint16_t bigY = 0; bigY < bigTileCountY; ++bigY) {
            const float tileMinY = static_cast<float>(bigY) * kLightBigTileSize;
            const float tileMaxY = std::min(tileMinY + kLightBigTileSize, static_cast<float>(height));
            for (uint16_t bigX = 0; bigX < bigTileCountX; ++bigX) {
                const float tileMinX = static_cast<float>(bigX) * kLightBigTileSize;
                const float tileMaxX = std::min(tileMinX + kLightBigTileSize, static_cast<float>(width));
                std::vector<uint32_t>& list =
                    bigLists[category][static_cast<std::size_t>(bigY) * bigTileCountX + bigX];
                for (uint32_t lightIndex = lights.directionalCount;
                     lightIndex < lights.lightCount && lightIndex < lightCapacity; ++lightIndex) {
                    if (list.size() >= lightCapacity) {
                        break;
                    }
                    if (lightCategory(lights, lightIndex) == category && lightVisible[lightIndex] &&
                        overlaps(lightBounds[lightIndex], tileMinX, tileMaxX, tileMinY, tileMaxY)) {
                        list.push_back(lightIndex);
                    }
                }
                m_data.maxLightsPerBigTile =
                    std::max(m_data.maxLightsPerBigTile, static_cast<uint32_t>(list.size()));
            }
        }
    }

    m_data.texels.assign(static_cast<std::size_t>(textureWidth) * tileCountY * kLightCategoryCount, 0u);
    m_data.tileFeatureFlags.assign(static_cast<std::size_t>(tileCountX) * tileCountY, 0u);
    for (uint16_t tileY = 0; tileY < tileCountY; ++tileY) {
        const float tileMinY = static_cast<float>(tileY) * kLightTileSize;
        const float tileMaxY = std::min(tileMinY + kLightTileSize, static_cast<float>(height));
        for (uint16_t tileX = 0; tileX < tileCountX; ++tileX) {
            const float tileMinX = static_cast<float>(tileX) * kLightTileSize;
            const float tileMaxX = std::min(tileMinX + kLightTileSize, static_cast<float>(width));
            uint32_t tileFlags =
                lights.directionalCount > 0 ? static_cast<uint32_t>(kLightFeatureDirectional) : 0u;
            for (uint32_t category = 0; category < kActiveLightCategoryCount; ++category) {
                const std::vector<uint32_t>& bigList =
                    bigLists[category][static_cast<std::size_t>(tileY / (kLightBigTileSize / kLightTileSize)) *
                                           bigTileCountX +
                                       tileX / (kLightBigTileSize / kLightTileSize)];
                const std::size_t row =
                    (static_cast<std::size_t>(category) * tileCountY + tileY) * textureWidth +
                    tileX * listStride;
                uint32_t count = 0;
                for (uint32_t lightIndex : bigList) {
                    if (count >= lightCapacity) {
                        break;
                    }
                    if (lightIndex < lightCapacity && lightVisible[lightIndex] &&
                        overlaps(lightBounds[lightIndex], tileMinX, tileMaxX, tileMinY, tileMaxY)) {
                        writeLightListIndex(m_data.texels, row, count, lightIndex);
                        tileFlags |= lightFeatureFlags(lights, lightIndex);
                        ++count;
                    }
                }
                m_data.texels[row] |= count & kLightListCountMask;
                m_data.maxLightsPerTile = std::max(m_data.maxLightsPerTile, count);
            }
            m_data.tileFeatureFlags[static_cast<std::size_t>(tileY) * tileCountX + tileX] = tileFlags;
            const std::size_t flagWord =
                static_cast<std::size_t>(tileY) * textureWidth + tileX * listStride + listStride - 1u;
            m_data.texels[flagWord] =
                (m_data.texels[flagWord] & 0xffffu) | ((tileFlags & 0xffffu) << 16u);
        }
    }

    m_data.tileCountX = tileCountX;
    m_data.tileCountY = tileCountY;
    m_data.bigTileCountX = bigTileCountX;
    m_data.bigTileCountY = bigTileCountY;
    m_data.width = textureWidth;
    m_data.height = static_cast<uint16_t>(tileCountY * kLightCategoryCount);
    m_data.capacity = lightCapacity;
    m_data.listStride = listStride;
    m_data.enabled = true;

    const uint16_t clusterTileCountX = tileCount(width, kClusterTileSize);
    const uint16_t clusterTileCountY = tileCount(height, kClusterTileSize);
    const uint32_t clusterCapacity = clusterCapacityFor(width, height, lightCapacity);
    if (clusterTileCountX == 0 || clusterTileCountY == 0 || clusterCapacity == 0) {
        return;
    }

    const std::size_t clusterCount =
        static_cast<std::size_t>(clusterTileCountX) * clusterTileCountY * kClusterSliceCount;
    m_clusterData.tileFeatureFlags.assign(static_cast<std::size_t>(clusterTileCountX) * clusterTileCountY, 0u);
    for (uint16_t tileY = 0; tileY < clusterTileCountY; ++tileY) {
        const uint16_t bigTileY = std::min<uint16_t>(tileY, static_cast<uint16_t>(bigTileCountY - 1u));
        for (uint16_t tileX = 0; tileX < clusterTileCountX; ++tileX) {
            const uint16_t bigTileX = std::min<uint16_t>(tileX, static_cast<uint16_t>(bigTileCountX - 1u));
            uint32_t flags =
                lights.directionalCount > 0 ? static_cast<uint32_t>(kLightFeatureDirectional) : 0u;
            for (uint32_t category = 0; category < kActiveLightCategoryCount; ++category) {
                for (uint32_t lightIndex : bigLists[category]
                                                  [static_cast<std::size_t>(bigTileY) * bigTileCountX + bigTileX]) {
                    flags |= lightFeatureFlags(lights, lightIndex);
                }
            }
            m_clusterData.tileFeatureFlags[static_cast<std::size_t>(tileY) * clusterTileCountX + tileX] = flags;
        }
    }
    std::array<std::vector<std::vector<uint32_t>>, kLightCategoryCount> clusterLists;
    std::array<std::vector<uint8_t>, kLightCategoryCount> truncated;
    for (uint32_t category = 0; category < kActiveLightCategoryCount; ++category) {
        clusterLists[category].resize(clusterCount);
        truncated[category].assign(clusterCount, 0u);
    }

    for (uint32_t category = 0; category < kActiveLightCategoryCount; ++category) {
        for (uint32_t lightIndex = lights.directionalCount;
             lightIndex < lights.lightCount && lightIndex < lightCapacity && lightIndex < lightVisible.size();
             ++lightIndex) {
            if (!lightVisible[lightIndex] || lightType(lights, lightIndex) == 0 ||
                lightCategory(lights, lightIndex) != category) {
                continue;
            }

            const LightScreenBounds& bounds = lightBounds[lightIndex];
            const float minDepth = bounds.min.z;
            const float maxDepth = bounds.min.w;
            if (maxDepth < minDepth) {
                continue;
            }

            const int sliceMin = clusterSliceForDepth(minDepth, nearClip, farClip);
            const int sliceMax = clusterSliceForDepth(maxDepth, nearClip, farClip);
            const float clusterTileSize = static_cast<float>(kClusterTileSize);
            const int minTileX = std::clamp(static_cast<int>(std::floor(bounds.min.x / clusterTileSize)), 0,
                                            static_cast<int>(clusterTileCountX) - 1);
            const int maxTileX = std::clamp(static_cast<int>(std::ceil(bounds.max.x / clusterTileSize)) - 1, 0,
                                            static_cast<int>(clusterTileCountX) - 1);
            const int minTileY = std::clamp(static_cast<int>(std::floor(bounds.min.y / clusterTileSize)), 0,
                                            static_cast<int>(clusterTileCountY) - 1);
            const int maxTileY = std::clamp(static_cast<int>(std::ceil(bounds.max.y / clusterTileSize)) - 1, 0,
                                            static_cast<int>(clusterTileCountY) - 1);
            for (int tileY = minTileY; tileY <= maxTileY; ++tileY) {
                for (int tileX = minTileX; tileX <= maxTileX; ++tileX) {
                    for (int slice = sliceMin; slice <= sliceMax; ++slice) {
                        const std::size_t clusterIndex =
                            (static_cast<std::size_t>(tileY) * clusterTileCountX + tileX) * kClusterSliceCount +
                            static_cast<std::size_t>(slice);
                        std::vector<uint32_t>& list = clusterLists[category][clusterIndex];
                        if (list.size() < clusterCapacity) {
                            list.push_back(lightIndex);
                        } else {
                            truncated[category][clusterIndex] = 1u;
                        }
                    }
                }
            }
        }
    }

    m_clusterData.offsets.assign(clusterCount * kLightCategoryCount, 0u);
    m_clusterData.truncated.assign(clusterCount * kLightCategoryCount, 0u);
    m_clusterData.indices.clear();
    m_clusterData.indices.reserve(clusterCount * clusterCapacity * kLightCategoryCount);
    for (uint32_t category = 0; category < kActiveLightCategoryCount; ++category) {
        for (uint16_t tileY = 0; tileY < clusterTileCountY; ++tileY) {
            for (uint16_t tileX = 0; tileX < clusterTileCountX; ++tileX) {
                for (uint32_t slice = 0; slice < kClusterSliceCount; ++slice) {
                    const std::size_t listIndex =
                        (static_cast<std::size_t>(tileY) * clusterTileCountX + tileX) * kClusterSliceCount + slice;
                    const std::size_t offsetIndex =
                        clusterOffsetIndex(tileX, tileY, slice, category, clusterTileCountX, clusterTileCountY);
                    const std::vector<uint32_t>& list = clusterLists[category][listIndex];
                    const uint32_t count = static_cast<uint32_t>(list.size());
                    const uint32_t offset = static_cast<uint32_t>(m_clusterData.indices.size());
                    m_clusterData.offsets[offsetIndex] = packClusterOffsetCount(offset, count);
                    m_clusterData.indices.insert(m_clusterData.indices.end(), list.begin(), list.end());
                    m_clusterData.maxLightsPerCluster = std::max(m_clusterData.maxLightsPerCluster, count);
                    if (truncated[category][listIndex] != 0u) {
                        m_clusterData.truncated[offsetIndex] = 1u;
                        ++m_clusterData.truncatedClusters;
                    }
                }
            }
        }
    }

    m_clusterData.tileCountX = clusterTileCountX;
    m_clusterData.tileCountY = clusterTileCountY;
    m_clusterData.capacity = clusterCapacity;
    m_clusterData.indexCapacity =
        static_cast<uint32_t>(clusterCount) * clusterCapacity * kLightCategoryCount;
    m_clusterData.usedIndexCount = static_cast<uint32_t>(m_clusterData.indices.size());
    m_clusterData.enabled = true;
}

uint32_t LightListBuilder::tileLightCount(uint16_t tileX, uint16_t tileY, uint32_t category) const {
    if (!m_data.enabled || tileX >= m_data.tileCountX || tileY >= m_data.tileCountY ||
        category >= kLightCategoryCount) {
        return 0;
    }
    const std::size_t row =
        (static_cast<std::size_t>(category) * m_data.tileCountY + tileY) * m_data.width +
        tileX * m_data.listStride;
    return row < m_data.texels.size() ? (m_data.texels[row] & kLightListCountMask) : 0;
}

uint32_t LightListBuilder::tileLightIndex(uint16_t tileX, uint16_t tileY, uint32_t index,
                                          uint32_t category) const {
    if (index >= tileLightCount(tileX, tileY, category)) {
        return 0;
    }
    const std::size_t row =
        (static_cast<std::size_t>(category) * m_data.tileCountY + tileY) * m_data.width +
        tileX * m_data.listStride;
    return readLightListIndex(m_data.texels, row, index);
}

uint32_t LightListBuilder::tileFeatureFlags(uint16_t tileX, uint16_t tileY) const {
    if (!m_data.enabled || tileX >= m_data.tileCountX || tileY >= m_data.tileCountY) {
        return 0;
    }
    const std::size_t index = static_cast<std::size_t>(tileY) * m_data.tileCountX + tileX;
    return index < m_data.tileFeatureFlags.size() ? m_data.tileFeatureFlags[index] : 0;
}

uint32_t LightListBuilder::clusterOffset(uint16_t tileX, uint16_t tileY, uint32_t slice,
                                         uint32_t category) const {
    if (!m_clusterData.enabled || tileX >= m_clusterData.tileCountX || tileY >= m_clusterData.tileCountY ||
        slice >= kClusterSliceCount || category >= kLightCategoryCount) {
        return 0;
    }
    const std::size_t index = clusterOffsetIndex(tileX, tileY, slice, category, m_clusterData.tileCountX,
                                                 m_clusterData.tileCountY);
    return index < m_clusterData.offsets.size() ? unpackClusterOffset(m_clusterData.offsets[index]) : 0;
}

uint32_t LightListBuilder::clusterLightCount(uint16_t tileX, uint16_t tileY, uint32_t slice,
                                             uint32_t category) const {
    if (!m_clusterData.enabled || tileX >= m_clusterData.tileCountX || tileY >= m_clusterData.tileCountY ||
        slice >= kClusterSliceCount || category >= kLightCategoryCount) {
        return 0;
    }
    const std::size_t index = clusterOffsetIndex(tileX, tileY, slice, category, m_clusterData.tileCountX,
                                                 m_clusterData.tileCountY);
    return index < m_clusterData.offsets.size() ? unpackClusterCount(m_clusterData.offsets[index]) : 0;
}

uint32_t LightListBuilder::clusterTruncated(uint16_t tileX, uint16_t tileY, uint32_t slice,
                                            uint32_t category) const {
    if (!m_clusterData.enabled || tileX >= m_clusterData.tileCountX ||
        tileY >= m_clusterData.tileCountY || slice >= kClusterSliceCount ||
        category >= kLightCategoryCount) {
        return 0;
    }
    const std::size_t index = clusterOffsetIndex(tileX, tileY, slice, category, m_clusterData.tileCountX,
                                                 m_clusterData.tileCountY);
    return index < m_clusterData.truncated.size() ? m_clusterData.truncated[index] : 0u;
}

uint32_t LightListBuilder::clusterLightIndex(uint16_t tileX, uint16_t tileY, uint32_t slice, uint32_t index,
                                             uint32_t category) const {
    if (index >= clusterLightCount(tileX, tileY, slice, category)) {
        return 0;
    }
    const uint32_t offset = clusterOffset(tileX, tileY, slice, category);
    const std::size_t position = static_cast<std::size_t>(offset) + index;
    return position < m_clusterData.indices.size() ? m_clusterData.indices[position] : 0;
}

} // namespace hbg::render
