#pragma once

#include "core/math.h"
#include "gfx/resource_manager.h"
#include "render/scene/render_scene.h"
#include "render/terrain/heightmap_tile_cache.h"
#include "render/terrain/heightmap_tile_streamer.h"
#include "render/terrain/terrain_material_tile_streamer.h"
#include "render/terrain/terrain_quadtree.h"

#include <array>
#include <string>
#include <unordered_map>

namespace hbg::render {

class TerrainLodController {
  public:
    ~TerrainLodController();
    bool load(const std::string& scenePath, RenderScene& scene, gfx::ResourceManager* resources = nullptr,
              std::string* error = nullptr);
    void update(RenderScene& scene, const Vec3& cameraPosition,
                const std::array<Vec4, 6>* frustumPlanes = nullptr,
                const Vec3* cameraForward = nullptr);
    void shutdown();

    bool active() const { return m_active; }
    const TerrainQuadTreeStats& stats() const { return m_tree.stats(); }
    uint32_t visibleObjects() const { return m_visibleObjects; }
    float groundHeight() const { return m_groundHeight; }
    const HeightmapTileCache& heightmapCache() const { return m_heightmapCache; }
    const HeightmapTileStreamer::Stats tileStats() const { return m_tileStreamer.stats(); }
    bool asyncTilesActive() const { return m_asyncTiles; }
    bool asyncGpuActive() const { return m_tileStreamer.gpuStorage().valid(); }
    bool materialTilesActive() const { return m_materialTilesActive; }
    bool sharedGeometryActive() const { return m_sharedGeometry; }
    const TerrainMaterialTileStreamer::Stats materialTileStats() const { return m_materialTiles.stats(); }
    const std::string& lastError() const { return m_lastError; }

  private:
    static std::string nodeName(const std::string& prefix, int level, int tx, int ty);

    bool m_active = false;
    bool m_asyncTiles = false;
    TerrainQuadTree m_tree;
    HeightmapTileCache m_heightmapCache;
    HeightmapTileCache m_heightmapSampler;
    HeightmapTileStreamer m_tileStreamer;
    TerrainMaterialTileStreamer m_materialTiles;
    std::string m_prefix = "TerrainLOD_";
    std::string m_lastError;
    gfx::ResourceManager* m_resources = nullptr;
    gfx::TextureHandle m_terrainHeightTexture;
    gfx::TextureHandle m_vegetationAtlasTexture;
    bool m_sharedGeometry = false;
    float m_skirtDepth = 0.0f;
    float m_groundHeight = 0.0f;
    Vec3 m_lastGroundCamera{};
    bool m_groundHeightSampled = false;
    Vec3 m_lastPrefetchCamera{};
    bool m_prefetchCameraInitialized = false;
    float m_worldSize = 0.0f;
    float m_originX = 0.0f;
    float m_originZ = 0.0f;
    int m_maxLevel = 0;
    bool m_frustumCulling = true;
    bool m_materialTilesActive = false;
    uint32_t m_visibleObjects = 0;
    std::unordered_map<std::string, uint32_t> m_objectByName;
};

} // namespace hbg::render
