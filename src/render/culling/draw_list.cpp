#include "render/culling/draw_list.h"

#include "render/culling/frustum.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace hbg::render {
namespace {

uint64_t makeDrawStateKey(uint32_t materialIndex, uint32_t meshIndex, uint32_t subMeshIndex) {
    return (static_cast<uint64_t>(materialIndex) << 40) | ((static_cast<uint64_t>(meshIndex) & 0xffffffu) << 16) |
           (static_cast<uint64_t>(subMeshIndex) & 0xffffu);
}

float itemDistance(const Vec3& cameraPosition, const Mat4& world, const Bounds& bounds) {
    const Vec3 center = bounds.valid() ? bounds.center() : Vec3(world.values[12], world.values[13], world.values[14]);
    return length(cameraPosition - center);
}

} // namespace

void DrawList::clear() {
    m_items.clear();
    m_batches.clear();
    m_filteredCount = 0;
    m_culledCount = 0;
}

void DrawList::buildCulled(const DrawList& source, const std::array<Vec4, 6>& planes) {
    clear();
    m_items.reserve(source.m_items.size());
    for (const DrawItem& item : source.m_items) {
        if (intersectsFrustum(planes, item.worldBounds)) {
            m_items.push_back(item);
        }
    }
    m_filteredCount = source.m_filteredCount;
    m_culledCount = static_cast<uint32_t>(source.m_items.size() - m_items.size());

    m_batches.reserve(m_items.size());
    for (std::size_t index = 0; index < m_items.size(); ++index) {
        const DrawItem& item = m_items[index];
        if (m_batches.empty() || m_batches.back().stateKey != item.batchKey ||
            m_batches.back().lightmapAtlas != item.lightmapAtlas) {
            DrawBatch batch;
            batch.firstItem = static_cast<uint32_t>(index);
            batch.itemCount = 1;
            batch.stateKey = item.batchKey;
            batch.meshIndex = item.meshIndex;
            batch.materialIndex = item.materialIndex;
            batch.lightmapAtlas = item.lightmapAtlas;
            m_batches.push_back(batch);
        } else {
            ++m_batches.back().itemCount;
        }
    }
}

void DrawList::buildRange(const DrawList& source, uint32_t firstItem, uint32_t itemCount) {
    clear();
    if (firstItem >= source.m_items.size()) {
        return;
    }
    const uint32_t count = std::min(itemCount, static_cast<uint32_t>(source.m_items.size() - firstItem));
    m_items.assign(source.m_items.begin() + firstItem, source.m_items.begin() + firstItem + count);
    m_batches.reserve(m_items.size());
    for (std::size_t index = 0; index < m_items.size(); ++index) {
        const DrawItem& item = m_items[index];
        if (m_batches.empty() || m_batches.back().stateKey != item.batchKey ||
            m_batches.back().lightmapAtlas != item.lightmapAtlas) {
            DrawBatch batch;
            batch.firstItem = static_cast<uint32_t>(index);
            batch.itemCount = 1;
            batch.stateKey = item.batchKey;
            batch.meshIndex = item.meshIndex;
            batch.materialIndex = item.materialIndex;
            batch.lightmapAtlas = item.lightmapAtlas;
            m_batches.push_back(batch);
        } else {
            ++m_batches.back().itemCount;
        }
    }
}

void DrawList::build(const RenderScene& scene, const ViewState& viewState, const DrawListDesc& desc) {
    clear();
    m_items.reserve(scene.objects.size());

    const Vec3 cameraPosition = viewState.cameraPosition();
    const std::array<Vec4, 6>& planes = viewState.constants().frustumPlanes;

    for (uint32_t objectIndex = 0; objectIndex < scene.objects.size(); ++objectIndex) {
        const RenderObject& object = scene.objects[objectIndex];
        if (object.meshIndex >= scene.meshes.size() ||
            (desc.requireLightmap && !object.hasLightmap()) ||
            (desc.shadowCasterFilter == ShadowCasterFilter::StaticOnly && !object.staticShadowCaster) ||
            (desc.shadowCasterFilter == ShadowCasterFilter::DynamicOnly && object.staticShadowCaster)) {
            ++m_filteredCount;
            continue;
        }
        const asset::Mesh& mesh = scene.meshes[object.meshIndex];
        for (uint32_t subMeshIndex = 0; subMeshIndex < mesh.submeshes.size(); ++subMeshIndex) {
            const asset::SubMesh& subMesh = mesh.submeshes[subMeshIndex];
            if (subMesh.materialIndex >= scene.materials.size()) {
                ++m_filteredCount;
                continue;
            }

            const asset::Material& material = scene.materials[subMesh.materialIndex];
            const ShaderPassMask passMask = defaultShaderPassMask(material);
            if ((passMask & desc.passTags) == 0) {
                ++m_filteredCount;
                continue;
            }

            const int renderQueue = material.renderQueue;
            if (!desc.queueRange.contains(renderQueue)) {
                ++m_filteredCount;
                continue;
            }

            const bool dynamicBounds = object.skinIndex >= 0 || !mesh.morphTargets.empty();
            const Bounds worldBounds =
                dynamicBounds ? object.worldBounds
                              : (subMesh.bounds.valid() ? transformBounds(object.world, subMesh.bounds)
                                                        : object.worldBounds);
            if (desc.frustumCull && !intersectsFrustum(planes, worldBounds)) {
                ++m_culledCount;
                continue;
            }

            const RenderQueueType queueType = renderQueueType(renderQueue);
            const uint64_t stateKey = makeDrawStateKey(subMesh.materialIndex, object.meshIndex, subMeshIndex);

            DrawItem item;
            item.objectIndex = objectIndex;
            item.meshIndex = object.meshIndex;
            item.subMeshIndex = subMeshIndex;
            item.materialIndex = subMesh.materialIndex;
            item.indexOffset = subMesh.indexOffset;
            item.indexCount = subMesh.indexCount;
            item.world = object.world;
            item.prevWorld = object.prevWorld;
            item.worldBounds = worldBounds;
            item.staticShadowCaster = object.staticShadowCaster;
            item.distance = itemDistance(cameraPosition, object.world, worldBounds);
            item.queueType = queueType;
            item.passMask = passMask;
            item.transparent = isTransparentQueue(queueType);
            item.lightmapAtlas = object.hasLightmap() ? object.lightmapAtlas : UINT32_MAX;
            item.batchKey = stateKey;
            item.sortKey.sortingLayer = object.sortingLayer;
            item.sortKey.renderQueue = renderQueue;
            item.sortKey.rendererPriority = object.rendererPriority;
            item.sortKey.sortingOrder = object.sortingOrder;
            item.sortKey.distance = item.distance;
            item.sortKey.quantizedDistance = quantizeDrawDistance(item.distance);
            item.sortKey.stateKey = stateKey;
            item.sortKey.lightmapAtlas = item.lightmapAtlas;
            item.sortKey.sequence = static_cast<uint32_t>(m_items.size());
            m_items.push_back(item);
        }
    }

    std::stable_sort(m_items.begin(), m_items.end(), [&desc](const DrawItem& lhs, const DrawItem& rhs) {
        return drawSortKeyLess(lhs.sortKey, rhs.sortKey, desc.sortingCriteria);
    });

    m_batches.reserve(m_items.size());
    for (std::size_t index = 0; index < m_items.size(); ++index) {
        const DrawItem& item = m_items[index];
        if (m_batches.empty() || m_batches.back().stateKey != item.batchKey ||
            m_batches.back().lightmapAtlas != item.lightmapAtlas) {
            DrawBatch batch;
            batch.firstItem = static_cast<uint32_t>(index);
            batch.itemCount = 1;
            batch.stateKey = item.batchKey;
            batch.meshIndex = item.meshIndex;
            batch.materialIndex = item.materialIndex;
            batch.lightmapAtlas = item.lightmapAtlas;
            m_batches.push_back(batch);
        } else {
            ++m_batches.back().itemCount;
        }
    }
}

} // namespace hbg::render
