#include "render/lighting/light_builder.h"
#include "render/scene/render_scene.h"
#include "render/shadows/contact_shadow.h"
#include "render/shadows/shadow_settings.h"

#include <doctest/doctest.h>

#include <string>

using namespace hbg;
using namespace hbg::render;

namespace {

Light makeDirectional() {
    Light light;
    light.type = LightType::Directional;
    light.intensity = 3.0f;
    light.castShadows = true;
    return light;
}

Light makePoint(float x) {
    Light light;
    light.type = LightType::Point;
    light.intensity = 10.0f;
    light.range = 5.0f;
    light.castShadows = true;
    light.world.values[12] = x;
    light.world.values[13] = 2.0f;
    light.world.values[14] = 0.0f;
    return light;
}

} // namespace

TEST_CASE("contact shadow settings defaults and sanitize") {
    ContactShadowSettings settings;
    CHECK_FALSE(settings.enabled);
    CHECK_FALSE(settings.directionalOnly);
    CHECK_FALSE(settings.halfResolution);
    CHECK(settings.length == doctest::Approx(0.15f));
    CHECK(settings.opacity == doctest::Approx(1.0f));
    CHECK(settings.distanceScaleFactor == doctest::Approx(0.5f));
    CHECK(settings.minDistance == doctest::Approx(0.0f));
    CHECK(settings.maxDistance == doctest::Approx(50.0f));
    CHECK(settings.fadeDistance == doctest::Approx(5.0f));
    CHECK(settings.fadeInDistance == doctest::Approx(0.0f));
    CHECK(settings.rayBias == doctest::Approx(0.2f));
    CHECK(settings.thicknessScale == doctest::Approx(0.15f));
    CHECK(settings.sampleCount == 10);

    settings.length = 9.0f;
    settings.opacity = -1.0f;
    settings.distanceScaleFactor = 9.0f;
    settings.minDistance = 10.0f;
    settings.maxDistance = 2.0f;
    settings.fadeDistance = -1.0f;
    settings.rayBias = 2.0f;
    settings.thicknessScale = 9.0f;
    settings.sampleCount = 1;
    sanitizeContactShadowSettings(settings);
    CHECK(settings.length == doctest::Approx(1.0f));
    CHECK(settings.opacity == doctest::Approx(0.0f));
    CHECK(settings.distanceScaleFactor == doctest::Approx(1.0f));
    CHECK(settings.maxDistance == doctest::Approx(10.01f));
    CHECK(settings.fadeDistance == doctest::Approx(0.0f));
    CHECK(settings.rayBias == doctest::Approx(1.0f));
    CHECK(settings.thicknessScale == doctest::Approx(1.0f));
    CHECK(settings.sampleCount == 4u);
}

TEST_CASE("contact shadow resolution and light masks") {
    const auto full = contactShadowResolution(1920, 1080, false);
    CHECK(full[0] == 1920u);
    CHECK(full[1] == 1080u);
    const auto half = contactShadowResolution(1920, 1080, true);
    CHECK(half[0] == 960u);
    CHECK(half[1] == 540u);
    const auto odd = contactShadowResolution(5, 3, true);
    CHECK(odd[0] == 3u);
    CHECK(odd[1] == 2u);

    CHECK(contactShadowBit(0) == 1u);
    CHECK(contactShadowBit(7) == 128u);
    CHECK(contactShadowBit(kMaxContactShadowLights) == 0u);
    CHECK(contactShadowCoversLight(0x05u, 0x04u));
    CHECK_FALSE(contactShadowCoversLight(0x05u, 0x02u));
    CHECK_FALSE(contactShadowCoversLight(0x05u, 0u));
}

TEST_CASE("contact shadow packed data and ray helpers") {
    CHECK(packContactShadowData(0.0f, 0u) == 0u);
    CHECK(packContactShadowData(1.0f, 0xFFu) == 0xFFFFu);
    const uint32_t packed = packContactShadowData(0.5f, 0x5Au);
    CHECK(packed == 0x805Au);
    float fade = 0.0f;
    uint32_t mask = 0u;
    unpackContactShadowData(packed, fade, mask);
    CHECK(fade == doctest::Approx(128.0f / 255.0f));
    CHECK(mask == 0x5Au);

    CHECK(computeContactShadowRayLength(0.15f, 0.5f, 1.0f) == doctest::Approx(0.075f));
    CHECK(computeContactShadowRayLength(0.15f, 0.5f, 10.0f) == doctest::Approx(0.75f));
    CHECK(computeContactShadowRayLength(0.15f, 0.5f, 0.1f) == doctest::Approx(0.075f));

    ContactShadowSettings settings;
    CHECK(computeContactShadowFade(0.0f, settings) == doctest::Approx(1.0f));
    CHECK(computeContactShadowFade(25.0f, settings) == doctest::Approx(1.0f));
    CHECK(computeContactShadowFade(50.0f, settings) == doctest::Approx(0.0f));
    CHECK(computeContactShadowFade(51.0f, settings) == doctest::Approx(0.0f));
    settings.minDistance = 2.0f;
    settings.fadeInDistance = 2.0f;
    CHECK(computeContactShadowFade(1.0f, settings) == doctest::Approx(0.0f));
    CHECK(computeContactShadowFade(3.0f, settings) == doctest::Approx(0.5f));
    settings.maxDistance = 10.0f;
    settings.fadeDistance = 2.0f;
    CHECK(computeContactShadowFade(9.0f, settings) == doctest::Approx(0.5f));
    CHECK(computeContactShadowThickness(0.15f) == doctest::Approx(0.015f));
    CHECK(computeContactShadowThickness(0.0f) == doctest::Approx(0.004f));
    CHECK(contactShadowDistanceHit(0.01f, 0.015f));
    CHECK_FALSE(contactShadowDistanceHit(0.02f, 0.015f));
}

TEST_CASE("light builder assigns contact shadow mask bits") {
    RenderScene scene;
    scene.lights.push_back(makeDirectional());
    scene.lights.push_back(makePoint(1.0f));
    scene.lights.push_back(makePoint(2.0f));

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 1.0f, 5.0f), Vec3(0.0f, 0.0f, -1.0f), 100.0f, true, false);
    const GpuLightData& data = builder.data();
    CHECK(data.texels[3].z == doctest::Approx(1.0f));
    CHECK(data.texels[kGpuLightTexels + 3].z == doctest::Approx(2.0f));
    CHECK(data.texels[2 * kGpuLightTexels + 3].z == doctest::Approx(4.0f));

    LightBuilder directionalOnlyBuilder;
    directionalOnlyBuilder.build(scene, Vec3(0.0f, 1.0f, 5.0f), Vec3(0.0f, 0.0f, -1.0f), 100.0f, true,
                                 true);
    const GpuLightData& directionalOnly = directionalOnlyBuilder.data();
    CHECK(directionalOnly.texels[3].z == doctest::Approx(1.0f));
    CHECK(directionalOnly.texels[kGpuLightTexels + 3].z == doctest::Approx(0.0f));

    LightBuilder disabledBuilder;
    disabledBuilder.build(scene, Vec3(0.0f, 1.0f, 5.0f), Vec3(0.0f, 0.0f, -1.0f), 100.0f, false, false);
    CHECK(disabledBuilder.data().texels[3].z == doctest::Approx(0.0f));
}

TEST_CASE("shadow settings parse and round-trip contact shadows") {
    const std::string json = R"({
        "contactShadows": {
            "enabled": true,
            "directionalOnly": true,
            "halfResolution": true,
            "length": 0.4,
            "opacity": 0.6,
            "distanceScaleFactor": 0.3,
            "minDistance": 1.5,
            "maxDistance": 20.0,
            "fadeDistance": 3.0,
            "fadeInDistance": 0.5,
            "rayBias": 0.1,
            "thicknessScale": 0.2,
            "sampleCount": 16
        }
    })";

    ShadowSettings settings;
    std::string error;
    REQUIRE(parseShadowSettings(json, settings, error));
    CHECK(settings.contactShadows.enabled);
    CHECK(settings.contactShadows.directionalOnly);
    CHECK(settings.contactShadows.halfResolution);
    CHECK(settings.contactShadows.length == doctest::Approx(0.4f));
    CHECK(settings.contactShadows.opacity == doctest::Approx(0.6f));
    CHECK(settings.contactShadows.sampleCount == 16u);

    ShadowSettings parsed;
    REQUIRE(parseShadowSettings(shadowSettingsToJson(settings), parsed, error));
    CHECK(parsed.contactShadows.enabled);
    CHECK(parsed.contactShadows.halfResolution);
    CHECK(parsed.contactShadows.distanceScaleFactor == doctest::Approx(0.3f));
    CHECK(parsed.contactShadows.maxDistance == doctest::Approx(20.0f));
    CHECK(parsed.contactShadows.thicknessScale == doctest::Approx(0.2f));
}
