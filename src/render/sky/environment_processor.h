#pragma once

#include "asset/texture_loader.h"
#include "core/math.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hbg::render {

constexpr uint32_t kEnvironmentMaxWidth = 1024;
constexpr uint32_t kEnvironmentSkyMaxWidth = 2048;
constexpr uint32_t kEnvironmentMaxMipCount = 7;
constexpr uint32_t kEnvironmentPrefilterSamples = 32;

struct EnvironmentImage {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipCount = 0;
    std::vector<std::vector<float>> mips;
    std::array<Vec4, 7> sh{};

    bool valid() const { return width > 0 && height > 0 && mipCount > 0 && mips.size() == mipCount; }
};

class EnvironmentProcessor {
  public:
    static bool build(const asset::ImageAsset& source, float rotationDegrees, EnvironmentImage& environment,
                      std::string* error = nullptr);
    static bool buildSky(const asset::ImageAsset& source, float rotationDegrees, EnvironmentImage& environment,
                         std::string* error = nullptr);

    static Vec3 evaluateSH(const std::array<Vec4, 7>& coefficients, const Vec3& normal);
    static Vec2 directionToEquirect(const Vec3& direction);
    static Vec3 equirectToDirection(const Vec2& uv);
};

} // namespace hbg::render
