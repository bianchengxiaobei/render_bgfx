#include "render/material/render_queue.h"

namespace hbg::render {

RenderQueueType renderQueueType(int renderQueue) {
    if (renderQueue == kRenderQueueBackground) {
        return RenderQueueType::Background;
    }
    if (kRenderQueueRangeAllOpaque.contains(renderQueue)) {
        return RenderQueueType::Opaque;
    }
    if (kRenderQueueRangeAfterPostProcessOpaque.contains(renderQueue)) {
        return RenderQueueType::AfterPostProcessOpaque;
    }
    if (kRenderQueueRangePreRefraction.contains(renderQueue)) {
        return RenderQueueType::PreRefraction;
    }
    if (kRenderQueueRangeTransparent.contains(renderQueue)) {
        return RenderQueueType::Transparent;
    }
    if (kRenderQueueRangeLowTransparent.contains(renderQueue)) {
        return RenderQueueType::LowTransparent;
    }
    if (kRenderQueueRangeAfterPostProcessTransparent.contains(renderQueue)) {
        return RenderQueueType::AfterPostprocessTransparent;
    }
    if (kRenderQueueRangeLineRendering.contains(renderQueue)) {
        return RenderQueueType::LineRendering;
    }
    if (kRenderQueueRangeOverlay.contains(renderQueue)) {
        return RenderQueueType::Overlay;
    }
    return RenderQueueType::Unknown;
}

bool isOpaqueQueue(RenderQueueType type) {
    return type == RenderQueueType::Background || type == RenderQueueType::Opaque ||
           type == RenderQueueType::AfterPostProcessOpaque;
}

bool isTransparentQueue(RenderQueueType type) {
    return type == RenderQueueType::PreRefraction || type == RenderQueueType::Transparent ||
           type == RenderQueueType::LowTransparent || type == RenderQueueType::AfterPostprocessTransparent ||
           type == RenderQueueType::LineRendering || type == RenderQueueType::Overlay;
}

std::string_view renderQueueTypeName(RenderQueueType type) {
    switch (type) {
    case RenderQueueType::Background:
        return "Background";
    case RenderQueueType::Opaque:
        return "Opaque";
    case RenderQueueType::AfterPostProcessOpaque:
        return "AfterPostProcessOpaque";
    case RenderQueueType::PreRefraction:
        return "PreRefraction";
    case RenderQueueType::Transparent:
        return "Transparent";
    case RenderQueueType::LowTransparent:
        return "LowTransparent";
    case RenderQueueType::AfterPostprocessTransparent:
        return "AfterPostprocessTransparent";
    case RenderQueueType::LineRendering:
        return "LineRendering";
    case RenderQueueType::Overlay:
        return "Overlay";
    case RenderQueueType::Unknown:
        break;
    }
    return "Unknown";
}

int changeRenderQueueType(RenderQueueType type, int offset, bool alphaTest, bool receiveDecal) {
    switch (type) {
    case RenderQueueType::Background:
        return kRenderQueueBackground;
    case RenderQueueType::Opaque:
        return alphaTest ? (receiveDecal ? kRenderQueueOpaqueDecalAlphaTest : kRenderQueueAlphaTest)
                         : (receiveDecal ? kRenderQueueOpaqueDecal : kRenderQueueOpaque);
    case RenderQueueType::AfterPostProcessOpaque:
        return alphaTest ? kRenderQueueAfterPostprocessOpaqueAlphaTest : kRenderQueueAfterPostprocessOpaque;
    case RenderQueueType::PreRefraction:
        return kRenderQueuePreRefraction + offset;
    case RenderQueueType::Transparent:
        return kRenderQueueTransparent + offset;
    case RenderQueueType::LowTransparent:
        return kRenderQueueLowTransparent + offset;
    case RenderQueueType::AfterPostprocessTransparent:
        return kRenderQueueAfterPostprocessTransparent + offset;
    case RenderQueueType::LineRendering:
        return kRenderQueueLineRendering + offset;
    case RenderQueueType::Overlay:
        return kRenderQueueOverlay;
    case RenderQueueType::Unknown:
        break;
    }
    return kRenderQueueOpaque;
}

} // namespace hbg::render
