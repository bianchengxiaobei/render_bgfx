#include "render/lighting/ltc_lut.h"

#include "render/lighting/ltc_data.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace hbg::render {
namespace {

float halfToFloat(uint16_t value) {
    const uint32_t sign = (static_cast<uint32_t>(value) >> 15) & 1u;
    const uint32_t exponent = (static_cast<uint32_t>(value) >> 10) & 0x1fu;
    const uint32_t mantissa = static_cast<uint32_t>(value) & 0x3ffu;
    float result = 0.0f;
    if (exponent == 0) {
        result = std::ldexp(static_cast<float>(mantissa) / 1024.0f, -14);
    } else if (exponent == 31) {
        result = mantissa == 0 ? INFINITY : NAN;
    } else {
        result = std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
                            static_cast<int>(exponent) - 15);
    }
    return sign != 0 ? -result : result;
}

std::array<Vec4, kLtcLutTexelCount> makeTable() {
    std::array<Vec4, kLtcLutTexelCount> table{};
    for (uint32_t index = 0; index < kLtcLutTexelCount; ++index) {
        const uint32_t base = index * 4u;
        table[index] = Vec4(halfToFloat(kLtcGgxHalf[base]), halfToFloat(kLtcGgxHalf[base + 1]),
                            halfToFloat(kLtcGgxHalf[base + 2]), halfToFloat(kLtcGgxHalf[base + 3]));
    }
    return table;
}

const std::array<Vec4, kLtcLutTexelCount> kTable = makeTable();

} // namespace

const std::array<Vec4, kLtcLutTexelCount>& ltcGgxTable() {
    return kTable;
}

} // namespace hbg::render
