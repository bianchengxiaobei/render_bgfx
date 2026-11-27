#include "render/river/hydro_flow.h"
#include "render/river/river_mesh.h"

#include <doctest/doctest.h>

#include <cmath>

using namespace hbg;
using namespace hbg::render;

TEST_CASE("hydro flow reproduces bank potential and stream velocity") {
    const RiverGraph graph = RiverGraph::straight(1000.0f, 60.0f, 64);
    const HydroFlow flow(graph);

    const HydroFlowSample center = flow.sample(Vec2(0.0f, 0.0f));
    REQUIRE(center.inside);
    CHECK(center.potential == doctest::Approx(0.5f).epsilon(0.02));
    CHECK(center.velocity.y > 0.0f);
    CHECK(std::abs(center.velocity.x) < std::abs(center.velocity.y));

    const HydroFlowSample right = flow.sample(Vec2(10.0f, 0.0f));
    REQUIRE(right.inside);
    CHECK(right.potential > center.potential);
    CHECK(right.velocity.y > 0.0f);

    const HydroFlowSample left = flow.sample(Vec2(-10.0f, 0.0f));
    REQUIRE(left.inside);
    CHECK(left.potential < center.potential);

    const HydroFlowSample outside = flow.sample(Vec2(50.0f, 0.0f));
    CHECK_FALSE(outside.inside);
    CHECK(outside.velocity == Vec2(0.0f, 0.0f));
}

TEST_CASE("hydro flow caches one potential per cell corner and keeps velocity cell-constant") {
    const RiverGraph graph = RiverGraph::straight(1000.0f, 60.0f, 64);
    const HydroFlow flow(graph);
    CHECK(flow.potentialStep() >= 1.0f);

    const HydroFlowSample a = flow.sample(Vec2(0.0f, 0.0f));
    const std::size_t cacheAfterA = flow.potentialCacheSize();
    const HydroFlowSample b = flow.sample(Vec2(1.0f, 1.0f));
    REQUIRE(a.inside);
    REQUIRE(b.inside);
    CHECK(a.potential == b.potential);
    CHECK(a.velocity.x == b.velocity.x);
    CHECK(a.velocity.y == b.velocity.y);
    CHECK(flow.potentialCacheSize() == cacheAfterA);

    const HydroFlowSample adjacent = flow.sample(Vec2(flow.potentialStep() + 1.0f, 0.0f));
    REQUIRE(adjacent.inside);
    CHECK(adjacent.potential > a.potential);
    CHECK(flow.potentialCacheSize() > cacheAfterA);
}

TEST_CASE("hydro flow meander keeps a finite downstream velocity") {
    const RiverGraph graph = RiverGraph::meander(1200.0f, 64.0f, 120.0f, 1.5f, 192);
    const HydroFlow flow(graph);

    int samples = 0;
    for (int index = 0; index <= 24; ++index) {
        const float t = static_cast<float>(index) / 24.0f;
        const float y = -600.0f + 1200.0f * t;
        const float x = 120.0f * std::sin(2.0f * 3.14159265358979323846f * 1.5f * t);
        const HydroFlowSample sample = flow.sample(Vec2(x, y));
        REQUIRE(sample.inside);
        CHECK(std::isfinite(sample.potential));
        CHECK(std::isfinite(sample.velocity.x));
        CHECK(std::isfinite(sample.velocity.y));
        CHECK(sample.velocity.y > 0.0f);
        ++samples;
    }
    CHECK(samples == 25);
}


TEST_CASE("river mesh follows the hydro graph centerline") {
    const RiverGraph graph = RiverGraph::meander(1600.0f, 64.0f, 180.0f, 0.75f, 128);
    RiverMeshSettings settings;
    settings.segments = 32;
    settings.crossSegments = 8;
    settings.baseHeight = 0.05f;
    settings.uvLength = 180.0f;

    const asset::Mesh mesh = buildRiverMesh(graph, settings, 7, "TestRiver");
    REQUIRE(mesh.vertices.size() == static_cast<std::size_t>(33 * 9));
    REQUIRE(mesh.indices.size() == static_cast<std::size_t>(32 * 8 * 6));
    REQUIRE(mesh.submeshes.size() == 1);
    CHECK(mesh.submeshes.front().materialIndex == 7);
    CHECK(mesh.submeshes.front().indexCount == mesh.indices.size());
    CHECK(mesh.bounds.valid());
    CHECK(mesh.vertices.front().position.y == doctest::Approx(settings.baseHeight));
    CHECK(mesh.vertices.front().texcoord0.y < 0.0f);
    CHECK(mesh.vertices.back().texcoord0.y > 0.0f);
    for (const asset::MeshVertex& vertex : mesh.vertices) {
        CHECK(vertex.normal == Vec3(0.0f, 1.0f, 0.0f));
        CHECK(std::isfinite(vertex.position.x));
        CHECK(std::isfinite(vertex.position.z));
    }
}
