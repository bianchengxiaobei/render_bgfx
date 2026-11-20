#pragma once

#include "vfx/device.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace vfx {

struct CompileOptions {
    std::filesystem::path shaderPack;
    std::filesystem::path shaderc;
    std::filesystem::path bgfxSource;
    std::vector<ShaderBackend> backends{ShaderBackend::Dxbc, ShaderBackend::Spirv};
    bool debugInfo = false;
};

struct CompileResult {
    std::vector<std::byte> binary;
    std::string error;

    bool ok() const noexcept { return error.empty(); }
};

CompileResult compileGraph(std::string_view json, const CompileOptions& options);
CompileResult compileGraphFile(const std::filesystem::path& path, const CompileOptions& options);

} // namespace vfx
