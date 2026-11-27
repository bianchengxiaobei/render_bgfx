#pragma once

#include <cstdint>
#include <string_view>

namespace hbg::render {

constexpr int kRenderQueueBackground = 1000;
constexpr int kRenderQueueOpaque = 2000;
constexpr int kRenderQueueOpaqueDecal = 2225;
constexpr int kRenderQueueAlphaTest = 2450;
constexpr int kRenderQueueOpaqueDecalAlphaTest = 2475;
constexpr int kRenderQueueOpaqueLast = 2500;
constexpr int kRenderQueueAfterPostprocessOpaque = 2501;
constexpr int kRenderQueueAfterPostprocessOpaqueAlphaTest = 2510;
constexpr int kRenderQueuePreRefractionFirst = 2650;
constexpr int kRenderQueuePreRefraction = 2750;
constexpr int kRenderQueuePreRefractionLast = 2850;
constexpr int kRenderQueueTransparentFirst = 2900;
constexpr int kRenderQueueTransparent = 3000;
constexpr int kRenderQueueTransparentLast = 3100;
constexpr int kRenderQueueLowTransparentFirst = 3300;
constexpr int kRenderQueueLowTransparent = 3400;
constexpr int kRenderQueueLowTransparentLast = 3500;
constexpr int kRenderQueueAfterPostprocessTransparentFirst = 3600;
constexpr int kRenderQueueAfterPostprocessTransparent = 3700;
constexpr int kRenderQueueAfterPostprocessTransparentLast = 3800;
constexpr int kRenderQueueLineRenderingFirst = 3750;
constexpr int kRenderQueueLineRendering = 3850;
constexpr int kRenderQueueLineRenderingLast = 3950;
constexpr int kRenderQueueOverlay = 4000;
constexpr int kRenderQueueOverlayLast = 5000;
constexpr int kTransparentPriorityQueueRangeStep = 100;
constexpr int kSortingPriorityRange = 50;

struct RenderQueueRange {
    int lowerBound = 0;
    int upperBound = 0;

    constexpr bool contains(int value) const { return lowerBound <= value && value <= upperBound; }
    constexpr bool empty() const { return lowerBound > upperBound; }
    constexpr int clamp(int value) const {
        return value < lowerBound ? lowerBound : (value > upperBound ? upperBound : value);
    }

    friend constexpr bool operator==(const RenderQueueRange&, const RenderQueueRange&) = default;
};

inline constexpr RenderQueueRange kRenderQueueRangeOpaqueNoAlphaTest{kRenderQueueBackground, kRenderQueueAlphaTest - 1};
inline constexpr RenderQueueRange kRenderQueueRangeOpaqueAlphaTest{kRenderQueueAlphaTest, kRenderQueueOpaqueLast};
inline constexpr RenderQueueRange kRenderQueueRangeOpaqueDecalAndAlphaTest{kRenderQueueOpaqueDecal,
                                                                           kRenderQueueOpaqueLast};
inline constexpr RenderQueueRange kRenderQueueRangeAllOpaque{kRenderQueueBackground, kRenderQueueOpaqueLast};
inline constexpr RenderQueueRange kRenderQueueRangeAfterPostProcessOpaque{kRenderQueueAfterPostprocessOpaque,
                                                                          kRenderQueueAfterPostprocessOpaqueAlphaTest};
inline constexpr RenderQueueRange kRenderQueueRangePreRefraction{kRenderQueuePreRefractionFirst,
                                                                 kRenderQueuePreRefractionLast};
inline constexpr RenderQueueRange kRenderQueueRangeTransparent{kRenderQueueTransparentFirst,
                                                               kRenderQueueTransparentLast};
inline constexpr RenderQueueRange kRenderQueueRangeTransparentWithLowRes{kRenderQueueTransparentFirst,
                                                                         kRenderQueueLowTransparentLast};
inline constexpr RenderQueueRange kRenderQueueRangeLowTransparent{kRenderQueueLowTransparentFirst,
                                                                  kRenderQueueLowTransparentLast};
inline constexpr RenderQueueRange kRenderQueueRangeAllTransparent{kRenderQueuePreRefractionFirst,
                                                                  kRenderQueueTransparentLast};
inline constexpr RenderQueueRange kRenderQueueRangeAllTransparentWithLowRes{kRenderQueuePreRefractionFirst,
                                                                            kRenderQueueLowTransparentLast};
inline constexpr RenderQueueRange kRenderQueueRangeAfterPostProcessTransparent{
    kRenderQueueAfterPostprocessTransparentFirst, kRenderQueueAfterPostprocessTransparentLast};
inline constexpr RenderQueueRange kRenderQueueRangeLineRendering{kRenderQueueLineRenderingFirst,
                                                                 kRenderQueueLineRenderingLast};
inline constexpr RenderQueueRange kRenderQueueRangeOverlay{kRenderQueueOverlay, kRenderQueueOverlayLast};
inline constexpr RenderQueueRange kRenderQueueRangeAll{0, kRenderQueueOverlayLast};

enum class RenderQueueType : uint8_t {
    Background,
    Opaque,
    AfterPostProcessOpaque,
    PreRefraction,
    Transparent,
    LowTransparent,
    AfterPostprocessTransparent,
    LineRendering,
    Overlay,
    Unknown,
};

RenderQueueType renderQueueType(int renderQueue);
bool isOpaqueQueue(RenderQueueType type);
bool isTransparentQueue(RenderQueueType type);
std::string_view renderQueueTypeName(RenderQueueType type);
int changeRenderQueueType(RenderQueueType type, int offset = 0, bool alphaTest = false, bool receiveDecal = false);

} // namespace hbg::render
