#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <list>
#include <string>
#include <unordered_map>

namespace hbg::render {

class HeightmapTileCache {
  public:
    struct Stats {
        uint32_t hits = 0;
        uint32_t misses = 0;
        uint32_t evictions = 0;
        uint32_t loadedTiles = 0;
    };

    bool load(const std::filesystem::path& path, std::string* error = nullptr);
    void close();
    bool loaded() const { return m_file.is_open(); }

    void setWorld(float originX, float originZ, float worldSize, float skirtDepth);
    void setCapacity(std::size_t capacity);

    bool query(int level, int tx, int ty, float& zmin, float& zmax);
    bool sampleHeight(float worldX, float worldZ, float& height);

    const Stats& stats() const { return m_stats; }
    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    float heightScale() const { return m_heightScale; }

  private:
    struct Tile {
        uint64_t key = 0;
        float zmin = 0.0f;
        float zmax = 0.0f;
    };

    static uint64_t tileKey(int level, int tx, int ty);
    bool readTexel(int x, int z, uint16_t& value);
    bool readBounds(int x0, int z0, int x1, int z1, float& zmin, float& zmax);

    std::ifstream m_file;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_dataOffset = 0;
    float m_heightScale = 1.0f;
    float m_originX = 0.0f;
    float m_originZ = 0.0f;
    float m_worldSize = 0.0f;
    float m_skirtDepth = 0.0f;
    std::size_t m_capacity = 64;
    Stats m_stats;

    std::list<Tile> m_tiles;
    std::unordered_map<uint64_t, std::list<Tile>::iterator> m_index;
};

} // namespace hbg::render
