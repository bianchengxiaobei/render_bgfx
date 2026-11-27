#pragma once

#include "core/math.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hbg::render {

struct TerrainQuadNode {
    TerrainQuadNode* parent = nullptr;
    int level = 0;
    int tx = 0;
    int ty = 0;
    float ox = 0.0f;
    float oy = 0.0f;
    float size = 0.0f;
    float zmin = 0.0f;
    float zmax = 0.0f;
    bool visible = true;
    bool occluded = false;
    bool horizonCulled = false;
    std::array<uint8_t, 4> childOrder{0, 1, 2, 3};
    std::array<std::unique_ptr<TerrainQuadNode>, 4> children;

    bool isLeaf() const { return children[0] == nullptr; }
    float xmax() const { return ox + size; }
    float ymax() const { return oy + size; }
};

struct TerrainQuadTreeSettings {
    float splitFactor = 2.0f;
    int maxLevel = 8;
    float viewportHeight = 1080.0f;
    float fovYRadians = 0.959931f;
    bool splitInvisibleQuads = false;
    bool horizonCulling = true;
};

struct TerrainQuadTreeStats {
    uint32_t nodeCount = 0;
    uint32_t leafCount = 0;
    uint32_t visibleLeafCount = 0;
    uint32_t horizonCulledLeafCount = 0;
    uint32_t maxDepth = 0;
    bool horizonActive = false;
};

class TerrainQuadTree {
  public:
    void init(float ox, float oy, float size, float zmin, float zmax, const TerrainQuadTreeSettings& settings);

    void update(const Vec3& cameraPosition, float groundHeight,
                const std::array<Vec4, 6>* frustumPlanes = nullptr,
                const Vec3* cameraForward = nullptr);

    void clearNodeHeightRanges();
    void setNodeHeightRange(int level, int tx, int ty, float zmin, float zmax);
    void setHeightRangeProvider(
        std::function<bool(int, int, int, float&, float&)> provider);

    const TerrainQuadNode& root() const { return *m_root; }
    TerrainQuadNode& root() { return *m_root; }

    const TerrainQuadTreeSettings& settings() const { return m_settings; }
    float splitDistance() const { return m_splitDistance; }
    const TerrainQuadTreeStats& stats() const { return m_stats; }

    void collectLeaves(std::vector<const TerrainQuadNode*>& leaves) const;
    static float computeSplitDistance(const TerrainQuadTreeSettings& settings);
    static float distanceToQuad(const TerrainQuadNode& node, const Vec3& cameraPosition, float groundHeight);

  private:
    static constexpr int kHorizonSize = 256;

    void updateNode(TerrainQuadNode& node, const Vec3& cameraPosition, float groundHeight,
                    const std::array<Vec4, 6>* frustumPlanes);
    void subdivide(TerrainQuadNode& node);
    void applyHeightRange(TerrainQuadNode& node) const;
    void setChildOrder(TerrainQuadNode& node, const Vec3& cameraPosition);
    void beginHorizon(const Vec3& cameraPosition, const Vec3* cameraForward);
    bool addOccluder(const TerrainQuadNode& node, const Vec3& cameraPosition);
    bool isOccluded(const TerrainQuadNode& node, const Vec3& cameraPosition) const;
    bool projectOccluder(const TerrainQuadNode& node, const Vec3& cameraPosition,
                         float* xmin, float* xmax, float* zmin, float* zmax) const;
    void collectLeavesRecursive(const TerrainQuadNode& node,
                                std::vector<const TerrainQuadNode*>& leaves) const;
    void count(const TerrainQuadNode& node);
    static uint64_t nodeKey(int level, int tx, int ty);

    std::unique_ptr<TerrainQuadNode> m_root;
    TerrainQuadTreeSettings m_settings;
    float m_splitDistance = 1.1f;
    TerrainQuadTreeStats m_stats;
    std::unordered_map<uint64_t, std::pair<float, float>> m_heightRanges;
    std::function<bool(int, int, int, float&, float&)> m_heightProvider;
    std::array<float, kHorizonSize> m_horizon{};
    float m_horizonDirX = 0.0f;
    float m_horizonDirZ = 0.0f;
    bool m_horizonActive = false;
};

} // namespace hbg::render
