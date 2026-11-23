#include "render/camera/view_state.h"
#include "render/culling/frustum.h"
#include "render/lighting/light_builder.h"
#include "render/scene/render_scene.h"
#include "render/shadows/shadow_manager.h"

#include <doctest/doctest.h>

#include <limits>

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

Light makeArea(bool castShadows = true) {
    Light light;
    light.name = "area";
    light.type = LightType::Area;
    light.areaShape = LightAreaShape::Rectangle;
    light.areaWidth = 2.0f;
    light.areaHeight = 1.0f;
    light.intensity = 20.0f;
    light.range = 8.0f;
    light.areaShadowCone = 120.0f;
    light.castShadows = castShadows;
    light.world.values[12] = 0.0f;
    light.world.values[13] = 3.0f;
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
    CHECK(manager.contextData().texels[6].z == doctest::Approx(3.0f));
    CHECK(manager.contextData().texels[7].z > 0.0f);
    CHECK(manager.contextData().texels[8].x > 0.0f);
    CHECK(manager.contextData().texels[9].x == doctest::Approx(16.0f));
    CHECK(manager.contextData().texels[9].y == doctest::Approx(24.0f));
    CHECK(manager.contextData().texels[9].z == doctest::Approx(0.01f));
    CHECK(manager.contextData().texels[9].w > 0.0f);
    CHECK(manager.requests()[0].lightAngle == doctest::Approx(0.0174532925f));
    CHECK(manager.requests()[0].maxDepthBias == doctest::Approx(0.001f));
    CHECK(manager.requests()[0].imsKernelSize == 5);
}

TEST_CASE("shadow settings drive cascade count, resolution and context layout") {
    RenderScene scene = makeScene();
    scene.lights.push_back(makeDirectional());
    const Camera camera = makeCamera();
    ViewState viewState;
    viewState.update(camera, 1280, 720, 1.0f, false, false);

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip);

    ShadowSettings settings;
    settings.maxDistance = 50.0f;
    settings.cascadeCount = 2;
    settings.cascadeSplits = {0.2f, 0.6f, 1.0f, 1.0f};
    settings.directionalResolution = 512;
    settings.directionalFilter = ShadowFilter::Pcf5x5;
    settings.normalBias = 0.4f;

    ShadowManager manager;
    REQUIRE(manager.setSettings(settings));
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));

    REQUIRE(manager.requests().size() == 2);
    CHECK(manager.directionalRequestCount() == 2);
    CHECK(manager.requests()[0].viewportWidth == 512);
    CHECK(manager.requests()[0].viewportHeight == 512);
    CHECK(manager.requests()[1].viewportWidth == 512);
    CHECK(manager.requests()[0].normalBias == doctest::Approx(0.4f));
    CHECK(manager.requests()[0].softness == doctest::Approx(0.5f));
    CHECK(manager.requests()[0].worldTexelSize > 0.0f);
    CHECK(manager.requests()[0].slopeBias == doctest::Approx(2.5f));
    CHECK(manager.requests()[0].blockerSamples == 16);
    CHECK(manager.requests()[0].filterSamples == 24);
    CHECK(manager.requests()[0].fadeScale > 0.0f);
    CHECK(manager.requests()[0].fadeBias < 0.0f);
    CHECK(manager.requests()[0].filter == ShadowFilter::Pcf5x5);
    CHECK(manager.cascadeSplits().x == doctest::Approx(10.08f));
    CHECK(manager.cascadeSplits().y == doctest::Approx(50.0f));
    CHECK(manager.cascadeSplits().z == doctest::Approx(50.0f));
    CHECK(manager.cascadeSplits().w == doctest::Approx(50.0f));
    CHECK(manager.contextData().texels[6].z == doctest::Approx(2.0f));
    CHECK(manager.contextData().texels[8].z == doctest::Approx(0.0f));

    Bounds inside;
    inside.expand(Vec3(-1.0f, 0.0f, -1.0f));
    inside.expand(Vec3(1.0f, 2.0f, 1.0f));
    Bounds outside;
    outside.expand(Vec3(999.0f, 0.0f, 999.0f));
    outside.expand(Vec3(1001.0f, 2.0f, 1001.0f));
    CHECK(intersectsFrustum(manager.requests()[0].frustumPlanes, inside));
    CHECK_FALSE(intersectsFrustum(manager.requests()[0].frustumPlanes, outside));
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
    CHECK(builder.data().texels[kGpuLightTexels + 3].x == doctest::Approx(4.0f));

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

TEST_CASE("shadow manager builds an independent rectangle area shadow request") {
    RenderScene scene = makeScene();
    scene.lights.push_back(makeArea());

    const Camera camera = makeCamera();
    ViewState viewState;
    viewState.update(camera, 1280, 720, 1.0f, false, false);

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip);

    ShadowSettings settings;
    settings.areaAtlasSize = 1024;
    settings.areaResolution = 256;
    settings.areaFilter = ShadowFilter::Pcss;
    settings.maxRequests = 8;

    ShadowManager manager;
    REQUIRE(manager.setSettings(settings));
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));

    REQUIRE(manager.requests().size() == 1);
    CHECK(manager.areaRequestCount() == 1);
    CHECK(manager.punctualRequestCount() == 0);
    CHECK(manager.shadowedLightCount() == 1);
    const ShadowRequest& request = manager.requests()[0];
    CHECK(request.type == ShadowRequestType::Rectangle);
    CHECK(request.atlas == ShadowAtlasType::Area);
    CHECK(request.atlasSize == 1024);
    CHECK(request.viewportWidth == 256);
    CHECK(request.viewportHeight == 256);
    CHECK(request.filter == ShadowFilter::Pcss);
    CHECK(request.nearPlane > 0.0f);
    CHECK(request.farPlane > request.nearPlane);
    CHECK(request.softness == doctest::Approx(0.5f));
    CHECK(builder.data().texels[3].x == doctest::Approx(0.0f));
    CHECK(builder.data().texels[3].w == doctest::Approx(11.0f));
    CHECK(manager.contextData().requestCount == 1);
}

TEST_CASE("shadow manager propagates EVSM filter mode to area requests") {
    RenderScene scene = makeScene();
    scene.lights.push_back(makeArea());

    const Camera camera = makeCamera();
    ViewState viewState;
    viewState.update(camera, 1280, 720, 1.0f, false, false);

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip);

    ShadowSettings settings;
    settings.areaFilter = ShadowFilter::Evsm;
    settings.maxRequests = 8;

    ShadowManager manager;
    REQUIRE(manager.setSettings(settings));
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));
    REQUIRE(manager.requests().size() == 1);
    CHECK(manager.requests()[0].filter == ShadowFilter::Evsm);
    CHECK(manager.contextData().texels[6].z == doctest::Approx(5.0f));
}

TEST_CASE("shadow manager propagates VSM filter mode to area requests") {
    RenderScene scene = makeScene();
    scene.lights.push_back(makeArea());

    const Camera camera = makeCamera();
    ViewState viewState;
    viewState.update(camera, 1280, 720, 1.0f, false, false);

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip);

    ShadowSettings settings;
    settings.areaFilter = ShadowFilter::Vsm;
    settings.maxRequests = 8;

    ShadowManager manager;
    REQUIRE(manager.setSettings(settings));
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));
    REQUIRE(manager.requests().size() == 1);
    CHECK(manager.requests()[0].filter == ShadowFilter::Vsm);
    CHECK(manager.contextData().texels[6].z == doctest::Approx(6.0f));
}

TEST_CASE("shadow manager propagates MSM filter mode to directional requests") {
    RenderScene scene;
    scene.lights.push_back(makeDirectional());

    const Camera camera = makeCamera();
    ViewState viewState;
    viewState.update(camera, 1280, 720, 1.0f, false, false);

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip);

    ShadowSettings settings;
    settings.directionalFilter = ShadowFilter::Msm;
    settings.punctualFilter = ShadowFilter::Off;
    settings.maxRequests = 8;

    ShadowManager manager;
    REQUIRE(manager.setSettings(settings));
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));
    REQUIRE(manager.directionalRequestCount() == kDirectionalCascadeCount);
    CHECK(manager.requests()[0].filter == ShadowFilter::Msm);
    CHECK(manager.contextData().texels[6].z == doctest::Approx(7.0f));
}

TEST_CASE("shadow manager propagates MSM filter mode to area requests") {
    RenderScene scene = makeScene();
    scene.lights.push_back(makeArea());

    const Camera camera = makeCamera();
    ViewState viewState;
    viewState.update(camera, 1280, 720, 1.0f, false, false);

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip);

    ShadowSettings settings;
    settings.areaFilter = ShadowFilter::Msm;
    settings.maxRequests = 8;

    ShadowManager manager;
    REQUIRE(manager.setSettings(settings));
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));
    REQUIRE(manager.requests().size() == 1);
    CHECK(manager.requests()[0].filter == ShadowFilter::Msm);
    CHECK(manager.contextData().texels[6].z == doctest::Approx(7.0f));
}

TEST_CASE("shadow manager scales to 128 requests with a 4096 atlas") {
    RenderScene scene = makeScene();
    constexpr uint32_t kSpotCount = 122;
    for (uint32_t index = 0; index < kSpotCount; ++index) {
        Light light = makeSpot();
        light.world.values[12] = -7.5f + static_cast<float>(index % 16u);
        light.world.values[13] = 2.0f + static_cast<float>((index / 16u) % 4u) * 0.5f;
        light.world.values[14] = -static_cast<float>(index / 16u);
        light.shadowConfigured = true;
        light.shadow.resolution = index < 32u ? 512u : 256u;
        scene.lights.push_back(light);
    }
    Light point = makePoint();
    point.world.values[12] = 0.0f;
    point.world.values[13] = 2.0f;
    point.world.values[14] = 0.0f;
    point.shadowConfigured = true;
    point.shadow.resolution = 256;
    scene.lights.push_back(point);

    const Camera camera = makeCamera();
    ViewState viewState;
    viewState.update(camera, 1280, 720, 1.0f, false, false);

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip, false, false,
                  nullptr, 128);

    ShadowSettings settings;
    settings.atlasSize = 4096;
    settings.maxRequests = 128;
    ShadowManager manager;
    REQUIRE(manager.setSettings(settings));
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));

    REQUIRE(manager.requests().size() == 128);
    CHECK(manager.punctualRequestCount() == 128);
    CHECK(manager.contextData().requestCount == 128);
    for (uint32_t index = 0; index < manager.requests().size(); ++index) {
        const ShadowRequest& request = manager.requests()[index];
        CHECK(request.requestIndex == index);
        CHECK(request.atlasSize == 4096);
        CHECK(request.viewportX + request.viewportWidth <= 4096);
        CHECK(request.viewportY + request.viewportHeight <= 4096);
    }

    int pointLightIndex = -1;
    const auto& selectedLights = builder.selectedLights();
    for (uint32_t index = 0; index < selectedLights.size(); ++index) {
        if (selectedLights[index]->type == LightType::Point) {
            pointLightIndex = static_cast<int>(index);
            break;
        }
    }
    REQUIRE(pointLightIndex >= 0);
    uint32_t pointBase = std::numeric_limits<uint32_t>::max();
    for (const ShadowRequest& request : manager.requests()) {
        if (request.lightIndex == static_cast<uint32_t>(pointLightIndex) &&
            request.type == ShadowRequestType::PointFace) {
            pointBase = std::min(pointBase, request.requestIndex);
        }
    }
    REQUIRE(pointBase != std::numeric_limits<uint32_t>::max());
    CHECK(builder.data().texels[static_cast<std::size_t>(pointLightIndex) * kGpuLightTexels + 3].x ==
          doctest::Approx(static_cast<float>(pointBase)));
    for (uint32_t face = 0; face < 6; ++face) {
        const ShadowRequest& request = manager.requests()[pointBase + face];
        CHECK(request.type == ShadowRequestType::PointFace);
        CHECK(request.faceIndex == face);
    }

    for (uint32_t lhs = 0; lhs < manager.requests().size(); ++lhs) {
        const ShadowRequest& a = manager.requests()[lhs];
        for (uint32_t rhs = lhs + 1; rhs < manager.requests().size(); ++rhs) {
            const ShadowRequest& b = manager.requests()[rhs];
            const bool overlap = a.viewportX < b.viewportX + b.viewportWidth &&
                                 b.viewportX < a.viewportX + a.viewportWidth &&
                                 a.viewportY < b.viewportY + b.viewportHeight &&
                                 b.viewportY < a.viewportY + a.viewportHeight;
            CHECK_FALSE(overlap);
        }
    }
}

TEST_CASE("directional cache invalidates on camera changes") {
    RenderScene scene = makeScene();
    scene.lights.push_back(makeDirectional());
    RenderObject object;
    object.name = "cached-caster";
    object.meshIndex = 0;
    object.world = Mat4::identity();
    object.worldBounds.expand(Vec3(-0.5f, -0.5f, -0.5f));
    object.worldBounds.expand(Vec3(0.5f, 0.5f, 0.5f));
    scene.objects.push_back(object);

    Camera camera = makeCamera();
    ViewState viewState;
    viewState.update(camera, 1280, 720, 1.0f, false, false);
    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip);

    ShadowSettings settings;
    settings.cachedDirectionalShadows = true;
    ShadowManager manager;
    REQUIRE(manager.setSettings(settings));
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));
    REQUIRE(manager.directionalCacheActive());
    CHECK(manager.cachedDirectionalRequestCount() == 0);
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));
    CHECK(manager.cachedDirectionalRequestCount() == manager.directionalRequestCount());
    CHECK_FALSE(manager.directionalCacheDirty());

    camera.world.values[12] += 1.0f;
    viewState.update(camera, 1280, 720, 1.0f, false, false);
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));
    CHECK(manager.cachedDirectionalRequestCount() == 0);
    CHECK(manager.directionalCacheDirty());
}

TEST_CASE("directional cache keeps unaffected cascades") {
    RenderScene base = makeScene();
    base.lights.push_back(makeDirectional());

    Camera camera = makeCamera();
    ViewState viewState;
    viewState.update(camera, 1280, 720, 1.0f, false, false);

    ShadowSettings probeSettings;
    probeSettings.cachedDirectionalShadows = false;
    ShadowManager probe;
    REQUIRE(probe.setSettings(probeSettings));

    RenderObject object;
    object.name = "moving-caster";
    object.meshIndex = 0;
    object.worldBounds.expand(Vec3(-0.5f, -0.5f, -0.5f));
    object.worldBounds.expand(Vec3(0.5f, 0.5f, 0.5f));

    bool foundPartial = false;
    uint32_t partialCount = 0;
    for (float z = -20.0f; z <= 20.0f && !foundPartial; z += 1.0f) {
        for (float x = -20.0f; x <= 20.0f && !foundPartial; x += 1.0f) {
            RenderScene probeScene = base;
            object.world = Mat4::identity();
            object.world.values[12] = x;
            object.world.values[14] = z;
            object.worldBounds = {};
            object.worldBounds.expand(Vec3(x - 0.5f, -0.5f, z - 0.5f));
            object.worldBounds.expand(Vec3(x + 0.5f, 0.5f, z + 0.5f));
            probeScene.objects.push_back(object);

            LightBuilder builder;
            builder.build(probeScene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f),
                          camera.farClip);
            if (!probe.update(probeScene, viewState.constants(), camera, builder)) {
                continue;
            }
            uint32_t intersections = 0;
            for (const ShadowRequest& request : probe.requests()) {
                if (request.atlas == ShadowAtlasType::Directional &&
                    intersectsFrustum(request.frustumPlanes, object.worldBounds)) {
                    ++intersections;
                }
            }
            if (intersections > 0 && intersections < kDirectionalCascadeCount) {
                foundPartial = true;
                partialCount = intersections;
            }
        }
    }
    REQUIRE(foundPartial);

    RenderScene scene = base;
    scene.objects.push_back(object);
    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip);

    ShadowSettings settings;
    settings.cachedDirectionalShadows = true;
    ShadowManager manager;
    REQUIRE(manager.setSettings(settings));
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));
    REQUIRE(manager.directionalCacheActive());
    CHECK(manager.cachedDirectionalRequestCount() == 0);

    scene.objects[0].world.values[12] += 0.1f;
    scene.objects[0].worldBounds.min.x += 0.1f;
    scene.objects[0].worldBounds.max.x += 0.1f;
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));
    CHECK(manager.cachedDirectionalRequestCount() ==
          kDirectionalCascadeCount - partialCount);
    CHECK(manager.directionalCacheMixed());
}

TEST_CASE("directional cache separates static and dynamic casters") {
    RenderScene scene = makeScene();
    scene.lights.push_back(makeDirectional());

    RenderObject staticObject;
    staticObject.name = "static";
    staticObject.worldBounds.expand(Vec3(-1.0f, 0.0f, -1.0f));
    staticObject.worldBounds.expand(Vec3(1.0f, 2.0f, 1.0f));
    scene.objects.push_back(staticObject);

    RenderObject dynamicObject;
    dynamicObject.name = "dynamic";
    dynamicObject.staticShadowCaster = false;
    dynamicObject.worldBounds.expand(Vec3(-0.5f, 0.0f, -0.5f));
    dynamicObject.worldBounds.expand(Vec3(0.5f, 2.0f, 0.5f));
    scene.objects.push_back(dynamicObject);

    const Camera camera = makeCamera();
    ViewState viewState;
    viewState.update(camera, 1280, 720, 1.0f, false, false);
    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 4.0f, 8.0f), Vec3(0.0f, -0.4f, -0.9f), camera.farClip);

    ShadowSettings settings;
    settings.cascadeCount = 1;
    settings.cachedDirectionalShadows = true;
    settings.cachedDirectionalMixedShadows = true;

    ShadowManager manager;
    REQUIRE(manager.setSettings(settings));
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));
    CHECK(manager.directionalCacheActive());
    CHECK(manager.directionalMixedCached());
    CHECK(manager.directionalCacheDirty());
    CHECK_FALSE(manager.requests()[0].isCached);
    CHECK(manager.requests()[0].hasDynamicCaster);

    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));
    CHECK(manager.directionalMixedCached());
    CHECK_FALSE(manager.directionalCacheDirty());
    CHECK(manager.requests()[0].isCached);
    CHECK(manager.requests()[0].hasDynamicCaster);

    scene.objects[1].world.values[12] = 0.25f;
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));
    CHECK_FALSE(manager.directionalCacheDirty());
    CHECK(manager.requests()[0].isCached);

    scene.objects[0].world.values[12] = 0.5f;
    REQUIRE(manager.update(scene, viewState.constants(), camera, builder));
    CHECK(manager.directionalCacheDirty());
    CHECK_FALSE(manager.requests()[0].isCached);
}
