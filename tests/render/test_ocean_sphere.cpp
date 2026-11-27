#include "render/water/ocean_fft.h"
#include "render/water/ocean_sphere.h"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace hbg;
using namespace hbg::render;

TEST_CASE("ocean sphere frame intersects the tangent plane and clamps the horizon") {
    constexpr float radius = 6360005.0f;
    constexpr float seaLevel = 0.5f;
    const Vec3 camera(1250.0f, 800.5f, -2400.0f);

    const OceanSphereHit nadir = intersectOceanSphere(camera, Vec3(0.0f, -1.0f, 0.0f), radius, seaLevel);
    REQUIRE(nadir.valid);
    CHECK(nadir.distance == doctest::Approx(800.0f).epsilon(0.001));
    CHECK(nadir.position.x == doctest::Approx(camera.x).epsilon(0.001));
    CHECK(nadir.position.y == doctest::Approx(seaLevel).epsilon(0.001));
    CHECK(nadir.position.z == doctest::Approx(camera.z).epsilon(0.001));

    const float altitudeRatio = (camera.y - seaLevel) / radius;
    const float horizonDown =
        std::sqrt(altitudeRatio * (2.0f + altitudeRatio)) / (1.0f + altitudeRatio);
    const Vec3 horizonRay(0.0f, -horizonDown, -1.0f / (1.0f + altitudeRatio));
    const OceanSphereHit horizon = intersectOceanSphere(camera, horizonRay, radius, seaLevel);
    REQUIRE(horizon.valid);
    CHECK_FALSE(horizon.clamped);
    const float expectedDistance = std::sqrt(800.0f * (2.0f * radius + 800.0f));
    CHECK(horizon.distance == doctest::Approx(expectedDistance).epsilon(0.001));
    CHECK(horizon.position.y == doctest::Approx(seaLevel - 800.0f).epsilon(0.01));

    const OceanSphereHit above = intersectOceanSphere(camera, Vec3(0.0f, 1.0f, 0.0f), radius, seaLevel);
    REQUIRE(above.valid);
    CHECK(above.clamped);
    CHECK(above.direction.y == doctest::Approx(-horizonDown).epsilon(0.001));
    CHECK(above.distance == doctest::Approx(expectedDistance).epsilon(0.001));

    const OceanSphereHit flat = intersectOceanSphere(camera, Vec3(0.0f, -1.0f, 0.0f), 0.0f, seaLevel);
    CHECK_FALSE(flat.valid);
}

TEST_CASE("ocean sphere slope follows the sphere normal") {
    constexpr float radius = 6360005.0f;
    constexpr float seaLevel = 0.5f;
    const Vec3 camera(0.0f, 800.5f, 0.0f);

    const Vec2 tangentSlope = oceanSphereSlope(Vec3(0.0f, seaLevel, 0.0f), camera, radius, seaLevel);
    CHECK(tangentSlope.x == doctest::Approx(0.0f));
    CHECK(tangentSlope.y == doctest::Approx(0.0f));

    const float offset = 1000.0f;
    const float sphereY = seaLevel - (offset * offset) / (2.0f * radius);
    const Vec2 slope = oceanSphereSlope(Vec3(offset, sphereY, 0.0f), camera, radius, seaLevel);
    CHECK(slope.x == doctest::Approx(offset / (radius + sphereY - seaLevel)).epsilon(0.001));
    CHECK(slope.y == doctest::Approx(0.0f));
}

TEST_CASE("ocean sphere screen grid stays finite and curves below the tangent plane") {
    OceanFFTSettings settings;
    settings.resolution = 16;
    settings.gridSizes = {128.0f, 32.0f, 8.0f, 2.0f};
    settings.updateHz = 1.0f;

    OceanFFT fft;
    REQUIRE(fft.init(settings));
    REQUIRE(fft.update(0.0f));

    constexpr int kGridWidth = 7;
    constexpr int kGridHeight = 3;
    const Vec3 camera(0.0f, 1000.0f, 0.0f);
    const Mat4 view = lookAtRH(camera, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    const Mat4 projection = perspectiveRH(1.0f, 1.0f, 1.0f, 100000.0f, true);
    const Mat4 invViewProj = inverse(projection * view);
    std::vector<asset::MeshVertex> vertices(kGridWidth * kGridHeight);

    fft.applyToScreenGrid(vertices, 0.5f, kGridWidth, kGridHeight, invViewProj, camera, Vec2(1.0f, 1.0f),
                          Vec2(0.0f, 0.0f), 6360005.0f);

    float minY = 0.5f;
    for (const asset::MeshVertex& vertex : vertices) {
        CHECK(std::isfinite(vertex.position.x));
        CHECK(std::isfinite(vertex.position.y));
        CHECK(std::isfinite(vertex.position.z));
        CHECK(std::isfinite(vertex.normal.x));
        CHECK(std::isfinite(vertex.normal.y));
        CHECK(std::isfinite(vertex.normal.z));
        const float normalLength = std::sqrt(vertex.normal.x * vertex.normal.x +
                                             vertex.normal.y * vertex.normal.y +
                                             vertex.normal.z * vertex.normal.z);
        CHECK(normalLength == doctest::Approx(1.0f).epsilon(0.001));
        minY = std::min(minY, vertex.position.y);
    }
    CHECK(minY < 0.5f);
}
