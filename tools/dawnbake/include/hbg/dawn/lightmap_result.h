#pragma once

#include "hbg/dawn/dawn_protocol.h"

#include <cstddef>
#include <string>

namespace hbg::dawn {

struct Lightmap2DResultBinding {
    std::size_t jobIndex = 0;
    std::size_t instanceIndex = 0;
    const Lightmap2DInput* input = nullptr;
    const Lightmap2DOutput* output = nullptr;
};

bool validateLightmap2DResult(const Lightmap2DOutput& output, const Lightmap2DInput& input, std::string* error);
bool bindLightmap2DResult(const SceneInfo& scene, const BakingJobInputs& jobs, const Lightmap2DOutput& output,
                          Lightmap2DResultBinding* binding, std::string* error);

}  // namespace hbg::dawn
