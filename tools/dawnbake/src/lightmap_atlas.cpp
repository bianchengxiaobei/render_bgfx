#include "hbg/dawn/lightmap_atlas.h"

#include "hbg/dawn/dawn_package.h"
#include "hbg/dawn/lightmap_result.h"

#include <bx/math.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace hbg::dawn {
namespace {

void setError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
}

std::string sizeText(uint32_t width, uint32_t height) {
    return std::to_string(width) + "x" + std::to_string(height);
}

struct AtlasRect {
    uint64_t x = 0;
    uint64_t y = 0;
    uint64_t width = 0;
    uint64_t height = 0;
};

bool overlaps(const AtlasRect& left, const AtlasRect& right) {
    return left.x < right.x + right.width && right.x < left.x + left.width && left.y < right.y + right.height &&
           right.y < left.y + left.height;
}

bool prepareAtlasImages(const std::vector<uint32_t>& atlasSizes, LightmapAtlasImageSet* images, std::string* error) {
    images->atlases.clear();
    images->texelsCopied = 0;
    images->atlases.resize(atlasSizes.size());
    for (std::size_t index = 0; index < atlasSizes.size(); ++index) {
        const uint32_t size = atlasSizes[index];
        if (size == 0u) {
            setError(error, "lightmap atlas " + std::to_string(index) + " has zero size");
            return false;
        }
        const uint64_t pixels = static_cast<uint64_t>(size) * static_cast<uint64_t>(size);
        if (pixels > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max() / 4u)) {
            setError(error, "lightmap atlas " + std::to_string(index) + " size is too large");
            return false;
        }
        LightmapAtlasImage& image = images->atlases[index];
        image.size = size;
        for (std::vector<uint16_t>& channel : image.sh) {
            channel.assign(static_cast<std::size_t>(pixels) * 4u, uint16_t{0});
        }
        image.mapped.assign(static_cast<std::size_t>(pixels), uint8_t{0});
    }
    return true;
}

void dilateBlock(std::array<std::vector<uint16_t>, 3>* block, std::vector<uint8_t>* valid, uint32_t width,
                 uint32_t height, uint32_t iterations) {
    if (iterations == 0u) {
        return;
    }
    bool anyValid = false;
    bool anyInvalid = false;
    for (const uint8_t value : *valid) {
        anyValid = anyValid || value != 0u;
        anyInvalid = anyInvalid || value == 0u;
    }
    if (!anyValid || !anyInvalid) {
        return;
    }

    constexpr int kNeighborOffsets[8][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1},
    };
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (uint32_t iteration = 0; iteration < iterations; ++iteration) {
        std::array<std::vector<uint16_t>, 3> next;
        for (std::vector<uint16_t>& channel : next) {
            channel.assign(pixels * 4u, uint16_t{0});
        }
        std::vector<uint8_t> nextValid(pixels, uint8_t{0});

        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * width + x;
                const std::size_t texel = index * 4u;
                if ((*valid)[index] != 0u) {
                    for (std::size_t channel = 0; channel < block->size(); ++channel) {
                        for (std::size_t component = 0; component < 4u; ++component) {
                            next[channel][texel + component] = (*block)[channel][texel + component];
                        }
                    }
                    nextValid[index] = 1u;
                    continue;
                }

                float sum[3][4] = {};
                uint32_t count = 0u;
                for (const auto& offset : kNeighborOffsets) {
                    const int neighborX = static_cast<int>(x) + offset[0];
                    const int neighborY = static_cast<int>(y) + offset[1];
                    if (neighborX < 0 || neighborY < 0 || neighborX >= static_cast<int>(width) ||
                        neighborY >= static_cast<int>(height)) {
                        continue;
                    }
                    const std::size_t neighborIndex =
                        static_cast<std::size_t>(neighborY) * width + static_cast<std::size_t>(neighborX);
                    if ((*valid)[neighborIndex] == 0u) {
                        continue;
                    }
                    const std::size_t neighborTexel = neighborIndex * 4u;
                    for (std::size_t channel = 0; channel < block->size(); ++channel) {
                        for (std::size_t component = 0; component < 4u; ++component) {
                            sum[channel][component] += bx::halfToFloat((*block)[channel][neighborTexel + component]);
                        }
                    }
                    ++count;
                }

                if (count != 0u) {
                    for (std::size_t channel = 0; channel < block->size(); ++channel) {
                        for (std::size_t component = 0; component < 4u; ++component) {
                            next[channel][texel + component] =
                                bx::halfFromFloat(sum[channel][component] / static_cast<float>(count));
                        }
                    }
                    nextValid[index] = 1u;
                } else {
                    for (std::size_t channel = 0; channel < block->size(); ++channel) {
                        for (std::size_t component = 0; component < 4u; ++component) {
                            next[channel][texel + component] = (*block)[channel][texel + component];
                        }
                    }
                }
            }
        }

        *block = std::move(next);
        *valid = std::move(nextValid);
    }
}

} // namespace

Float4 lightmapAtlasTexel(const Lightmap2DSample& sample, std::size_t channel) {
    switch (channel) {
    case 0:
        return sample.incidentLighting;
    case 1:
        return sample.shVectorR;
    default:
        return Float4{};
    }
}

bool buildLightmapAtlasImages(const SceneInfo& scene, const BakingJobInputs& jobs,
                              const std::vector<LightmapAtlasResult>& results, const std::vector<uint32_t>& atlasSizes,
                              LightmapAtlasImageSet* images, std::string* error, const LightmapAtlasOptions& options) {
    if (images == nullptr) {
        setError(error, "lightmap atlas output is null");
        return false;
    }
    if (!prepareAtlasImages(atlasSizes, images, error)) {
        return false;
    }

    std::vector<std::vector<AtlasRect>> occupied(atlasSizes.size());
    for (const LightmapAtlasResult& result : results) {
        if (result.output == nullptr) {
            setError(error, "lightmap atlas result has no output");
            return false;
        }
        const Lightmap2DOutput& output = *result.output;
        Lightmap2DResultBinding binding;
        if (!bindLightmap2DResult(scene, jobs, output, &binding, error)) {
            return false;
        }
        const Lightmap2DInput& input = *binding.input;
        const LightmapAtlasPlacement& placement = result.placement;
        if (placement.atlasIndex >= atlasSizes.size()) {
            setError(error, "lightmap result " + formatGuid(output.header.jobId) + " references atlas " +
                                std::to_string(placement.atlasIndex) + " of " + std::to_string(atlasSizes.size()));
            return false;
        }
        if (placement.width == 0u || placement.height == 0u) {
            setError(error, "lightmap result " + formatGuid(output.header.jobId) + " has an empty atlas rect");
            return false;
        }
        if (placement.width != static_cast<uint32_t>(input.size.x) ||
            placement.height != static_cast<uint32_t>(input.size.y)) {
            setError(error, "lightmap result " + formatGuid(output.header.jobId) + " atlas rect " +
                                sizeText(placement.width, placement.height) + " does not match input size " +
                                sizeText(static_cast<uint32_t>(input.size.x), static_cast<uint32_t>(input.size.y)));
            return false;
        }

        const uint32_t padding = (input.flags & kLightmapFlagPadding) != 0u ? 1u : 0u;
        if (placement.x < padding || placement.y < padding) {
            setError(error, "lightmap result " + formatGuid(output.header.jobId) +
                                " atlas rect does not leave room for padding " + std::to_string(padding));
            return false;
        }
        const uint64_t atlasSize = atlasSizes[placement.atlasIndex];
        const AtlasRect target{
            static_cast<uint64_t>(placement.x - padding), static_cast<uint64_t>(placement.y - padding),
            static_cast<uint64_t>(output.header.size.x), static_cast<uint64_t>(output.header.size.y)};
        if (target.width == 0u || target.height == 0u || target.x + target.width > atlasSize ||
            target.y + target.height > atlasSize) {
            setError(error, "lightmap result " + formatGuid(output.header.jobId) + " target rect " +
                                sizeText(static_cast<uint32_t>(target.width), static_cast<uint32_t>(target.height)) +
                                " at " + std::to_string(target.x) + "," + std::to_string(target.y) +
                                " is outside atlas " + std::to_string(placement.atlasIndex));
            return false;
        }
        uint32_t edgePadding = options.edgePadding;
        if (padding != 0u) {
            edgePadding = static_cast<uint32_t>(std::min<uint64_t>(
                {edgePadding, target.x, target.y, atlasSize - target.x - target.width,
                 atlasSize - target.y - target.height}));
        } else if (target.x < edgePadding || target.y < edgePadding ||
                   target.x + target.width + edgePadding > atlasSize ||
                   target.y + target.height + edgePadding > atlasSize) {
            setError(error, "lightmap result " + formatGuid(output.header.jobId) + " target rect " +
                                sizeText(static_cast<uint32_t>(target.width), static_cast<uint32_t>(target.height)) +
                                " at " + std::to_string(target.x) + "," + std::to_string(target.y) +
                                " is outside atlas " + std::to_string(placement.atlasIndex) +
                                " (does not fit with edge padding " + std::to_string(edgePadding) + ")");
            return false;
        }
        const AtlasRect footprint{target.x - edgePadding, target.y - edgePadding, target.width + 2u * edgePadding,
                                  target.height + 2u * edgePadding};
        for (const AtlasRect& used : occupied[placement.atlasIndex]) {
            if (overlaps(footprint, used)) {
                setError(error, "lightmap result " + formatGuid(output.header.jobId) +
                                    " overlaps another result in atlas " + std::to_string(placement.atlasIndex) +
                                    " including edge padding");
                return false;
            }
        }
        occupied[placement.atlasIndex].push_back(footprint);

        const uint32_t width = static_cast<uint32_t>(target.width);
        const uint32_t height = static_cast<uint32_t>(target.height);
        const uint32_t blockWidth = width + 2u * edgePadding;
        const uint32_t blockHeight = height + 2u * edgePadding;
        const std::size_t blockPixels = static_cast<std::size_t>(blockWidth) * static_cast<std::size_t>(blockHeight);
        std::array<std::vector<uint16_t>, 3> block;
        for (std::vector<uint16_t>& channel : block) {
            channel.assign(blockPixels * 4u, uint16_t{0});
        }
        std::vector<uint8_t> valid(blockPixels, uint8_t{0});
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const std::size_t sourceIndex = static_cast<std::size_t>(y) * width + x;
                const Lightmap2DSample& sample = output.lightmapData[sourceIndex];
                const std::size_t blockIndex =
                    static_cast<std::size_t>(y + edgePadding) * blockWidth + static_cast<std::size_t>(x + edgePadding);
                const std::size_t texel = blockIndex * 4u;
                for (std::size_t channel = 0; channel < block.size(); ++channel) {
                    const Float4 value = lightmapAtlasTexel(sample, channel);
                    block[channel][texel + 0u] = bx::halfFromFloat(value.x);
                    block[channel][texel + 1u] = bx::halfFromFloat(value.y);
                    block[channel][texel + 2u] = bx::halfFromFloat(value.z);
                    block[channel][texel + 3u] = bx::halfFromFloat(value.w);
                }
                valid[blockIndex] = sample.bIsMapped != 0u ? uint8_t{1} : uint8_t{0};
            }
        }

        dilateBlock(&block, &valid, blockWidth, blockHeight, options.dilationIterations);

        LightmapAtlasImage& image = images->atlases[placement.atlasIndex];
        for (uint32_t y = 0; y < blockHeight; ++y) {
            for (uint32_t x = 0; x < blockWidth; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * blockWidth + x;
                const std::size_t sourceTexel = index * 4u;
                const std::size_t targetIndex =
                    static_cast<std::size_t>(target.y - edgePadding + y) * static_cast<std::size_t>(atlasSize) +
                    static_cast<std::size_t>(target.x - edgePadding + x);
                const std::size_t targetTexel = targetIndex * 4u;
                for (std::size_t channel = 0; channel < image.sh.size(); ++channel) {
                    for (std::size_t component = 0; component < 4u; ++component) {
                        image.sh[channel][targetTexel + component] = block[channel][sourceTexel + component];
                    }
                }
                image.mapped[targetIndex] = valid[index];
            }
        }
        images->texelsCopied += output.lightmapData.size();
    }
    return true;
}

} // namespace hbg::dawn
