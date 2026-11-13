#include "render/camera/view_state.h"
#include "render/lighting/light_builder.h"
#include "render/scene/render_scene.h"
#include "render/shadows/shadow_manager.h"
#include "render/shadows/shadow_settings.h"

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

Light makePoint(const Vec3& position) {
    Light light;
    light.name = "point";
    light.type = LightType::Point;
    light.intensity = 20.0f;
    light.range = 8.0f;
    light.castShadows = true;
    light.world.values[12] = position.x;
    light.world.values[13] = position.y;
    light.world.values[14] = position.z;
    return light;
}

Light makeSpot(const Vec3& position) {
    Light light = makePoint(position);
    light.name = "spot";
    light.type = LightType::Spot;
    light.innerConeAngle = 0.25f;
    light.outerConeAngle = 0.5f;
    return light;
}

RenderScene makeScene() {
    RenderScene scene;
    scene.bounds.expand(Vec3(-3.0f, 0.0f, -3.0f));
    scene.bounds.expand(Vec3(3.0f, 3.0f, 3.0f));
    return scene;
}

} // namespace

TEST_CASE("per-light shadow settings sanitize and inherit global defaults") {
    Light light;
    ShadowSettings global;
    global.shadowDimmer = 0.7f;
    global.shadowFadeDistance = 60.0f;
    global.shadowTint = Vec3(0.2f, 0.3f, 0.4f);
    global.shadowNearPlane = 0.3f;
    global.softness = 1.1f;
    global.blockerSamples = 12;
    global.filterSamples = 18;

    const LightShadowSettings inherited = resolveLightShadow(light, global);
    CHECK(inherited.dimmer == doctest::Approx(0.7f));
    CHECK(inherited.fadeDistance == doctest::Approx(60.0f));
    CHECK(inherited.tint == Vec3(0.2f, 0.3f, 0.4f));
    CHECK(inherited.nearPlane == doctest::Approx(0.3f));
    CHECK(inherited.softness == doctest::Approx(1.1f));
    CHECK(inherited.blockerSamples == 12);
    CHECK(inherited.filterSamples == 18);

    LightShadowSettings custom;
    custom.dimmer = 2.0f;
    custom.fadeDistance = -5.0f;
    custom.nearPlane = 0.0f;
    custom.resolution = 123;
    custom.tint = Vec3(2.0f, -1.0f, 0.5f);
    custom.softness = -1.0f;
    custom.blockerSamples = 0;
    custom.filterSamples = 100;
    sanitizeLightShadowSettings(custom);
    CHECK(custom.dimmer == doctest::Approx(1.0f));
    CHECK(custom.fadeDistance == doctest::Approx(0.0f));
    CHECK(custom.nearPlane == doctest::Approx(0.01f));
    CHECK(custom.resolution == 512u);
    CHECK(custom.tint == Vec3(1.0f, 0.0f, 0.5f));
    CHECK(custom.softness == doctest::Approx(0.0f));
    CHECK(custom.blockerSamples == 1u);
    CHECK(custom.filterSamples == 64u);

    light.shadowConfigured = true;
    light.shadow = custom;
    const LightShadowSettings resolved = resolveLightShadow(light, global);
    CHECK(resolved.resolution == 512u);
    CHECK(resolved.dimmer == doctest::Approx(1.0f));
}

TEST_CASE("light builder writes global shadow defaults for unconfigured lights") {
    RenderScene scene = makeScene();
    scene.lights.push_back(makePoint(Vec3(1.0f, 2.0f, 1.0f)));

    ShadowSettings settings;
    settings.shadowDimmer = 0.25f;
    settings.shadowFadeDistance = 80.0f;
    settings.shadowTint = Vec3(0.4f, 0.2f, 0.1f);

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), 100.0f, false, false, &settings);
    const GpuLightData& data = builder.data();
    CHECK(data.texels[4] == Vec4(0.4f, 0.2f, 0.1f, 0.25f));
    CHECK(data.texels[5].x == doctest::Approx(80.0f));
    CHECK(data.texels[5].y == doctest::Approx(0.0f));
}

TEST_CASE("shadow manager consumes per-light resolution near plane dimmer and tint") {
    RenderScene scene = makeScene();
    Light light = makePoint(Vec3(1.0f, 2.0f, 1.0f));
    light.shadowConfigured = true;
    light.shadow.dimmer = 0.4f;
    light.shadow.fadeDistance = 100.0f;
    light.shadow.nearPlane = 0.25f;
    light.shadow.resolution = 512;
    light.shadow.tint = Vec3(0.1f, 0.2f, 0.3f);
    light.shadow.penumbraTint = true;
    light.shadow.contactShadows = false;
    light.shadow.softness = 0.9f;
    light.shadow.blockerSamples = 8;
    light.shadow.filterSamples = 12;
    light.shadow.updateMode = ShadowUpdateMode::OnEnable;
    scene.lights.push_back(light);

    const Camera camera = makeCamera();
    ViewState viewState;
    viewState.update(camera, 1280, 720, 1.0f, false, false);

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip, true, false);

    ShadowSettings settings;
    ShadowManager manager;
    REQUIRE(manager.setSettings(settings));
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));

    REQUIRE(manager.requests().size() == 6);
    for (const ShadowRequest& request : manager.requests()) {
        CHECK(request.viewportWidth == 512);
        CHECK(request.viewportHeight == 512);
        CHECK(request.nearPlane == doctest::Approx(0.25f));
        CHECK(request.softness == doctest::Approx(0.9f));
        CHECK(request.blockerSamples == 8);
        CHECK(request.filterSamples == 12);
        CHECK_FALSE(request.isCached);
    }

    const GpuLightData& data = builder.data();
    CHECK(data.texels[3].z == doctest::Approx(0.0f));
    CHECK(data.texels[4].x == doctest::Approx(0.1f));
    CHECK(data.texels[4].y == doctest::Approx(0.2f));
    CHECK(data.texels[4].z == doctest::Approx(0.3f));
    CHECK(data.texels[4].w == doctest::Approx(0.4f));
    CHECK(data.texels[5].x == doctest::Approx(100.0f));
    CHECK(data.texels[5].y == doctest::Approx(1.0f));

    light.shadow.fadeDistance = 1.0f;
    scene.lights[0] = light;
    LightBuilder culledBuilder;
    culledBuilder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip, true, false);
    CHECK_FALSE(manager.update(scene, viewState.constants(), camera, culledBuilder));
    CHECK(manager.punctualRequestCount() == 0);
    CHECK(culledBuilder.data().texels[3].x == doctest::Approx(-1.0f));

    light.shadow.fadeDistance = 100.0f;
    light.shadow.dimmer = 0.0f;
    scene.lights[0] = light;
    LightBuilder dimmedBuilder;
    dimmedBuilder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip, true, false);
    CHECK_FALSE(manager.update(scene, viewState.constants(), camera, dimmedBuilder));
    CHECK(dimmedBuilder.data().texels[3].x == doctest::Approx(-1.0f));
}

TEST_CASE("spot shadow uses its own resolution and near plane") {
    RenderScene scene = makeScene();
    Light light = makeSpot(Vec3(1.0f, 2.0f, 2.0f));
    light.shadowConfigured = true;
    light.shadow.resolution = 1024;
    light.shadow.nearPlane = 0.4f;
    light.shadow.dimmer = 1.0f;
    scene.lights.push_back(light);

    const Camera camera = makeCamera();
    ViewState viewState;
    viewState.update(camera, 1280, 720, 1.0f, false, false);
    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip, false, false);

    ShadowManager manager;
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));
    REQUIRE(manager.requests().size() == 1);
    CHECK(manager.requests()[0].viewportWidth == 1024);
    CHECK(manager.requests()[0].nearPlane == doctest::Approx(0.4f));
}

TEST_CASE("static punctual cache skips compatible requests and invalidates on changes") {
    RenderScene scene = makeScene();
    RenderObject object;
    object.name = "caster";
    object.world = Mat4::identity();
    scene.objects.push_back(object);

    Light light = makePoint(Vec3(1.0f, 2.0f, 1.0f));
    light.shadowConfigured = true;
    light.shadow.updateMode = ShadowUpdateMode::OnEnable;
    scene.lights.push_back(light);

    const Camera camera = makeCamera();
    ViewState viewState;
    viewState.update(camera, 1280, 720, 1.0f, false, false);

    ShadowSettings settings;
    settings.cachedPunctualShadows = true;
    ShadowManager manager;
    REQUIRE(manager.setSettings(settings));

    LightBuilder firstBuilder;
    firstBuilder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip, false, false);
    REQUIRE(manager.update(scene, viewState.constants(), camera, firstBuilder));
    CHECK(manager.cacheActive());
    CHECK(manager.cachedRequestCount() == 0);
    for (const ShadowRequest& request : manager.requests()) {
        CHECK_FALSE(request.isCached);
    }

    LightBuilder secondBuilder;
    secondBuilder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip, false, false);
    REQUIRE(manager.update(scene, viewState.constants(), camera, secondBuilder));
    CHECK(manager.cachedRequestCount() == 6);
    for (const ShadowRequest& request : manager.requests()) {
        CHECK(request.isCached);
    }

    scene.objects[0].world.values[12] = 2.0f;
    LightBuilder movedObjectBuilder;
    movedObjectBuilder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip, false,
                             false);
    REQUIRE(manager.update(scene, viewState.constants(), camera, movedObjectBuilder));
    CHECK(manager.cachedRequestCount() == 0);

    LightBuilder warmBuilder;
    warmBuilder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip, false, false);
    REQUIRE(manager.update(scene, viewState.constants(), camera, warmBuilder));
    CHECK(manager.cachedRequestCount() == 6);

    scene.objects[0].world.values[12] = 0.0f;
    scene.objects[0].world.values[13] = 1.0f;
    scene.objects[0].world.values[14] = 0.0f;
    scene.lights[0].world.values[13] = 2.5f;
    LightBuilder movedLightBuilder;
    movedLightBuilder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip, false,
                            false);
    REQUIRE(manager.update(scene, viewState.constants(), camera, movedLightBuilder));
    CHECK(manager.cachedRequestCount() == 0);

    LightBuilder thirdBuilder;
    thirdBuilder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip, false, false);
    REQUIRE(manager.update(scene, viewState.constants(), camera, thirdBuilder));
    CHECK(manager.cachedRequestCount() == 6);

    scene.materials.emplace_back();
    scene.materials.back().alphaCutoff = 0.65f;
    LightBuilder materialBuilder;
    materialBuilder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip, false, false);
    REQUIRE(manager.update(scene, viewState.constants(), camera, materialBuilder));
    CHECK(manager.cachedRequestCount() == 0);

    manager.setCacheSuppressed(true);
    LightBuilder suppressedBuilder;
    suppressedBuilder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip, false,
                            false);
    REQUIRE(manager.update(scene, viewState.constants(), camera, suppressedBuilder));
    CHECK_FALSE(manager.cacheActive());
    CHECK(manager.cachedRequestCount() == 0);
}

TEST_CASE("shadow cache is opt-in and sidecar every-frame lights disable it") {
    RenderScene scene = makeScene();
    Light light = makePoint(Vec3(1.0f, 2.0f, 1.0f));
    light.shadowConfigured = true;
    light.shadow.updateMode = ShadowUpdateMode::EveryFrame;
    scene.lights.push_back(light);

    const Camera camera = makeCamera();
    ViewState viewState;
    viewState.update(camera, 1280, 720, 1.0f, false, false);
    ShadowSettings settings;
    settings.cachedPunctualShadows = true;
    ShadowManager manager;
    REQUIRE(manager.setSettings(settings));

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip, false, false);
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));
    CHECK_FALSE(manager.cacheActive());
    CHECK(manager.cachedRequestCount() == 0);

    settings.cachedPunctualShadows = false;
    manager.setSettings(settings);
    light.shadow.updateMode = ShadowUpdateMode::OnEnable;
    scene.lights[0] = light;
    LightBuilder disabledBuilder;
    disabledBuilder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip, false, false);
    REQUIRE(manager.update(scene, viewState.constants(), camera, disabledBuilder));
    CHECK_FALSE(manager.cacheActive());
}
