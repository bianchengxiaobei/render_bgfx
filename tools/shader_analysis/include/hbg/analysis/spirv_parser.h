#pragma once

#include "hbg/analysis/report.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hbg::analysis {

struct SpirvReflection {
    std::vector<Resource> resources;
    std::optional<std::int64_t> uniformBufferBytes;
    std::optional<std::int64_t> pushConstantBytes;
    bool available = false;
};

struct SpirvAnalysisOptions {
    bool debugInfo = false;
};

struct SpirvAnalysisResult {
    DetailReport detail;
    Metrics metrics;
};

SpirvReflection parseSpirvReflectionJson(std::string_view text);

SpirvAnalysisResult analyzeSpirvDisassembly(std::string_view disassembly,
                                            const SpirvReflection& reflection,
                                            const SpirvAnalysisOptions& options,
                                            std::string source, std::string stage,
                                            std::string variantId);

std::filesystem::path findSpirvTool(const std::filesystem::path& configured,
                                    std::string_view name);

} // namespace hbg::analysis
