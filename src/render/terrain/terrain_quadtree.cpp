#include "render/terrain/terrain_quadtree.h"

#include "render/culling/frustum.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hbg::render {
namespace {

constexpr float kSplitDistanceMin = 1.1f;
constexpr float kReferenceFov = 1.3962634f;
constexpr float kReferenceViewportHeight = 1024.0f;

Bounds nodeBounds(const TerrainQuadNode& node) {
    Bounds bounds;
    bounds.min = Vec3(node.ox, node.zmin, node.oy);
    bounds.max = Vec3(node.xmax(), node.zmax, node.ymax());
    return bounds;
}

} // namespace

void TerrainQuadTree::init(float ox, float oy, float size, float zmin, float zmax,
                           const TerrainQuadTreeSettings& settings) {
    m_settings = settings;
    m_settings.maxLevel = std::max(0, m_settings.maxLevel);
    m_settings.splitFactor = std::max(0.0f, m_settings.splitFactor);
    m_splitDistance = computeSplitDistance(m_settings);

    m_root = std::make_unique<TerrainQuadNode>();
    m_root->level = 0;
    m_root->tx = 0;
    m_root->ty = 0;
    m_root->ox = ox;
    m_root->oy = oy;
    m_root->size = size;
    m_root->zmin = zmin;
    m_root->zmax = zmax;
    applyHeightRange(*m_root);

    m_stats = {};
}

uint64_t TerrainQuadTree::nodeKey(int level, int tx, int ty) {
    return (static_cast<uint64_t>(level) << 32) | (static_cast<uint64_t>(tx) << 16) |
           static_cast<uint64_t>(ty);
}

void TerrainQuadTree::clearNodeHeightRanges() {
    m_heightRanges.clear();
}

void TerrainQuadTree::setNodeHeightRange(int level, int tx, int ty, float zmin, float zmax) {
    if (zmin > zmax) {
        std::swap(zmin, zmax);
    }
    m_heightRanges[nodeKey(level, tx, ty)] = {zmin, zmax};
    if (m_root && m_root->level == level && m_root->tx == tx && m_root->ty == ty) {
        m_root->zmin = zmin;
        m_root->zmax = zmax;
    }
}

void TerrainQuadTree::setHeightRangeProvider(
    std::function<bool(int, int, int, float&, float&)> provider) {
    m_heightProvider = std::move(provider);
}

void TerrainQuadTree::applyHeightRange(TerrainQuadNode& node) const {
    if (m_heightProvider) {
        float zmin = node.zmin;
        float zmax = node.zmax;
        if (m_heightProvider(node.level, node.tx, node.ty, zmin, zmax)) {
            if (zmin > zmax) {
                std::swap(zmin, zmax);
            }
            node.zmin = zmin;
            node.zmax = zmax;
            return;
        }
    }
    const auto found = m_heightRanges.find(nodeKey(node.level, node.tx, node.ty));
    if (found != m_heightRanges.end()) {
        node.zmin = found->second.first;
        node.zmax = found->second.second;
    }
}

float TerrainQuadTree::computeSplitDistance(const TerrainQuadTreeSettings& settings) {
    const float fov = std::max(settings.fovYRadians, 0.0001f);
    const float viewportHeight = std::max(settings.viewportHeight, 1.0f);
    float distance = settings.splitFactor * (viewportHeight / kReferenceViewportHeight) *
                     std::tan(kReferenceFov * 0.5f) / std::tan(fov * 0.5f);
    if (!std::isfinite(distance) || distance < kSplitDistanceMin) {
        distance = kSplitDistanceMin;
    }
    return distance;
}

float TerrainQuadTree::distanceToQuad(const TerrainQuadNode& node, const Vec3& cameraPosition, float groundHeight) {
    const float zmax = std::max(0.0f, groundHeight);
    const float dz = std::abs(cameraPosition.y - zmax);
    const float dx =
        std::min(std::abs(cameraPosition.x - node.ox), std::abs(cameraPosition.x - node.xmax()));
    const float dz2 = std::min(std::abs(cameraPosition.z - node.oy), std::abs(cameraPosition.z - node.ymax()));
    return std::max(dz, std::max(dx, dz2));
}

void TerrainQuadTree::update(const Vec3& cameraPosition, float groundHeight,
                             const std::array<Vec4, 6>* frustumPlanes,
                             const Vec3* cameraForward) {
    if (!m_root) {
        return;
    }
    beginHorizon(cameraPosition, cameraForward);
    updateNode(*m_root, cameraPosition, groundHeight, frustumPlanes);
    m_stats = {};
    m_stats.horizonActive = m_horizonActive;
    count(*m_root);
}

void TerrainQuadTree::updateNode(TerrainQuadNode& node, const Vec3& cameraPosition, float groundHeight,
                                 const std::array<Vec4, 6>* frustumPlanes) {
    node.horizonCulled = false;
    if (!m_horizonActive) {
        node.occluded = false;
    }
    applyHeightRange(node);
    node.visible = frustumPlanes == nullptr || intersectsFrustum(*frustumPlanes, nodeBounds(node));

    if (!node.visible && !m_settings.splitInvisibleQuads) {
        for (auto& child : node.children) {
            child.reset();
        }
        return;
    }

    if (m_horizonActive && node.occluded) {
        node.occluded = isOccluded(node, cameraPosition);
        if (node.occluded) {
            node.visible = false;
            node.horizonCulled = true;
        }
    }

    const float distance = distanceToQuad(node, cameraPosition, groundHeight);
    const bool shouldSplit = node.level < m_settings.maxLevel && distance < node.size * m_splitDistance &&
                             (m_settings.splitInvisibleQuads || node.visible);

    if (shouldSplit) {
        if (node.isLeaf()) {
            subdivide(node);
        }
        setChildOrder(node, cameraPosition);
        for (const uint8_t order : node.childOrder) {
            updateNode(*node.children[order], cameraPosition, groundHeight, frustumPlanes);
        }
        node.occluded = node.children[0]->occluded && node.children[1]->occluded &&
                        node.children[2]->occluded && node.children[3]->occluded;
    } else {
        if (node.visible) {
            node.occluded = addOccluder(node, cameraPosition);
            if (node.occluded) {
                node.visible = false;
                node.horizonCulled = true;
            }
        }
        for (auto& child : node.children) {
            child.reset();
        }
    }
}

void TerrainQuadTree::beginHorizon(const Vec3& cameraPosition, const Vec3* cameraForward) {
    m_horizonActive = false;
    if (!m_settings.horizonCulling || cameraForward == nullptr || !m_root) {
        return;
    }
    if (cameraPosition.y > m_root->zmax) {
        return;
    }
    const float length = std::sqrt(cameraForward->x * cameraForward->x + cameraForward->z * cameraForward->z);
    if (length < 1e-6f) {
        return;
    }
    const float forwardX = cameraForward->x / length;
    const float forwardZ = cameraForward->z / length;
    m_horizonDirX = -forwardX;
    m_horizonDirZ = -forwardZ;
    m_horizon.fill(-std::numeric_limits<float>::infinity());
    m_horizonActive = true;
}

bool TerrainQuadTree::projectOccluder(const TerrainQuadNode& node, const Vec3& cameraPosition,
                                      float* xmin, float* xmax, float* zmin, float* zmax) const {
    const float cornersX[4] = {node.ox, node.ox, node.xmax(), node.xmax()};
    const float cornersZ[4] = {node.oy, node.ymax(), node.oy, node.ymax()};
    float projectedX[4];
    float depth[4];
    for (int index = 0; index < 4; ++index) {
        const float dx = cornersX[index] - cameraPosition.x;
        const float dz = cornersZ[index] - cameraPosition.z;
        projectedX[index] = m_horizonDirZ * dx - m_horizonDirX * dz;
        depth[index] = -m_horizonDirX * dx - m_horizonDirZ * dz;
        if (depth[index] <= 0.0f) {
            return false;
        }
    }

    const float dzmin = node.zmin - cameraPosition.y;
    const float dzmax = node.zmax - cameraPosition.y;
    float rawXmin = projectedX[0] / depth[0];
    float rawXmax = rawXmin;
    float rawZmin = dzmin / depth[0];
    float rawZmax = dzmax / depth[0];
    for (int index = 1; index < 4; ++index) {
        const float x = projectedX[index] / depth[index];
        rawXmin = std::min(rawXmin, x);
        rawXmax = std::max(rawXmax, x);
        rawZmin = std::min(rawZmin, dzmin / depth[index]);
        rawZmax = std::max(rawZmax, dzmax / depth[index]);
    }

    *xmin = rawXmin * 0.33f + 0.5f;
    *xmax = rawXmax * 0.33f + 0.5f;
    *zmin = rawZmin;
    *zmax = rawZmax;
    return true;
}

bool TerrainQuadTree::addOccluder(const TerrainQuadNode& node, const Vec3& cameraPosition) {
    if (!m_horizonActive) {
        return false;
    }

    float xmin = 0.0f;
    float xmax = 0.0f;
    float zmin = 0.0f;
    float zmax = 0.0f;
    if (!projectOccluder(node, cameraPosition, &xmin, &xmax, &zmin, &zmax)) {
        return false;
    }

    int imin = std::clamp(static_cast<int>(std::floor(xmin * kHorizonSize)), 0, kHorizonSize - 1);
    int imax = std::clamp(static_cast<int>(std::ceil(xmax * kHorizonSize)), 0, kHorizonSize - 1);

    bool occluded = imax >= imin;
    if (occluded) {
        for (int index = imin; index <= imax; ++index) {
            if (zmax > m_horizon[index]) {
                occluded = false;
                break;
            }
        }
    }

    if (!occluded) {
        imin = std::clamp(static_cast<int>(std::ceil(xmin * kHorizonSize)), 0, kHorizonSize - 1);
        imax = std::clamp(static_cast<int>(std::floor(xmax * kHorizonSize)), 0, kHorizonSize - 1);
        for (int index = imin; index <= imax; ++index) {
            m_horizon[index] = std::max(m_horizon[index], zmin);
        }
    }
    return occluded;
}

bool TerrainQuadTree::isOccluded(const TerrainQuadNode& node, const Vec3& cameraPosition) const {
    if (!m_horizonActive) {
        return false;
    }

    float xmin = 0.0f;
    float xmax = 0.0f;
    float zmin = 0.0f;
    float zmax = 0.0f;
    if (!projectOccluder(node, cameraPosition, &xmin, &xmax, &zmin, &zmax)) {
        return false;
    }

    const int imin = std::clamp(static_cast<int>(std::floor(xmin * kHorizonSize)), 0, kHorizonSize - 1);
    const int imax = std::clamp(static_cast<int>(std::ceil(xmax * kHorizonSize)), 0, kHorizonSize - 1);
    for (int index = imin; index <= imax; ++index) {
        if (zmax > m_horizon[index]) {
            return false;
        }
    }
    return imax >= imin;
}

void TerrainQuadTree::subdivide(TerrainQuadNode& node) {
    const float half = node.size * 0.5f;
    node.children[0] = std::make_unique<TerrainQuadNode>();
    node.children[0]->parent = &node;
    node.children[0]->level = node.level + 1;
    node.children[0]->tx = node.tx * 2;
    node.children[0]->ty = node.ty * 2;
    node.children[0]->ox = node.ox;
    node.children[0]->oy = node.oy;
    node.children[0]->size = half;

    node.children[1] = std::make_unique<TerrainQuadNode>();
    node.children[1]->parent = &node;
    node.children[1]->level = node.level + 1;
    node.children[1]->tx = node.tx * 2 + 1;
    node.children[1]->ty = node.ty * 2;
    node.children[1]->ox = node.ox + half;
    node.children[1]->oy = node.oy;
    node.children[1]->size = half;

    node.children[2] = std::make_unique<TerrainQuadNode>();
    node.children[2]->parent = &node;
    node.children[2]->level = node.level + 1;
    node.children[2]->tx = node.tx * 2;
    node.children[2]->ty = node.ty * 2 + 1;
    node.children[2]->ox = node.ox;
    node.children[2]->oy = node.oy + half;
    node.children[2]->size = half;

    node.children[3] = std::make_unique<TerrainQuadNode>();
    node.children[3]->parent = &node;
    node.children[3]->level = node.level + 1;
    node.children[3]->tx = node.tx * 2 + 1;
    node.children[3]->ty = node.ty * 2 + 1;
    node.children[3]->ox = node.ox + half;
    node.children[3]->oy = node.oy + half;
    node.children[3]->size = half;

    for (auto& child : node.children) {
        child->zmin = node.zmin;
        child->zmax = node.zmax;
        applyHeightRange(*child);
    }
}

void TerrainQuadTree::setChildOrder(TerrainQuadNode& node, const Vec3& cameraPosition) {
    const float centerX = node.ox + node.size * 0.5f;
    const float centerZ = node.oy + node.size * 0.5f;
    if (cameraPosition.z < centerZ) {
        if (cameraPosition.x < centerX) {
            node.childOrder = {0, 1, 2, 3};
        } else {
            node.childOrder = {1, 0, 3, 2};
        }
    } else {
        if (cameraPosition.x < centerX) {
            node.childOrder = {2, 0, 3, 1};
        } else {
            node.childOrder = {3, 1, 2, 0};
        }
    }
}

void TerrainQuadTree::count(const TerrainQuadNode& node) {
    ++m_stats.nodeCount;
    m_stats.maxDepth = std::max(m_stats.maxDepth, static_cast<uint32_t>(node.level));
    if (node.isLeaf()) {
        ++m_stats.leafCount;
        if (node.visible) {
            ++m_stats.visibleLeafCount;
        } else if (node.horizonCulled) {
            ++m_stats.horizonCulledLeafCount;
        }
        return;
    }
    for (const auto& child : node.children) {
        count(*child);
    }
}

void TerrainQuadTree::collectLeaves(std::vector<const TerrainQuadNode*>& leaves) const {
    leaves.clear();
    if (!m_root) {
        return;
    }
    collectLeavesRecursive(*m_root, leaves);
}

void TerrainQuadTree::collectLeavesRecursive(const TerrainQuadNode& node,
                                             std::vector<const TerrainQuadNode*>& leaves) const {
    if (node.isLeaf()) {
        leaves.push_back(&node);
        return;
    }
    for (const uint8_t order : node.childOrder) {
        collectLeavesRecursive(*node.children[order], leaves);
    }
}

} // namespace hbg::render
