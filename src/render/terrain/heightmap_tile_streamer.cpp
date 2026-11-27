#include "render/terrain/heightmap_tile_streamer.h"

#include "core/log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace hbg::render {
namespace {

constexpr std::size_t kHeaderSize = 20;
constexpr uint32_t kMaxQueuedTiles = 1024;

} // namespace

HeightmapTileStreamer::~HeightmapTileStreamer() {
    close();
}

bool HeightmapTileStreamer::init(const std::filesystem::path& path, float originX, float originZ, float worldSize,
                                 float skirtDepth, uint32_t tileSize, uint32_t capacity,
                                 gfx::ResourceManager* resources, std::string* error) {
    close();
    if (!readHeader(path, error)) {
        return false;
    }

    m_path = path;
    m_originX = originX;
    m_originZ = originZ;
    m_worldSize = worldSize;
    m_skirtDepth = skirtDepth;
    m_tileSize = std::max(2u, tileSize);
    m_capacity = std::max(1u, capacity);
    m_resources = resources;

    if (resources != nullptr) {
        m_gpuEnabled = m_gpu.init(*resources, m_tileSize, m_capacity, gfx::TextureFormat::R32F,
                                  "terrain.heightmap.tiles");
        if (!m_gpuEnabled) {
            HBG_LOGW("heightmap tile streamer: GPU tile storage unavailable, continuing with CPU bounds");
        }
    }

    m_layerUsed.assign(m_capacity, 0);
    m_freeLayers = m_capacity;
    m_loaded = true;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = false;
        m_workerFailed = false;
    }
    m_worker = std::thread([this]() { run(); });
    HBG_LOGI("heightmap tile streamer: {}x{} scale={} tile={} capacity={} gpu={}", m_width, m_height, m_heightScale,
             m_tileSize, m_capacity, m_gpuEnabled ? "true" : "false");
    return true;
}

void HeightmapTileStreamer::close() {
    if (m_worker.joinable()) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
            m_queue.clear();
        }
        m_condition.notify_all();
        m_worker.join();
    }

    if (m_resources != nullptr) {
        m_gpu.shutdown(*m_resources);
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
        m_pending.clear();
        m_ready.clear();
        m_failed.clear();
        m_completed.clear();
        m_stop = false;
        m_workerFailed = false;
        m_stats = {};
    }
    m_gpuTiles.clear();
    m_gpuIndex.clear();
    m_layerUsed.clear();
    m_freeLayers = 0;
    m_gpuEnabled = false;
    m_loaded = false;
    m_resources = nullptr;
    m_path.clear();
    m_width = 0;
    m_height = 0;
    m_dataOffset = 0;
    m_heightScale = 1.0f;
    m_originX = 0.0f;
    m_originZ = 0.0f;
    m_worldSize = 0.0f;
    m_skirtDepth = 0.0f;
    m_tileSize = 0;
    m_capacity = 0;
}

bool HeightmapTileStreamer::readHeader(const std::filesystem::path& path, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error != nullptr) {
            *error = "heightmap tile streamer: cannot open " + path.string();
        }
        return false;
    }

    char magic[8] = {};
    uint32_t width = 0;
    uint32_t height = 0;
    float scale = 1.0f;
    file.read(magic, sizeof(magic));
    file.read(reinterpret_cast<char*>(&width), sizeof(width));
    file.read(reinterpret_cast<char*>(&height), sizeof(height));
    file.read(reinterpret_cast<char*>(&scale), sizeof(scale));
    if (!file || std::memcmp(magic, "HBGHMAP1", 8) != 0 || width == 0 || height == 0 ||
        !std::isfinite(scale) || scale <= 0.0f) {
        if (error != nullptr) {
            *error = "heightmap tile streamer: invalid header";
        }
        return false;
    }

    m_width = width;
    m_height = height;
    m_heightScale = scale;
    m_dataOffset = static_cast<uint32_t>(kHeaderSize);
    return true;
}

uint64_t HeightmapTileStreamer::tileKey(int level, int tx, int ty) {
    return (static_cast<uint64_t>(level) << 32) | (static_cast<uint64_t>(tx) << 16) |
           static_cast<uint64_t>(ty);
}

bool HeightmapTileStreamer::query(int level, int tx, int ty, float& zmin, float& zmax) {
    if (!m_loaded || m_worldSize <= 0.0f || level < 0 || level >= 31) {
        return false;
    }
    const int count = 1 << level;
    if (tx < 0 || ty < 0 || tx >= count || ty >= count) {
        return false;
    }

    const uint64_t key = tileKey(level, tx, ty);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_workerFailed) {
        return false;
    }
    const auto found = m_ready.find(key);
    if (found != m_ready.end()) {
        ++m_stats.hits;
        zmin = found->second.first;
        zmax = found->second.second;
        return true;
    }
    if (m_failed.find(key) != m_failed.end() || m_pending.find(key) != m_pending.end()) {
        return false;
    }
    if (m_queue.size() >= kMaxQueuedTiles) {
        return false;
    }

    m_pending.insert(key);
    m_queue.push_back({key, level, tx, ty});
    ++m_stats.misses;
    m_condition.notify_one();
    return false;
}

void HeightmapTileStreamer::poll() {
    std::vector<TileResult> completed;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        completed.swap(m_completed);
    }

    for (TileResult& result : completed) {
        int gpuResult = -1;
        if (result.ok && m_gpuEnabled && !result.samples.empty()) {
            gpuResult = assignGpuTile(result.key, result);
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.erase(result.key);
        if (result.ok) {
            m_ready[result.key] = {result.zmin, result.zmax};
            ++m_stats.produced;
            if (gpuResult >= 0) {
                ++m_stats.uploads;
                if (gpuResult > 0) {
                    ++m_stats.evictions;
                }
            }
        } else {
            m_failed.insert(result.key);
            ++m_stats.failed;
        }
    }
}

HeightmapTileStreamer::Stats HeightmapTileStreamer::stats() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    Stats snapshot = m_stats;
    snapshot.pending = static_cast<uint32_t>(m_pending.size());
    snapshot.gpuLayers = static_cast<uint32_t>(m_gpuTiles.size());
    snapshot.gpuBytes = m_gpu.gpuBytes();
    return snapshot;
}

void HeightmapTileStreamer::run() {
    std::ifstream file(m_path, std::ios::binary);
    if (!file) {
        HBG_LOGE("heightmap tile streamer: worker cannot open '{}'", m_path.string());
        std::lock_guard<std::mutex> lock(m_mutex);
        m_workerFailed = true;
        m_pending.clear();
        m_queue.clear();
        return;
    }

    for (;;) {
        TileRequest request;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_condition.wait(lock, [this]() { return m_stop || !m_queue.empty(); });
            if (m_stop && m_queue.empty()) {
                break;
            }
            request = m_queue.front();
            m_queue.pop_front();
        }

        TileResult result;
        result.key = request.key;
        result.ok = produceTile(file, request, result);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_completed.push_back(std::move(result));
        }
    }
}

bool HeightmapTileStreamer::produceTile(std::ifstream& file, const TileRequest& request, TileResult& result) const {
    if (request.level < 0 || request.level >= 31) {
        return false;
    }
    const int count = 1 << request.level;
    if (request.tx < 0 || request.ty < 0 || request.tx >= count || request.ty >= count) {
        return false;
    }

    const float size = m_worldSize / static_cast<float>(count);
    const float ox = m_originX + static_cast<float>(request.tx) * size;
    const float oz = m_originZ + static_cast<float>(request.ty) * size;
    const float half = m_worldSize * 0.5f;
    int x0 = static_cast<int>(std::floor((ox + half) / m_worldSize * static_cast<float>(m_width - 1))) - 1;
    int x1 = static_cast<int>(std::ceil((ox + size + half) / m_worldSize * static_cast<float>(m_width - 1))) + 1;
    int z0 = static_cast<int>(std::floor((oz + half) / m_worldSize * static_cast<float>(m_height - 1))) - 1;
    int z1 = static_cast<int>(std::ceil((oz + size + half) / m_worldSize * static_cast<float>(m_height - 1))) + 1;
    x0 = std::max(0, x0);
    z0 = std::max(0, z0);
    x1 = std::min(static_cast<int>(m_width) - 1, x1);
    z1 = std::min(static_cast<int>(m_height) - 1, z1);
    if (x1 < x0 || z1 < z0) {
        return false;
    }

    const std::size_t columns = static_cast<std::size_t>(x1 - x0 + 1);
    const std::size_t rows = static_cast<std::size_t>(z1 - z0 + 1);
    const std::size_t rowBytes = columns * sizeof(uint16_t);
    std::vector<uint16_t> block(columns * rows);
    std::vector<uint8_t> row(rowBytes);
    for (std::size_t z = 0; z < rows; ++z) {
        const std::streamoff offset = static_cast<std::streamoff>(m_dataOffset) +
                                      (static_cast<std::streamoff>(z0 + static_cast<int>(z)) * m_width + x0) *
                                          static_cast<std::streamoff>(sizeof(uint16_t));
        file.clear();
        file.seekg(offset, std::ios::beg);
        if (!file) {
            return false;
        }
        file.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(rowBytes));
        if (file.gcount() != static_cast<std::streamsize>(rowBytes)) {
            return false;
        }
        for (std::size_t x = 0; x < columns; ++x) {
            block[z * columns + x] = static_cast<uint16_t>(row[x * 2]) |
                                     static_cast<uint16_t>(row[x * 2 + 1] << 8);
        }
    }

    uint16_t minRaw = block.front();
    uint16_t maxRaw = block.front();
    for (const uint16_t value : block) {
        minRaw = std::min(minRaw, value);
        maxRaw = std::max(maxRaw, value);
    }
    result.zmin = static_cast<float>(minRaw) * m_heightScale - m_skirtDepth;
    result.zmax = static_cast<float>(maxRaw) * m_heightScale;

    if (!m_gpuEnabled) {
        return true;
    }

    const uint32_t sampleSize = m_tileSize;
    result.samples.resize(static_cast<std::size_t>(sampleSize) * sampleSize);
    const float denominator = static_cast<float>(sampleSize - 1);
    for (uint32_t py = 0; py < sampleSize; ++py) {
        const float worldZ = oz + static_cast<float>(py) / denominator * size;
        const float sourceZ =
            std::clamp((worldZ + half) / m_worldSize * static_cast<float>(m_height - 1) - static_cast<float>(z0),
                       0.0f, static_cast<float>(rows - 1));
        const uint32_t zLow = static_cast<uint32_t>(std::floor(sourceZ));
        const uint32_t zHigh = std::min(zLow + 1, static_cast<uint32_t>(rows - 1));
        const float tz = sourceZ - static_cast<float>(zLow);
        for (uint32_t px = 0; px < sampleSize; ++px) {
            const float worldX = ox + static_cast<float>(px) / denominator * size;
            const float sourceX =
                std::clamp((worldX + half) / m_worldSize * static_cast<float>(m_width - 1) - static_cast<float>(x0),
                           0.0f, static_cast<float>(columns - 1));
            const uint32_t xLow = static_cast<uint32_t>(std::floor(sourceX));
            const uint32_t xHigh = std::min(xLow + 1, static_cast<uint32_t>(columns - 1));
            const float tx = sourceX - static_cast<float>(xLow);

            const float h00 = static_cast<float>(block[zLow * columns + xLow]);
            const float h10 = static_cast<float>(block[zLow * columns + xHigh]);
            const float h01 = static_cast<float>(block[zHigh * columns + xLow]);
            const float h11 = static_cast<float>(block[zHigh * columns + xHigh]);
            const float top = h00 + (h10 - h00) * tx;
            const float bottom = h01 + (h11 - h01) * tx;
            result.samples[static_cast<std::size_t>(py) * sampleSize + px] =
                (top + (bottom - top) * tz) * m_heightScale;
        }
    }
    return true;
}

int HeightmapTileStreamer::assignGpuTile(uint64_t key, const TileResult& result) {
    const auto existing = m_gpuIndex.find(key);
    if (existing != m_gpuIndex.end()) {
        m_gpuTiles.splice(m_gpuTiles.begin(), m_gpuTiles, existing->second);
        return 0;
    }

    uint32_t layer = 0;
    bool foundFree = false;
    bool evicted = false;
    if (m_freeLayers > 0) {
        for (uint32_t candidate = 0; candidate < m_capacity; ++candidate) {
            if (m_layerUsed[candidate] == 0) {
                layer = candidate;
                foundFree = true;
                break;
            }
        }
    }
    if (!foundFree) {
        if (m_gpuTiles.empty()) {
            return -1;
        }
        const GpuTileEntry victim = m_gpuTiles.back();
        layer = victim.layer;
        m_gpuIndex.erase(victim.key);
        m_gpuTiles.pop_back();
        evicted = true;
    }

    const uint32_t pitch = m_tileSize * static_cast<uint32_t>(sizeof(float));
    const uint32_t size = static_cast<uint32_t>(result.samples.size() * sizeof(float));
    if (!m_gpu.upload(layer, result.samples.data(), size, pitch)) {
        if (evicted) {
            m_layerUsed[layer] = 0;
            ++m_freeLayers;
        }
        return -1;
    }
    if (foundFree) {
        m_layerUsed[layer] = 1;
        --m_freeLayers;
    }
    m_gpuTiles.push_front({key, layer});
    m_gpuIndex[key] = m_gpuTiles.begin();
    return evicted ? 1 : 0;
}

} // namespace hbg::render
