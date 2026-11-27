#include "render/camera/resolution_group.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hbg::render {
namespace {

uint16_t scaledDimension(uint16_t dimension, float scale) {
    if (dimension == 0) {
        return 0;
    }
    const long scaled = std::lround(static_cast<double>(dimension) * static_cast<double>(scale));
    return static_cast<uint16_t>(std::clamp<long>(scaled, 1, std::numeric_limits<uint16_t>::max()));
}

float ratio(uint16_t value, uint16_t reference) {
    return reference == 0 ? 0.0f : static_cast<float>(value) / static_cast<float>(reference);
}

} // namespace

uint16_t ViewResolution::width(ResolutionGroup group) const {
    return group == ResolutionGroup::AfterDynamicResUpscale ? targetWidth : renderWidth;
}

uint16_t ViewResolution::height(ResolutionGroup group) const {
    return group == ResolutionGroup::AfterDynamicResUpscale ? targetHeight : renderHeight;
}

Vec4 ViewResolution::screenSize(ResolutionGroup group) const {
    const uint16_t widthValue = width(group);
    const uint16_t heightValue = height(group);
    if (widthValue == 0 || heightValue == 0) {
        return Vec4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    return Vec4(static_cast<float>(widthValue), static_cast<float>(heightValue), 1.0f / static_cast<float>(widthValue),
                1.0f / static_cast<float>(heightValue));
}

Vec4 ViewResolution::dynamicResolutionScale() const {
    return Vec4(ratio(renderWidth, targetWidth), ratio(renderHeight, targetHeight), 0.0f, 0.0f);
}

Vec4 ViewResolution::rtScale() const {
    const float scaleX = ratio(renderWidth, targetWidth);
    const float scaleY = ratio(renderHeight, targetHeight);
    return Vec4(scaleX, scaleY, scaleX, scaleY);
}

ViewResolution makeViewResolution(uint16_t targetWidth, uint16_t targetHeight, float scale) {
    ViewResolution resolution;
    resolution.targetWidth = targetWidth;
    resolution.targetHeight = targetHeight;
    resolution.scale = std::clamp(scale, 0.1f, 1.0f);
    resolution.renderWidth = scaledDimension(targetWidth, resolution.scale);
    resolution.renderHeight = scaledDimension(targetHeight, resolution.scale);
    resolution.scale = ratio(resolution.renderWidth, resolution.targetWidth);
    return resolution;
}

const char* resolutionGroupName(ResolutionGroup group) {
    switch (group) {
    case ResolutionGroup::BeforeDynamicResUpscale:
        return "BeforeDynamicResUpscale";
    case ResolutionGroup::AfterDynamicResUpscale:
        return "AfterDynamicResUpscale";
    }
    return "Unknown";
}

} // namespace hbg::render
