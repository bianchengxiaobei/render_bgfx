#include "hbg/dawn/dawn_atlas.h"

#include "hbg/dawn/atlas_node.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

namespace hbg::dawn {

namespace {

std::vector<uint32_t> scaleLightmapSizes(const std::vector<uint32_t>& sizes, float scale) {
    std::vector<uint32_t> scaled(sizes.size());
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        const double value = std::ceil(static_cast<double>(sizes[i]) * scale);
        scaled[i] = static_cast<uint32_t>(std::max(4.0, value));
    }
    return scaled;
}

bool tryPackAtScale(const std::vector<uint32_t>& sizes, const LightmapPackingSettings& settings, float scale,
                    LightmapPackingResult& outResult) {
    outResult = {};
    if (sizes.empty()) {
        return true;
    }
    if (settings.atlasSize == 0 || settings.maxAtlases == 0) {
        return false;
    }
    for (uint32_t size : sizes) {
        if (size == 0) {
            return false;
        }
    }

    const std::vector<uint32_t> scaled = scaleLightmapSizes(sizes, scale);
    std::vector<uint32_t> remaining(sizes.size());
    std::iota(remaining.begin(), remaining.end(), 0u);
    std::sort(remaining.begin(), remaining.end(), [&scaled](uint32_t lhs, uint32_t rhs) {
        if (scaled[lhs] != scaled[rhs]) {
            return scaled[lhs] > scaled[rhs];
        }
        return lhs < rhs;
    });

    LightmapPackingResult result;
    result.placements.resize(sizes.size());
    while (!remaining.empty()) {
        if (result.atlases.size() >= settings.maxAtlases) {
            return false;
        }

        std::vector<uint32_t> atlasIndices;
        std::vector<uint32_t> deferred;
        std::vector<uint32_t> atlasSizes;
        LightmapAtlasLayout atlasLayout;
        for (uint32_t index : remaining) {
            atlasSizes.push_back(scaled[index]);
            LightmapAtlasLayout trial;
            if (packLightmapSquares(atlasSizes, settings.atlasSize, settings.padding, trial)) {
                atlasIndices.push_back(index);
                atlasLayout = std::move(trial);
            } else {
                atlasSizes.pop_back();
                deferred.push_back(index);
            }
        }
        if (atlasIndices.empty()) {
            return false;
        }

        const uint32_t atlasIndex = static_cast<uint32_t>(result.atlases.size());
        result.atlases.push_back(LightmapAtlasPlan{settings.atlasSize, scale});
        for (std::size_t i = 0; i < atlasIndices.size(); ++i) {
            result.placements[atlasIndices[i]] = LightmapPlacement{atlasIndex, atlasLayout.rects[i], scale};
        }
        remaining = std::move(deferred);
    }

    outResult = std::move(result);
    return true;
}

bool findBestPacking(const std::vector<uint32_t>& sizes, const LightmapPackingSettings& settings, uint32_t atlasLimit,
                     LightmapPackingResult& outResult) {
    LightmapPackingSettings limited = settings;
    limited.maxAtlases = atlasLimit;

    LightmapPackingResult best;
    if (tryPackAtScale(sizes, limited, 1.0f, best)) {
        outResult = std::move(best);
        return true;
    }

    const float lowerLimit = std::clamp(settings.minScale, 0.01f, 1.0f);
    if (lowerLimit >= 1.0f || !tryPackAtScale(sizes, limited, lowerLimit, best)) {
        return false;
    }

    float lower = lowerLimit;
    float upper = 1.0f;
    for (int iteration = 0; iteration < 32 && upper - lower > 1.0e-4f; ++iteration) {
        const float middle = (lower + upper) * 0.5f;
        LightmapPackingResult trial;
        if (tryPackAtScale(sizes, limited, middle, trial)) {
            lower = middle;
            best = std::move(trial);
        } else {
            upper = middle;
        }
    }

    outResult = std::move(best);
    return true;
}

} // namespace

int calculateLightmapSize(const LightmapUvLayout& layout, float scaleInLightmap, int lodIndex,
                          const LightmapSizeSettings& settings) {
    const float uvWidth = std::max(layout.uvMaxTexels.x - layout.uvMinTexels.x, 1.0e-8f);
    const float uvHeight = std::max(layout.uvMaxTexels.y - layout.uvMinTexels.y, 1.0e-8f);
    if (layout.surfaceArea <= 0.0f) {
        return 4;
    }

    const int lod = std::max(lodIndex, 0);
    const float lodScale = std::ldexp(1.0f, -lod);
    const float resolution = std::max(0.0f, settings.texelsPerUnit * scaleInLightmap * lodScale);
    const float normalizedToWorldScale = std::sqrt(layout.surfaceArea / (uvWidth * uvHeight));
    float texelSize = resolution * std::max(uvWidth, uvHeight) * normalizedToWorldScale;
    texelSize = std::clamp(texelSize, 1.0f, static_cast<float>(settings.maxLightmapSize));

    int size = static_cast<int>(std::floor(texelSize));
    int maxSize = static_cast<int>(std::max(settings.maxLightmapSize, 4u));
    if (settings.padding && size > 2) {
        maxSize = std::max(maxSize - 2, 4);
    }
    return std::clamp(size, 4, maxSize);
}

bool packLightmapSquares(const std::vector<uint32_t>& sizes, uint32_t atlasSize, uint32_t padding,
                         LightmapAtlasLayout& outLayout) {
    outLayout = {};
    if (sizes.empty()) {
        outLayout.size = atlasSize;
        return true;
    }
    if (atlasSize == 0u) {
        return false;
    }

    std::vector<uint32_t> order(sizes.size());
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(), [&sizes](uint32_t lhs, uint32_t rhs) {
        if (sizes[lhs] != sizes[rhs]) {
            return sizes[lhs] > sizes[rhs];
        }
        return lhs < rhs;
    });

    if (atlasSize > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    outLayout.size = atlasSize;
    outLayout.rects.resize(sizes.size());
    detail::AtlasNode root(static_cast<int>(atlasSize), static_cast<int>(atlasSize));

    for (uint32_t index : order) {
        const uint32_t size = sizes[index];
        if (size == 0u) {
            outLayout = {};
            return false;
        }
        const uint64_t cell = static_cast<uint64_t>(size) + 2ull * padding;
        if (cell > atlasSize || cell > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            outLayout = {};
            return false;
        }

        detail::AtlasNodeRect node;
        if (!root.insert(static_cast<int>(cell), static_cast<int>(cell), &node)) {
            outLayout = {};
            return false;
        }

        outLayout.rects[index] = LightmapRect{
            static_cast<uint32_t>(node.x + static_cast<int>(padding)),
            static_cast<uint32_t>(node.y + static_cast<int>(padding)), size};
    }

    return true;
}

bool packLightmapAtlases(const std::vector<uint32_t>& sizes, const LightmapPackingSettings& settings,
                         LightmapPackingResult& outResult) {
    if (sizes.empty()) {
        outResult = {};
        return true;
    }
    if (settings.maxAtlases == 0) {
        return false;
    }

    for (uint32_t atlasLimit = 1; atlasLimit <= settings.maxAtlases; ++atlasLimit) {
        LightmapPackingResult result;
        if (findBestPacking(sizes, settings, atlasLimit, result)) {
            outResult = std::move(result);
            return true;
        }
    }
    return false;
}

} // namespace hbg::dawn
