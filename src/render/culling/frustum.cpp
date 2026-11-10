#include "render/culling/frustum.h"

#include <cmath>

namespace hbg::render {

bool intersectsFrustum(const std::array<Vec4, 6>& planes, const Bounds& bounds) {
    if (!bounds.valid()) {
        return true;
    }
    const Vec3 center = bounds.center();
    const Vec3 extents = bounds.extents();
    for (const Vec4& plane : planes) {
        const float distance = plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w;
        const float radius =
            std::abs(plane.x) * extents.x + std::abs(plane.y) * extents.y + std::abs(plane.z) * extents.z;
        if (distance + radius < 0.0f) {
            return false;
        }
    }
    return true;
}

} // namespace hbg::render
