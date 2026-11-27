#include "render/water/ocean_fft.h"
#include "render/water/water_screen_grid.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace hbg;
using namespace hbg::render;

TEST_CASE("ocean fft displaces water vertices and updates normals") {
    OceanFFTSettings settings;
    settings.resolution = 32;
    settings.gridSizes = {256.0f, 64.0f, 16.0f, 4.0f};
    settings.amplitude = 1.0f;
    settings.updateHz = 1.0f;

    OceanFFT fft;
    REQUIRE(fft.init(settings));
    CHECK(fft.maxSlopeVariance() > 0.0f);
    CHECK(fft.slopeVarianceLutValid());
    CHECK(fft.mipLevelCount() == 6);
    REQUIRE(fft.update(0.0f));

    std::vector<asset::MeshVertex> vertices;
    for (int z = 0; z < 5; ++z) {
        for (int x = 0; x < 5; ++x) {
            asset::MeshVertex vertex;
            vertex.position = Vec3(static_cast<float>(x) * 37.0f, 0.5f, static_cast<float>(z) * 41.0f);
            vertex.normal = Vec3(0.0f, 1.0f, 0.0f);
            vertices.push_back(vertex);
        }
    }

    fft.applyToVertices(vertices, 0.5f);

    bool displaced = false;
    for (const asset::MeshVertex& vertex : vertices) {
        REQUIRE(std::isfinite(vertex.position.y));
        const float normalLength = std::sqrt(vertex.normal.x * vertex.normal.x + vertex.normal.y * vertex.normal.y +
                                             vertex.normal.z * vertex.normal.z);
        CHECK(normalLength == doctest::Approx(1.0f).epsilon(0.001));
        displaced = displaced || std::abs(vertex.position.y - 0.5f) > 1.0e-6f;
    }
    CHECK(displaced);
}

TEST_CASE("ocean fft exposes mipmapped texture layers") {
    OceanFFTSettings settings;
    settings.resolution = 32;
    settings.gridSizes = {256.0f, 64.0f, 16.0f, 4.0f};
    settings.updateHz = 1.0f;

    OceanFFT fft;
    REQUIRE(fft.init(settings));
    CHECK(fft.textureLayerCount() == 5);
    CHECK(fft.gridSizes() == settings.gridSizes);

    int preUpdateLayers = 0;
    fft.visitTextureLayers([&](uint16_t, uint8_t, const float*, uint16_t, uint16_t) { ++preUpdateLayers; });
    CHECK(preUpdateLayers == 0);

    REQUIRE(fft.update(0.0f));
    int visited = 0;
    bool finiteValues = true;
    bool nonZeroHeight = false;
    bool nonZeroSlope = false;
    bool nonZeroChoppy = false;
    const uint32_t mipCount = fft.mipLevelCount();
    fft.visitTextureLayers([&](uint16_t layer, uint8_t mip, const float* data, uint16_t width, uint16_t height) {
        ++visited;
        CHECK(layer < 5);
        CHECK(mip < mipCount);
        const uint16_t expected = static_cast<uint16_t>(std::max(32 >> mip, 1));
        CHECK(width == expected);
        CHECK(height == expected);
        REQUIRE(data != nullptr);
        const int texelCount = static_cast<int>(width) * static_cast<int>(height);
        for (int texel = 0; texel < texelCount; ++texel) {
            for (int component = 0; component < 4; ++component) {
                const float value = data[texel * 4 + component];
                finiteValues = finiteValues && std::isfinite(value);
                if (std::abs(value) > 1.0e-6f) {
                    nonZeroHeight = nonZeroHeight || layer == 0;
                    nonZeroSlope = nonZeroSlope || layer == 1 || layer == 2;
                    nonZeroChoppy = nonZeroChoppy || layer == 3 || layer == 4;
                }
            }
        }
    });
    CHECK(visited == static_cast<int>(5u * mipCount));
    CHECK(finiteValues);
    CHECK(nonZeroHeight);
    CHECK(nonZeroSlope);
    CHECK(nonZeroChoppy);
}

TEST_CASE("ocean fft texture slopes match the spectral derivative") {
    OceanFFTSettings settings;
    settings.resolution = 32;
    settings.gridSizes = {256.0f, 64.0f, 16.0f, 4.0f};
    settings.updateHz = 1.0f;

    OceanFFT fft;
    REQUIRE(fft.init(settings));
    REQUIRE(fft.update(0.75f));

    constexpr int kSize = 32;
    std::vector<float> height(static_cast<std::size_t>(kSize) * kSize);
    std::vector<float> slopeX(height.size());
    std::vector<float> slopeZ(height.size());
    fft.visitTextureLayers([&](uint16_t layer, uint8_t mip, const float* data, uint16_t width, uint16_t heightPixels) {
        if (mip != 0 || width != kSize || heightPixels != kSize) {
            return;
        }
        if (layer == 0) {
            for (std::size_t texel = 0; texel < height.size(); ++texel) {
                height[texel] = data[texel * 4];
            }
        } else if (layer == 1) {
            for (std::size_t texel = 0; texel < height.size(); ++texel) {
                slopeX[texel] = data[texel * 4];
                slopeZ[texel] = data[texel * 4 + 1];
            }
        }
    });

    bool nonZero = false;
    for (float value : height) {
        nonZero = nonZero || std::abs(value) > 1.0e-6f;
    }
    REQUIRE(nonZero);

    std::vector<std::complex<double>> spectrum(height.size());
    const double twoPi = 2.0 * 3.14159265358979323846;
    for (int ky = 0; ky < kSize; ++ky) {
        const int signedKy = ky < kSize / 2 ? ky : ky - kSize;
        for (int kx = 0; kx < kSize; ++kx) {
            const int signedKx = kx < kSize / 2 ? kx : kx - kSize;
            std::complex<double> sum(0.0, 0.0);
            for (int y = 0; y < kSize; ++y) {
                for (int x = 0; x < kSize; ++x) {
                    const double angle = twoPi * static_cast<double>(signedKx * x + signedKy * y) / kSize;
                    sum += static_cast<double>(height[static_cast<std::size_t>(y) * kSize + x]) *
                           std::complex<double>(std::cos(angle), -std::sin(angle));
                }
            }
            spectrum[static_cast<std::size_t>(ky) * kSize + kx] = sum / static_cast<double>(kSize * kSize);
        }
    }

    std::vector<double> derivativeX(height.size(), 0.0);
    std::vector<double> derivativeZ(height.size(), 0.0);
    const double spacing = static_cast<double>(settings.gridSizes[0]) / kSize;
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            double sumX = 0.0;
            double sumZ = 0.0;
            for (int ky = 0; ky < kSize; ++ky) {
                const int signedKy = ky < kSize / 2 ? ky : ky - kSize;
                const double kz = twoPi * static_cast<double>(signedKy) / (kSize * spacing);
                for (int kx = 0; kx < kSize; ++kx) {
                    const int signedKx = kx < kSize / 2 ? kx : kx - kSize;
                    const double kxValue = twoPi * static_cast<double>(signedKx) / (kSize * spacing);
                    const double angle = twoPi * static_cast<double>(signedKx * x + signedKy * y) / kSize;
                    const std::complex<double> wave(std::cos(angle), std::sin(angle));
                    const std::complex<double> value =
                        spectrum[static_cast<std::size_t>(ky) * kSize + kx] * wave;
                    sumX += (value * std::complex<double>(0.0, kxValue)).real();
                    sumZ += (value * std::complex<double>(0.0, kz)).real();
                }
            }
            derivativeX[static_cast<std::size_t>(y) * kSize + x] = sumX;
            derivativeZ[static_cast<std::size_t>(y) * kSize + x] = sumZ;
        }
    }

    for (std::size_t texel = 0; texel < height.size(); ++texel) {
        CHECK(slopeX[texel] == doctest::Approx(static_cast<float>(derivativeX[texel])).epsilon(0.02));
        CHECK(slopeZ[texel] == doctest::Approx(static_cast<float>(derivativeZ[texel])).epsilon(0.02));
    }
}

TEST_CASE("ocean fft choppy displacement is relative to base positions") {
    OceanFFTSettings settings;
    settings.resolution = 32;
    settings.gridSizes = {256.0f, 64.0f, 16.0f, 4.0f};
    settings.amplitude = 1.0f;
    settings.choppyFactor = 2.5f;
    settings.updateHz = 1.0f;

    OceanFFT fft;
    REQUIRE(fft.init(settings));
    REQUIRE(fft.update(0.0f));

    std::vector<asset::MeshVertex> base;
    for (int z = 0; z < 5; ++z) {
        for (int x = 0; x < 5; ++x) {
            asset::MeshVertex vertex;
            vertex.position = Vec3(static_cast<float>(x) * 61.0f, 0.5f, static_cast<float>(z) * 73.0f);
            base.push_back(vertex);
        }
    }

    std::vector<asset::MeshVertex> first = base;
    fft.applyToVertices(first, 0.5f, &base);

    float maxDisplacement = 0.0f;
    for (std::size_t i = 0; i < base.size(); ++i) {
        REQUIRE(std::isfinite(first[i].position.x));
        REQUIRE(std::isfinite(first[i].position.z));
        maxDisplacement = std::max(maxDisplacement, std::abs(first[i].position.x - base[i].position.x));
        maxDisplacement = std::max(maxDisplacement, std::abs(first[i].position.z - base[i].position.z));
    }
    CHECK(maxDisplacement > 1.0e-5f);

    std::vector<asset::MeshVertex> second = first;
    fft.applyToVertices(second, 0.5f, &base);
    for (std::size_t i = 0; i < base.size(); ++i) {
        CHECK(second[i].position.x == doctest::Approx(first[i].position.x).epsilon(1.0e-5));
        CHECK(second[i].position.z == doctest::Approx(first[i].position.z).epsilon(1.0e-5));
    }
}

TEST_CASE("ocean fft throttles updates to the configured rate") {
    OceanFFTSettings settings;
    settings.resolution = 16;
    settings.gridSizes = {128.0f, 32.0f, 8.0f, 2.0f};
    settings.updateHz = 2.0f;

    OceanFFT fft;
    REQUIRE(fft.init(settings));
    REQUIRE(fft.update(0.0f));
    CHECK(fft.updateCount() == 1);
    CHECK_FALSE(fft.update(0.1f));
    CHECK(fft.updateCount() == 1);
    CHECK(fft.update(0.6f));
    CHECK(fft.updateCount() == 2);
}

TEST_CASE("ocean fft screen grid stays finite and on the water plane") {
    OceanFFTSettings settings;
    settings.resolution = 16;
    settings.gridSizes = {128.0f, 32.0f, 8.0f, 2.0f};
    settings.updateHz = 1.0f;

    OceanFFT fft;
    REQUIRE(fft.init(settings));
    REQUIRE(fft.update(0.0f));

    constexpr int kGridWidth = 7;
    constexpr int kGridHeight = 3;
    const Mat4 view = lookAtRH(Vec3(0.0f, 20.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
    const Mat4 projection = perspectiveRH(1.0f, 1.0f, 1.0f, 1000.0f, true);
    const Mat4 invViewProj = inverse(projection * view);
    std::vector<asset::MeshVertex> vertices(kGridWidth * kGridHeight);

    fft.applyToScreenGrid(vertices, 0.5f, kGridWidth, kGridHeight, invViewProj, Vec3(0.0f, 20.0f, 0.0f),
                          Vec2(1.0f, 1.0f), Vec2(0.0f, 0.0f));

    for (const asset::MeshVertex& vertex : vertices) {
        CHECK(std::isfinite(vertex.position.x));
        CHECK(std::isfinite(vertex.position.y));
        CHECK(std::isfinite(vertex.position.z));
        CHECK(std::isfinite(vertex.texcoord0.x));
        CHECK(std::isfinite(vertex.texcoord0.y));
        CHECK(vertex.position.y > 0.0f);
        const float normalLength = std::sqrt(vertex.normal.x * vertex.normal.x + vertex.normal.y * vertex.normal.y +
                                             vertex.normal.z * vertex.normal.z);
        CHECK(normalLength == doctest::Approx(1.0f).epsilon(0.001));
    }
}

TEST_CASE("water screen grid follows original viewport density") {
    asset::Mesh mesh;
    mesh.name = "Water";
    mesh.vertices.resize(4);
    mesh.vertices[0].position = Vec3(-100.0f, 0.5f, -50.0f);
    mesh.vertices[0].texcoord0 = Vec2(0.0f, 0.0f);
    mesh.vertices[1].position = Vec3(100.0f, 0.5f, -50.0f);
    mesh.vertices[1].texcoord0 = Vec2(4.0f, 0.0f);
    mesh.vertices[2].position = Vec3(-100.0f, 0.5f, 50.0f);
    mesh.vertices[2].texcoord0 = Vec2(0.0f, 4.0f);
    mesh.vertices[3].position = Vec3(100.0f, 0.5f, 50.0f);
    mesh.vertices[3].texcoord0 = Vec2(4.0f, 4.0f);
    mesh.indices = {0, 2, 1, 1, 2, 3};
    asset::SubMesh sourceSubmesh;
    sourceSubmesh.indexCount = 6;
    sourceSubmesh.materialIndex = 7;
    mesh.submeshes.push_back(sourceSubmesh);
    mesh.recomputeBounds();

    WaterScreenGridDesc desc;
    REQUIRE(configureWaterScreenGrid(mesh, 1280, 720, &desc));
    CHECK(desc.width == 200);
    CHECK(desc.height == 112);
    CHECK(mesh.vertices.size() == 22400);
    CHECK(mesh.indices.size() == 132534);
    REQUIRE(mesh.submeshes.size() == 1);
    CHECK(mesh.submeshes.front().materialIndex == 7);
    CHECK(mesh.submeshes.front().indexCount == mesh.indices.size());
    CHECK(mesh.bounds.min.x == doctest::Approx(-100.0f));
    CHECK(mesh.bounds.max.x == doctest::Approx(100.0f));
    CHECK(mesh.bounds.min.z == doctest::Approx(-50.0f));
    CHECK(mesh.bounds.max.z == doctest::Approx(50.0f));
    CHECK(mesh.vertices.front().position.y == doctest::Approx(0.5f));
    CHECK(mesh.vertices.back().texcoord0.x == doctest::Approx(4.0f));
    CHECK(mesh.vertices.back().texcoord0.y == doctest::Approx(4.0f));

    REQUIRE(configureWaterScreenGrid(mesh, 640, 360, &desc));
    CHECK(desc.width == 100);
    CHECK(desc.height == 56);
    CHECK(mesh.vertices.size() == 5600);
    CHECK(mesh.indices.size() == 32670);

    CHECK_FALSE(configureWaterScreenGrid(mesh, 0, 720, &desc));
}
