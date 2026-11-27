#include "render/river/river_terrain.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_set>

namespace hbg::render {
namespace {

Vec2 subtract(Vec2 lhs, Vec2 rhs) {
    return Vec2(lhs.x - rhs.x, lhs.y - rhs.y);
}

Vec2 add(Vec2 lhs, Vec2 rhs) {
    return Vec2(lhs.x + rhs.x, lhs.y + rhs.y);
}

Vec2 scale(Vec2 value, float factor) {
    return Vec2(value.x * factor, value.y * factor);
}

float dot(Vec2 lhs, Vec2 rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y;
}

float lengthSq(Vec2 value) {
    return dot(value, value);
}

float pointSegmentDistanceSq(Vec2 point, Vec2 start, Vec2 end) {
    const Vec2 segment = subtract(end, start);
    const float segmentLengthSq = dot(segment, segment);
    if (segmentLengthSq <= 1.0e-12f) {
        return lengthSq(subtract(point, start));
    }
    const float t = std::clamp(dot(subtract(point, start), segment) / segmentLengthSq, 0.0f, 1.0f);
    return lengthSq(subtract(point, add(start, scale(segment, t))));
}

float distanceToAxis(const std::vector<Vec2>& axis, Vec2 position) {
    if (axis.empty()) {
        return std::numeric_limits<float>::infinity();
    }
    if (axis.size() == 1) {
        return std::sqrt(lengthSq(subtract(position, axis.front())));
    }
    float best = std::numeric_limits<float>::max();
    for (std::size_t index = 1; index < axis.size(); ++index) {
        best = std::min(best, pointSegmentDistanceSq(position, axis[index - 1], axis[index]));
    }
    return std::sqrt(best);
}

float smoothStep01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Vec3 subtract3(const Vec3& lhs, const Vec3& rhs) {
    return Vec3(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z);
}

Vec3 scale3(const Vec3& value, float factor) {
    return Vec3(value.x * factor, value.y * factor, value.z * factor);
}

float dot3(const Vec3& lhs, const Vec3& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Vec3 crossProduct(const Vec3& lhs, const Vec3& rhs) {
    return Vec3(lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z, lhs.x * rhs.y - lhs.y * rhs.x);
}

void addTo(Vec3& target, const Vec3& value) {
    target.x += value.x;
    target.y += value.y;
    target.z += value.z;
}

Vec3 normalize3(const Vec3& value, const Vec3& fallback) {
    const float magnitude = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (magnitude <= 1.0e-8f) {
        return fallback;
    }
    return Vec3(value.x / magnitude, value.y / magnitude, value.z / magnitude);
}

} // namespace
void RiverTerrainResidualCache::reset() {
    m_valid = false;
    m_columns = 0;
    m_rows = 0;
    m_cells = 0;
    m_leafCells = 0;
    m_samples = 0;
    m_maxLevel = 0;
    m_minX = 0.0f;
    m_minZ = 0.0f;
    m_spacingX = 0.0f;
    m_spacingZ = 0.0f;
    m_levels.clear();
    m_stats = RiverTerrainResidualStats{};
}

bool RiverTerrainResidualCache::initialize(const std::vector<asset::MeshVertex>& baseVertices,
                                           const RiverTerrainResidualSettings& settings) {
    reset();
    if (baseVertices.size() < 4) {
        return false;
    }

    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();
    for (const asset::MeshVertex& vertex : baseVertices) {
        minX = std::min(minX, vertex.position.x);
        maxX = std::max(maxX, vertex.position.x);
        minZ = std::min(minZ, vertex.position.z);
        maxZ = std::max(maxZ, vertex.position.z);
    }

    const float firstZ = baseVertices.front().position.z;
    uint32_t columns = 0;
    while (columns < baseVertices.size() && std::abs(baseVertices[columns].position.z - firstZ) <= 1.0e-4f) {
        ++columns;
    }
    if (columns < 2 || baseVertices.size() % columns != 0) {
        return false;
    }
    const uint32_t rows = static_cast<uint32_t>(baseVertices.size() / columns);
    if (rows < 2 || rows != columns) {
        return false;
    }

    const uint32_t cells = columns - 1;
    const float spacingX = (maxX - minX) / static_cast<float>(cells);
    const float spacingZ = (maxZ - minZ) / static_cast<float>(cells);
    if (spacingX <= 1.0e-6f || spacingZ <= 1.0e-6f) {
        return false;
    }
    const float tolerance = std::max(1.0e-3f, std::max(spacingX, spacingZ) * 0.01f);
    for (uint32_t row = 0; row < rows; ++row) {
        for (uint32_t column = 0; column < columns; ++column) {
            const asset::MeshVertex& vertex = baseVertices[static_cast<std::size_t>(row) * columns + column];
            const float expectedX = minX + static_cast<float>(column) * spacingX;
            const float expectedZ = minZ + static_cast<float>(row) * spacingZ;
            if (std::abs(vertex.position.x - expectedX) > tolerance ||
                std::abs(vertex.position.z - expectedZ) > tolerance) {
                return false;
            }
        }
    }

    const uint32_t requestedTiles = std::clamp(settings.leafTiles, 1u, 4u);
    uint32_t leafTiles = 1;
    for (uint32_t candidate = 2; candidate <= requestedTiles; candidate *= 2) {
        if (cells % candidate == 0) {
            leafTiles = candidate;
        }
    }

    m_columns = columns;
    m_rows = rows;
    m_cells = cells;
    m_leafCells = cells / leafTiles;
    m_samples = m_leafCells + 1;
    m_maxLevel = 0;
    while ((1u << (m_maxLevel + 1)) <= leafTiles) {
        ++m_maxLevel;
    }
    m_minX = minX;
    m_minZ = minZ;
    m_spacingX = spacingX;
    m_spacingZ = spacingZ;
    m_levels.assign(m_maxLevel + 1, {});
    m_stats = RiverTerrainResidualStats{};
    m_stats.gridWidth = columns;
    m_stats.gridHeight = rows;
    m_stats.leafTiles = leafTiles;
    m_stats.levels = m_maxLevel + 1;
    m_stats.samplesPerTile = m_samples;
    m_valid = true;
    return true;
}

float RiverTerrainResidualCache::sampleTile(const Tile& tile, float localX, float localY, uint32_t level) const {
    if (!m_valid || m_samples < 2 || tile.values.size() != static_cast<std::size_t>(m_samples) * m_samples) {
        return 0.0f;
    }
    const float step = static_cast<float>(cellsPerTile(level)) / static_cast<float>(m_samples - 1);
    if (step <= 0.0f) {
        return 0.0f;
    }
    const float sampleX = localX / step;
    const float sampleY = localY / step;
    const int x0 = std::clamp(static_cast<int>(std::floor(sampleX)), 0, static_cast<int>(m_samples) - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(sampleY)), 0, static_cast<int>(m_samples) - 1);
    const int x1 = std::min(x0 + 1, static_cast<int>(m_samples) - 1);
    const int y1 = std::min(y0 + 1, static_cast<int>(m_samples) - 1);
    const float tx = std::clamp(sampleX - static_cast<float>(x0), 0.0f, 1.0f);
    const float ty = std::clamp(sampleY - static_cast<float>(y0), 0.0f, 1.0f);
    const auto at = [&](int x, int y) {
        return tile.values[static_cast<std::size_t>(y) * m_samples + static_cast<std::size_t>(x)];
    };
    const float v00 = at(x0, y0);
    const float v10 = at(x1, y0);
    const float v01 = at(x0, y1);
    const float v11 = at(x1, y1);
    const float top = v00 + (v10 - v00) * tx;
    const float bottom = v01 + (v11 - v01) * tx;
    return top + (bottom - top) * ty;
}

float RiverTerrainResidualCache::sampleDelta(float x, float z, uint32_t level) const {
    if (!m_valid || level > m_maxLevel || m_spacingX <= 0.0f || m_spacingZ <= 0.0f) {
        return 0.0f;
    }
    float gridX = (x - m_minX) / m_spacingX;
    float gridZ = (z - m_minZ) / m_spacingZ;
    if (gridX < -0.001f || gridX > static_cast<float>(m_cells) + 0.001f || gridZ < -0.001f ||
        gridZ > static_cast<float>(m_cells) + 0.001f) {
        return 0.0f;
    }
    gridX = std::clamp(gridX, 0.0f, static_cast<float>(m_cells));
    gridZ = std::clamp(gridZ, 0.0f, static_cast<float>(m_cells));
    const uint32_t tiles = tilesPerAxis(level);
    const uint32_t cells = cellsPerTile(level);
    const uint32_t tx = std::min(static_cast<uint32_t>(gridX) / cells, tiles - 1);
    const uint32_t ty = std::min(static_cast<uint32_t>(gridZ) / cells, tiles - 1);
    const auto& levelTiles = m_levels[level];
    const auto iterator = levelTiles.find(tileKey(level, tx, ty));
    if (iterator == levelTiles.end()) {
        return 0.0f;
    }
    return sampleTile(iterator->second, gridX - static_cast<float>(tx * cells),
                      gridZ - static_cast<float>(ty * cells), level);
}

float RiverTerrainResidualCache::sampleDeltaWithFallback(float x, float z, uint32_t level) const {
    if (!m_valid) {
        return 0.0f;
    }
    const uint32_t startLevel = std::min(level, m_maxLevel);
    for (int candidate = static_cast<int>(startLevel); candidate >= 0; --candidate) {
        const uint32_t candidateLevel = static_cast<uint32_t>(candidate);
        if (m_levels[candidateLevel].empty()) {
            continue;
        }
        float gridX = (x - m_minX) / m_spacingX;
        float gridZ = (z - m_minZ) / m_spacingZ;
        if (gridX < -0.001f || gridX > static_cast<float>(m_cells) + 0.001f || gridZ < -0.001f ||
            gridZ > static_cast<float>(m_cells) + 0.001f) {
            return 0.0f;
        }
        gridX = std::clamp(gridX, 0.0f, static_cast<float>(m_cells));
        gridZ = std::clamp(gridZ, 0.0f, static_cast<float>(m_cells));
        const uint32_t tiles = tilesPerAxis(candidateLevel);
        const uint32_t cells = cellsPerTile(candidateLevel);
        const uint32_t tx = std::min(static_cast<uint32_t>(gridX) / cells, tiles - 1);
        const uint32_t ty = std::min(static_cast<uint32_t>(gridZ) / cells, tiles - 1);
        const auto& levelTiles = m_levels[candidateLevel];
        const auto iterator = levelTiles.find(tileKey(candidateLevel, tx, ty));
        if (iterator != levelTiles.end()) {
            return sampleTile(iterator->second, gridX - static_cast<float>(tx * cells),
                              gridZ - static_cast<float>(ty * cells), candidateLevel);
        }
    }
    return 0.0f;
}

void RiverTerrainResidualCache::rebuildParents() {
    for (uint32_t level = 0; level < m_maxLevel; ++level) {
        m_levels[level].clear();
    }
    if (m_maxLevel == 0) {
        return;
    }

    for (int levelValue = static_cast<int>(m_maxLevel) - 1; levelValue >= 0; --levelValue) {
        const uint32_t level = static_cast<uint32_t>(levelValue);
        std::set<uint64_t> parentKeys;
        for (const auto& entry : m_levels[level + 1]) {
            const TileKey child = decodeKey(entry.first);
            parentKeys.insert(tileKey(level, child.tx / 2, child.ty / 2));
        }

        const uint32_t parentCells = cellsPerTile(level);
        const uint32_t childCells = cellsPerTile(level + 1);
        const uint32_t childTiles = tilesPerAxis(level + 1);
        const uint32_t step = parentCells / (m_samples - 1);
        for (uint64_t parentKey : parentKeys) {
            const TileKey parent = decodeKey(parentKey);
            Tile tile;
            tile.values.assign(static_cast<std::size_t>(m_samples) * m_samples, 0.0f);
            bool modified = false;
            for (uint32_t sampleY = 0; sampleY < m_samples; ++sampleY) {
                const uint32_t gridZ = parent.ty * parentCells + sampleY * step;
                const uint32_t childY = std::min(gridZ / childCells, childTiles - 1);
                const uint32_t localZ = gridZ - childY * childCells;
                for (uint32_t sampleX = 0; sampleX < m_samples; ++sampleX) {
                    const uint32_t gridX = parent.tx * parentCells + sampleX * step;
                    const uint32_t childX = std::min(gridX / childCells, childTiles - 1);
                    const uint32_t localX = gridX - childX * childCells;
                    const auto& childLevelTiles = m_levels[level + 1];
                    const auto child = childLevelTiles.find(tileKey(level + 1, childX, childY));
                    float value = 0.0f;
                    if (child != childLevelTiles.end()) {
                        value = sampleTile(child->second, static_cast<float>(localX), static_cast<float>(localZ),
                                           level + 1);
                    }
                    tile.values[static_cast<std::size_t>(sampleY) * m_samples + sampleX] = value;
                    modified = modified || std::abs(value) > 1.0e-6f;
                }
            }
            if (modified) {
                m_levels[level][parentKey] = std::move(tile);
            }
        }
    }
}

bool RiverTerrainResidualCache::readBack(const std::vector<asset::MeshVertex>& baseVertices,
                                         const std::vector<asset::MeshVertex>& deformedVertices,
                                         const std::vector<float>& exactDelta, std::vector<float>& delta) {
    if (!m_valid || baseVertices.size() != deformedVertices.size() ||
        baseVertices.size() != static_cast<std::size_t>(m_columns) * m_rows ||
        (!exactDelta.empty() && exactDelta.size() != baseVertices.size())) {
        return false;
    }

    const uint32_t leafTiles = tilesPerAxis(m_maxLevel);
    std::unordered_set<uint64_t> affectedLeaves;
    for (const auto& entry : m_levels[m_maxLevel]) {
        affectedLeaves.insert(entry.first);
    }

    for (std::size_t index = 0; index < baseVertices.size(); ++index) {
        if (!exactDelta.empty() && std::abs(exactDelta[index]) <= 1.0e-6f) {
            continue;
        }
        const uint32_t gridX = static_cast<uint32_t>(index % m_columns);
        const uint32_t gridZ = static_cast<uint32_t>(index / m_columns);
        const int centerX = std::min(static_cast<int>(gridX / m_leafCells), static_cast<int>(leafTiles) - 1);
        const int centerY = std::min(static_cast<int>(gridZ / m_leafCells), static_cast<int>(leafTiles) - 1);
        for (int offsetY = -1; offsetY <= 1; ++offsetY) {
            for (int offsetX = -1; offsetX <= 1; ++offsetX) {
                const int tx = centerX + offsetX;
                const int ty = centerY + offsetY;
                if (tx < 0 || ty < 0 || tx >= static_cast<int>(leafTiles) || ty >= static_cast<int>(leafTiles)) {
                    continue;
                }
                affectedLeaves.insert(tileKey(m_maxLevel, static_cast<uint32_t>(tx), static_cast<uint32_t>(ty)));
            }
        }
    }

    m_levels[m_maxLevel].clear();
    for (uint64_t key : affectedLeaves) {
        const TileKey leaf = decodeKey(key);
        Tile tile;
        tile.values.assign(static_cast<std::size_t>(m_samples) * m_samples, 0.0f);
        bool modified = false;
        for (uint32_t sampleY = 0; sampleY < m_samples; ++sampleY) {
            const uint32_t gridZ = std::min(leaf.ty * m_leafCells + sampleY, m_cells);
            for (uint32_t sampleX = 0; sampleX < m_samples; ++sampleX) {
                const uint32_t gridX = std::min(leaf.tx * m_leafCells + sampleX, m_cells);
                const std::size_t index = static_cast<std::size_t>(gridZ) * m_columns + gridX;
                const float value = deformedVertices[index].position.y - baseVertices[index].position.y;
                tile.values[static_cast<std::size_t>(sampleY) * m_samples + sampleX] = value;
                modified = modified || std::abs(value) > 1.0e-6f;
            }
        }
        if (modified) {
            m_levels[m_maxLevel][key] = std::move(tile);
        }
    }

    rebuildParents();

    delta.assign(baseVertices.size(), 0.0f);
    float reconstructedMin = 0.0f;
    float reconstructedMax = 0.0f;
    float maxError = 0.0f;
    for (std::size_t index = 0; index < baseVertices.size(); ++index) {
        const uint32_t gridX = static_cast<uint32_t>(index % m_columns);
        const uint32_t gridZ = static_cast<uint32_t>(index / m_columns);
        const uint32_t tx = std::min(gridX / m_leafCells, leafTiles - 1);
        const uint32_t ty = std::min(gridZ / m_leafCells, leafTiles - 1);
        const auto& leafTilesMap = m_levels[m_maxLevel];
        const auto tile = leafTilesMap.find(tileKey(m_maxLevel, tx, ty));
        float value = 0.0f;
        if (tile != leafTilesMap.end()) {
            value = sampleTile(tile->second, static_cast<float>(gridX - tx * m_leafCells),
                               static_cast<float>(gridZ - ty * m_leafCells), m_maxLevel);
        }
        delta[index] = value;
        if (std::abs(value) > 1.0e-6f) {
            reconstructedMin = std::min(reconstructedMin, value);
            reconstructedMax = std::max(reconstructedMax, value);
        }
        if (!exactDelta.empty()) {
            maxError = std::max(maxError, std::abs(value - exactDelta[index]));
        }
    }

    uint32_t modifiedLeafTiles = 0;
    uint32_t modifiedParentTiles = 0;
    for (uint32_t level = 0; level <= m_maxLevel; ++level) {
        if (level == m_maxLevel) {
            modifiedLeafTiles = static_cast<uint32_t>(m_levels[level].size());
        } else {
            modifiedParentTiles += static_cast<uint32_t>(m_levels[level].size());
        }
    }
    m_stats.modifiedLeafTiles = modifiedLeafTiles;
    m_stats.modifiedParentTiles = modifiedParentTiles;
    m_stats.reconstructedMin = reconstructedMin;
    m_stats.reconstructedMax = reconstructedMax;
    m_stats.maxError = maxError;
    return true;
}

bool computeRiverTerrainDelta(const RiverGraph& graph, const RiverMeshSettings& mesh,
                              const RiverTerrainSettings& settings, const std::vector<asset::MeshVertex>& baseVertices,
                              std::vector<float>& delta) {
    delta.clear();
    if (!settings.enabled || baseVertices.empty() || graph.axis.size() < 2 || graph.width <= 0.0f ||
        settings.bedDepth <= 0.0f) {
        return false;
    }

    const float halfWidth = graph.width * 0.5f;
    const float bedHalfWidth = halfWidth * std::max(settings.bedWidthFactor, 0.0f);
    const float bankHalfWidth = halfWidth * std::max(settings.bankWidthFactor, 0.0f);
    if (bankHalfWidth <= bedHalfWidth + 1.0e-4f) {
        return false;
    }

    const float bedFloor = mesh.baseHeight - settings.bedDepth;
    delta.assign(baseVertices.size(), 0.0f);
    bool anyChanged = false;
    for (std::size_t index = 0; index < baseVertices.size(); ++index) {
        const asset::MeshVertex& base = baseVertices[index];
        const Vec2 position(base.position.x, base.position.z);
        const float distance = distanceToAxis(graph.axis, position);
        if (distance >= bankHalfWidth) {
            continue;
        }
        const float weight = distance <= bedHalfWidth
                                 ? 1.0f
                                 : 1.0f - smoothStep01((distance - bedHalfWidth) / (bankHalfWidth - bedHalfWidth));
        const float offset = (bedFloor - base.position.y) * weight;
        if ((!settings.fillBed && offset >= 0.0f) || std::abs(offset) <= 1.0e-5f) {
            continue;
        }
        delta[index] = offset;
        anyChanged = true;
    }
    return anyChanged;
}

bool applyRiverTerrainDelta(const std::vector<asset::MeshVertex>& baseVertices, const std::vector<float>& delta,
                            const std::vector<uint32_t>& indices, std::vector<asset::MeshVertex>& vertices) {
    if (baseVertices.size() != delta.size() || baseVertices.empty()) {
        return false;
    }

    vertices = baseVertices;
    std::vector<uint8_t> changed(vertices.size(), 0);
    bool anyChanged = false;
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        if (std::abs(delta[index]) <= 1.0e-5f) {
            continue;
        }
        vertices[index].position.y = baseVertices[index].position.y + delta[index];
        changed[index] = 1;
        anyChanged = true;
    }
    if (!anyChanged) {
        return false;
    }

    if (indices.empty()) {
        return true;
    }

    std::vector<uint8_t> normalAffected = changed;
    for (std::size_t index = 0; index + 2 < indices.size(); index += 3) {
        const uint32_t ia = indices[index];
        const uint32_t ib = indices[index + 1];
        const uint32_t ic = indices[index + 2];
        if (ia >= changed.size() || ib >= changed.size() || ic >= changed.size()) {
            continue;
        }
        if (changed[ia] == 0 && changed[ib] == 0 && changed[ic] == 0) {
            continue;
        }
        normalAffected[ia] = 1;
        normalAffected[ib] = 1;
        normalAffected[ic] = 1;
    }

    std::vector<Vec3> normals(vertices.size(), Vec3(0.0f, 0.0f, 0.0f));
    for (std::size_t index = 0; index + 2 < indices.size(); index += 3) {
        const uint32_t ia = indices[index];
        const uint32_t ib = indices[index + 1];
        const uint32_t ic = indices[index + 2];
        if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) {
            continue;
        }
        const Vec3 normal = crossProduct(subtract3(vertices[ib].position, vertices[ia].position),
                                         subtract3(vertices[ic].position, vertices[ia].position));
        addTo(normals[ia], normal);
        addTo(normals[ib], normal);
        addTo(normals[ic], normal);
    }
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        if (normalAffected[index] == 0) {
            continue;
        }
        vertices[index].normal = normalize3(normals[index], baseVertices[index].normal);
    }

    std::vector<Vec3> tangentSums(vertices.size(), Vec3(0.0f, 0.0f, 0.0f));
    std::vector<uint8_t> tangentAffected(vertices.size(), 0);
    for (std::size_t index = 0; index + 2 < indices.size(); index += 3) {
        const uint32_t ia = indices[index];
        const uint32_t ib = indices[index + 1];
        const uint32_t ic = indices[index + 2];
        if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) {
            continue;
        }
        if (normalAffected[ia] == 0 && normalAffected[ib] == 0 && normalAffected[ic] == 0) {
            continue;
        }
        const Vec3 edge1 = subtract3(vertices[ib].position, vertices[ia].position);
        const Vec3 edge2 = subtract3(vertices[ic].position, vertices[ia].position);
        const Vec2 duv1(vertices[ib].texcoord0.x - vertices[ia].texcoord0.x,
                        vertices[ib].texcoord0.y - vertices[ia].texcoord0.y);
        const Vec2 duv2(vertices[ic].texcoord0.x - vertices[ia].texcoord0.x,
                        vertices[ic].texcoord0.y - vertices[ia].texcoord0.y);
        const float determinant = duv1.x * duv2.y - duv1.y * duv2.x;
        if (std::abs(determinant) <= 1.0e-8f) {
            continue;
        }
        const Vec3 tangent = scale3(subtract3(scale3(edge1, duv2.y), scale3(edge2, duv1.y)), 1.0f / determinant);
        addTo(tangentSums[ia], tangent);
        addTo(tangentSums[ib], tangent);
        addTo(tangentSums[ic], tangent);
        tangentAffected[ia] = 1;
        tangentAffected[ib] = 1;
        tangentAffected[ic] = 1;
    }
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        if (tangentAffected[index] == 0) {
            continue;
        }
        const Vec3 normal = vertices[index].normal;
        const Vec3 projected = subtract3(tangentSums[index], scale3(normal, dot3(normal, tangentSums[index])));
        Vec3 fallback =
            Vec3(baseVertices[index].tangent.x, baseVertices[index].tangent.y, baseVertices[index].tangent.z);
        if (dot3(fallback, fallback) <= 1.0e-8f) {
            fallback = Vec3(1.0f, 0.0f, 0.0f);
        }
        const Vec3 tangent = normalize3(projected, fallback);
        const float sign = baseVertices[index].tangent.w < 0.0f ? -1.0f : 1.0f;
        vertices[index].tangent = Vec4(tangent.x, tangent.y, tangent.z, sign);
    }

    return true;
}

bool deformRiverTerrain(const RiverGraph& graph, const RiverMeshSettings& mesh, const RiverTerrainSettings& settings,
                        const std::vector<asset::MeshVertex>& baseVertices, const std::vector<uint32_t>& indices,
                        std::vector<asset::MeshVertex>& vertices) {
    std::vector<float> delta;
    if (!computeRiverTerrainDelta(graph, mesh, settings, baseVertices, delta)) {
        return false;
    }
    return applyRiverTerrainDelta(baseVertices, delta, indices, vertices);
}

} // namespace hbg::render