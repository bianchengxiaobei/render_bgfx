#pragma once

#include "vfx/graph.h"
#include "vfx/handles.h"

#include <array>
#include <span>
#include <string>

namespace vfx {

struct PropertyValue {
    PropertyType type = PropertyType::Float;
    std::array<float, 4> f{};
    TextureHandle texture{};
    std::span<const CurveKey> curve{};
    std::span<const GradientKey> gradient{};
};

struct EventAttribute {
    std::string name;
    PropertyValue value;
};

} // namespace vfx
