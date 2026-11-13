#pragma once

#include "core/math.h"

#include <array>
#include <cstdint>
#include <vector>

namespace hbg::render {

constexpr uint32_t kMaxGpuLights = 128;
constexpr uint32_t kGpuLightTexels = 6;
constexpr uint32_t kLightTileSize = 16;
constexpr uint32_t kLightBigTileSize = 64;
constexpr uint32_t kLightListMaxLights = 128;
constexpr uint32_t kLightListStrideMax = kLightListMaxLights + 1;
constexpr uint32_t kLightListDefaultCapacity = 32;
constexpr uint32_t kClusterTileSize = 64;
constexpr uint32_t kClusterSliceCount = 32;
constexpr uint32_t kLightClusterMaxLights = 32;
constexpr uint32_t kLightClusterStride = kLightClusterMaxLights + 1;

constexpr bool isValidLightListCapacity(uint32_t capacity) {
    return capacity == 32 || capacity == 64 || capacity == 128;
}

constexpr uint32_t sanitizeLightListCapacity(uint32_t capacity) {
    return capacity >= 128 ? 128 : (capacity >= 64 ? 64 : 32);
}

constexpr uint32_t lightListStrideFor(uint32_t capacity) {
    return sanitizeLightListCapacity(capacity) + 1;
}


enum class GpuLightType : uint32_t {
    Directional = 0,
    Point = 1,
    Spot = 2,
};

struct GpuLightData {
    std::array<Vec4, kMaxGpuLights * kGpuLightTexels> texels{};
    uint32_t lightCount = 0;
    uint32_t directionalCount = 0;
    uint32_t punctualCount = 0;
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
    std::vector<uint32_t> texels;
    uint16_t tileCountX = 0;
    uint16_t tileCountY = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t listStride = 0;
    uint32_t maxLightsPerCluster = 0;
    uint32_t overflowClusters = 0;
    bool enabled = false;
};

} // namespace hbg::render
