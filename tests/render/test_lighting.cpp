#include "render/lighting/light_builder.h"
#include "render/scene/render_scene.h"

#include <doctest/doctest.h>

#include <cmath>

using namespace hbg;
using namespace hbg::render;

namespace {

Light makeDirectional(bool castShadows = true) {
    Light light;
    light.name = "sun";
    light.type = LightType::Directional;
    light.color = Vec3(1.0f, 0.9f, 0.8f);
    light.intensity = 3.0f;
    light.castShadows = castShadows;
    return light;
}

Light makePoint(const char* name, const Vec3& position, float intensity = 10.0f, float range = 5.0f) {
    Light light;
    light.name = name;
    light.type = LightType::Point;
    light.color = Vec3(1.0f, 0.5f, 0.25f);
    light.intensity = intensity;
    light.range = range;
    light.world.values[12] = position.x;
    light.world.values[13] = position.y;
    light.world.values[14] = position.z;
    return light;
}

} // namespace

TEST_CASE("light builder caps lights and writes directional shadow data") {
    RenderScene scene;
    scene.lights.push_back(makeDirectional());
    for (int index = 0; index < 40; ++index) {
        scene.lights.push_back(makePoint("point", Vec3(static_cast<float>(index % 4), 1.0f,
                                                       4.0f - static_cast<float>(index) * 0.01f)));
    }

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 1.0f, 5.0f), Vec3(0.0f, 0.0f, -1.0f), 100.0f);
    const GpuLightData& data = builder.data();

    CHECK(data.lightCount == 41u);
    CHECK(data.directionalCount == 1);
    CHECK(data.punctualCount == 40u);
    CHECK(data.truncatedLightCount == 0u);
    CHECK(data.shadowedCount == 1);
    REQUIRE(builder.shadowCaster() != nullptr);
    CHECK(builder.shadowCaster()->type == LightType::Directional);

    CHECK(data.texels[0].x == doctest::Approx(0.0f));
    CHECK(data.texels[2].x == doctest::Approx(0.0f));
    CHECK(data.texels[2].y == doctest::Approx(0.0f));
    CHECK(data.texels[2].z == doctest::Approx(1.0f));
    CHECK(data.texels[3].x == doctest::Approx(0.0f));
    CHECK(data.texels[3].w == doctest::Approx(8.0f));

    CHECK(data.texels[kGpuLightTexels].w == doctest::Approx(5.0f));
    CHECK(data.texels[kGpuLightTexels + 1].w == doctest::Approx(10.0f));
}

TEST_CASE("light builder writes spot cone data and culls lights behind camera") {
    RenderScene scene;
    Light spot;
    spot.type = LightType::Spot;
    spot.intensity = 2.0f;
    spot.range = 8.0f;
    spot.innerConeAngle = 0.5f;
    spot.outerConeAngle = 1.0f;
    spot.world.values[12] = 0.0f;
    spot.world.values[13] = 2.0f;
    spot.world.values[14] = 1.0f;
    scene.lights.push_back(spot);
    scene.lights.push_back(makePoint("behind", Vec3(0.0f, 0.0f, 10.0f), 100.0f, 1.0f));

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), 50.0f);
    const GpuLightData& data = builder.data();

    CHECK(data.lightCount == 1);
    CHECK(data.directionalCount == 0);
    CHECK(data.punctualCount == 1);
    CHECK(data.texels[0].w == doctest::Approx(8.0f));
    CHECK(data.texels[2].z == doctest::Approx(-1.0f));
    CHECK(data.texels[2].w == doctest::Approx(std::cos(1.0f)));
    CHECK(data.texels[3].y == doctest::Approx(std::cos(0.5f)));
    CHECK(data.texels[3].w == doctest::Approx(2.0f));
}

TEST_CASE("light layers gate light data by support flag") {
    RenderScene scene;
    scene.lights.push_back(makePoint("layer0", Vec3(0.0f, 0.0f, 0.0f), 2.0f, 1.0f));
    scene.lights.back().lightLayerMask = 0x0001u;

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, -1.0f), 100.0f, false, false, nullptr, 64,
                  false);
    CHECK(builder.data().texels[6].x == doctest::Approx(65535.0f));

    builder.build(scene, Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, -1.0f), 100.0f, false, false, nullptr, 64,
                  true);
    CHECK(builder.data().texels[6].x == doctest::Approx(1.0f));
    CHECK(lightLayerMatches(0x0001u, 0x8001u));
    CHECK_FALSE(lightLayerMatches(0x0002u, 0x8001u));
}


TEST_CASE("light builder writes cookie and IES samples") {
    RenderScene scene;
    Light light = makePoint("cookie", Vec3(0.0f, 0.0f, 0.0f), 2.0f, 5.0f);
    light.cookieMode = LightCookieMode::Repeat;
    light.cookieType = LightCookieType::PointOctahedral;
    light.cookieSamples[0] = 0.25f;
    light.cookieSamples[1] = 0.75f;
    light.hasIes = true;
    light.iesSamples[0] = 0.5f;
    scene.lights.push_back(light);

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, -1.0f), 100.0f);
    const GpuLightData& data = builder.data();
    CHECK(data.texels[7].x == doctest::Approx(static_cast<float>(LightCookieMode::Repeat)));
    CHECK(data.texels[7].y == doctest::Approx(static_cast<float>(LightCookieType::PointOctahedral)));
    CHECK(data.texels[7].z == doctest::Approx(0.0f));
    CHECK(data.texels[7].w == doctest::Approx(8.0f));
    CHECK(data.texels[kLightIesBaseTexel].x == doctest::Approx(0.5f));

    builder.setCookieAtlas(0, 5, Vec4(0.25f, 0.5f, 0.125f, 0.25f));
    CHECK(builder.data().texels[7].z == doctest::Approx(6.0f));
    CHECK(builder.data().texels[11].x == doctest::Approx(0.25f));
    CHECK(builder.data().texels[11].y == doctest::Approx(0.5f));
    CHECK(builder.data().texels[11].z == doctest::Approx(0.125f));
    CHECK(builder.data().texels[11].w == doctest::Approx(0.25f));
}
TEST_CASE("light builder writes rectangle area data and cookies") {
    RenderScene scene;
    Light light;
    light.name = "area";
    light.type = LightType::Area;
    light.areaShape = LightAreaShape::Rectangle;
    light.areaWidth = 2.0f;
    light.areaHeight = 1.0f;
    light.intensity = 3.0f;
    light.range = 6.0f;
    light.cookieMode = LightCookieMode::Repeat;
    light.cookieType = LightCookieType::Spot2D;
    light.cookieScale = Vec2(2.0f, 3.0f);
    light.cookieOffset = Vec2(0.25f, 0.5f);
    scene.lights.push_back(light);

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, -1.0f), 100.0f);
    const GpuLightData& data = builder.data();

    CHECK(data.lightCount == 1u);
    CHECK(data.areaCount == 1u);
    CHECK(data.punctualCount == 0u);
    CHECK(data.texels[0].w == doctest::Approx(6.0f));
    CHECK(data.texels[2].x == doctest::Approx(0.0f));
    CHECK(data.texels[2].y == doctest::Approx(0.0f));
    CHECK(data.texels[2].z == doctest::Approx(-1.0f));
    CHECK(data.texels[3].w == doctest::Approx(static_cast<float>(GpuLightType::AreaRectangle)));
    CHECK(data.texels[6].y == doctest::Approx(static_cast<float>(LightCategory::Area)));
    CHECK(data.texels[6].z == doctest::Approx(static_cast<float>(kLightFeatureArea)));
    CHECK(data.texels[8].w == doctest::Approx(1.0f));
    CHECK(data.texels[9].w == doctest::Approx(0.5f));
    CHECK(data.texels[10].x == doctest::Approx(2.0f));
    CHECK(data.texels[10].y == doctest::Approx(3.0f));
    CHECK(data.texels[10].z == doctest::Approx(0.25f));
    CHECK(data.texels[10].w == doctest::Approx(0.5f));
}

TEST_CASE("light builder writes tube area data as a zero-height line") {
    RenderScene scene;
    Light light;
    light.name = "tube";
    light.type = LightType::Area;
    light.areaShape = LightAreaShape::Tube;
    light.areaWidth = 4.0f;
    light.areaHeight = 2.0f;
    light.intensity = 3.0f;
    light.range = 6.0f;
    scene.lights.push_back(light);

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, -1.0f), 100.0f);
    const GpuLightData& data = builder.data();

    CHECK(data.lightCount == 1u);
    CHECK(data.areaCount == 1u);
    CHECK(data.texels[3].w == doctest::Approx(static_cast<float>(GpuLightType::AreaTube)));
    CHECK(data.texels[6].y == doctest::Approx(static_cast<float>(LightCategory::Area)));
    CHECK(data.texels[6].z == doctest::Approx(static_cast<float>(kLightFeatureArea)));
    CHECK(data.texels[7].w == doctest::Approx(static_cast<float>(LightAreaShape::Tube)));
    CHECK(data.texels[8].w == doctest::Approx(2.0f));
    CHECK(data.texels[9].w == doctest::Approx(0.0f));
}

TEST_CASE("light builder supplies a fallback directional when the scene has no lights") {
    RenderScene scene;

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), 100.0f);
    const GpuLightData& data = builder.data();

    CHECK(data.lightCount == 1);
    CHECK(data.directionalCount == 1);
    CHECK(data.punctualCount == 0);
    CHECK(data.hasDirectional);
    CHECK(data.primaryIntensity == doctest::Approx(4.0f));
    CHECK(builder.shadowCaster() == nullptr);
}
