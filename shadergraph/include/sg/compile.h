#pragma once

#include "sg/graph.h"
#include "sg/registry.h"
#include "sg/subgraph.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace sg {

enum class Backend : std::uint8_t {
    Dxbc = 0,
    Spirv = 1,
};

enum class VariantPolicy : std::uint8_t {
    Used,
    All,
};

struct CompileOptions {
    std::filesystem::path rootDirectory;
    std::filesystem::path shaderc;
    std::filesystem::path bgfxSource;
    std::filesystem::path cacheDirectory;
    std::vector<Backend> backends{Backend::Dxbc, Backend::Spirv};
    VariantPolicy variants = VariantPolicy::Used;
    bool keepHlsl = false;
    int maxVariants = 64;
    int shaderTimeoutSeconds = 600;
    SubGraphLibrary subgraphs;
};

struct CompileStats {
    int activeNodes = 0;
    int passes = 0;
    int variants = 1;
    int shaders = 0;
    int cacheHits = 0;
    double elapsedSeconds = 0.0;
};

struct CompileResult {
    bool ok = false;
    std::vector<std::uint8_t> binary;
    std::vector<Diagnostic> diagnostics;
    std::string error;
    CompileStats stats;
};

CompileResult compile(const Graph& graph, const NodeRegistry& registry, const CompileOptions& options);
CompileResult compile(const Graph& graph, const CompileOptions& options);

std::string generatorVersion();

} // namespace sg
