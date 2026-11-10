#pragma once

#include <cstdint>

namespace hbg::render {

enum class SortingCriteria : uint32_t {
    None = 0,
    SortingLayer = 1u << 0,
    RenderQueue = 1u << 1,
    BackToFront = 1u << 2,
    QuantizedFrontToBack = 1u << 3,
    OptimizeStateChanges = 1u << 4,
    CanvasOrder = 1u << 5,
    RendererPriority = 1u << 6,
};

constexpr SortingCriteria operator|(SortingCriteria lhs, SortingCriteria rhs) {
    return static_cast<SortingCriteria>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr SortingCriteria operator&(SortingCriteria lhs, SortingCriteria rhs) {
    return static_cast<SortingCriteria>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

constexpr SortingCriteria operator~(SortingCriteria value) {
    return static_cast<SortingCriteria>(~static_cast<uint32_t>(value));
}

constexpr SortingCriteria& operator|=(SortingCriteria& lhs, SortingCriteria rhs) {
    lhs = lhs | rhs;
    return lhs;
}

constexpr bool hasSortingCriteria(SortingCriteria value, SortingCriteria flag) {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

inline constexpr SortingCriteria kCommonOpaqueSortingCriteria =
    SortingCriteria::SortingLayer | SortingCriteria::RenderQueue | SortingCriteria::QuantizedFrontToBack |
    SortingCriteria::OptimizeStateChanges | SortingCriteria::CanvasOrder;
inline constexpr SortingCriteria kCommonTransparentSortingCriteria =
    SortingCriteria::SortingLayer | SortingCriteria::RenderQueue | SortingCriteria::BackToFront |
    SortingCriteria::OptimizeStateChanges | SortingCriteria::CanvasOrder;

inline constexpr SortingCriteria kOpaqueSortingCriteria =
    kCommonOpaqueSortingCriteria & ~SortingCriteria::QuantizedFrontToBack;
inline constexpr SortingCriteria kTransparentSortingCriteria =
    kCommonTransparentSortingCriteria | SortingCriteria::RendererPriority;

struct DrawSortKey {
    int sortingLayer = 0;
    int renderQueue = 0;
    int rendererPriority = 0;
    int sortingOrder = 0;
    float distance = 0.0f;
    uint32_t quantizedDistance = 0;
    uint64_t stateKey = 0;
    uint32_t sequence = 0;
};

uint32_t quantizeDrawDistance(float distance);
bool drawSortKeyLess(const DrawSortKey& lhs, const DrawSortKey& rhs, SortingCriteria criteria);

} // namespace hbg::render
