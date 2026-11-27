#include "render/culling/sort_key.h"

#include <limits>

namespace hbg::render {

uint32_t quantizeDrawDistance(float distance) {
    if (!(distance > 0.0f)) {
        return 0;
    }
    constexpr float kDistanceBucketsPerUnit = 64.0f;
    const float scaled = distance * kDistanceBucketsPerUnit;
    if (scaled >= static_cast<float>(std::numeric_limits<uint32_t>::max())) {
        return std::numeric_limits<uint32_t>::max();
    }
    return static_cast<uint32_t>(scaled);
}

bool drawSortKeyLess(const DrawSortKey& lhs, const DrawSortKey& rhs, SortingCriteria criteria) {
    if (hasSortingCriteria(criteria, SortingCriteria::SortingLayer) && lhs.sortingLayer != rhs.sortingLayer) {
        return lhs.sortingLayer < rhs.sortingLayer;
    }
    if (hasSortingCriteria(criteria, SortingCriteria::RenderQueue) && lhs.renderQueue != rhs.renderQueue) {
        return lhs.renderQueue < rhs.renderQueue;
    }
    if (hasSortingCriteria(criteria, SortingCriteria::RendererPriority) &&
        lhs.rendererPriority != rhs.rendererPriority) {
        return lhs.rendererPriority > rhs.rendererPriority;
    }
    if (hasSortingCriteria(criteria, SortingCriteria::CanvasOrder) && lhs.sortingOrder != rhs.sortingOrder) {
        return lhs.sortingOrder < rhs.sortingOrder;
    }
    if (hasSortingCriteria(criteria, SortingCriteria::BackToFront) && lhs.distance != rhs.distance) {
        return lhs.distance > rhs.distance;
    }
    if (hasSortingCriteria(criteria, SortingCriteria::QuantizedFrontToBack) &&
        lhs.quantizedDistance != rhs.quantizedDistance) {
        return lhs.quantizedDistance < rhs.quantizedDistance;
    }
    if (hasSortingCriteria(criteria, SortingCriteria::OptimizeStateChanges) && lhs.stateKey != rhs.stateKey) {
        return lhs.stateKey < rhs.stateKey;
    }
    return lhs.sequence < rhs.sequence;
}

} // namespace hbg::render
