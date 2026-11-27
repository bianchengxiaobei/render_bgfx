#pragma once

#include "render/terrain/gpu_tile_storage.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hbg::render {

class HeightmapTileStreamer {
  public:
    struct Stats {
        uint32_t hits = 0;
        uint32_t misses = 0;
        uint32_t produced = 0;
        uint32_t failed = 0;
        uint32_t uploads = 0;
        uint32_t evictions = 0;
        uint32_t pending = 0;
        uint32_t gpuLayers = 0;
        uint64_t gpuBytes = 0;
    };

    ~HeightmapTileStreamer();

    HeightmapTileStreamer() = default;
    HeightmapTileStreamer(const HeightmapTileStreamer&) = delete;
    HeightmapTileStreamer& operator=(const HeightmapTileStreamer&) = delete;

    bool init(const std::filesystem::path& path, float originX, float originZ, float worldSize, float skirtDepth,
              uint32_t tileSize, uint32_t capacity, gfx::ResourceManager* resources = nullptr,
              std::string* error = nullptr);
    void close();

    bool loaded() const { return m_loaded; }
    bool query(int level, int tx, int ty, float& zmin, float& zmax);
    void poll();

    Stats stats() const;
    const GpuTileStorage& gpuStorage() const { return m_gpu; }
    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    float heightScale() const { return m_heightScale; }

  private:
    struct TileRequest {
        uint64_t key = 0;
        int level = 0;
        int tx = 0;
        int ty = 0;
    };

    struct TileResult {
        uint64_t key = 0;
        bool ok = false;
        float zmin = 0.0f;
        float zmax = 0.0f;
        std::vector<float> samples;
    };

    struct GpuTileEntry {
        uint64_t key = 0;
        uint32_t layer = 0;
    };

    static uint64_t tileKey(int level, int tx, int ty);
    bool readHeader(const std::filesystem::path& path, std::string* error);
    void run();
    bool produceTile(std::ifstream& file, const TileRequest& request, TileResult& result) const;
    int assignGpuTile(uint64_t key, const TileResult& result);

    std::filesystem::path m_path;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_dataOffset = 0;
    float m_heightScale = 1.0f;
    float m_originX = 0.0f;
    float m_originZ = 0.0f;
    float m_worldSize = 0.0f;
    float m_skirtDepth = 0.0f;
    uint32_t m_tileSize = 0;
    uint32_t m_capacity = 0;
    bool m_gpuEnabled = false;
    bool m_loaded = false;

    gfx::ResourceManager* m_resources = nullptr;
    GpuTileStorage m_gpu;

    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<TileRequest> m_queue;
    std::unordered_set<uint64_t> m_pending;
    std::unordered_map<uint64_t, std::pair<float, float>> m_ready;
    std::unordered_set<uint64_t> m_failed;
    std::vector<TileResult> m_completed;
    bool m_stop = false;
    bool m_workerFailed = false;
    std::thread m_worker;
    Stats m_stats;

    std::list<GpuTileEntry> m_gpuTiles;
    std::unordered_map<uint64_t, std::list<GpuTileEntry>::iterator> m_gpuIndex;
    std::vector<uint8_t> m_layerUsed;
    uint32_t m_freeLayers = 0;
};

} // namespace hbg::render
