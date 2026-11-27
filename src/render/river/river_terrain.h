#pragma once

#include "asset/mesh.h"
#include "render/river/hydro_flow.h"
#include "render/river/river_mesh.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace hbg::render {

struct RiverTerrainSettings {
    bool enabled = false;
    bool fillBed = true;
    std::string meshName;
    float bedDepth = 1.5f;
    float bedWidthFactor = 0.9f;
    float bankWidthFactor = 2.2f;
    uint32_t residualTiles = 4;
    uint32_t meshIndex = UINT32_MAX;
    uint32_t objectIndex = UINT32_MAX;
};

struct RiverTerrainResidualSettings {
    uint32_t leafTiles = 4;
};

struct RiverTerrainResidualStats {
    uint32_t gridWidth = 0;
    uint32_t gridHeight = 0;
    uint32_t leafTiles = 0;
    uint32_t levels = 0;
    uint32_t samplesPerTile = 0;
    uint32_t modifiedLeafTiles = 0;
    uint32_t modifiedParentTiles = 0;
    float reconstructedMin = 0.0f;
    float reconstructedMax = 0.0f;
    float maxError = 0.0f;
};

class RiverTerrainResidualCache {
public:
    bool initialize(const std::vector<asset::MeshVertex>& baseVertices,
                    const RiverTerrainResidualSettings& settings = {});
    void reset();

    bool valid() const { return m_valid; }
    const RiverTerrainResidualStats& stats() const { return m_stats; }

    bool readBack(const std::vector<asset::MeshVertex>& baseVertices,
                  const std::vector<asset::MeshVertex>& deformedVertices, const std::vector<float>& exactDelta,
                  std::vector<float>& delta);

    float sampleDelta(float x, float z, uint32_t level) const;
    float sampleDeltaWithFallback(float x, float z, uint32_t level) const;

private:
    struct Tile {
        std::vector<float> values;
    };

    struct TileKey {
        uint32_t level = 0;
        uint32_t tx = 0;
        uint32_t ty = 0;
    };

    static uint64_t tileKey(uint32_t level, uint32_t tx, uint32_t ty) {
        return (static_cast<uint64_t>(level) << 32) | (static_cast<uint64_t>(tx) << 16) |
               static_cast<uint64_t>(ty);
    }

    static TileKey decodeKey(uint64_t key) {
        return TileKey{static_cast<uint32_t>(key >> 32), static_cast<uint32_t>((key >> 16) & 0xffffu),
                       static_cast<uint32_t>(key & 0xffffu)};
    }

    uint32_t tilesPerAxis(uint32_t level) const { return 1u << level; }
    uint32_t cellsPerTile(uint32_t level) const { return m_cells >> level; }
    float sampleTile(const Tile& tile, float localX, float localY, uint32_t level) const;
    void rebuildParents();

    bool m_valid = false;
    uint32_t m_columns = 0;
    uint32_t m_rows = 0;
    uint32_t m_cells = 0;
    uint32_t m_leafCells = 0;
    uint32_t m_samples = 0;
    uint32_t m_maxLevel = 0;
    float m_minX = 0.0f;
    float m_minZ = 0.0f;
    float m_spacingX = 0.0f;
    float m_spacingZ = 0.0f;
    std::vector<std::unordered_map<uint64_t, Tile>> m_levels;
    RiverTerrainResidualStats m_stats;
};

bool computeRiverTerrainDelta(const RiverGraph& graph, const RiverMeshSettings& mesh,
                              const RiverTerrainSettings& settings, const std::vector<asset::MeshVertex>& baseVertices,
                              std::vector<float>& delta);

bool applyRiverTerrainDelta(const std::vector<asset::MeshVertex>& baseVertices, const std::vector<float>& delta,
                            const std::vector<uint32_t>& indices, std::vector<asset::MeshVertex>& vertices);

bool deformRiverTerrain(const RiverGraph& graph, const RiverMeshSettings& mesh, const RiverTerrainSettings& settings,
                        const std::vector<asset::MeshVertex>& baseVertices, const std::vector<uint32_t>& indices,
                        std::vector<asset::MeshVertex>& vertices);
} // namespace hbg::render
