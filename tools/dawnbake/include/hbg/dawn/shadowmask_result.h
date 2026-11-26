#pragma once

#include "hbg/dawn/dawn_protocol.h"

#include <cstddef>
#include <string>

namespace hbg::dawn {

struct SdfShadowResultBinding {
    std::size_t jobIndex = 0;
    std::size_t instanceIndex = 0;
    const SdfShadowInput* input = nullptr;
    const SdfShadowOutput* output = nullptr;
};

bool validateSdfShadowResult(const SdfShadowOutput& output, const SdfShadowInput& input, std::string* error);
bool bindSdfShadowResult(const SceneInfo& scene, const BakingJobInputs& jobs, const SdfShadowOutput& output,
                         SdfShadowResultBinding* binding, std::string* error);

}  // namespace hbg::dawn
