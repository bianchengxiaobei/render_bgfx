#pragma once

#include "core/math.h"
#include "render/lighting/gpu_light_data.h"

#include <array>

namespace hbg::render {

const std::array<Vec4, kLtcLutTexelCount>& ltcGgxTable();

} // namespace hbg::render
