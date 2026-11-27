#include "render/terrain/heightmap_tile_cache.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace hbg::render {
namespace {

constexpr std::size_t kHeaderSize = 20;

} // namespace

bool HeightmapTileCache::load(const std::filesystem::path& path, std::string* error) {
    close();
    m_file.open(path, std::ios::binary);
    if (!m_file) {
        if (error != nullptr) {
            *error = "heightmap tile cache: cannot open " + path.string();
        }
        return false;
    }

    char magic[8] = {};
    m_file.read(magic, sizeof(magic));
    uint32_t width = 0;
    uint32_t height = 0;
    float scale = 1.0f;
    m_file.read(reinterpret_cast<char*>(&width), sizeof(width));
    m_file.read(reinterpret_cast<char*>(&height), sizeof(height));
    m_file.read(reinterpret_cast<char*>(&scale), sizeof(scale));
    if (!m_file || std::memcmp(magic, "HBGHMAP1", 8) != 0 || width == 0 || height == 0 ||
        !std::isfinite(scale) || scale <= 0.0f) {
        if (error != nullptr) {
            *error = "heightmap tile cache: invalid header";
        }
        close();
        return false;
    }

    m_width = width;
    m_height = height;
    m_heightScale = scale;
    m_dataOffset = static_cast<uint32_t>(kHeaderSize);
    m_stats = {};
    return true;
}

void HeightmapTileCache::close() {
    if (m_file.is_open()) {
        m_file.close();
    }
    m_width = 0;
    m_height = 0;
    m_dataOffset = 0;
    m_heightScale = 1.0f;
    m_originX = 0.0f;
    m_originZ = 0.0f;
    m_worldSize = 0.0f;
    m_skirtDepth = 0.0f;
    m_stats = {};
    m_tiles.clear();
    m_index.clear();
}

void HeightmapTileCache::setWorld(float originX, float originZ, float worldSize, float skirtDepth) {
    m_originX = originX;
    m_originZ = originZ;
    m_worldSize = worldSize;
    m_skirtDepth = skirtDepth;
    m_tiles.clear();
    m_index.clear();
}

void HeightmapTileCache::setCapacity(std::size_t capacity) {
    m_capacity = std::max<std::size_t>(capacity, 1);
    while (m_tiles.size() > m_capacity) {
        m_index.erase(m_tiles.back().key);
        m_tiles.pop_back();
    }
}

uint64_t HeightmapTileCache::tileKey(int level, int tx, int ty) {
    return (static_cast<uint64_t>(level) << 32) | (static_cast<uint64_t>(tx) << 16) |
           static_cast<uint64_t>(ty);
}

bool HeightmapTileCache::readTexel(int x, int z, uint16_t& value) {
    if (!m_file.is_open() || x < 0 || z < 0 || x >= static_cast<int>(m_width) ||
        z >= static_cast<int>(m_height)) {
        return false;
    }
    const std::streamoff offset = static_cast<std::streamoff>(m_dataOffset) +
                                  (static_cast<std::streamoff>(z) * m_width + x) *
                                      static_cast<std::streamoff>(sizeof(uint16_t));
    m_file.clear();
    m_file.seekg(offset, std::ios::beg);
    if (!m_file) {
        return false;
    }
    uint8_t bytes[2] = {};
    m_file.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
    if (m_file.gcount() != static_cast<std::streamsize>(sizeof(bytes))) {
        return false;
    }
    value = static_cast<uint16_t>(bytes[0]) | static_cast<uint16_t>(bytes[1] << 8);
    return true;
}

bool HeightmapTileCache::sampleHeight(float worldX, float worldZ, float& height) {
    if (!m_file.is_open() || m_worldSize <= 0.0f || m_width == 0 || m_height == 0 ||
        !std::isfinite(worldX) || !std::isfinite(worldZ)) {
        return false;
    }
    const float u = (worldX - m_originX) / m_worldSize;
    const float v = (worldZ - m_originZ) / m_worldSize;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
        return false;
    }

    const float px = u * static_cast<float>(m_width - 1);
    const float pz = v * static_cast<float>(m_height - 1);
    const int x0 = static_cast<int>(std::floor(px));
    const int z0 = static_cast<int>(std::floor(pz));
    const int x1 = std::min(x0 + 1, static_cast<int>(m_width) - 1);
    const int z1 = std::min(z0 + 1, static_cast<int>(m_height) - 1);
    const float fx = px - static_cast<float>(x0);
    const float fz = pz - static_cast<float>(z0);

    uint16_t h00 = 0;
    uint16_t h10 = 0;
    uint16_t h01 = 0;
    uint16_t h11 = 0;
    if (!readTexel(x0, z0, h00) || !readTexel(x1, z0, h10) || !readTexel(x0, z1, h01) ||
        !readTexel(x1, z1, h11)) {
        return false;
    }

    const float top = static_cast<float>(h00) + (static_cast<float>(h10) - static_cast<float>(h00)) * fx;
    const float bottom = static_cast<float>(h01) + (static_cast<float>(h11) - static_cast<float>(h01)) * fx;
    height = (top + (bottom - top) * fz) * m_heightScale;
    return true;
}

bool HeightmapTileCache::readBounds(int x0, int z0, int x1, int z1, float& zminOut, float& zmaxOut) {
    if (!m_file.is_open() || x0 < 0 || z0 < 0 || x1 < x0 || z1 < z0 ||
        x1 >= static_cast<int>(m_width) || z1 >= static_cast<int>(m_height)) {
        return false;
    }

    const std::size_t columns = static_cast<std::size_t>(x1 - x0 + 1);
    const std::size_t rowBytes = columns * sizeof(uint16_t);
    std::vector<uint8_t> row(rowBytes);
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
    bool any = false;
    for (int z = z0; z <= z1; ++z) {
        const std::streamoff offset = static_cast<std::streamoff>(m_dataOffset) +
                                      (static_cast<std::streamoff>(z) * m_width + x0) *
                                          static_cast<std::streamoff>(sizeof(uint16_t));
        m_file.clear();
        m_file.seekg(offset, std::ios::beg);
        if (!m_file) {
            return false;
        }
        m_file.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(rowBytes));
        if (m_file.gcount() != static_cast<std::streamsize>(rowBytes)) {
            return false;
        }
        for (std::size_t index = 0; index < columns; ++index) {
            const uint16_t value = static_cast<uint16_t>(row[index * 2]) |
                                   static_cast<uint16_t>(row[index * 2 + 1] << 8);
            const float height = static_cast<float>(value) * m_heightScale;
            if (!any) {
                minHeight = height;
                maxHeight = height;
                any = true;
            } else {
                minHeight = std::min(minHeight, height);
                maxHeight = std::max(maxHeight, height);
            }
        }
    }

    if (!any) {
        return false;
    }
    zminOut = minHeight - m_skirtDepth;
    zmaxOut = maxHeight;
    return true;
}

bool HeightmapTileCache::query(int level, int tx, int ty, float& zmin, float& zmax) {
    if (!m_file.is_open() || m_worldSize <= 0.0f || level < 0 || level >= 31) {
        return false;
    }
    const int count = 1 << level;
    if (tx < 0 || ty < 0 || tx >= count || ty >= count) {
        return false;
    }

    const uint64_t key = tileKey(level, tx, ty);
    const auto found = m_index.find(key);
    if (found != m_index.end()) {
        ++m_stats.hits;
        m_tiles.splice(m_tiles.begin(), m_tiles, found->second);
        zmin = found->second->zmin;
        zmax = found->second->zmax;
        return true;
    }

    const float size = m_worldSize / static_cast<float>(count);
    const float ox = m_originX + static_cast<float>(tx) * size;
    const float oz = m_originZ + static_cast<float>(ty) * size;
    const float half = m_worldSize * 0.5f;
    int x0 = static_cast<int>(std::floor((ox + half) / m_worldSize * static_cast<float>(m_width - 1))) - 1;
    int x1 = static_cast<int>(std::ceil((ox + size + half) / m_worldSize * static_cast<float>(m_width - 1))) + 1;
    int z0 = static_cast<int>(std::floor((oz + half) / m_worldSize * static_cast<float>(m_height - 1))) - 1;
    int z1 = static_cast<int>(std::ceil((oz + size + half) / m_worldSize * static_cast<float>(m_height - 1))) + 1;
    x0 = std::max(0, x0);
    z0 = std::max(0, z0);
    x1 = std::min(static_cast<int>(m_width) - 1, x1);
    z1 = std::min(static_cast<int>(m_height) - 1, z1);
    if (!readBounds(x0, z0, x1, z1, zmin, zmax)) {
        return false;
    }

    ++m_stats.misses;
    ++m_stats.loadedTiles;
    m_tiles.push_front({key, zmin, zmax});
    m_index.emplace(key, m_tiles.begin());
    while (m_tiles.size() > m_capacity) {
        m_index.erase(m_tiles.back().key);
        m_tiles.pop_back();
        ++m_stats.evictions;
    }
    return true;
}

} // namespace hbg::render
