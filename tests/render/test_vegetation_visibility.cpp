#include "render/culling/vegetation_visibility.h"

#include <doctest/doctest.h>

#include <array>
#include <vector>

using namespace hbg;
using namespace hbg::render;

namespace {

DrawItem makeItem(float x, float z, float radius = 1.0f) {
    DrawItem item;
    item.world.values[12] = x;
    item.world.values[13] = 0.0f;
    item.world.values[14] = z;
    item.worldBounds.min = Vec3(x - radius, -radius, z - radius);
    item.worldBounds.max = Vec3(x + radius, radius, z + radius);
    return item;
}

std::array<Vec4, 6> noFrustumCulling() {
    std::array<Vec4, 6> planes{};
    for (Vec4& plane : planes) {
        plane = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    return planes;
}

} // namespace

TEST_CASE("vegetation tiles group instances by spatial cell") {
    std::vector<DrawItem> items = {
        makeItem(10.0f, 10.0f),
        makeItem(15.0f, 15.0f),
        makeItem(110.0f, 10.0f),
    };
    std::vector<VegetationTile> tiles;
    buildVegetationTiles(items, 100.0f, tiles);

    REQUIRE(tiles.size() == 2);
    CHECK(tiles[0].itemIndices.size() == 2);
    CHECK(tiles[1].itemIndices.size() == 1);
    CHECK(tiles[0].bounds.valid());
}

TEST_CASE("vegetation visibility applies tile and instance frustum culling") {
    std::vector<DrawItem> items = {
        makeItem(-50.0f, 0.0f),
        makeItem(50.0f, 0.0f),
        makeItem(150.0f, 0.0f),
    };
    std::vector<VegetationTile> tiles;
    buildVegetationTiles(items, 100.0f, tiles);

    std::array<Vec4, 6> planes = noFrustumCulling();
    planes[0] = Vec4(-1.0f, 0.0f, 0.0f, 0.0f);

    const std::vector<uint32_t> visible =
        collectVisibleVegetation(items, tiles, Vec3(0.0f, 0.0f, 0.0f), planes, 0.0f);
    REQUIRE(visible.size() == 1);
    CHECK(visible[0] == 0);
}

TEST_CASE("vegetation visibility applies the Proland-style max distance") {
    std::vector<DrawItem> items = {
        makeItem(10.0f, 0.0f),
        makeItem(120.0f, 0.0f),
        makeItem(300.0f, 0.0f),
    };
    std::vector<VegetationTile> tiles;
    buildVegetationTiles(items, 100.0f, tiles);

    const std::vector<uint32_t> visible =
        collectVisibleVegetation(items, tiles, Vec3(0.0f, 0.0f, 0.0f), noFrustumCulling(), 150.0f);
    REQUIRE(visible.size() == 2);
    CHECK(visible[0] == 0);
    CHECK(visible[1] == 1);
}

TEST_CASE("vegetation visibility rejects a tile beyond max distance") {
    std::vector<DrawItem> items = {
        makeItem(1000.0f, 0.0f),
        makeItem(1100.0f, 0.0f),
    };
    std::vector<VegetationTile> tiles;
    buildVegetationTiles(items, 100.0f, tiles);

    const std::vector<uint32_t> visible =
        collectVisibleVegetation(items, tiles, Vec3(0.0f, 0.0f, 0.0f), noFrustumCulling(), 500.0f);
    CHECK(visible.empty());
}
