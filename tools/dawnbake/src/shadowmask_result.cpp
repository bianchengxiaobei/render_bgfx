#include "hbg/dawn/shadowmask_result.h"

#include "hbg/dawn/dawn_package.h"

#include <cstdint>
#include <limits>
#include <string>

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

bool validateSdfShadowResult(const SdfShadowOutput& output, const SdfShadowInput& input, std::string* error) {
    if (!sameGuid(output.header.jobId, input.header.jobId)) {
        setError(error, "result JobID " + formatGuid(output.header.jobId) + " does not match SDF job " +
                            formatGuid(input.header.jobId));
        return false;
    }
    if (!sameGuid(output.header.meshId, input.header.meshId)) {
        setError(error, "result MeshID " + formatGuid(output.header.meshId) + " does not match SDF mesh " +
                            formatGuid(input.header.meshId));
        return false;
    }
    if (input.header.size.x <= 0 || input.header.size.y <= 0) {
        setError(error, "SDF input size must be positive, got " + sizeText(input.header.size));
        return false;
    }

    Int2 expectedSize = input.header.size;
    if ((input.header.flags & kLightmapFlagPadding) != 0u) {
        expectedSize.x += 2;
        expectedSize.y += 2;
    }
    if (!sameSize(output.header.size, expectedSize)) {
        setError(error, "result size " + sizeText(output.header.size) + " does not match expected " +
                            sizeText(expectedSize) + " (SDF input " + sizeText(input.header.size) +
                            ((input.header.flags & kLightmapFlagPadding) != 0u ? " + padding" : "") + ")");
        return false;
    }

    const uint64_t expectedSamples =
        static_cast<uint64_t>(output.header.size.x) * static_cast<uint64_t>(output.header.size.y);
    if (expectedSamples > std::numeric_limits<std::size_t>::max()) {
        setError(error, "SDF result sample count overflows size " + sizeText(output.header.size));
        return false;
    }
    if (output.shadowMaps.size() != input.shadowmaskLights.size()) {
        setError(error, "SDF result light count " + std::to_string(output.shadowMaps.size()) +
                            " does not match input shadowmask light count " +
                            std::to_string(input.shadowmaskLights.size()));
        return false;
    }
    for (std::size_t index = 0; index < output.shadowMaps.size(); ++index) {
        const SdfShadowMap& map = output.shadowMaps[index];
        if (!sameGuid(map.lightId, input.shadowmaskLights[index])) {
            setError(error, "SDF result light[" + std::to_string(index) + "] LightID " +
                                formatGuid(map.lightId) + " does not match input shadowmask light " +
                                formatGuid(input.shadowmaskLights[index]));
            return false;
        }
        if (map.shadowData.size() != static_cast<std::size_t>(expectedSamples)) {
            setError(error, "SDF result light[" + std::to_string(index) + "] sample count " +
                                std::to_string(map.shadowData.size()) + " does not match size " +
                                sizeText(output.header.size) + " (" + std::to_string(expectedSamples) + ")");
            return false;
        }
    }
    return true;
}

bool bindSdfShadowResult(const SceneInfo& scene, const BakingJobInputs& jobs, const SdfShadowOutput& output,
                         SdfShadowResultBinding* binding, std::string* error) {
    const SdfShadowInput* matchedInput = nullptr;
    std::size_t matchedJobIndex = 0;
    for (std::size_t index = 0; index < jobs.sdfShadowJobs.size(); ++index) {
        const SdfShadowInput& input = jobs.sdfShadowJobs[index];
        if (!sameGuid(input.header.jobId, output.header.jobId)) {
            continue;
        }
        if (matchedInput != nullptr) {
            setError(error, "duplicate SDF job JobID " + formatGuid(output.header.jobId));
            return false;
        }
        matchedInput = &input;
        matchedJobIndex = index;
    }
    if (matchedInput == nullptr) {
        setError(error, "no SDF job for JobID " + formatGuid(output.header.jobId));
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

    if (!validateSdfShadowResult(output, *matchedInput, error)) {
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
