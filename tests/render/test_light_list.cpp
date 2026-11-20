#include "render/camera/view_state.h"
#include "render/lighting/light_builder.h"
#include "render/lighting/light_list_builder.h"
#include "render/passes/deferred_variant.h"
#include "render/scene/render_scene.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <random>

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
    data.texels[base + 6] = Vec4(65535.0f, 0.0f, 1.0f, 0.0f);
}

GpuLightData makePunctualData(uint32_t lightCount) {
    GpuLightData data;
    data.lightCount = lightCount;
    data.punctualCount = lightCount;
    return data;
}

} // namespace

TEST_CASE("light screen bounds match CPU screen-space and depth semantics") {
    const ViewState state = makeViewState(320, 180);
    const ViewConstants& constants = state.constants();

    LightScreenBounds bounds;
    REQUIRE(LightListBuilder::projectLightScreenBounds(constants.viewProj, constants.view, Vec3(0.0f, 0.0f, 0.0f),
                                                       0.5f, 0.1f, 100.0f, 320, 180, bounds));
    CHECK(bounds.max.z == 1.0f);
    CHECK(bounds.min.x < 160.0f);
    CHECK(bounds.max.x > 160.0f);
    CHECK(bounds.min.y < 90.0f);
    CHECK(bounds.max.y > 90.0f);
    CHECK(bounds.min.z == doctest::Approx(4.5f));
    CHECK(bounds.min.w == doctest::Approx(5.5f));

    CHECK_FALSE(LightListBuilder::projectLightScreenBounds(constants.viewProj, constants.view, Vec3(100.0f, 0.0f, 0.0f),
                                                           0.5f, 0.1f, 100.0f, 320, 180, bounds));
    CHECK(bounds.min == Vec4(0.0f, 0.0f, 0.0f, 0.0f));
    CHECK(bounds.max == Vec4(0.0f, 0.0f, 0.0f, 0.0f));

    CHECK_FALSE(LightListBuilder::projectLightScreenBounds(constants.viewProj, constants.view, Vec3(0.0f, 0.0f, 10.0f),
                                                           0.5f, 0.1f, 100.0f, 320, 180, bounds));

    REQUIRE(LightListBuilder::projectLightScreenBounds(constants.viewProj, constants.view, Vec3(0.0f, 0.0f, 5.0f),
                                                       2.0f, 0.1f, 100.0f, 320, 180, bounds));
    CHECK(bounds.min.x == 0.0f);
    CHECK(bounds.min.y == 0.0f);
    CHECK(bounds.max.x == 320.0f);
    CHECK(bounds.max.y == 180.0f);
    CHECK(bounds.min.z == doctest::Approx(0.1f));
    CHECK(bounds.min.w == doctest::Approx(2.0f));
}

TEST_CASE("area lights use HDRP oriented screen bounds and one-sided depth") {
    const ViewState state = makeViewState(320, 180);
    const ViewConstants& constants = state.constants();

    LightBounds rectangle;
    rectangle.position = Vec3(0.0f, 0.0f, 0.0f);
    rectangle.forward = Vec3(0.0f, 0.0f, -1.0f);
    rectangle.right = Vec3(1.0f, 0.0f, 0.0f);
    rectangle.up = Vec3(0.0f, 1.0f, 0.0f);
    rectangle.range = 2.0f;
    rectangle.halfWidth = 1.0f;
    rectangle.halfHeight = 1.0f;
    rectangle.type = GpuLightType::AreaRectangle;

    LightScreenBounds bounds;
    REQUIRE(LightListBuilder::projectLightScreenBounds(constants.viewProj, constants.view, rectangle, 0.1f, 100.0f,
                                                       320, 180, bounds));
    CHECK(bounds.min.z == doctest::Approx(5.0f));
    CHECK(bounds.min.w == doctest::Approx(7.0f));
    CHECK(bounds.min.x > 0.0f);
    CHECK(bounds.max.x < 320.0f);

    LightScreenBounds sphereBounds;
    REQUIRE(LightListBuilder::projectLightScreenBounds(constants.viewProj, constants.view, rectangle.position,
                                                       2.0f, 0.1f, 100.0f, 320, 180, sphereBounds));
    CHECK(sphereBounds.min.z == doctest::Approx(3.0f));
    CHECK(bounds.min.z > sphereBounds.min.z);

    LightBounds tube = rectangle;
    tube.type = GpuLightType::AreaTube;
    tube.halfHeight = 0.0f;
    REQUIRE(LightListBuilder::projectLightScreenBounds(constants.viewProj, constants.view, tube, 0.1f, 100.0f,
                                                       320, 180, bounds));
    CHECK(bounds.min.z == doctest::Approx(3.0f));
    CHECK(bounds.min.w == doctest::Approx(7.0f));

    GpuLightData lights = makePunctualData(1);
    lights.punctualCount = 0;
    lights.areaCount = 1;
    lights.texels[0] = Vec4(0.0f, 0.0f, 0.0f, 2.0f);
    lights.texels[2] = Vec4(0.0f, 0.0f, -1.0f, 0.0f);
    lights.texels[3] = Vec4(-1.0f, 0.0f, 0.0f, static_cast<float>(GpuLightType::AreaRectangle));
    lights.texels[6] = Vec4(65535.0f, static_cast<float>(LightCategory::Area), kLightFeatureArea, 0.0f);
    lights.texels[8] = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
    lights.texels[9] = Vec4(0.0f, 1.0f, 0.0f, 1.0f);

    LightListBuilder builder;
    builder.build(lights, constants.viewProj, constants.view, 0.1f, 100.0f, 320, 180);
    const uint32_t areaCategory = static_cast<uint32_t>(LightCategory::Area);
    const int oldSphereSlice = LightListBuilder::clusterSliceForDepth(3.0f, 0.1f, 100.0f);
    const int rectangleSlice = LightListBuilder::clusterSliceForDepth(5.0f, 0.1f, 100.0f);
    CHECK(builder.clusterLightCount(2, 1, oldSphereSlice, areaCategory) == 0);
    CHECK(builder.clusterLightCount(2, 1, rectangleSlice, areaCategory) >= 1);
}

TEST_CASE("light list uses HDRP 16x16 tiles and 64x64 big tiles") {
    CHECK(LightListBuilder::tileCount(320, kLightTileSize) == 20);
    CHECK(LightListBuilder::tileCount(180, kLightTileSize) == 12);
    CHECK(LightListBuilder::tileCount(320, kLightBigTileSize) == 5);
    CHECK(LightListBuilder::tileCount(180, kLightBigTileSize) == 3);
    CHECK(LightListBuilder::textureWidthFor(320) == 20 * lightListStrideFor(kLightListDefaultCapacity));
    CHECK(LightListBuilder::textureHeightFor(180) == 12 * kLightCategoryCount);
    CHECK(LightListBuilder::bigTileTextureWidthFor(320) == 5 * lightListStrideFor(kLightListDefaultCapacity));
    CHECK(LightListBuilder::bigTileTextureHeightFor(180) == 3 * kLightCategoryCount);
    CHECK(LightListBuilder::bigTileTextureWidthFor(1920) == 30 * lightListStrideFor(kLightListDefaultCapacity));
    CHECK(LightListBuilder::bigTileTextureHeightFor(1080) == 17 * kLightCategoryCount);
    CHECK(LightListBuilder::textureWidthFor(0) == 0);
    CHECK(LightListBuilder::bigTileTextureWidthFor(0) == 0);
}

TEST_CASE("light list texture capacity clamps to the backend texture limit") {
    CHECK(LightListBuilder::textureFits(1920, 1080, 128, 16384));
    CHECK(LightListBuilder::textureFits(2560, 1440, 128, 16384));
    CHECK(LightListBuilder::textureFits(2560, 1440, 64, 16384));
    CHECK(LightListBuilder::textureFits(3840, 2160, 128, 16384));
    CHECK(LightListBuilder::textureFits(3840, 2160, 64, 16384));

    CHECK(LightListBuilder::textureCapacityFor(1920, 1080, 128, 16384) == 128);
    CHECK(LightListBuilder::textureCapacityFor(2560, 1440, 128, 16384) == 128);
    CHECK(LightListBuilder::textureCapacityFor(2560, 1440, 128, 32768) == 128);
    CHECK(LightListBuilder::textureCapacityFor(3840, 2160, 128, 16384) == 128);
    CHECK(LightListBuilder::textureCapacityFor(3840, 2160, 128, 32768) == 128);
    CHECK(LightListBuilder::textureCapacityFor(3840, 2160, 64, 16384) == 64);
    CHECK(LightListBuilder::textureCapacityFor(7680, 4320, 128, 32768) == 64);
    CHECK(LightListBuilder::textureCapacityFor(1920, 1080, 64, 16384) == 64);
    CHECK(LightListBuilder::textureCapacityFor(960, 540, 128, 16384) == 128);
    CHECK(LightListBuilder::textureCapacityFor(2560, 1440, 64, 16384) == 64);
    CHECK(LightListBuilder::textureCapacityFor(2560, 1440, 128, 8192) == 64);
    CHECK(LightListBuilder::textureCapacityFor(2560, 1440, 128, 0) == 0);
}

TEST_CASE("light cluster uses 64x64 tiles and 32 logarithmic depth slices") {
    CHECK(LightListBuilder::clusterOffsetTextureWidthFor(320) == 5);
    CHECK(LightListBuilder::clusterOffsetTextureHeightFor(180) ==
          3 * kClusterSliceCount * kLightCategoryCount);
    CHECK(LightListBuilder::clusterOffsetTextureWidthFor(0) == 0);
    CHECK(LightListBuilder::clusterIndexTextureWidthFor(0) == 0);
    CHECK(LightListBuilder::clusterSliceForDepth(0.1f, 0.1f, 100.0f) == 0);
    CHECK(LightListBuilder::clusterSliceForDepth(100.0f, 0.1f, 100.0f) ==
          static_cast<int>(kClusterSliceCount) - 1);
    CHECK(LightListBuilder::clusterSliceForDepth(5.0f, 0.1f, 100.0f) >
          LightListBuilder::clusterSliceForDepth(1.0f, 0.1f, 100.0f));
}

TEST_CASE("light cluster culls off-screen punctual lights and records their depth slice") {
    const ViewState state = makeViewState(320, 180);
    GpuLightData lights = makePunctualData(2);
    addPointLight(lights, 0, Vec3(0.0f, 0.0f, 0.0f), 0.5f);
    addPointLight(lights, 1, Vec3(100.0f, 0.0f, 0.0f), 0.5f);

    LightListBuilder builder;
    builder.build(lights, state.constants().viewProj, state.constants().view, 0.1f, 100.0f, 320, 180);
    const GpuLightClusterData& cluster = builder.clusterData();

    REQUIRE(cluster.enabled);
    CHECK(cluster.tileCountX == 5);
    CHECK(cluster.tileCountY == 3);
    CHECK(cluster.capacity == kLightListDefaultCapacity);
    CHECK(cluster.offsets.size() == 5u * 3u * kClusterSliceCount * kLightCategoryCount);
    CHECK(cluster.indexCapacity == 5u * 3u * kClusterSliceCount * kLightListDefaultCapacity * kLightCategoryCount);
    const int slice = LightListBuilder::clusterSliceForDepth(5.0f, 0.1f, 100.0f);
    CHECK(builder.clusterLightCount(2, 1, slice) >= 1);
    CHECK(builder.clusterLightCount(2, 1, 0) == 0);

    bool found = false;
    for (uint32_t index = 0; index < builder.clusterLightCount(2, 1, slice); ++index) {
        if (builder.clusterLightIndex(2, 1, slice, index) == 0) {
            found = true;
        }
        CHECK(builder.clusterLightIndex(2, 1, slice, index) != 1);
    }
    CHECK(found);
}

TEST_CASE("light cluster excludes directional lights and caps cluster lists") {
    GpuLightData directional;
    directional.lightCount = 1;
    directional.directionalCount = 1;
    directional.texels[3] = Vec4(-1.0f, 0.0f, 1.0f, 0.0f);

    LightListBuilder builder;
    builder.build(directional, Mat4::identity(), Mat4::identity(), 0.1f, 100.0f, 320, 180);
    REQUIRE(builder.clusterData().enabled);
    for (uint16_t y = 0; y < builder.clusterData().tileCountY; ++y) {
        for (uint16_t x = 0; x < builder.clusterData().tileCountX; ++x) {
            for (uint32_t slice = 0; slice < kClusterSliceCount; ++slice) {
                CHECK(builder.clusterLightCount(x, y, slice) == 0);
            }
        }
    }

    GpuLightData lights = makePunctualData(kMaxGpuLights);
    for (uint32_t index = 0; index < kMaxGpuLights; ++index) {
        addPointLight(lights, index, Vec3(0.0f, 0.0f, 0.0f), 1.0f);
    }
    const ViewState state = makeViewState(320, 180);
    builder.build(lights, state.constants().viewProj, state.constants().view, 0.1f, 100.0f, 320, 180, 32);
    const int slice = LightListBuilder::clusterSliceForDepth(5.0f, 0.1f, 100.0f);
    CHECK(builder.clusterData().maxLightsPerCluster == kLightClusterMaxLights);
    CHECK(builder.clusterData().truncatedClusters == 0);
    CHECK(builder.clusterLightCount(2, 1, slice) == kLightClusterMaxLights);
}

TEST_CASE("light cluster matches FPTL screen coverage and per-slice depth ranges") {
    constexpr float kNear = 0.1f;
    constexpr float kFar = 100.0f;
    const ViewState state = makeViewState(320, 180);
    const ViewConstants& constants = state.constants();

    GpuLightData lights = makePunctualData(16);
    std::mt19937 rng(123456u);
    std::uniform_real_distribution<float> positionDist(-3.0f, 3.0f);
    std::uniform_real_distribution<float> depthDist(1.0f, 9.0f);
    std::uniform_real_distribution<float> rangeDist(0.4f, 2.5f);
    for (uint32_t index = 0; index < lights.punctualCount; ++index) {
        addPointLight(lights, index,
                      Vec3(positionDist(rng), positionDist(rng), depthDist(rng) - 5.0f), rangeDist(rng));
    }

    LightListBuilder builder;
    builder.build(lights, constants.viewProj, constants.view, kNear, kFar, 320, 180);
    const GpuLightListData& fptl = builder.data();
    const GpuLightClusterData& cluster = builder.clusterData();
    REQUIRE(fptl.enabled);
    REQUIRE(cluster.enabled);

    const auto depthOf = [&](uint32_t lightIndex) {
        const Vec4& positionRange = lights.texels[lightIndex * kGpuLightTexels];
        const Vec3 position(positionRange.x, positionRange.y, positionRange.z);
        return -(constants.view[2] * position.x + constants.view[6] * position.y +
                 constants.view[10] * position.z + constants.view[14]);
    };
    const auto rangeOf = [&](uint32_t lightIndex) {
        return std::max(lights.texels[lightIndex * kGpuLightTexels].w, 0.0f);
    };

    for (uint16_t clusterY = 0; clusterY < cluster.tileCountY; ++clusterY) {
        for (uint16_t clusterX = 0; clusterX < cluster.tileCountX; ++clusterX) {
            std::array<bool, kMaxGpuLights> screenOverlap{};
            for (uint32_t subY = 0; subY < kClusterTileSize / kLightTileSize; ++subY) {
                for (uint32_t subX = 0; subX < kClusterTileSize / kLightTileSize; ++subX) {
                    const uint16_t tileY = static_cast<uint16_t>(clusterY * (kClusterTileSize / kLightTileSize) + subY);
                    const uint16_t tileX = static_cast<uint16_t>(clusterX * (kClusterTileSize / kLightTileSize) + subX);
                    if (tileX >= fptl.tileCountX || tileY >= fptl.tileCountY) {
                        continue;
                    }
                    for (uint32_t offset = 0; offset < builder.tileLightCount(tileX, tileY); ++offset) {
                        const uint32_t lightIndex = builder.tileLightIndex(tileX, tileY, offset);
                        if (lightIndex < kMaxGpuLights) {
                            screenOverlap[lightIndex] = true;
                        }
                    }
                }
            }

            for (uint32_t slice = 0; slice < kClusterSliceCount; ++slice) {
                std::array<bool, kMaxGpuLights> expected{};
                for (uint32_t lightIndex = 0; lightIndex < lights.punctualCount; ++lightIndex) {
                    if (!screenOverlap[lightIndex]) {
                        continue;
                    }
                    const float range = rangeOf(lightIndex);
                    const float center = depthOf(lightIndex);
                    const float minDepth = std::max(center - range, kNear);
                    const float maxDepth = std::min(center + range, kFar);
                    if (maxDepth < minDepth) {
                        continue;
                    }
                    const int minSlice = LightListBuilder::clusterSliceForDepth(minDepth, kNear, kFar);
                    const int maxSlice = LightListBuilder::clusterSliceForDepth(maxDepth, kNear, kFar);
                    expected[lightIndex] = slice >= static_cast<uint32_t>(minSlice) &&
                                           slice <= static_cast<uint32_t>(maxSlice);
                }

                for (uint32_t offset = 0; offset < builder.clusterLightCount(clusterX, clusterY, slice); ++offset) {
                    const uint32_t lightIndex = builder.clusterLightIndex(clusterX, clusterY, slice, offset);
                    REQUIRE(lightIndex < lights.punctualCount);
                    CHECK(expected[lightIndex]);
                }
                for (uint32_t lightIndex = 0; lightIndex < lights.punctualCount; ++lightIndex) {
                    if (!expected[lightIndex]) {
                        continue;
                    }
                    bool found = false;
                    for (uint32_t offset = 0; offset < builder.clusterLightCount(clusterX, clusterY, slice); ++offset) {
                        found = found || builder.clusterLightIndex(clusterX, clusterY, slice, offset) == lightIndex;
                    }
                    CHECK(found);
                }
            }
        }
    }
}

TEST_CASE("light list culls off-screen punctual lights and records the center tile") {
    const ViewState state = makeViewState(320, 180);
    GpuLightData lights = makePunctualData(2);
    addPointLight(lights, 0, Vec3(0.0f, 0.0f, 0.0f), 0.5f);
    addPointLight(lights, 1, Vec3(100.0f, 0.0f, 0.0f), 0.5f);

    LightListBuilder builder;
    builder.build(lights, state.constants().viewProj, state.constants().view, 0.1f, 100.0f, 320, 180);
    const GpuLightListData& data = builder.data();

    REQUIRE(data.enabled);
    CHECK(data.tileCountX == 20);
    CHECK(data.tileCountY == 12);
    CHECK(data.width == 20 * lightListStrideFor(kLightListDefaultCapacity));
    CHECK(data.height == 12 * kLightCategoryCount);
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

TEST_CASE("light list routes area lights into the area category") {
    const ViewState state = makeViewState(320, 180);
    GpuLightData lights = makePunctualData(1);
    addPointLight(lights, 0, Vec3(0.0f, 0.0f, 0.0f), 0.5f);
    lights.areaCount = 1;
    lights.punctualCount = 0;
    lights.texels[3] = Vec4(-1.0f, 0.0f, 0.0f, static_cast<float>(GpuLightType::AreaRectangle));
    lights.texels[6] = Vec4(65535.0f, static_cast<float>(LightCategory::Area), kLightFeatureArea, 0.0f);

    LightListBuilder builder;
    builder.build(lights, state.constants().viewProj, state.constants().view, 0.1f, 100.0f, 320, 180);
    REQUIRE(builder.data().enabled);
    CHECK(builder.tileLightCount(10, 5, static_cast<uint32_t>(LightCategory::Area)) >= 1);
    CHECK(builder.tileLightCount(10, 5, static_cast<uint32_t>(LightCategory::Punctual)) == 0);
    const int slice = LightListBuilder::clusterSliceForDepth(5.0f, 0.1f, 100.0f);
    CHECK(builder.clusterLightCount(2, 1, slice, static_cast<uint32_t>(LightCategory::Area)) >= 1);
    CHECK(builder.clusterLightCount(2, 1, slice, static_cast<uint32_t>(LightCategory::Punctual)) == 0);
    const GpuLightClusterData& cluster = builder.clusterData();
    REQUIRE(cluster.enabled);
    REQUIRE(cluster.tileFeatureFlags.size() == static_cast<std::size_t>(cluster.tileCountX) * cluster.tileCountY);
    CHECK((cluster.tileFeatureFlags[static_cast<std::size_t>(1) * cluster.tileCountX + 2] & kLightFeatureArea) != 0u);
}

TEST_CASE("light list rejects lights entirely behind the camera") {
    const ViewState state = makeViewState(320, 180);
    GpuLightData lights = makePunctualData(1);
    addPointLight(lights, 0, Vec3(0.0f, 0.0f, 10.0f), 0.5f);

    LightListBuilder builder;
    builder.build(lights, state.constants().viewProj, state.constants().view, 0.1f, 100.0f, 320, 180);
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
    builder.build(lights, state.constants().viewProj, state.constants().view, 0.1f, 100.0f, 320, 180);
    const GpuLightListData& data = builder.data();

    CHECK(data.enabled);
    CHECK(builder.tileLightCount(0, 0) == 1);
    CHECK(builder.tileLightIndex(0, 0, 0) == 0);
    CHECK(builder.tileLightCount(data.tileCountX - 1, data.tileCountY - 1) == 1);
}

TEST_CASE("light list order is ascending for big-tile and FPTL output") {
    const ViewState state = makeViewState(320, 180);
    GpuLightData lights = makePunctualData(8);
    for (uint32_t index = 0; index < lights.punctualCount; ++index) {
        addPointLight(lights, index, Vec3(static_cast<float>(index) * 0.35f - 1.2f, 0.0f, 0.0f), 2.0f);
    }

    LightListBuilder builder;
    builder.build(lights, state.constants().viewProj, state.constants().view, 0.1f, 100.0f, 320, 180);
    REQUIRE(builder.data().enabled);

    for (uint16_t tileY = 0; tileY < builder.data().tileCountY; ++tileY) {
        for (uint16_t tileX = 0; tileX < builder.data().tileCountX; ++tileX) {
            uint32_t previous = 0;
            for (uint32_t offset = 0; offset < builder.tileLightCount(tileX, tileY); ++offset) {
                const uint32_t lightIndex = builder.tileLightIndex(tileX, tileY, offset);
                if (offset > 0) {
                    CHECK(lightIndex > previous);
                }
                previous = lightIndex;
            }
        }
    }
}

TEST_CASE("light list keeps directional lights out of punctual tiles and caps list length") {
    GpuLightData directional;
    directional.lightCount = 1;
    directional.directionalCount = 1;
    directional.texels[3] = Vec4(-1.0f, 0.0f, 1.0f, 0.0f);

    LightListBuilder builder;
    builder.build(directional, Mat4::identity(), Mat4::identity(), 0.1f, 100.0f, 320, 180);
    CHECK(builder.data().enabled);
    CHECK(builder.tileLightCount(0, 0) == 0);

    GpuLightData lights = makePunctualData(kMaxGpuLights);
    for (uint32_t index = 0; index < kMaxGpuLights; ++index) {
        addPointLight(lights, index, Vec3(0.0f, 0.0f, 0.0f), 1.0f);
    }
    const ViewState state = makeViewState(320, 180);
    builder.build(lights, state.constants().viewProj, state.constants().view, 0.1f, 100.0f, 320, 180);
    CHECK(builder.tileLightCount(10, 5) == kLightListDefaultCapacity);
    CHECK(builder.data().maxLightsPerTile == kLightListDefaultCapacity);
}


TEST_CASE("light list expands to 64 and 128 capacity at runtime") {
    const ViewState state = makeViewState(320, 180);
    GpuLightData lights = makePunctualData(kLightListMaxLights);
    for (uint32_t index = 0; index < kLightListMaxLights; ++index) {
        addPointLight(lights, index, Vec3(0.0f, 0.0f, 0.0f), 1.0f);
    }

    LightListBuilder builder;
    for (const uint32_t capacity : {64u, 128u}) {
        builder.build(lights, state.constants().viewProj, state.constants().view, 0.1f, 100.0f, 320, 180,
                      capacity);
        const GpuLightListData& data = builder.data();
        const GpuLightClusterData& cluster = builder.clusterData();
        REQUIRE(data.enabled);
        REQUIRE(cluster.enabled);
        CHECK(data.capacity == capacity);
        CHECK(data.listStride == lightListStrideFor(capacity));
        CHECK(data.width == 20u * lightListStrideFor(capacity));
        CHECK(data.maxLightsPerTile == capacity);
        CHECK(builder.tileLightCount(10, 5) == capacity);
        CHECK(cluster.offsets.size() == 5u * 3u * kClusterSliceCount * kLightCategoryCount);
        CHECK(cluster.capacity == capacity);
        CHECK(cluster.maxLightsPerCluster == capacity);
        CHECK(cluster.truncatedClusters == 0);
        for (uint32_t offset = 0; offset < builder.tileLightCount(10, 5); ++offset) {
            const uint32_t index = builder.tileLightIndex(10, 5, offset);
            CHECK(index < capacity);
            if (offset > 0) {
                CHECK(index > builder.tileLightIndex(10, 5, offset - 1));
            }
        }
    }

    CHECK(LightListBuilder::textureWidthFor(320, 64) == 20u * 33u);
    CHECK(LightListBuilder::textureWidthFor(320, 128) == 20u * 65u);
    CHECK(LightListBuilder::bigTileTextureWidthFor(1920, 128) == 30u * 65u);
}

TEST_CASE("light cluster is layered and no longer overflows at 32 lights per cell") {
    const ViewState state = makeViewState(320, 180);
    GpuLightData lights = makePunctualData(33);
    for (uint32_t index = 0; index < 33; ++index) {
        addPointLight(lights, index, Vec3(0.0f, 0.0f, 0.0f), 1.0f);
    }

    LightListBuilder builder;
    builder.build(lights, state.constants().viewProj, state.constants().view, 0.1f, 100.0f, 320, 180, 64);
    const int slice = LightListBuilder::clusterSliceForDepth(5.0f, 0.1f, 100.0f);
    CHECK(builder.clusterLightCount(2, 1, slice) == 33u);
    CHECK(builder.clusterTruncated(2, 1, slice) == 0u);
    CHECK(builder.clusterData().truncatedClusters == 0u);
    CHECK(builder.clusterData().maxLightsPerCluster == 33u);
    for (uint32_t offset = 0; offset < 33u; ++offset) {
        CHECK(builder.clusterLightIndex(2, 1, slice, offset) == offset);
    }

    builder.build(lights, state.constants().viewProj, state.constants().view, 0.1f, 100.0f, 320, 180, 32);
    CHECK(builder.clusterLightCount(2, 1, slice) == 32u);
    CHECK(builder.clusterTruncated(2, 1, slice) == 0u);
    CHECK(builder.clusterData().truncatedClusters == 0u);
    CHECK(LightListBuilder::clusterCapacityFor(7680, 4320, 128) == 64u);
}

TEST_CASE("light list keeps empty area env decal categories and records tile feature flags") {
    const ViewState state = makeViewState(320, 180);
    GpuLightData lights = makePunctualData(1);
    addPointLight(lights, 0, Vec3(0.0f, 0.0f, 0.0f), 1.0f);

    LightListBuilder builder;
    builder.build(lights, state.constants().viewProj, state.constants().view, 0.1f, 100.0f, 320, 180, 64);
    CHECK(builder.tileLightCount(10, 5, 0) == 1u);
    CHECK(builder.tileLightCount(10, 5, 1) == 0u);
    CHECK(builder.tileLightCount(10, 5, 2) == 0u);
    CHECK(builder.tileLightCount(10, 5, 3) == 0u);
    CHECK((builder.tileFeatureFlags(10, 5) & kLightFeaturePunctual) != 0u);
    CHECK(builder.data().height == 12u * kLightCategoryCount);
    const GpuLightListData& data = builder.data();
    const std::size_t stride = data.listStride;
    const std::size_t flagWord = static_cast<std::size_t>(5) * data.width + 10u * stride + stride - 1u;
    CHECK(((data.texels[flagWord] >> 16u) & 0xffffu) == builder.tileFeatureFlags(10, 5));
}
TEST_CASE("light builder truncates to the requested capacity") {
    RenderScene scene;
    for (int index = 0; index < 33; ++index) {
        Light light;
        light.type = LightType::Point;
        light.intensity = 10.0f;
        light.range = 2.0f;
        light.world.values[12] = static_cast<float>(index % 5) * 0.1f;
        light.world.values[13] = 0.0f;
        light.world.values[14] = 0.0f;
        scene.lights.push_back(light);
    }

    LightBuilder builder;
    builder.build(scene, Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, -1.0f), 100.0f, false, false, nullptr, 32);
    CHECK(builder.data().lightCount == 32u);
    CHECK(builder.data().punctualCount == 32u);
    CHECK(builder.data().truncatedLightCount == 1u);

    builder.build(scene, Vec3(0.0f, 0.0f, 5.0f), Vec3(0.0f, 0.0f, -1.0f), 100.0f, false, false, nullptr, 64);
    CHECK(builder.data().lightCount == 33u);
    CHECK(builder.data().truncatedLightCount == 0u);
}

TEST_CASE("deferred Lit variant table covers every material and light feature combination") {
    uint32_t materialFeatureMask = 0u;
    const std::array<uint32_t, 5> materialFeatures = {
        asset::MaterialFeatureClearCoat,
        asset::MaterialFeatureTransmission,
        asset::MaterialFeatureIridescence,
        asset::MaterialFeatureAnisotropy,
        asset::MaterialFeatureSubsurface,
    };
    for (uint32_t feature : materialFeatures) {
        materialFeatureMask |= feature;
    }
    for (uint32_t features = 0; features <= materialFeatureMask; ++features) {
        if ((features & ~materialFeatureMask) != 0u) {
            continue;
        }
        const std::array<uint32_t, 4> lightFeatures = {
            kLitLightPunctual,
            kLitLightArea,
            kLitLightDirectional,
            kLitLightShadows,
        };
        for (uint32_t lightMask = 0u; lightMask < 16u; ++lightMask) {
            uint32_t lightFlags = 0u;
            for (std::size_t index = 0; index < lightFeatures.size(); ++index) {
                if ((lightMask & (1u << index)) != 0u) {
                    lightFlags |= lightFeatures[index];
                }
            }
            const uint32_t featureFlags = litMaterialFlags(features) | lightFlags;
            const uint32_t variant = litVariantForMaterial(features, lightFlags);
            CHECK(variant < kLitVariantCount);
            CHECK((kLitFeatureVariantFlags[variant] & featureFlags) == featureFlags);
            CHECK((litVariantStencilRef(variant) & 1u) == 1u);
            CHECK(litVariantStencilRef(variant) <= 57u);
        }
    }

    for (uint32_t variant = 0; variant < kLitVariantCount; ++variant) {
        CHECK((kLitFeatureVariantFlags[variant] & kLitMaterialStandard) != 0u);
        for (uint32_t other = variant + 1; other < kLitVariantCount; ++other) {
            CHECK(kLitFeatureVariantFlags[variant] != kLitFeatureVariantFlags[other]);
        }
    }
}

TEST_CASE("deferred Lit variant table follows the classification order") {
    CHECK(kLitVariantCount == 29u);
    CHECK(litVariantForMaterial(0, kLitLightDirectional | kLitLightPunctual) == 2u);
    CHECK(litVariantForMaterial(asset::MaterialFeatureClearCoat,
                                kLitLightDirectional | kLitLightPunctual) == 20u);
    CHECK(litVariantForMaterial(asset::MaterialFeatureIridescence,
                                kLitLightDirectional) == 24u);
    CHECK(litVariantForMaterial(asset::MaterialFeatureAnisotropy,
                                kLitLightDirectional | kLitLightArea) == 16u);
    CHECK(litVariantForMaterial(asset::MaterialFeatureSubsurface,
                                kLitLightDirectional | kLitLightPunctual) == 10u);
    CHECK(litVariantFor(0, 0) == 0u);
    CHECK(litVariantFor(kLitMaterialAll,
                        kLitLightDirectional | kLitLightPunctual | kLitLightArea | kLitLightShadows) == 28u);
    CHECK(litVariantForMaterial(0, kLitLightPunctual) == 2u);
    for (uint32_t variant = 0; variant < kLitVariantCount; ++variant) {
        CHECK((litVariantStencilRef(variant) & 1u) == 1u);
        CHECK(litVariantStencilRef(variant) <= 57u);
        for (uint32_t other = variant + 1; other < kLitVariantCount; ++other) {
            CHECK(litVariantStencilRef(variant) != litVariantStencilRef(other));
        }
    }
}
