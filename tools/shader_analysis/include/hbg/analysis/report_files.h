#pragma once

#include "hbg/analysis/report.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace hbg::analysis {

struct LoadedSummary {
    std::filesystem::path path;
    std::string relativePath;
    ReportSummary summary;

    bool operator==(const LoadedSummary&) const = default;
};

std::vector<LoadedSummary> loadReportSummaries(const std::filesystem::path& reportsDir);
std::vector<LoadedSummary> loadReportSummariesFromPath(const std::filesystem::path& path);

ReportIndex buildReportIndex(const std::vector<LoadedSummary>& summaries,
                             std::string generatedAt);

std::size_t writeReportIndex(const std::filesystem::path& reportsDir);

} // namespace hbg::analysis
