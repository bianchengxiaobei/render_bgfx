#pragma once

#include "core/bounds.h"

#include <array>

namespace hbg::render {

bool intersectsFrustum(const std::array<Vec4, 6>& planes, const Bounds& bounds);

} // namespace hbg::render
