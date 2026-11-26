#include "hbg/dawn/lightmap_result.h"

#include "hbg/dawn/dawn_package.h"

#include <cstdint>
#include <limits>
#include <sstream>
#include <utility>

namespace hbg::dawn {
namespace {

void setError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
}

std::string sizeText(const Int2& size) {
    return std::to_string(size.x) + "x" + std::to_string(size.y);
}

bool sameGuid(const Guid& left, const Guid& right) {
    return left.a == right.a && left.b == right.b && left.c == right.c && left.d == right.d;
}

bool sameSize(const Int2& left, const Int2& right) {
    return left.x == right.x && left.y == right.y;
}

}  // namespace

bool validateLightmap2DResult(const Lightmap2DOutput& output, const Lightmap2DInput& input, std::string* error) {
    if (!sameGuid(output.header.jobId, input.jobId)) {
        setError(error, "result JobID " + formatGuid(output.header.jobId) + " does not match lightmap input " +
                            formatGuid(input.jobId));
        return false;
    }
    if (!sameGuid(output.header.meshId, input.meshId)) {
        setError(error, "result MeshID " + formatGuid(output.header.meshId) + " does not match lightmap input " +
                            formatGuid(input.meshId));
        return false;
    }
    if (input.size.x <= 0 || input.size.y <= 0) {
        setError(error, "lightmap input size must be positive, got " + sizeText(input.size));
        return false;
    }
    Int2 expectedSize = input.size;
    if ((input.flags & kLightmapFlagPadding) != 0u) {
        expectedSize.x += 2;
        expectedSize.y += 2;
    }
    if (!sameSize(output.header.size, expectedSize)) {
        setError(error, "result size " + sizeText(output.header.size) + " does not match expected " +
                            sizeText(expectedSize) + " (lightmap input " + sizeText(input.size) +
                            ((input.flags & kLightmapFlagPadding) != 0u ? " + padding" : "") + ")");
        return false;
    }

    const uint64_t expectedSamples =
        static_cast<uint64_t>(output.header.size.x) * static_cast<uint64_t>(output.header.size.y);
    if (expectedSamples > std::numeric_limits<std::size_t>::max()) {
        setError(error, "result sample count overflows size " + sizeText(output.header.size));
        return false;
    }
    if (output.lightmapData.size() != static_cast<std::size_t>(expectedSamples)) {
        setError(error, "result sample count " + std::to_string(output.lightmapData.size()) + " does not match size " +
                            sizeText(output.header.size) + " (" + std::to_string(expectedSamples) + ")");
        return false;
    }
    return true;
}

bool bindLightmap2DResult(const SceneInfo& scene, const BakingJobInputs& jobs, const Lightmap2DOutput& output,
                          Lightmap2DResultBinding* binding, std::string* error) {
    const Lightmap2DInput* matchedInput = nullptr;
    std::size_t matchedJobIndex = 0;
    for (std::size_t index = 0; index < jobs.lightmap2DJobs.size(); ++index) {
        const Lightmap2DInput& input = jobs.lightmap2DJobs[index];
        if (!sameGuid(input.jobId, output.header.jobId)) {
            continue;
        }
        if (matchedInput != nullptr) {
            setError(error, "duplicate lightmap job JobID " + formatGuid(output.header.jobId));
            return false;
        }
        matchedInput = &input;
        matchedJobIndex = index;
    }
    if (matchedInput == nullptr) {
        setError(error, "no lightmap job for JobID " + formatGuid(output.header.jobId));
        return false;
    }

    const MeshInstanceInfo* matchedInstance = nullptr;
    std::size_t matchedInstanceIndex = 0;
    for (std::size_t index = 0; index < scene.meshInstances.size(); ++index) {
        const MeshInstanceInfo& instance = scene.meshInstances[index];
        if (!sameGuid(instance.header.guid, output.header.meshId)) {
            continue;
        }
        if (matchedInstance != nullptr) {
            setError(error, "duplicate mesh instance for MeshID " + formatGuid(output.header.meshId));
            return false;
        }
        matchedInstance = &instance;
        matchedInstanceIndex = index;
    }
    if (matchedInstance == nullptr) {
        setError(error, "no mesh instance for MeshID " + formatGuid(output.header.meshId));
        return false;
    }

    if (!validateLightmap2DResult(output, *matchedInput, error)) {
        return false;
    }

    if (binding != nullptr) {
        binding->jobIndex = matchedJobIndex;
        binding->instanceIndex = matchedInstanceIndex;
        binding->input = matchedInput;
        binding->output = &output;
    }
    return true;
}

bool mergeLightmapSplitResults(const Lightmap2DInput& input, const std::vector<const MappingSplitInput*>& splits,
                               const std::vector<const Lightmap2DOutput*>& outputs, Lightmap2DOutput* merged,
                               std::string* error) {
    if (merged == nullptr) {
        setError(error, "merged lightmap output is null");
        return false;
    }
    if (splits.empty() || splits.size() != outputs.size()) {
        setError(error, "lightmap split count mismatch");
        return false;
    }
    if (input.size.x <= 0 || input.size.y <= 0) {
        setError(error, "lightmap input size must be positive, got " + sizeText(input.size));
        return false;
    }

    Int2 fullSize = input.size;
    if ((input.flags & kLightmapFlagPadding) != 0u) {
        fullSize.x += 2;
        fullSize.y += 2;
    }
    const uint64_t fullSamples =
        static_cast<uint64_t>(fullSize.x) * static_cast<uint64_t>(fullSize.y);
    if (fullSamples > std::numeric_limits<std::size_t>::max()) {
        setError(error, "merged lightmap sample count overflows size " + sizeText(fullSize));
        return false;
    }

    Lightmap2DOutput result;
    result.header.jobId = input.jobId;
    result.header.meshId = input.meshId;
    result.header.size = fullSize;
    result.jobInputIndex = outputs.front()->jobInputIndex;
    result.lightmapData.assign(static_cast<std::size_t>(fullSamples), Lightmap2DSample{});
    std::vector<uint8_t> covered(static_cast<std::size_t>(fullSamples), 0u);

    for (std::size_t index = 0; index < splits.size(); ++index) {
        const MappingSplitInput& split = *splits[index];
        const Lightmap2DOutput& output = *outputs[index];
        if (!sameGuid(output.header.jobId, split.jobId)) {
            setError(error, "split result JobID " + formatGuid(output.header.jobId) + " does not match split " +
                                formatGuid(split.jobId));
            return false;
        }
        if (!sameGuid(output.header.meshId, input.meshId)) {
            setError(error, "split result MeshID " + formatGuid(output.header.meshId) +
                                " does not match lightmap input " + formatGuid(input.meshId));
            return false;
        }
        if (split.splitSize <= 0 || split.overlapSize < 0 || split.overlapSize * 2 >= split.splitSize) {
            setError(error, "invalid lightmap split size/overlap");
            return false;
        }
        if (!sameSize(output.header.size, Int2{split.splitSize, split.splitSize})) {
            setError(error, "split result size " + sizeText(output.header.size) + " does not match split size " +
                                sizeText(Int2{split.splitSize, split.splitSize}));
            return false;
        }
        const uint64_t splitSamples =
            static_cast<uint64_t>(split.splitSize) * static_cast<uint64_t>(split.splitSize);
        if (splitSamples > std::numeric_limits<std::size_t>::max() ||
            output.lightmapData.size() != static_cast<std::size_t>(splitSamples)) {
            setError(error, "split result sample count does not match split size");
            return false;
        }
        if (split.mappingOffsets.x < 0 || split.mappingOffsets.y < 0) {
            setError(error, "split offset must not be negative");
            return false;
        }

        const int32_t copyStartX = split.mappingOffsets.x == 0 ? 0 : split.overlapSize;
        const int32_t copyStartY = split.mappingOffsets.y == 0 ? 0 : split.overlapSize;
        int32_t copyEndX = split.splitSize - split.overlapSize;
        int32_t copyEndY = split.splitSize - split.overlapSize;
        if (split.mappingOffsets.x + split.splitSize >= fullSize.x) {
            copyEndX = split.splitSize;
        }
        if (split.mappingOffsets.y + split.splitSize >= fullSize.y) {
            copyEndY = split.splitSize;
        }

        for (int32_t y = copyStartY; y < copyEndY; ++y) {
            const int32_t outY = split.mappingOffsets.y + y;
            if (outY >= fullSize.y) {
                break;
            }
            for (int32_t x = copyStartX; x < copyEndX; ++x) {
                const int32_t outX = split.mappingOffsets.x + x;
                if (outX >= fullSize.x) {
                    break;
                }
                const std::size_t sourceIndex =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(split.splitSize) +
                    static_cast<std::size_t>(x);
                const std::size_t targetIndex =
                    static_cast<std::size_t>(outY) * static_cast<std::size_t>(fullSize.x) +
                    static_cast<std::size_t>(outX);
                result.lightmapData[targetIndex] = output.lightmapData[sourceIndex];
                covered[targetIndex] = 1u;
            }
        }

        result.header.executionTime += output.header.executionTime;
        result.bHasPrtHqTextures = static_cast<uint8_t>(result.bHasPrtHqTextures | output.bHasPrtHqTextures);
        result.bHasPrtLqTextures = static_cast<uint8_t>(result.bHasPrtLqTextures | output.bHasPrtLqTextures);
        result.bHasStaticTextures = static_cast<uint8_t>(result.bHasStaticTextures | output.bHasStaticTextures);
        if (result.lights.empty()) {
            result.lights = output.lights;
        }
    }

    for (std::size_t index = 0; index < covered.size(); ++index) {
        if (covered[index] == 0u) {
            setError(error, "lightmap split results do not cover the full lightmap");
            return false;
        }
    }

    *merged = std::move(result);
    return true;
}

}  // namespace hbg::dawn
