#pragma once

#include "core/math.h"

#include <algorithm>
#include <limits>

namespace hbg {

struct Bounds {
    Vec3 min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
             std::numeric_limits<float>::max()};
    Vec3 max{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
             -std::numeric_limits<float>::max()};

    bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

    void expand(const Vec3& point) {
        min.x = std::min(min.x, point.x);
        min.y = std::min(min.y, point.y);
        min.z = std::min(min.z, point.z);
        max.x = std::max(max.x, point.x);
        max.y = std::max(max.y, point.y);
        max.z = std::max(max.z, point.z);
    }

    void expand(const Bounds& other) {
        if (!other.valid()) {
            return;
        }
        expand(other.min);
        expand(other.max);
    }

    Vec3 center() const { return Vec3((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f); }

    Vec3 extents() const {
        return Vec3((max.x - min.x) * 0.5f, (max.y - min.y) * 0.5f, (max.z - min.z) * 0.5f);
    }
};

inline Bounds transformBounds(const Mat4& matrix, const Bounds& bounds) {
    if (!bounds.valid()) {
        return {};
    }

    Bounds result{};
    for (uint32_t corner = 0; corner < 8; ++corner) {
        const Vec3 point((corner & 1) != 0 ? bounds.max.x : bounds.min.x,
                         (corner & 2) != 0 ? bounds.max.y : bounds.min.y,
                         (corner & 4) != 0 ? bounds.max.z : bounds.min.z);
        result.expand(transformPoint(matrix, point));
    }
    return result;
}

} // namespace hbg
