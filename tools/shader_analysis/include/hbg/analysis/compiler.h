#pragma once

#include "hbg/analysis/report.h"
#include "hbg/analysis/variant_manifest.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace hbg::analysis {

struct ShadercLogs {
    std::vector<LogEntry> warnings;
    std::vector<LogEntry> errors;

    bool operator==(const ShadercLogs&) const = default;
};

ShadercLogs parseShadercLogs(std::string_view stdoutText, std::string_view stderrText,
                             bool succeeded);

struct CompilerOptions {
    std::filesystem::path root;
    std::filesystem::path reportsDir;
    std::filesystem::path shaderc;
    std::filesystem::path bgfxSrc;
    std::filesystem::path spirvDis;
    std::filesystem::path spirvCross;
    std::string toolVersion;
    std::string progress = "plain";
    int jobs = 8;
    int timeoutSeconds = 300;
    int retries = 0;
    bool force = false;
    bool debugInfo = false;
    bool keepTemp = false;
    bool noCache = false;
    bool failFast = false;
};

struct CompileRunResult {
    std::string shadercVersion;
    std::vector<VariantReport> variants;
    bool cancelled = false;
    bool anyFailed = false;
};

CompileRunResult compileVariants(const VariantManifest& manifest,
                                 const std::vector<CompileTask>& tasks,
                                 const CompilerOptions& options);

std::size_t writeSummaries(const VariantManifest& manifest,
                           const std::vector<CompileTask>& tasks,
                           const std::vector<VariantReport>& variants,
                           const CompilerOptions& options,
                           std::string_view shadercVersion);

} // namespace hbg::analysis
