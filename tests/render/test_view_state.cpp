#include "render/camera/history_buffer.h"
#include "render/camera/resolution_group.h"
#include "render/camera/view_state.h"
#include "render/scene/render_scene.h"

#include <doctest/doctest.h>

#include <cmath>

using namespace hbg;
using namespace hbg::render;

namespace {

void checkMatrix(const Mat4& actual, const Mat4& expected) {
    for (std::size_t index = 0; index < 16; ++index) {
        CHECK(actual[index] == doctest::Approx(expected[index]).epsilon(1.0e-4));
    }
}

Camera makeCamera(const Vec3& position) {
    Camera camera;
    camera.world = inverse(lookAtRH(position, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)));
    camera.yfov = 1.0471975512f;
    camera.nearClip = 0.1f;
    camera.farClip = 100.0f;
    return camera;
}

Vec4 projectNdc(const Mat4& viewProj, const Vec3& worldPosition) {
    const float clipX =
        viewProj[0] * worldPosition.x + viewProj[4] * worldPosition.y + viewProj[8] * worldPosition.z + viewProj[12];
    const float clipY =
        viewProj[1] * worldPosition.x + viewProj[5] * worldPosition.y + viewProj[9] * worldPosition.z + viewProj[13];
    const float clipW =
        viewProj[3] * worldPosition.x + viewProj[7] * worldPosition.y + viewProj[11] * worldPosition.z + viewProj[15];
    return Vec4(clipX / clipW, clipY / clipW, 0.0f, clipW);
}

} // namespace

TEST_CASE("HistoryBuffer keeps current and previous frames in two slots") {
    HistoryBuffer<int> history;
    CHECK_FALSE(history.hasHistory());
    CHECK(history.frameCount() == 0);

    history.push(1);
    CHECK(history.current() == 1);
    CHECK(history.previous() == 1);
    CHECK_FALSE(history.hasHistory());
    CHECK(history.frameCount() == 1);

    history.push(2);
    CHECK(history.current() == 2);
    CHECK(history.previous() == 1);
    CHECK(history.hasHistory());
    CHECK(history.frameCount() == 2);

    history.reset(7);
    CHECK(history.current() == 7);
    CHECK(history.previous() == 7);
    CHECK_FALSE(history.hasHistory());
    CHECK(history.frameCount() == 0);
}

TEST_CASE("ViewResolution separates dynamic render and final target sizes") {
    const ViewResolution resolution = makeViewResolution(1280, 720, 0.5f);
    CHECK(resolution.targetWidth == 1280);
    CHECK(resolution.targetHeight == 720);
    CHECK(resolution.renderWidth == 640);
    CHECK(resolution.renderHeight == 360);
    CHECK(resolution.width(ResolutionGroup::BeforeDynamicResUpscale) == 640);
    CHECK(resolution.height(ResolutionGroup::BeforeDynamicResUpscale) == 360);
    CHECK(resolution.width(ResolutionGroup::AfterDynamicResUpscale) == 1280);
    CHECK(resolution.height(ResolutionGroup::AfterDynamicResUpscale) == 720);

    const Vec4 before = resolution.screenSize(ResolutionGroup::BeforeDynamicResUpscale);
    CHECK(before.x == doctest::Approx(640.0f));
    CHECK(before.y == doctest::Approx(360.0f));
    CHECK(before.z == doctest::Approx(1.0f / 640.0f));
    CHECK(before.w == doctest::Approx(1.0f / 360.0f));

    const Vec4 after = resolution.screenSize(ResolutionGroup::AfterDynamicResUpscale);
    CHECK(after.x == doctest::Approx(1280.0f));
    CHECK(after.y == doctest::Approx(720.0f));

    const Vec4 dynamicScale = resolution.dynamicResolutionScale();
    CHECK(dynamicScale.x == doctest::Approx(0.5f));
    CHECK(dynamicScale.y == doctest::Approx(0.5f));
    CHECK(dynamicScale.z == doctest::Approx(0.0f));
    CHECK(dynamicScale.w == doctest::Approx(0.0f));

    const ViewResolution clamped = makeViewResolution(100, 100, 0.0f);
    CHECK(clamped.renderWidth == 10);
    CHECK(clamped.renderHeight == 10);
}

TEST_CASE("ViewState builds jittered and non-jittered camera matrices") {
    ViewState state;
    const Camera camera = makeCamera(Vec3(0.0f, 0.0f, 5.0f));
    state.update(camera, 1280, 720, 1.0f, false, false);

    const ViewConstants& constants = state.constants();
    checkMatrix(constants.view, inverse(camera.world));
    checkMatrix(constants.invView * constants.view, Mat4::identity());
    checkMatrix(constants.invProj * constants.proj, Mat4::identity());
    checkMatrix(constants.invViewProj * constants.viewProj, Mat4::identity());
    checkMatrix(constants.nonJitteredViewProj, constants.proj * constants.view);
    checkMatrix(constants.prevViewProj, constants.viewProj);
    CHECK(state.cameraPosition() == Vec3(0.0f, 0.0f, 5.0f));
    CHECK(state.cameraForward() == Vec3(0.0f, 0.0f, -1.0f));
    CHECK(state.taaFrameIndex() == 0);
    CHECK_FALSE(state.historyValid());
    CHECK(state.taaJitter() == Vec4{});
    CHECK(state.screenSize() == Vec4(1280.0f, 720.0f, 1.0f / 1280.0f, 1.0f / 720.0f));

    REQUIRE(constants.frustumPlanes.size() == 6);
    CHECK(constants.frustumPlanes[4].z == doctest::Approx(-1.0f));
    CHECK(constants.frustumPlanes[4].w == doctest::Approx(4.9f));
    CHECK(constants.frustumPlanes[5].z == doctest::Approx(1.0f));
    CHECK(constants.frustumPlanes[5].w == doctest::Approx(95.0f));
}

TEST_CASE("ViewState applies HDRP Halton jitter and resets it when disabled") {
    ViewState state;
    const Camera camera = makeCamera(Vec3(0.0f, 0.0f, 5.0f));
    state.update(camera, 1280, 720, 1.0f, true, false);

    CHECK(state.taaFrameIndex() == 1);
    CHECK(state.taaJitter().x == doctest::Approx(-0.25f));
    CHECK(state.taaJitter().y == doctest::Approx(1.0f / 6.0f).epsilon(1.0e-5));
    CHECK(state.taaJitter().z == doctest::Approx(-0.25f / 1280.0f));
    CHECK(state.taaJitter().w == doctest::Approx((1.0f / 6.0f) / 720.0f).epsilon(1.0e-5));
    CHECK(state.taaFrameInfo().z == doctest::Approx(1.0f));
    CHECK(state.taaFrameInfo().w == doctest::Approx(1.0f));

    const Mat4 jittered = state.constants().viewProj;
    const Mat4 nonJittered = state.constants().nonJitteredViewProj;
    const Vec4 jitteredCenter = projectNdc(jittered, Vec3(0.0f, 0.0f, 0.0f));
    const Vec4 nonJitteredCenter = projectNdc(nonJittered, Vec3(0.0f, 0.0f, 0.0f));
    CHECK(jitteredCenter.x == doctest::Approx(2.0f * -0.25f / 1280.0f).epsilon(1.0e-5));
    CHECK(jitteredCenter.y == doctest::Approx(2.0f * (1.0f / 6.0f) / 720.0f).epsilon(1.0e-5));
    CHECK(nonJitteredCenter.x == doctest::Approx(0.0f));
    CHECK(nonJitteredCenter.y == doctest::Approx(0.0f));

    const Mat4 previous = state.constants().nonJitteredViewProj;
    state.update(camera, 1280, 720, 1.0f, true, false);
    CHECK(state.historyValid());
    checkMatrix(state.constants().prevViewProj, previous);

    state.update(camera, 1280, 720, 1.0f, false, false);
    CHECK(state.taaFrameIndex() == 0);
    CHECK(state.taaJitter() == Vec4{});
    CHECK(state.taaFrameInfo().w == doctest::Approx(0.0f));
    CHECK_FALSE(state.historyValid());
    checkMatrix(state.constants().prevViewProj, state.constants().viewProj);
}

TEST_CASE("ViewState ignores unchanged jitter scale and resets when it changes") {
    ViewState state;
    const Camera camera = makeCamera(Vec3(0.0f, 0.0f, 5.0f));
    state.update(camera, 1280, 720, 1.0f, true, false);
    state.update(camera, 1280, 720, 1.0f, true, false);
    REQUIRE(state.historyValid());

    state.setJitterScale(1.0f);
    CHECK(state.historyValid());

    state.setJitterScale(0.5f);
    CHECK_FALSE(state.historyValid());
    CHECK(state.taaFrameIndex() == 0);
}

TEST_CASE("ViewState shifts previous matrices and resets history on resolution changes") {
    ViewState state;
    const Camera camera = makeCamera(Vec3(0.0f, 0.0f, 5.0f));
    state.update(camera, 1280, 720, 1.0f, false, false);
    const Mat4 firstViewProj = state.constants().nonJitteredViewProj;

    const Camera moved = makeCamera(Vec3(0.0f, 0.0f, 6.0f));
    state.update(moved, 1280, 720, 1.0f, false, false);
    CHECK(state.historyValid());
    checkMatrix(state.constants().prevViewProj, firstViewProj);
    CHECK(state.constants().prevWorldSpaceCameraPos == Vec3(0.0f, 0.0f, 5.0f));
    CHECK(state.constants().worldSpaceCameraPos == Vec3(0.0f, 0.0f, 6.0f));

    state.update(moved, 640, 360, 1.0f, false, false);
    CHECK_FALSE(state.historyValid());
    checkMatrix(state.constants().prevViewProj, state.constants().viewProj);
    CHECK(state.renderWidth() == 640);
    CHECK(state.renderHeight() == 360);
}

TEST_CASE("ViewState exposes dynamic resolution scale and orthographic jitter") {
    ViewState state;
    const Camera camera = makeCamera(Vec3(0.0f, 0.0f, 5.0f));
    state.update(camera, 1280, 720, 0.5f, false, false);
    CHECK(state.renderWidth() == 640);
    CHECK(state.renderHeight() == 360);
    CHECK(state.rtScale().x == doctest::Approx(0.5f));
    CHECK(state.rtScale().y == doctest::Approx(0.5f));
    CHECK(state.rtScale().z == doctest::Approx(0.5f));
    CHECK(state.rtScale().w == doctest::Approx(0.5f));
    CHECK(state.dynamicResolutionScale().x == doctest::Approx(0.5f));
    CHECK(state.dynamicResolutionScale().y == doctest::Approx(0.5f));
    CHECK(state.screenSize() == Vec4(640.0f, 360.0f, 1.0f / 640.0f, 1.0f / 360.0f));

    Camera orthographic = camera;
    orthographic.projection = Camera::Projection::Orthographic;
    orthographic.xmag = 4.0f;
    orthographic.ymag = 2.0f;
    ViewState orthoState;
    orthoState.update(orthographic, 100, 100, 1.0f, true, false);
    const Vec4 center = projectNdc(orthoState.constants().viewProj, Vec3(0.0f, 0.0f, 0.0f));
    CHECK(center.x == doctest::Approx(2.0f * -0.25f / 100.0f).epsilon(1.0e-5));
    CHECK(center.y == doctest::Approx(2.0f * (1.0f / 6.0f) / 100.0f).epsilon(1.0e-5));
}
