#include "render/camera/view_state.h"
#include "render/lighting/light_list_builder.h"
#include "render/scene/render_scene.h"

#include <doctest/doctest.h>

#include <cstdint>

using namespace hbg;
using namespace hbg::render;

namespace {

ViewState makeViewState(uint16_t width, uint16_t height) {
    Camera camera;
    camera.world = inverse(lookAtRH(Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)));
    camera.nearClip = 0.1f;
    camera.farClip = 100.0f;

    ViewState state;
    state.update(camera, width, height, 1.0f, false, false);
    return state;
}

void addPointLight(GpuLightData& data, uint32_t index, const Vec3& position, float range, float intensity = 1.0f) {
    const uint32_t base = index * kGpuLightTexels;
    data.texels[base + 0] = Vec4(position.x, position.y, position.z, range);
    data.texels[base + 1] = Vec4(1.0f, 1.0f, 1.0f, intensity);
    data.texels[base + 2] = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    data.texels[base + 3] = Vec4(-1.0f, 0.0f, 1.0f, 1.0f);
}

GpuLightData makePunctualData(uint32_t lightCount) {
    GpuLightData data;
    data.lightCount = lightCount;
    data.punctualCount = lightCount;
    return data;
}

} // namespace

TEST_CASE("light list uses HDRP 16x16 tiles and 64x64 big tiles") {
    CHECK(LightListBuilder::tileCount(320, kLightTileSize) == 20);
    CHECK(LightListBuilder::tileCount(180, kLightTileSize) == 12);
    CHECK(LightListBuilder::tileCount(320, kLightBigTileSize) == 5);
    CHECK(LightListBuilder::tileCount(180, kLightBigTileSize) == 3);
    CHECK(LightListBuilder::textureWidthFor(320) == 20 * kLightListStride);
    CHECK(LightListBuilder::textureHeightFor(180) == 12);
    CHECK(LightListBuilder::textureWidthFor(0) == 0);
}

TEST_CASE("light list culls off-screen punctual lights and records the center tile") {
    const ViewState state = makeViewState(320, 180);
    GpuLightData lights = makePunctualData(2);
    addPointLight(lights, 0, Vec3(0.0f, 0.0f, 0.0f), 0.5f);
    addPointLight(lights, 1, Vec3(100.0f, 0.0f, 0.0f), 0.5f);

    LightListBuilder builder;
    builder.build(lights, state.constants().viewProj, 320, 180);
    const GpuLightListData& data = builder.data();

    REQUIRE(data.enabled);
    CHECK(data.tileCountX == 20);
    CHECK(data.tileCountY == 12);
    CHECK(data.width == 20 * kLightListStride);
    CHECK(data.height == 12);
    CHECK(data.maxLightsPerTile >= 1);
    CHECK(builder.tileLightCount(10, 5) >= 1);

    bool found = false;
    for (uint32_t index = 0; index < builder.tileLightCount(10, 5); ++index) {
        if (builder.tileLightIndex(10, 5, index) == 0) {
            found = true;
        }
        CHECK(builder.tileLightIndex(10, 5, index) != 1);
    }
    CHECK(found);
}

TEST_CASE("light list rejects lights entirely behind the camera") {
    const ViewState state = makeViewState(320, 180);
    GpuLightData lights = makePunctualData(1);
    addPointLight(lights, 0, Vec3(0.0f, 0.0f, 10.0f), 0.5f);

    LightListBuilder builder;
    builder.build(lights, state.constants().viewProj, 320, 180);
    const GpuLightListData& data = builder.data();

    CHECK(data.enabled);
    CHECK(data.maxLightsPerTile == 0);
    for (uint16_t y = 0; y < data.tileCountY; ++y) {
        for (uint16_t x = 0; x < data.tileCountX; ++x) {
            CHECK(builder.tileLightCount(x, y) == 0);
        }
    }
}

TEST_CASE("light list covers the screen when a punctual light contains the camera") {
    const ViewState state = makeViewState(320, 180);
    GpuLightData lights = makePunctualData(1);
    addPointLight(lights, 0, Vec3(0.0f, 0.0f, 5.0f), 2.0f);

    LightListBuilder builder;
    builder.build(lights, state.constants().viewProj, 320, 180);
    const GpuLightListData& data = builder.data();

    CHECK(data.enabled);
    CHECK(builder.tileLightCount(0, 0) == 1);
    CHECK(builder.tileLightIndex(0, 0, 0) == 0);
    CHECK(builder.tileLightCount(data.tileCountX - 1, data.tileCountY - 1) == 1);
}

TEST_CASE("light list keeps directional lights out of punctual tiles and caps list length") {
    GpuLightData directional;
    directional.lightCount = 1;
    directional.directionalCount = 1;
    directional.texels[3] = Vec4(-1.0f, 0.0f, 1.0f, 0.0f);

    LightListBuilder builder;
    builder.build(directional, Mat4::identity(), 320, 180);
    CHECK(builder.data().enabled);
    CHECK(builder.tileLightCount(0, 0) == 0);

    GpuLightData lights = makePunctualData(kMaxGpuLights);
    for (uint32_t index = 0; index < kMaxGpuLights; ++index) {
        addPointLight(lights, index, Vec3(0.0f, 0.0f, 0.0f), 1.0f);
    }
    builder.build(lights, makeViewState(320, 180).constants().viewProj, 320, 180);
    CHECK(builder.tileLightCount(10, 5) == kLightListMaxLights);
    CHECK(builder.data().maxLightsPerTile == kLightListMaxLights);
}
