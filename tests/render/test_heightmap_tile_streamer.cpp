#include "render/terrain/heightmap_tile_cache.h"
#include "render/terrain/heightmap_tile_streamer.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <thread>
#include <vector>

using namespace hbg::render;

namespace {

std::filesystem::path writeTestHeightmap() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "hbg_heightmap_tile_streamer_test.bin";
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

TEST_CASE("heightmap tile streamer produces bounds asynchronously") {
    const std::filesystem::path path = writeTestHeightmap();
    REQUIRE_FALSE(path.empty());

    HeightmapTileStreamer streamer;
    std::string error;
    REQUIRE(streamer.init(path, -4.0f, -4.0f, 8.0f, 0.0f, 8, 2, nullptr, &error));

    float zmin = 0.0f;
    float zmax = 0.0f;
    bool ready = false;
    for (int attempt = 0; attempt < 500 && !ready; ++attempt) {
        streamer.poll();
        ready = streamer.query(3, 0, 0, zmin, zmax);
        if (!ready) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    REQUIRE(ready);
    CHECK(zmin == doctest::Approx(0.0f));
    CHECK(zmax == doctest::Approx(18.0f));

    const HeightmapTileStreamer::Stats stats = streamer.stats();
    CHECK(stats.produced >= 1);
    CHECK(stats.misses >= 1);
    CHECK(stats.hits >= 1);
    CHECK(stats.pending == 0);

    streamer.close();
    CHECK_FALSE(streamer.loaded());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST_CASE("heightmap tile streamer matches the synchronous cache for every tile") {
    const std::filesystem::path path = writeTestHeightmap();
    REQUIRE_FALSE(path.empty());

    HeightmapTileCache syncCache;
    std::string error;
    REQUIRE(syncCache.load(path, &error));
    syncCache.setWorld(-4.0f, -4.0f, 8.0f, 0.0f);

    HeightmapTileStreamer streamer;
    REQUIRE(streamer.init(path, -4.0f, -4.0f, 8.0f, 0.0f, 8, 64, nullptr, &error));

    struct TileCoord {
        int level = 0;
        int tx = 0;
        int ty = 0;
    };
    std::vector<TileCoord> tiles;
    for (int level = 0; level <= 3; ++level) {
        const int count = 1 << level;
        for (int ty = 0; ty < count; ++ty) {
            for (int tx = 0; tx < count; ++tx) {
                tiles.push_back({level, tx, ty});
                float zmin = 0.0f;
                float zmax = 0.0f;
                streamer.query(level, tx, ty, zmin, zmax);
            }
        }
    }

    for (int attempt = 0; attempt < 1000 && streamer.stats().pending > 0; ++attempt) {
        streamer.poll();
        if (streamer.stats().pending > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    CHECK(streamer.stats().pending == 0);
    CHECK(streamer.stats().produced == static_cast<uint32_t>(tiles.size()));

    for (const TileCoord& tile : tiles) {
        float asyncZmin = 0.0f;
        float asyncZmax = 0.0f;
        float syncZmin = 0.0f;
        float syncZmax = 0.0f;
        REQUIRE(streamer.query(tile.level, tile.tx, tile.ty, asyncZmin, asyncZmax));
        REQUIRE(syncCache.query(tile.level, tile.tx, tile.ty, syncZmin, syncZmax));
        CHECK(asyncZmin == doctest::Approx(syncZmin));
        CHECK(asyncZmax == doctest::Approx(syncZmax));
    }

    streamer.close();
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST_CASE("heightmap tile streamer rejects invalid files") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "hbg_heightmap_tile_streamer_invalid.bin";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        REQUIRE(file.good());
        file.write("not-a-heightmap", 15);
    }

    HeightmapTileStreamer streamer;
    std::string error;
    CHECK_FALSE(streamer.init(path, -4.0f, -4.0f, 8.0f, 0.0f, 8, 2, nullptr, &error));
    CHECK_FALSE(error.empty());
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}
