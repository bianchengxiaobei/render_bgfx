#include "render/terrain/heightmap_tile_cache.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

using namespace hbg::render;

namespace {

std::filesystem::path writeTestHeightmap() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "hbg_heightmap_tile_cache_test.bin";
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.good()) {
        return {};
    }
    file.write("HBGHMAP1", 8);
    const uint32_t width = 8;
    const uint32_t height = 8;
    const float scale = 1.0f;
    file.write(reinterpret_cast<const char*>(&width), sizeof(width));
    file.write(reinterpret_cast<const char*>(&height), sizeof(height));
    file.write(reinterpret_cast<const char*>(&scale), sizeof(scale));
    for (uint32_t z = 0; z < height; ++z) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint16_t value = static_cast<uint16_t>(z * width + x);
            const uint8_t bytes[2] = {static_cast<uint8_t>(value & 0xffu),
                                      static_cast<uint8_t>((value >> 8) & 0xffu)};
            file.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
        }
    }
    return path;
}

} // namespace

TEST_CASE("heightmap tile cache reads bounded tiles and maintains LRU statistics") {
    const std::filesystem::path path = writeTestHeightmap();
    REQUIRE_FALSE(path.empty());
    HeightmapTileCache cache;
    std::string error;
    REQUIRE(cache.load(path, &error));
    CHECK(cache.width() == 8);
    CHECK(cache.height() == 8);
    CHECK(cache.heightScale() == doctest::Approx(1.0f));
    cache.setWorld(-4.0f, -4.0f, 8.0f, 0.0f);

    float sampledHeight = 0.0f;
    REQUIRE(cache.sampleHeight(-4.0f, -4.0f, sampledHeight));
    CHECK(sampledHeight == doctest::Approx(0.0f));
    REQUIRE(cache.sampleHeight(0.0f, 0.0f, sampledHeight));
    CHECK(sampledHeight == doctest::Approx(31.5f));
    REQUIRE(cache.sampleHeight(4.0f, 4.0f, sampledHeight));
    CHECK(sampledHeight == doctest::Approx(63.0f));
    CHECK_FALSE(cache.sampleHeight(5.0f, 0.0f, sampledHeight));

    float zmin = 0.0f;
    float zmax = 0.0f;
    REQUIRE(cache.query(3, 0, 0, zmin, zmax));
    CHECK(zmin == doctest::Approx(0.0f));
    CHECK(zmax == doctest::Approx(18.0f));
    REQUIRE(cache.query(3, 0, 0, zmin, zmax));
    CHECK(cache.stats().hits == 1);
    CHECK(cache.stats().misses == 1);

    cache.setCapacity(1);
    REQUIRE(cache.query(3, 1, 1, zmin, zmax));
    CHECK(cache.stats().misses == 2);
    CHECK(cache.stats().loadedTiles == 2);
    CHECK(cache.stats().evictions == 1);
    CHECK_FALSE(cache.query(4, 16, 0, zmin, zmax));

    cache.close();
    CHECK_FALSE(cache.loaded());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST_CASE("heightmap tile cache rejects invalid files") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "hbg_heightmap_tile_cache_invalid.bin";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        REQUIRE(file.good());
        file.write("not-a-heightmap", 15);
    }

    HeightmapTileCache cache;
    std::string error;
    CHECK_FALSE(cache.load(path, &error));
    CHECK_FALSE(error.empty());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}
