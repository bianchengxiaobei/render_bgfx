#pragma once

#include "codegen.h"
#include "vfx/compiler.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vfx::detail {

struct BinaryBlob {
    ShaderStage stage = ShaderStage::Compute;
    ShaderBackend backend = ShaderBackend::Dxbc;
    std::vector<std::byte> data;
};

std::vector<std::byte> writeAssetBinary(const CompiledAsset& asset,
                                        const std::vector<std::vector<BinaryBlob>>& kernelBlobs,
                                        const CompileOptions& options, std::string& error);

} // namespace vfx::detail
