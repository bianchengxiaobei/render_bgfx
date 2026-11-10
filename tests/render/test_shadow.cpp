#include "render/camera/view_state.h"
#include "render/lighting/light_builder.h"
#include "render/scene/render_scene.h"
#include "render/shadows/shadow_manager.h"

#include <doctest/doctest.h>

using namespace hbg;
using namespace hbg::render;

namespace {

Camera makeCamera() {
    Camera camera;
    camera.world = inverse(lookAtRH(Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)));
    camera.nearClip = 0.1f;
    camera.farClip = 100.0f;
    camera.aspect = 16.0f / 9.0f;
    return camera;
}

Light makeDirectional(bool castShadows = true) {
    Light light;
    light.name = "sun";
    light.type = LightType::Directional;
    light.intensity = 3.0f;
    light.castShadows = castShadows;
    return light;
}

Light makePoint(bool castShadows = true) {
    Light light;
    light.name = "point";
    light.type = LightType::Point;
    light.intensity = 20.0f;
    light.range = 8.0f;
    light.castShadows = castShadows;
    light.world.values[12] = 1.0f;
    light.world.values[13] = 2.0f;
    light.world.values[14] = 1.0f;
    return light;
}

Light makeSpot(bool castShadows = true) {
    Light light;
    light.name = "spot";
    light.type = LightType::Spot;
    light.intensity = 20.0f;
    light.range = 8.0f;
    light.innerConeAngle = 0.25f;
    light.outerConeAngle = 0.5f;
    light.castShadows = castShadows;
    light.world.values[12] = 1.0f;
    light.world.values[13] = 2.0f;
    light.world.values[14] = 2.0f;
    return light;
}

RenderScene makeScene() {
    RenderScene scene;
    scene.bounds.expand(Vec3(-3.0f, 0.0f, -3.0f));
    scene.bounds.expand(Vec3(3.0f, 3.0f, 3.0f));
    return scene;
}

} // namespace

TEST_CASE("shadow manager builds four directional cascades") {
    RenderScene scene = makeScene();
    scene.lights.push_back(makeDirectional());
    const Camera camera = makeCamera();
    ViewState viewState;
    viewState.update(camera, 1280, 720, 1.0f, false, false);

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip);

    ShadowManager manager;
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));

    CHECK(manager.directionalRequestCount() == kDirectionalCascadeCount);
    CHECK(manager.punctualRequestCount() == 0);
    CHECK(manager.requests().size() == kDirectionalCascadeCount);
    CHECK(manager.shadowedLightCount() == 1);
    CHECK(manager.cascadeSplits().x > 0.0f);
    CHECK(manager.cascadeSplits().x < manager.cascadeSplits().y);
    CHECK(manager.cascadeSplits().y < manager.cascadeSplits().z);
    CHECK(manager.cascadeSplits().z < manager.cascadeSplits().w);
    CHECK(manager.requests()[0].viewportWidth == 1024);
    CHECK(manager.requests()[1].viewportWidth == 1024);
    CHECK(manager.requests()[2].viewportY == 1024);
    CHECK(builder.data().texels[3].x == doctest::Approx(0.0f));
    CHECK(manager.contextData().requestCount == kDirectionalCascadeCount);
    CHECK(manager.contextData().texels[4].x == doctest::Approx(0.5f));
    CHECK(manager.contextData().texels[6].w == doctest::Approx(1.0f));
}

TEST_CASE("shadow manager builds point faces and patches light shadow indices") {
    RenderScene scene = makeScene();
    scene.lights.push_back(makeDirectional());
    scene.lights.push_back(makePoint());
    const Camera camera = makeCamera();
    ViewState viewState;
    viewState.update(camera, 1280, 720, 1.0f, false, false);

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip);

    ShadowManager manager;
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));

    CHECK(manager.directionalRequestCount() == kDirectionalCascadeCount);
    CHECK(manager.punctualRequestCount() == 6);
    CHECK(manager.requests().size() == kDirectionalCascadeCount + 6);
    CHECK(manager.shadowedLightCount() == 2);
    CHECK(builder.data().texels[3].x == doctest::Approx(0.0f));
    CHECK(builder.data().texels[4 + 3].x == doctest::Approx(4.0f));

    for (uint32_t face = 0; face < 6; ++face) {
        const ShadowRequest& request = manager.requests()[kDirectionalCascadeCount + face];
        CHECK(request.type == ShadowRequestType::PointFace);
        CHECK(request.atlas == ShadowAtlasType::Punctual);
        CHECK(request.faceIndex == face);
        CHECK(request.viewportWidth == 256);
        CHECK(request.viewportHeight == 256);
    }
    CHECK(manager.contextData().texels[(kDirectionalCascadeCount + 3) * kShadowContextTexels + 6].w ==
          doctest::Approx(1.0f));
}

TEST_CASE("shadow manager builds one spot request and supports disabled shadows") {
    RenderScene scene = makeScene();
    scene.lights.push_back(makeSpot());
    const Camera camera = makeCamera();
    ViewState viewState;
    viewState.update(camera, 1280, 720, 1.0f, false, false);

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip);

    ShadowManager manager;
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));
    REQUIRE(manager.requests().size() == 1);
    CHECK(manager.requests()[0].type == ShadowRequestType::Spot);
    CHECK(manager.requests()[0].atlas == ShadowAtlasType::Punctual);
    CHECK(manager.requests()[0].viewportWidth == 512);
    CHECK(builder.data().texels[3].x == doctest::Approx(0.0f));

    RenderScene disabledScene = makeScene();
    disabledScene.lights.push_back(makeSpot(false));
    LightBuilder disabledBuilder;
    disabledBuilder.build(disabledScene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip);
    ShadowManager disabledManager;
    CHECK_FALSE(disabledManager.update(disabledScene, viewState.constants(), camera, disabledBuilder));
    CHECK_FALSE(disabledManager.valid());
    CHECK(disabledBuilder.data().texels[3].x == doctest::Approx(-1.0f));
}
