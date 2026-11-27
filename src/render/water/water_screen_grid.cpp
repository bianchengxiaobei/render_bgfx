#include "render/water/water_screen_grid.h"

#include "core/log.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hbg::render {
namespace {

constexpr float kScreenGridPixels = 8.0f;
constexpr float kOverscan = 1.25f;
constexpr int kMaxGridDimension = 1024;

} // namespace

bool configureWaterScreenGrid(asset::Mesh& waterMesh, uint16_t viewportWidth, uint16_t viewportHeight,
                              WaterScreenGridDesc* desc, const WaterScreenGridSource* source) {
    if (viewportWidth == 0 || viewportHeight == 0 || waterMesh.vertices.empty()) {
        return false;
    }

    const int width = std::max(static_cast<int>(kOverscan * static_cast<float>(viewportWidth) / kScreenGridPixels), 2);
    const int height =
        std::max(static_cast<int>(kOverscan * static_cast<float>(viewportHeight) / kScreenGridPixels), 2);
    if (width > kMaxGridDimension || height > kMaxGridDimension) {
        HBG_LOGE("water screen grid: viewport {}x{} exceeds supported grid size", viewportWidth, viewportHeight);
        return false;
    }

    Bounds sourceBounds = source != nullptr ? source->bounds : waterMesh.bounds;
    if (!sourceBounds.valid()) {
        sourceBounds = {};
        for (const asset::MeshVertex& vertex : waterMesh.vertices) {
            sourceBounds.expand(vertex.position);
        }
    }
    if (!sourceBounds.valid()) {
        return false;
    }

    float minU = source != nullptr ? source->uvMin.x : waterMesh.vertices.front().texcoord0.x;
    float maxU = source != nullptr ? source->uvMax.x : minU;
    float minV = source != nullptr ? source->uvMin.y : waterMesh.vertices.front().texcoord0.y;
    float maxV = source != nullptr ? source->uvMax.y : minV;
    if (source == nullptr) {
        for (const asset::MeshVertex& vertex : waterMesh.vertices) {
            minU = std::min(minU, vertex.texcoord0.x);
            maxU = std::max(maxU, vertex.texcoord0.x);
            minV = std::min(minV, vertex.texcoord0.y);
            maxV = std::max(maxV, vertex.texcoord0.y);
        }
    }
    if (maxU <= minU) {
        minU = 0.0f;
        maxU = 1.0f;
    }
    if (maxV <= minV) {
        minV = 0.0f;
        maxV = 1.0f;
    }

    const uint32_t materialIndex =
        source != nullptr ? source->materialIndex
                          : (waterMesh.submeshes.empty() ? 0u : waterMesh.submeshes.front().materialIndex);
    const float baseHeight = source != nullptr ? source->baseHeight : waterMesh.vertices.front().position.y;
    const float minX = sourceBounds.min.x;
    const float maxX = sourceBounds.max.x;
    const float minZ = sourceBounds.min.z;
    const float maxZ = sourceBounds.max.z;
    const float inverseX = maxX > minX ? 1.0f / (maxX - minX) : 0.0f;
    const float inverseZ = maxZ > minZ ? 1.0f / (maxZ - minZ) : 0.0f;

    std::vector<asset::MeshVertex> vertices;
    vertices.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int row = 0; row < height; ++row) {
        const float tz = height > 1 ? static_cast<float>(row) / static_cast<float>(height - 1) : 0.0f;
        const float z = minZ + (maxZ - minZ) * tz;
        for (int column = 0; column < width; ++column) {
            const float tx = width > 1 ? static_cast<float>(column) / static_cast<float>(width - 1) : 0.0f;
            const float x = minX + (maxX - minX) * tx;
            asset::MeshVertex vertex;
            vertex.position = Vec3(x, baseHeight, z);
            vertex.normal = Vec3(0.0f, 1.0f, 0.0f);
            vertex.tangent = Vec4(1.0f, 0.0f, 0.0f, -1.0f);
            vertex.texcoord0 =
                Vec2(minU + (maxU - minU) * (x - minX) * inverseX, minV + (maxV - minV) * (z - minZ) * inverseZ);
            vertices.push_back(vertex);
        }
    }

    const uint64_t indexCount = static_cast<uint64_t>(width - 1) * static_cast<uint64_t>(height - 1) * 6u;
    if (indexCount > std::numeric_limits<uint32_t>::max()) {
        return false;
    }

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<std::size_t>(indexCount));
    for (int row = 0; row < height - 1; ++row) {
        for (int column = 0; column < width - 1; ++column) {
            const uint32_t a = static_cast<uint32_t>(row * width + column);
            const uint32_t b = a + 1u;
            const uint32_t c = a + static_cast<uint32_t>(width);
            const uint32_t d = c + 1u;
            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(b);
            indices.push_back(b);
            indices.push_back(c);
            indices.push_back(d);
        }
    }

    waterMesh.vertices = std::move(vertices);
    waterMesh.indices = std::move(indices);
    waterMesh.submeshes.clear();
    asset::SubMesh submesh;
    submesh.indexOffset = 0;
    submesh.indexCount = static_cast<uint32_t>(waterMesh.indices.size());
    submesh.materialIndex = materialIndex;
    submesh.bounds = sourceBounds;
    waterMesh.submeshes.push_back(submesh);
    waterMesh.bounds = sourceBounds;
    waterMesh.hasNormals = true;
    waterMesh.hasTangents = true;
    waterMesh.hasUv0 = true;
    waterMesh.hasUv1 = false;
    waterMesh.hasColors = false;

    if (desc != nullptr) {
        desc->width = width;
        desc->height = height;
    }
    HBG_LOGI("water screen grid: viewport={}x{} grid={}x{} vertices={} indices={}", viewportWidth, viewportHeight,
             width, height, waterMesh.vertices.size(), waterMesh.indices.size());
    return true;
}

} // namespace hbg::render
