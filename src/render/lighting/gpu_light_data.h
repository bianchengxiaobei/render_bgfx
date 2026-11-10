#pragma once

#include "core/math.h"

#include <array>
#include <cstdint>
#include <vector>

namespace hbg::render {

constexpr uint32_t kMaxGpuLights = 32;
constexpr uint32_t kGpuLightTexels = 4;
constexpr uint32_t kLightTileSize = 16;
constexpr uint32_t kLightBigTileSize = 64;
constexpr uint32_t kLightListMaxLights = 32;
constexpr uint32_t kLightListStride = kLightListMaxLights + 1;

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
    Vec3 primaryDirection{0.0f, 0.0f, 1.0f};
    Vec3 primaryColor{1.0f, 1.0f, 1.0f};
    float primaryIntensity = 0.0f;
    bool hasDirectional = false;
};

struct GpuLightListData {
    std::vector<uint32_t> texels;
    uint16_t tileCountX = 0;
    uint16_t tileCountY = 0;
    uint16_t bigTileCountX = 0;
    uint16_t bigTileCountY = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t maxLightsPerTile = 0;
    uint32_t maxLightsPerBigTile = 0;
    bool enabled = false;
};

} // namespace hbg::render
