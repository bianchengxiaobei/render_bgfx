#include "render/sky/environment_processor.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>

using namespace hbg;
using namespace hbg::render;

namespace {

asset::ImageAsset makeImage(uint32_t width, uint32_t height, const std::array<float, 4>& color) {
    asset::ImageAsset image;
    image.name = "test_environment";
    image.width = width;
    image.height = height;
    image.format = gfx::TextureFormat::RGBA32F;
    image.pixels.resize(static_cast<std::size_t>(width) * height * 4 * sizeof(float));
    auto* pixels = reinterpret_cast<float*>(image.pixels.data());
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4;
            pixels[offset + 0] = color[0];
            pixels[offset + 1] = color[1];
            pixels[offset + 2] = color[2];
            pixels[offset + 3] = color[3];
        }
    }
    return image;
}

} // namespace

TEST_CASE("environment processor builds GGX mips and SH irradiance") {
    const std::array<float, 4> color = {2.0f, 4.0f, 6.0f, 1.0f};
    const asset::ImageAsset source = makeImage(64, 32, color);
    EnvironmentImage environment;
    std::string error;
    REQUIRE(EnvironmentProcessor::build(source, 0.0f, environment, &error));
    CHECK(error.empty());
    REQUIRE(environment.valid());
    CHECK(environment.width == 64);
    CHECK(environment.height == 32);
    CHECK(environment.mipCount == 6);
    REQUIRE(environment.mips.size() == 6);
    CHECK(environment.mips[0].size() == 64u * 32u * 4u);
    CHECK(environment.mips[5].size() == 2u * 1u * 4u);

    const Vec3 up = EnvironmentProcessor::evaluateSH(environment.sh, Vec3(0.0f, 1.0f, 0.0f));
    CHECK(up.x == doctest::Approx(color[0] / 3.14159265f).epsilon(0.002));
    CHECK(up.y == doctest::Approx(color[1] / 3.14159265f).epsilon(0.002));
    CHECK(up.z == doctest::Approx(color[2] / 3.14159265f).epsilon(0.002));

    for (const std::vector<float>& mip : environment.mips) {
        REQUIRE(!mip.empty());
        for (float value : mip) {
            CHECK(std::isfinite(value));
        }
    }
}

TEST_CASE("environment processor accepts a six-by-one cubemap strip") {
    const std::array<float, 4> color = {1.0f, 2.0f, 3.0f, 1.0f};
    const asset::ImageAsset source = makeImage(48, 8, color);
    EnvironmentImage environment;
    REQUIRE(EnvironmentProcessor::build(source, 0.0f, environment));
    CHECK(environment.width == 32);
    CHECK(environment.height == 16);
    CHECK(environment.mipCount == 5);
    const Vec3 up = EnvironmentProcessor::evaluateSH(environment.sh, Vec3(0.0f, 1.0f, 0.0f));
    CHECK(up.x == doctest::Approx(color[0] / 3.14159265f).epsilon(0.01));
}

TEST_CASE("environment SH keeps a vertical gradient") {
    asset::ImageAsset source = makeImage(64, 32, {0.0f, 0.0f, 0.0f, 1.0f});
    auto* pixels = reinterpret_cast<float*>(source.pixels.data());
    for (uint32_t y = 0; y < source.height; ++y) {
        const float value = 1.0f - static_cast<float>(y) / static_cast<float>(source.height - 1);
        for (uint32_t x = 0; x < source.width; ++x) {
            const std::size_t offset = (static_cast<std::size_t>(y) * source.width + x) * 4;
            pixels[offset + 0] = value;
            pixels[offset + 1] = value;
            pixels[offset + 2] = value;
        }
    }

    EnvironmentImage environment;
    REQUIRE(EnvironmentProcessor::build(source, 0.0f, environment));
    const Vec3 top = EnvironmentProcessor::evaluateSH(environment.sh, Vec3(0.0f, 1.0f, 0.0f));
    const Vec3 bottom = EnvironmentProcessor::evaluateSH(environment.sh, Vec3(0.0f, -1.0f, 0.0f));
    CHECK(top.x > bottom.x);
    CHECK(top.y > bottom.y);
    CHECK(top.z > bottom.z);
}

TEST_CASE("environment direction and equirect coordinates round trip") {
    const std::array<Vec3, 5> directions = {
        Vec3(0.0f, 0.0f, 1.0f),
        Vec3(1.0f, 0.0f, 0.0f),
        Vec3(0.0f, 1.0f, 0.0f),
        Vec3(0.0f, -1.0f, 0.0f),
        normalize(Vec3(0.3f, 0.7f, -0.2f)),
    };
    for (const Vec3& direction : directions) {
        const Vec2 uv = EnvironmentProcessor::directionToEquirect(direction);
        CHECK(uv.x >= 0.0f);
        CHECK(uv.x <= 1.0f);
        CHECK(uv.y >= 0.0f);
        CHECK(uv.y <= 1.0f);
        const Vec3 rebuilt = EnvironmentProcessor::equirectToDirection(uv);
        CHECK(length(rebuilt - direction) < 1.0e-4f);
    }
}

TEST_CASE("environment processor rejects invalid images") {
    EnvironmentImage environment;
    std::string error;
    CHECK_FALSE(EnvironmentProcessor::build(asset::ImageAsset{}, 0.0f, environment, &error));
    CHECK_FALSE(error.empty());
}
