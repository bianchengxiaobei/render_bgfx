#pragma once

#include "hbg/analysis/variant_manifest.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace hbg::analysis {

struct CompileCommand {
    std::string source;
    std::string output;
    std::vector<std::string> args;
};

struct CompileCommandOptions {
    bool debugInfo = false;
    bool disasm = false;
    bool depends = false;
    std::optional<std::filesystem::path> outputPath;
};

CompileCommand buildCompileCommand(const VariantManifest& manifest, const CompileTask& task,
                                   const std::filesystem::path& root,
                                   const std::filesystem::path& bgfxSrc,
                                   const std::filesystem::path& shaderc,
                                   const CompileCommandOptions& options = {});

std::string formatCommandLine(const std::vector<std::string>& args);

} // namespace hbg::analysis
