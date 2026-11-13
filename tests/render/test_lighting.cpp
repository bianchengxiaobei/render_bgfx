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

    CHECK(data.lightCount == kLightListDefaultCapacity);
    CHECK(data.directionalCount == 1);
    CHECK(data.punctualCount == kLightListDefaultCapacity - 1);
    CHECK(data.truncatedLightCount == 41u - kLightListDefaultCapacity);
    CHECK(data.shadowedCount == 1);
    REQUIRE(builder.shadowCaster() != nullptr);
    CHECK(builder.shadowCaster()->type == LightType::Directional);

    CHECK(data.texels[0].x == doctest::Approx(0.0f));
    CHECK(data.texels[2].x == doctest::Approx(0.0f));
    CHECK(data.texels[2].y == doctest::Approx(0.0f));
    CHECK(data.texels[2].z == doctest::Approx(1.0f));
    CHECK(data.texels[3].x == doctest::Approx(0.0f));
    CHECK(data.texels[3].w == doctest::Approx(4.0f));

    CHECK(data.texels[6].w == doctest::Approx(5.0f));
    CHECK(data.texels[7].w == doctest::Approx(10.0f));
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
