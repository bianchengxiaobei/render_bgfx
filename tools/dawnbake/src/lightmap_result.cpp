#include "hbg/dawn/lightmap_result.h"

#include "hbg/dawn/dawn_package.h"

#include <cstdint>
#include <limits>
#include <sstream>

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

}  // namespace hbg::dawn
