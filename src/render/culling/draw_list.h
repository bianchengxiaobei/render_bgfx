#pragma once

#include "core/bounds.h"
#include "core/math.h"
#include "render/camera/view_state.h"
#include "render/culling/sort_key.h"
#include "render/material/render_queue.h"
#include "render/material/shader_pass.h"
#include "render/scene/render_scene.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hbg::render {

struct DrawItem {
    uint32_t objectIndex = UINT32_MAX;
    uint32_t meshIndex = UINT32_MAX;
    uint32_t subMeshIndex = 0;
    uint32_t materialIndex = 0;
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    Mat4 world = Mat4::identity();
    Mat4 prevWorld = Mat4::identity();
    Bounds worldBounds;
    float distance = 0.0f;
    bool staticShadowCaster = true;
    RenderQueueType queueType = RenderQueueType::Unknown;
    ShaderPassMask passMask = 0;
    bool transparent = false;
    uint32_t lightmapAtlas = UINT32_MAX;
    DrawSortKey sortKey;
    uint64_t batchKey = 0;
};

struct DrawBatch {
    uint32_t firstItem = 0;
    uint32_t itemCount = 0;
    uint64_t stateKey = 0;
    uint32_t meshIndex = UINT32_MAX;
    uint32_t materialIndex = 0;
    uint32_t lightmapAtlas = UINT32_MAX;
};

enum class ShadowCasterFilter : uint8_t {
    All,
    StaticOnly,
    DynamicOnly,
};

struct DrawListDesc {
    ShaderPassMask passTags = kShaderPassMaskAll;
    RenderQueueRange queueRange = kRenderQueueRangeAll;
    SortingCriteria sortingCriteria = kOpaqueSortingCriteria;
    ShadowCasterFilter shadowCasterFilter = ShadowCasterFilter::All;
    bool frustumCull = true;
    bool requireLightmap = false;
};

class DrawList {
  public:
    void build(const RenderScene& scene, const ViewState& viewState, const DrawListDesc& desc);
    void buildCulled(const DrawList& source, const std::array<Vec4, 6>& planes);
    void clear();

    bool empty() const { return m_items.empty(); }
    std::size_t size() const { return m_items.size(); }
    const std::vector<DrawItem>& items() const { return m_items; }
    const std::vector<DrawBatch>& batches() const { return m_batches; }
    uint32_t filteredCount() const { return m_filteredCount; }
    uint32_t culledCount() const { return m_culledCount; }

  private:
    std::vector<DrawItem> m_items;
    std::vector<DrawBatch> m_batches;
    uint32_t m_filteredCount = 0;
    uint32_t m_culledCount = 0;
};

} // namespace hbg::render
