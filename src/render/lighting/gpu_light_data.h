#pragma once

#include "core/math.h"

#include <array>
#include <cstdint>
#include <vector>

namespace hbg::render {

constexpr uint32_t kMaxGpuLights = 128;
constexpr uint32_t kLightBaseTexelCount = 11;
constexpr uint32_t kLightIesBaseTexel = kLightBaseTexelCount;
constexpr uint32_t kLightIesSampleCount = 8;
constexpr uint32_t kGpuLightTexels = kLightIesBaseTexel + kLightIesSampleCount;
constexpr uint32_t kLightCookieSize = 8;
constexpr uint32_t kCookieTileSize = kLightCookieSize;
constexpr uint32_t kCookieAtlasWidth = 64;
constexpr uint32_t kCookieAtlasHeight = 128;
constexpr uint32_t kCookieAtlasTilesX = kCookieAtlasWidth / kCookieTileSize;
constexpr uint32_t kCookieAtlasTilesY = kCookieAtlasHeight / kCookieTileSize;
constexpr uint32_t kCookieAtlasTileCapacity = kCookieAtlasTilesX * kCookieAtlasTilesY;
constexpr uint32_t kCookieAtlasOriginX = kGpuLightTexels;
constexpr uint32_t kLightDataTextureWidth = kGpuLightTexels + kCookieAtlasWidth;
static_assert(kCookieAtlasOriginX + kCookieAtlasWidth == kLightDataTextureWidth);
static_assert(kCookieAtlasTileCapacity == kMaxGpuLights);
constexpr uint32_t kLtcLutSize = 64;
constexpr uint32_t kLtcLutTexelCount = kLtcLutSize * kLtcLutSize;
constexpr uint32_t kLtcLutTexelRows = kLtcLutSize;
constexpr uint32_t kLightDataTextureRows = kMaxGpuLights + kLtcLutTexelRows;
constexpr uint32_t kLightTileSize = 16;
constexpr uint32_t kLightBigTileSize = 64;
constexpr uint32_t kLightListMaxLights = 128;
constexpr uint32_t kLightListDwordStrideMax = (kLightListMaxLights + 2) / 2;
constexpr uint32_t kLightListStrideMax = kLightListDwordStrideMax;
constexpr uint32_t kLightListDefaultCapacity = 64;
constexpr uint32_t kClusterTileSize = 64;
constexpr uint32_t kClusterSliceCount = 32;
constexpr uint32_t kLightClusterMaxLights = 32;
constexpr uint32_t kLightClusterStride = kLightClusterMaxLights + 1;
constexpr uint32_t kLightCategoryCount = 4;
constexpr uint32_t kClusterCountBits = 8;
constexpr uint32_t kClusterOffsetBits = 24;
constexpr uint32_t kClusterOffsetMask = (1u << kClusterOffsetBits) - 1u;
constexpr uint32_t kClusterCountMask = (1u << kClusterCountBits) - 1u;
constexpr uint32_t kRenderingLayersMask = 0xffffu;

enum class LightCategory : uint32_t {
    Punctual = 0,
    Area = 1,
    Env = 2,
    Decal = 3,
};

enum LightFeatureFlags : uint32_t {
    kLightFeaturePunctual = 1u << 0,
    kLightFeatureArea = 1u << 1,
    kLightFeatureDirectional = 1u << 2,
    kLightFeatureEnv = 1u << 3,
    kLightFeatureSky = 1u << 4,
    kLightFeatureSSRefraction = 1u << 5,
    kLightFeatureSSReflection = 1u << 6,
    kLightFeatureSSGI = 1u << 7,
};

constexpr bool isValidLightListCapacity(uint32_t capacity) {
    return capacity == 32 || capacity == 64 || capacity == 128;
}

constexpr uint32_t sanitizeLightListCapacity(uint32_t capacity) {
    return capacity >= 128 ? 128 : (capacity >= 64 ? 64 : 32);
}

static_assert(kMaxGpuLights <= 0xffffu, "packed light-list indices are 16-bit");
static_assert(kLightListMaxLights <= 0xffffu, "packed light-list indices are 16-bit");
static_assert(kClusterSliceCount <= 0xffu, "cluster count is packed into 8 bits");

constexpr uint32_t lightListDwordStrideFor(uint32_t capacity) {
    return (sanitizeLightListCapacity(capacity) + 2) / 2;
}

constexpr uint32_t lightListStrideFor(uint32_t capacity) {
    return lightListDwordStrideFor(capacity);
}

constexpr uint32_t packClusterOffsetCount(uint32_t offset, uint32_t count) {
    return (offset & kClusterOffsetMask) | ((count & kClusterCountMask) << kClusterOffsetBits);
}

constexpr uint32_t unpackClusterOffset(uint32_t packed) {
    return packed & kClusterOffsetMask;
}

constexpr uint32_t unpackClusterCount(uint32_t packed) {
    return (packed >> kClusterOffsetBits) & kClusterCountMask;
}

constexpr bool lightLayerMatches(uint32_t lightMask, uint32_t objectMask) {
    return (lightMask & objectMask & kRenderingLayersMask) != 0u;
}


enum class GpuLightType : uint32_t {
    Directional = 0,
    Point = 1,
    Spot = 2,
    AreaRectangle = 3,
    AreaTube = 4,
    AreaDisc = 5,
};

static_assert(static_cast<uint32_t>(GpuLightType::AreaDisc) < 8u, "light type uses three flag bits");

struct GpuLightData {
    std::array<Vec4, kMaxGpuLights * kGpuLightTexels> texels{};
    uint32_t lightCount = 0;
    uint32_t directionalCount = 0;
    uint32_t punctualCount = 0;
    uint32_t areaCount = 0;
    uint32_t shadowedCount = 0;
    uint32_t truncatedLightCount = 0;
    Vec3 primaryDirection{0.0f, 0.0f, 1.0f};
    Vec3 primaryColor{1.0f, 1.0f, 1.0f};
    float primaryIntensity = 0.0f;
    bool hasDirectional = false;
};

struct LightScreenBounds {
    Vec4 min; // minX, minY, minViewDepth, maxViewDepth
    Vec4 max; // maxX, maxY, visible, 0
};

struct GpuLightListData {
    std::vector<uint32_t> texels;
    std::vector<uint32_t> tileFeatureFlags;
    uint16_t tileCountX = 0;
    uint16_t tileCountY = 0;
    uint16_t bigTileCountX = 0;
    uint16_t bigTileCountY = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t capacity = 0;
    uint32_t listStride = 0;
    uint32_t maxLightsPerTile = 0;
    uint32_t maxLightsPerBigTile = 0;
    bool enabled = false;
};

struct GpuLightClusterData {
    std::vector<uint32_t> offsets;
    std::vector<uint32_t> indices;
    std::vector<uint32_t> tileFeatureFlags;
    std::vector<uint8_t> truncated;
    uint16_t tileCountX = 0;
    uint16_t tileCountY = 0;
    uint32_t capacity = 0;
    uint32_t indexCapacity = 0;
    uint32_t usedIndexCount = 0;
    uint32_t maxLightsPerCluster = 0;
    uint32_t truncatedClusters = 0;
    bool enabled = false;
};

} // namespace hbg::render
