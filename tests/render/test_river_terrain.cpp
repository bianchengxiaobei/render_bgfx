#include "render/river/river_terrain.h"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace hbg;
using namespace hbg::render;

namespace {

asset::Mesh makeTerrainGrid(int cells, float halfExtent, float height) {
    asset::Mesh mesh;
    mesh.name = "RiverTerrain";
    const int size = cells + 1;
    mesh.vertices.reserve(static_cast<std::size_t>(size) * size);
    for (int z = 0; z < size; ++z) {
        for (int x = 0; x < size; ++x) {
            asset::MeshVertex vertex;
            vertex.position =
                Vec3(-halfExtent + 2.0f * halfExtent * static_cast<float>(x) / static_cast<float>(cells), height,
                     -halfExtent + 2.0f * halfExtent * static_cast<float>(z) / static_cast<float>(cells));
            vertex.normal = Vec3(0.0f, 1.0f, 0.0f);
            vertex.texcoord0 = Vec2(static_cast<float>(x) / static_cast<float>(cells),
                                    static_cast<float>(z) / static_cast<float>(cells));
            mesh.vertices.push_back(vertex);
        }
    }
    for (int z = 0; z < cells; ++z) {
        for (int x = 0; x < cells; ++x) {
            const uint32_t a = static_cast<uint32_t>(z * size + x);
            const uint32_t b = a + 1;
            const uint32_t c = a + static_cast<uint32_t>(size);
            const uint32_t d = c + 1;
            mesh.indices.push_back(a);
            mesh.indices.push_back(c);
            mesh.indices.push_back(b);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);
            mesh.indices.push_back(d);
        }
    }
    mesh.recomputeBounds();
    mesh.hasNormals = true;
    return mesh;
}

std::size_t vertexIndex(int cells, int x, int z) {
    return static_cast<std::size_t>(z) * static_cast<std::size_t>(cells + 1) + static_cast<std::size_t>(x);
}

RiverTerrainSettings terrainSettings() {
    RiverTerrainSettings settings;
    settings.enabled = true;
    settings.bedDepth = 1.5f;
    settings.bedWidthFactor = 0.9f;
    settings.bankWidthFactor = 2.2f;
    return settings;
}

} // namespace

TEST_CASE("river terrain carves the current channel and leaves distant ground untouched") {
    asset::Mesh base = makeTerrainGrid(8, 100.0f, 4.0f);
    RiverMeshSettings mesh;
    mesh.baseHeight = 0.5f;
    const RiverGraph graph = RiverGraph::straight(200.0f, 40.0f, 16);

    std::vector<asset::MeshVertex> deformed;
    REQUIRE(deformRiverTerrain(graph, mesh, terrainSettings(), base.vertices, base.indices, deformed));
    REQUIRE(deformed.size() == base.vertices.size());

    const std::size_t center = vertexIndex(8, 4, 4);
    const std::size_t far = vertexIndex(8, 8, 4);
    CHECK(deformed[center].position.y == doctest::Approx(-1.0f));
    CHECK(deformed[far].position.y == doctest::Approx(4.0f));
    CHECK(deformed[center].position.y < mesh.baseHeight);
    CHECK(std::abs(deformed[center].normal.y) > 0.9f);
    CHECK(std::abs(deformed[far].normal.y - 1.0f) < 1.0e-4f);
    const float normalLength = std::sqrt(deformed[center].normal.x * deformed[center].normal.x +
                                         deformed[center].normal.y * deformed[center].normal.y +
                                         deformed[center].normal.z * deformed[center].normal.z);
    CHECK(normalLength == doctest::Approx(1.0f));
    const float tangentLength = std::sqrt(deformed[center].tangent.x * deformed[center].tangent.x +
                                          deformed[center].tangent.y * deformed[center].tangent.y +
                                          deformed[center].tangent.z * deformed[center].tangent.z);
    CHECK(tangentLength == doctest::Approx(1.0f));
    CHECK(std::abs(deformed[center].tangent.w) == doctest::Approx(1.0f));
}

TEST_CASE("river terrain residual delta can fill an over-deep authored bed") {
    asset::Mesh base = makeTerrainGrid(8, 100.0f, -4.0f);
    RiverMeshSettings mesh;
    mesh.baseHeight = 0.5f;
    const RiverGraph graph = RiverGraph::straight(200.0f, 40.0f, 16);

    std::vector<float> delta;
    REQUIRE(computeRiverTerrainDelta(graph, mesh, terrainSettings(), base.vertices, delta));
    REQUIRE(delta.size() == base.vertices.size());
    const std::size_t center = vertexIndex(8, 4, 4);
    CHECK(delta[center] == doctest::Approx(3.0f));
    CHECK(delta[vertexIndex(8, 8, 4)] == doctest::Approx(0.0f));

    std::vector<asset::MeshVertex> deformed;
    REQUIRE(applyRiverTerrainDelta(base.vertices, delta, base.indices, deformed));
    CHECK(deformed[center].position.y == doctest::Approx(-1.0f));
    CHECK(deformed[vertexIndex(8, 8, 4)].position.y == doctest::Approx(-4.0f));
}

TEST_CASE("river terrain can preserve over-deep authored beds") {
    asset::Mesh base = makeTerrainGrid(8, 100.0f, -4.0f);
    RiverMeshSettings mesh;
    mesh.baseHeight = 0.5f;
    RiverTerrainSettings settings = terrainSettings();
    settings.fillBed = false;
    const RiverGraph graph = RiverGraph::straight(200.0f, 40.0f, 16);

    std::vector<float> delta;
    CHECK_FALSE(computeRiverTerrainDelta(graph, mesh, settings, base.vertices, delta));
    REQUIRE(delta.size() == base.vertices.size());
    CHECK(delta[vertexIndex(8, 4, 4)] == doctest::Approx(0.0f));

    std::vector<asset::MeshVertex> deformed;
    CHECK_FALSE(deformRiverTerrain(graph, mesh, settings, base.vertices, base.indices, deformed));
    CHECK(deformed.empty());
}

TEST_CASE("river terrain follows a moved axis") {
    asset::Mesh base = makeTerrainGrid(8, 100.0f, 4.0f);
    RiverMeshSettings mesh;
    mesh.baseHeight = 0.5f;
    RiverGraph moved;
    moved.width = 40.0f;
    moved.axis = {Vec2(25.0f, -100.0f), Vec2(25.0f, 100.0f)};

    std::vector<asset::MeshVertex> deformed;
    REQUIRE(deformRiverTerrain(moved, mesh, terrainSettings(), base.vertices, base.indices, deformed));

    const std::size_t movedCenter = vertexIndex(8, 5, 4);
    CHECK(deformed[movedCenter].position.y == doctest::Approx(-1.0f));
    CHECK(deformed[vertexIndex(8, 4, 4)].position.y < 4.0f);
    CHECK(deformed[vertexIndex(8, 8, 4)].position.y == doctest::Approx(4.0f));
}

TEST_CASE("moving the river clears the previous residual delta") {
    asset::Mesh base = makeTerrainGrid(8, 100.0f, 4.0f);
    RiverMeshSettings mesh;
    mesh.baseHeight = 0.5f;
    RiverTerrainSettings settings = terrainSettings();
    settings.bankWidthFactor = 1.1f;
    const RiverGraph centered = RiverGraph::straight(200.0f, 20.0f, 16);

    std::vector<float> centeredDelta;
    REQUIRE(computeRiverTerrainDelta(centered, mesh, settings, base.vertices, centeredDelta));
    const std::size_t oldCenter = vertexIndex(8, 4, 4);
    CHECK(centeredDelta[oldCenter] < 0.0f);

    RiverGraph moved;
    moved.width = 20.0f;
    moved.axis = {Vec2(25.0f, -100.0f), Vec2(25.0f, 100.0f)};
    std::vector<float> movedDelta;
    REQUIRE(computeRiverTerrainDelta(moved, mesh, settings, base.vertices, movedDelta));
    CHECK(movedDelta[oldCenter] == doctest::Approx(0.0f));
    CHECK(movedDelta[vertexIndex(8, 5, 4)] < 0.0f);
}

TEST_CASE("river terrain residual cache reconstructs tiled signed deltas and parents") {
    asset::Mesh base = makeTerrainGrid(8, 100.0f, 4.0f);
    RiverMeshSettings mesh;
    mesh.baseHeight = 0.5f;
    const RiverGraph graph = RiverGraph::straight(200.0f, 40.0f, 16);

    std::vector<float> exactDelta;
    REQUIRE(computeRiverTerrainDelta(graph, mesh, terrainSettings(), base.vertices, exactDelta));
    std::vector<asset::MeshVertex> candidate;
    REQUIRE(applyRiverTerrainDelta(base.vertices, exactDelta, base.indices, candidate));

    RiverTerrainResidualCache cache;
    RiverTerrainResidualSettings settings;
    settings.leafTiles = 2;
    REQUIRE(cache.initialize(base.vertices, settings));
    CHECK(cache.stats().gridWidth == 9);
    CHECK(cache.stats().gridHeight == 9);
    CHECK(cache.stats().leafTiles == 2);
    CHECK(cache.stats().levels == 2);
    CHECK(cache.stats().samplesPerTile == 5);

    std::vector<float> delta;
    REQUIRE(cache.readBack(base.vertices, candidate, exactDelta, delta));
    REQUIRE(delta.size() == exactDelta.size());
    for (std::size_t index = 0; index < delta.size(); ++index) {
        CHECK(delta[index] == doctest::Approx(exactDelta[index]).epsilon(1.0e-5));
    }
    CHECK(cache.stats().modifiedLeafTiles > 0);
    CHECK(cache.stats().modifiedParentTiles > 0);
    CHECK(cache.stats().maxError < 1.0e-4f);

    const std::size_t center = vertexIndex(8, 4, 4);
    const float centerDelta = candidate[center].position.y - base.vertices[center].position.y;
    CHECK(cache.sampleDelta(0.0f, 0.0f, 1) == doctest::Approx(centerDelta));
    CHECK(cache.sampleDelta(0.0f, 0.0f, 0) == doctest::Approx(centerDelta));
}

TEST_CASE("river terrain residual cache clears stale leaves after moving the river") {
    asset::Mesh base = makeTerrainGrid(8, 100.0f, 4.0f);
    RiverMeshSettings mesh;
    mesh.baseHeight = 0.5f;
    RiverTerrainSettings terrain = terrainSettings();
    terrain.bankWidthFactor = 1.1f;
    const RiverGraph centered = RiverGraph::straight(200.0f, 20.0f, 16);

    std::vector<float> centeredDelta;
    REQUIRE(computeRiverTerrainDelta(centered, mesh, terrain, base.vertices, centeredDelta));
    std::vector<asset::MeshVertex> centeredCandidate;
    REQUIRE(applyRiverTerrainDelta(base.vertices, centeredDelta, base.indices, centeredCandidate));

    RiverTerrainResidualCache cache;
    RiverTerrainResidualSettings settings;
    settings.leafTiles = 2;
    REQUIRE(cache.initialize(base.vertices, settings));
    std::vector<float> cachedDelta;
    REQUIRE(cache.readBack(base.vertices, centeredCandidate, centeredDelta, cachedDelta));
    CHECK(cache.sampleDelta(0.0f, 0.0f, 1) < 0.0f);

    RiverGraph moved;
    moved.width = 20.0f;
    moved.axis = {Vec2(-75.0f, -100.0f), Vec2(-75.0f, 100.0f)};
    std::vector<float> movedDelta;
    REQUIRE(computeRiverTerrainDelta(moved, mesh, terrain, base.vertices, movedDelta));
    std::vector<asset::MeshVertex> movedCandidate;
    REQUIRE(applyRiverTerrainDelta(base.vertices, movedDelta, base.indices, movedCandidate));
    REQUIRE(cache.readBack(base.vertices, movedCandidate, movedDelta, cachedDelta));
    CHECK(cache.sampleDelta(0.0f, 0.0f, 1) == doctest::Approx(0.0f));
    CHECK(cache.sampleDelta(-75.0f, 0.0f, 1) < 0.0f);
}

TEST_CASE("river terrain rejects an invalid graph or mismatched delta") {
    asset::Mesh base = makeTerrainGrid(2, 100.0f, 4.0f);
    RiverMeshSettings mesh;
    mesh.baseHeight = 0.5f;
    std::vector<asset::MeshVertex> deformed;
    CHECK_FALSE(deformRiverTerrain(RiverGraph{}, mesh, terrainSettings(), base.vertices, base.indices, deformed));
    CHECK(deformed.empty());

    std::vector<float> badDelta(base.vertices.size() + 1, 0.0f);
    CHECK_FALSE(applyRiverTerrainDelta(base.vertices, badDelta, base.indices, deformed));
}
