#include "render/scene/render_scene.h"

#include <cmath>

namespace hbg::render {
namespace {

constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;

Mat4 eulerRotation(const Vec3& degrees) {
    const float x = degrees.x * kDegreesToRadians;
    const float y = degrees.y * kDegreesToRadians;
    const float z = degrees.z * kDegreesToRadians;
    const float cx = std::cos(x);
    const float sx = std::sin(x);
    const float cy = std::cos(y);
    const float sy = std::sin(y);
    const float cz = std::cos(z);
    const float sz = std::sin(z);

    Mat4 rotationX = Mat4::identity();
    rotationX[5] = cx;
    rotationX[6] = sx;
    rotationX[9] = -sx;
    rotationX[10] = cx;

    Mat4 rotationY = Mat4::identity();
    rotationY[0] = cy;
    rotationY[2] = -sy;
    rotationY[8] = sy;
    rotationY[10] = cy;

    Mat4 rotationZ = Mat4::identity();
    rotationZ[0] = cz;
    rotationZ[1] = sz;
    rotationZ[4] = -sz;
    rotationZ[5] = cz;

    return rotationZ * rotationY * rotationX;
}

} // namespace

Mat4 vfxInstanceWorld(const VfxInstance& instance) {
    Mat4 scale = Mat4::identity();
    scale[0] = instance.scale.x;
    scale[5] = instance.scale.y;
    scale[10] = instance.scale.z;

    Mat4 translation = Mat4::identity();
    translation[12] = instance.position.x;
    translation[13] = instance.position.y;
    translation[14] = instance.position.z;
    return translation * eulerRotation(instance.rotation) * scale;
}

void RenderScene::recomputeBounds() {
    bounds = {};
    for (const RenderObject& object : objects) {
        bounds.expand(object.worldBounds);
    }
}

} // namespace hbg::render
