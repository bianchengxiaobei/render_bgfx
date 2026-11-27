#pragma once

#include "asset/texture_loader.h"
#include "core/math.h"
#include "render/terrain/gpu_tile_storage.h"

#include <bgfx/bgfx.h>

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hbg::render {

class TerrainMaterialTileStreamer {
  public:
    struct Binding {
        int layer = -1;
        float originX = 0.0f;
        float originZ = 0.0f;
        float size = 0.0f;
    };

    enum class OrthoMode { Baked, Detail, Procedural };

    struct OrthoParams {
        OrthoMode mode = OrthoMode::Baked;
        float strength = 0.0f;
        float scale = 2.0f;
        Vec4 rootColor{0.5f, 0.5f, 0.5f, 0.5f};
        Vec4 noiseColor{1.0f, 1.0f, 1.0f, 1.0f};
        std::vector<float> noiseAmp;
        bool hsv = false;
        bool quadtree = false;
    };

    struct Stats {
        uint32_t hits = 0;
        uint32_t fallbacks = 0;
        uint32_t misses = 0;
        uint32_t produced = 0;
        uint32_t failed = 0;
        uint32_t uploads = 0;
        uint32_t evictions = 0;
        uint32_t prefetched = 0;
        uint32_t gpuProduced = 0;
        uint32_t gpuFailed = 0;
        uint32_t pending = 0;
        uint32_t residentTiles = 0;
        uint32_t gpuLayers = 0;
        uint64_t gpuBytes = 0;
    };

    ~TerrainMaterialTileStreamer();

    TerrainMaterialTileStreamer() = default;
    TerrainMaterialTileStreamer(const TerrainMaterialTileStreamer&) = delete;
    TerrainMaterialTileStreamer& operator=(const TerrainMaterialTileStreamer&) = delete;

    bool init(const asset::ImageAsset& albedo, const asset::ImageAsset& normal, float originX, float originZ,
              float worldSize, uint32_t tileSize, uint32_t capacity, const OrthoParams& ortho,
              gfx::ResourceManager& resources, std::string* error = nullptr);
    void close();

    bool loaded() const { return m_loaded; }
    bool request(int level, int tx, int ty);
    bool prefetch(int level, int tx, int ty);
    uint32_t freeSlots() const;
    bool bind(int level, int tx, int ty, Binding& binding);
    void poll();
    void refreshTileMap(float splitDistance, float cameraX, float cameraZ, int maxLevel);

    bool quadtreeActive() const { return m_tileMapEnabled; }
    gfx::TextureHandle tileMap() const { return m_tileMapTexture; }
    uint32_t contentSize() const { return m_contentSize; }
    uint32_t textureSize() const { return m_textureSize; }

    Stats stats() const;
    gfx::TextureHandle texture() const { return m_gpu.texture(); }
    const GpuTileStorage& gpuStorage() const { return m_gpu; }
    uint32_t tileSize() const { return m_tileSize; }

  private:
    struct TileRequest {
        uint64_t key = 0;
        int level = 0;
        int tx = 0;
        int ty = 0;
    };

    struct TileResult {
        uint64_t key = 0;
        int level = 0;
        int tx = 0;
        int ty = 0;
        bool ok = false;
        std::vector<uint8_t> pixels;
    };

    struct GpuSlot {
        uint64_t key = 0;
        uint32_t slot = 0;
    };

    static uint64_t tileKey(int level, int tx, int ty);
    static uint32_t tileMapKey(int level, int tx, int ty);
    void makeBinding(int level, int tx, int ty, uint32_t layer, Binding& binding) const;
    void applyOrtho(int level, float worldX, float worldZ, uint8_t* color) const;
    void run();
    bool produceTile(const TileRequest& request, TileResult& result) const;
    bool uploadMipChain(uint32_t layer, const uint8_t* base);
    bool initGpuOrtho();
    void shutdownGpuOrtho();
    bool renderGpuTile(const TileResult& result, uint32_t slot, uint32_t parentSlot, uint32_t viewOrdinal);
    int assignGpuSlot(const TileResult& result, uint32_t viewOrdinal, bool& gpuAttempted);
    static void sampleImage(const asset::ImageAsset& image, float u, float v, uint8_t* out);

    const asset::ImageAsset* m_albedo = nullptr;
    const asset::ImageAsset* m_normal = nullptr;
    float m_originX = 0.0f;
    float m_originZ = 0.0f;
    float m_worldSize = 0.0f;
    uint32_t m_tileSize = 0;
    uint32_t m_contentSize = 0;
    uint32_t m_textureSize = 0;
    uint32_t m_capacity = 0;
    OrthoParams m_ortho;
    bool m_loaded = false;

    gfx::ResourceManager* m_resources = nullptr;
    GpuTileStorage m_gpu;

    bool m_gpuOrtho = false;
    gfx::ProgramHandle m_orthoProgram;
    gfx::BufferHandle m_orthoVertexBuffer;
    gfx::TextureHandle m_orthoSourceTexture;
    gfx::UniformHandle m_orthoParentSampler;
    gfx::UniformHandle m_orthoSourceSampler;
    gfx::UniformHandle m_orthoNoiseSampler;
    gfx::UniformHandle m_orthoTransformUniform;
    gfx::UniformHandle m_orthoCoarseUniform;
    gfx::UniformHandle m_orthoInfoUniform;
    gfx::UniformHandle m_orthoFlagsUniform;
    gfx::UniformHandle m_orthoColorUniform;
    gfx::UniformHandle m_orthoRootUniform;
    gfx::UniformHandle m_orthoNoiseUniform;
    gfx::TextureHandle m_orthoNoiseTexture;
    gfx::TextureHandle m_orthoScratchTexture;
    gfx::TextureHandle m_tileMapTexture;
    std::vector<uint8_t> m_tileMapPixels;
    bool m_tileMapEnabled = false;
    bool m_tileMapDirty = false;
    bool m_tileMapCollision = false;
    uint32_t m_tileMapLoggedEntries = 0xffffffffu;
    float m_tileMapCameraX = 0.0f;
    float m_tileMapCameraZ = 0.0f;
    float m_tileMapSplitDistance = -1.0f;
    int m_tileMapMaxLevel = -1;
    bgfx::FrameBufferHandle m_orthoScratchFramebuffer{bgfx::kInvalidHandle};

    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<TileRequest> m_queue;
    std::unordered_set<uint64_t> m_pending;
    std::unordered_map<uint64_t, uint32_t> m_ready;
    std::unordered_set<uint64_t> m_failed;
    std::vector<TileResult> m_completed;
    bool m_stop = false;
    std::thread m_worker;
    Stats m_stats;

    std::list<GpuSlot> m_lru;
    std::unordered_map<uint64_t, std::list<GpuSlot>::iterator> m_lruIndex;
    std::vector<uint8_t> m_slotUsed;
    uint32_t m_freeSlots = 0;
};

} // namespace hbg::render

