#include "render/water/water_screen_grid.h"

#include <doctest/doctest.h>

#include <vector>

using namespace hbg;
using namespace hbg::render;

namespace {

asset::Mesh makeWaterMesh() {
    asset::Mesh mesh;
    mesh.name = "test_water";
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 2; ++column) {
            asset::MeshVertex vertex;
            vertex.position = Vec3(static_cast<float>(column) * 1000.0f, 0.5f,
                                   static_cast<float>(row) * 1000.0f);
            vertex.normal = Vec3(0.0f, 1.0f, 0.0f);
            vertex.texcoord0 = Vec2(static_cast<float>(column), static_cast<float>(row));
            mesh.vertices.push_back(vertex);
            mesh.bounds.expand(vertex.position);
        }
    }
    mesh.indices = {0, 2, 1, 1, 2, 3};
    asset::SubMesh submesh;
    submesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
    submesh.materialIndex = 7;
    submesh.bounds = mesh.bounds;
    mesh.submeshes.push_back(submesh);
    return mesh;
}

} // namespace

TEST_CASE("water screen grid rebuilds for viewport changes") {
    asset::Mesh mesh = makeWaterMesh();
    WaterScreenGridDesc desc;
    REQUIRE(configureWaterScreenGrid(mesh, 1280, 720, &desc));

    CHECK(desc.width == 200);
    CHECK(desc.height == 112);
    CHECK(mesh.vertices.size() == 22400);
    CHECK(mesh.indices.size() == 132534);
    CHECK(mesh.vertices.front().position.y == doctest::Approx(0.5f));
    CHECK(mesh.submeshes.front().materialIndex == 7);

    REQUIRE(configureWaterScreenGrid(mesh, 640, 360, &desc));
    CHECK(desc.width == 100);
    CHECK(desc.height == 56);
    CHECK(mesh.vertices.size() == 5600);
    CHECK(mesh.indices.size() == 32670);
}

TEST_CASE("water screen grid uses stable source data after displacement") {
    asset::Mesh mesh = makeWaterMesh();
    WaterScreenGridDesc desc;
    REQUIRE(configureWaterScreenGrid(mesh, 640, 360, &desc));

    const WaterScreenGridSource source{mesh.bounds, 0.5f, Vec2(0.0f, 0.0f), Vec2(1.0f, 1.0f), 7};
    for (asset::MeshVertex& vertex : mesh.vertices) {
        vertex.position.y += 20.0f;
    }

    REQUIRE(configureWaterScreenGrid(mesh, 1280, 720, &desc, &source));
    CHECK(desc.width == 200);
    CHECK(desc.height == 112);
    CHECK(mesh.vertices.front().position.y == doctest::Approx(0.5f));
    CHECK(mesh.vertices.back().position.y == doctest::Approx(0.5f));
    CHECK(mesh.bounds.min.x == doctest::Approx(0.0f));
    CHECK(mesh.bounds.max.x == doctest::Approx(1000.0f));
    CHECK(mesh.bounds.min.z == doctest::Approx(0.0f));
    CHECK(mesh.bounds.max.z == doctest::Approx(1000.0f));
    CHECK(mesh.submeshes.front().materialIndex == 7);
    CHECK(mesh.vertices.front().texcoord0.x == doctest::Approx(0.0f));
    CHECK(mesh.vertices.front().texcoord0.y == doctest::Approx(0.0f));
}
