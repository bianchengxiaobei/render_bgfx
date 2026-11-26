#pragma once

#include "hbg/analysis/report.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace hbg::analysis {

std::string buildCsv(const std::vector<ReportSummary>& summaries);

std::size_t exportCsv(const std::filesystem::path& reportPath,
                      const std::filesystem::path& outputPath);

} // namespace hbg::analysis
