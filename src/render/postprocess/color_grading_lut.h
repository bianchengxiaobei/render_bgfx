#pragma once

#include "render/postprocess/post_process_settings.h"

#include <cstdint>
#include <vector>

namespace hbg::render {

constexpr uint32_t kColorGradingLutSize = 64;
constexpr uint32_t kColorGradingLutWidth = kColorGradingLutSize;
constexpr uint32_t kColorGradingLutHeight = kColorGradingLutSize * kColorGradingLutSize;

struct ColorGradingLut {
    bool valid = false;
    uint32_t size = kColorGradingLutSize;
    std::vector<uint16_t> texels;
    uint32_t uploadCount = 0;

    Vec3 sample(Vec3 logColor) const;
};

uint32_t colorGradingLutStripIndex(uint32_t x, uint32_t y, uint32_t z);
uint32_t colorGradingLutUploadBytes();

Vec3 linearToLogC(const Vec3& linear);
Vec3 logCToLinear(const Vec3& logC);
Vec3 tonemapColor(const Vec3& linear, TonemappingMode mode);
Vec3 colorGrade(const Vec3& linear, const ColorGradingSettings& settings);
Vec3 evaluateColorGradingLut(const Vec3& logColor, const ColorGradingSettings& settings, TonemappingMode mode);
bool buildColorGradingLut(const ColorGradingSettings& settings, TonemappingMode mode, ColorGradingLut& lut);

} // namespace hbg::render
