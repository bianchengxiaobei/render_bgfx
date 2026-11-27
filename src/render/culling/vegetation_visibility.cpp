#include "render/culling/vegetation_visibility.h"

#include "render/culling/frustum.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace hbg::render {
namespace {

constexpr float kDefaultVegetationTileSize = 500.0f;

int32_t tileCoordinate(float value, float tileSize) {
    return static_cast<int32_t>(std::floor(value / tileSize));
}

uint64_t makeTileKey(int32_t x, int32_t y) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
           static_cast<uint64_t>(static_cast<uint32_t>(y));
}

Vec3 itemPosition(const DrawItem& item) {
    return Vec3(item.world.values[12], item.world.values[13], item.world.values[14]);
}

Bounds itemBounds(const DrawItem& item) {
    if (item.worldBounds.valid()) {
        return item.worldBounds;
    }
    Bounds bounds;
    bounds.expand(itemPosition(item));
    return bounds;
}

float distanceToBounds(const Vec3& point, const Bounds& bounds) {
    if (!bounds.valid()) {
        return 0.0f;
    }
    const float dx = std::max(std::max(bounds.min.x - point.x, point.x - bounds.max.x), 0.0f);
    const float dy = std::max(std::max(bounds.min.y - point.y, point.y - bounds.max.y), 0.0f);
    const float dz = std::max(std::max(bounds.min.z - point.z, point.z - bounds.max.z), 0.0f);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

void buildVegetationTiles(const std::vector<DrawItem>& items, float tileSize,
                          std::vector<VegetationTile>& tiles) {
    tiles.clear();
    if (items.empty()) {
        return;
    }

    const float size = tileSize > 1.0f ? tileSize : kDefaultVegetationTileSize;
    std::unordered_map<uint64_t, std::size_t> tileLookup;
    tileLookup.reserve(std::min<std::size_t>(items.size(), 16384));

    for (uint32_t index = 0; index < static_cast<uint32_t>(items.size()); ++index) {
        const DrawItem& item = items[index];
        const Vec3 position = itemPosition(item);
        const uint64_t key = makeTileKey(tileCoordinate(position.x, size), tileCoordinate(position.z, size));
        const auto found = tileLookup.find(key);
        if (found == tileLookup.end()) {
            VegetationTile tile;
            tile.key = key;
            tile.bounds = itemBounds(item);
            tile.itemIndices.push_back(index);
            tileLookup.emplace(key, tiles.size());
            tiles.push_back(std::move(tile));
        } else {
            VegetationTile& tile = tiles[found->second];
            tile.bounds.expand(itemBounds(item));
            tile.itemIndices.push_back(index);
        }
    }

    std::sort(tiles.begin(), tiles.end(),
              [](const VegetationTile& lhs, const VegetationTile& rhs) { return lhs.key < rhs.key; });
}

std::vector<uint32_t> collectVisibleVegetation(const std::vector<DrawItem>& items,
                                               const std::vector<VegetationTile>& tiles,
                                               const Vec3& cameraPosition,
                                               const std::array<Vec4, 6>& frustumPlanes,
                                               float maxDistance) {
    std::vector<uint32_t> visible;
    if (items.empty() || tiles.empty()) {
        return visible;
    }

    visible.reserve(items.size());
    for (const VegetationTile& tile : tiles) {
        if (!intersectsFrustum(frustumPlanes, tile.bounds)) {
            continue;
        }
        if (maxDistance > 0.0f && distanceToBounds(cameraPosition, tile.bounds) > maxDistance) {
            continue;
        }
        for (uint32_t index : tile.itemIndices) {
            if (index >= items.size()) {
                continue;
            }
            const DrawItem& item = items[index];
            if (!intersectsFrustum(frustumPlanes, item.worldBounds)) {
                continue;
            }
            if (maxDistance > 0.0f && distanceToBounds(cameraPosition, item.worldBounds) > maxDistance) {
                continue;
            }
            visible.push_back(index);
        }
    }

    std::sort(visible.begin(), visible.end());
    return visible;
}

} // namespace hbg::render
