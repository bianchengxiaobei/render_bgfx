#include "render/camera/editor_camera.h"
#include "render/scene/render_scene.h"

#include <doctest/doctest.h>

#include <cmath>

using namespace hbg;
using namespace hbg::render;

namespace {

Camera makeCamera() {
    Camera camera;
    camera.projection = Camera::Projection::Perspective;
    const Vec3 eye(300.0f, 220.0f, 390.0f);
    const Vec3 target(100.0f, 0.0f, -60.0f);
    camera.world = inverse(lookAtRH(eye, target, Vec3(0.0f, 1.0f, 0.0f)));
    camera.yfov = 55.0f * 3.14159265358979323846f / 180.0f;
    camera.nearClip = 1.0f;
    camera.farClip = 5000.0f;
    return camera;
}

EditorCameraInput cameraInput(float x, float y, bool rightDown = false, bool middleDown = false, float scroll = 0.0f,
                              bool captured = false) {
    EditorCameraInput input;
    input.mouseX = x;
    input.mouseY = y;
    input.scrollY = scroll;
    input.rightDown = rightDown;
    input.middleDown = middleDown;
    input.mouseCaptured = captured;
    return input;
}

} // namespace

TEST_CASE("editor camera initializes from the authored camera target") {
    Camera camera = makeCamera();
    EditorCamera editorCamera;
    REQUIRE(editorCamera.init(camera, Bounds{}));
    CHECK(editorCamera.valid());
    CHECK(editorCamera.target().x == doctest::Approx(100.0f).epsilon(0.001));
    CHECK(editorCamera.target().y == doctest::Approx(0.0f).epsilon(0.001));
    CHECK(editorCamera.target().z == doctest::Approx(-60.0f).epsilon(0.001));
    const float expectedDistance = std::sqrt(200.0f * 200.0f + 220.0f * 220.0f + 450.0f * 450.0f);
    CHECK(editorCamera.distance() == doctest::Approx(expectedDistance).epsilon(0.002));
}

TEST_CASE("editor camera orbits, pans and zooms") {
    Camera camera = makeCamera();
    EditorCamera editorCamera;
    REQUIRE(editorCamera.init(camera, Bounds{}));
    const Vec3 originalTarget = editorCamera.target();
    const float originalDistance = editorCamera.distance();
    const float originalYaw = editorCamera.yaw();

    CHECK_FALSE(editorCamera.update(cameraInput(300.0f, 200.0f, true), camera));
    CHECK(editorCamera.update(cameraInput(360.0f, 200.0f, true), camera));
    CHECK(std::abs(editorCamera.yaw() - originalYaw) > 0.01f);
    CHECK(editorCamera.target() == originalTarget);

    const Vec3 eye(camera.world.values[12], camera.world.values[13], camera.world.values[14]);
    CHECK(length(eye - editorCamera.target()) == doctest::Approx(editorCamera.distance()).epsilon(0.01));

    CHECK_FALSE(editorCamera.update(cameraInput(360.0f, 200.0f, false, true), camera));
    CHECK(editorCamera.update(cameraInput(420.0f, 240.0f, false, true), camera));
    CHECK(editorCamera.target() != originalTarget);

    const float beforeZoom = editorCamera.distance();
    CHECK(editorCamera.update(cameraInput(420.0f, 240.0f, false, false, 1.0f), camera));
    CHECK(editorCamera.distance() < beforeZoom);
    CHECK(editorCamera.distance() < originalDistance);
}

TEST_CASE("editor camera ignores input while imgui captures the mouse") {
    Camera camera = makeCamera();
    EditorCamera editorCamera;
    REQUIRE(editorCamera.init(camera, Bounds{}));
    const float yaw = editorCamera.yaw();
    const float distance = editorCamera.distance();

    CHECK_FALSE(editorCamera.update(cameraInput(100.0f, 100.0f, true), camera));
    CHECK_FALSE(editorCamera.update(cameraInput(200.0f, 100.0f, true, false, 0.0f, true), camera));
    CHECK(editorCamera.yaw() == doctest::Approx(yaw));
    CHECK(editorCamera.distance() == doctest::Approx(distance));
}
