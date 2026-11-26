#pragma once

#include "hbg/analysis/report.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hbg::analysis {

enum class DiffStatus {
    Same,
    Changed,
    Added,
    Removed,
    Incomparable,
};

std::string diffStatusToString(DiffStatus status);
DiffStatus diffStatusFromString(std::string_view value);

struct DiffReportSide {
    std::string root;
    std::optional<std::string> generatedAt;

    bool operator==(const DiffReportSide&) const = default;
};

struct DiffEntry {
    std::string backend;
    std::string source;
    std::string stage;
    std::string variantId;
    std::vector<std::string> canonicalDefines;
    std::map<std::string, std::int64_t> metricDeltas;
    DiffStatus status = DiffStatus::Same;

    bool operator==(const DiffEntry&) const = default;
};

struct DiffVariantRef {
    std::string backend;
    std::string source;
    std::string stage;
    std::string variantId;
    std::vector<std::string> canonicalDefines;
    DiffStatus status = DiffStatus::Added;

    bool operator==(const DiffVariantRef&) const = default;
};

struct ReportDiff {
    int schemaVersion = kReportSchemaVersion;
    DiffReportSide current;
    DiffReportSide reference;
    std::vector<DiffEntry> entries;
    std::vector<DiffVariantRef> addedVariants;
    std::vector<DiffVariantRef> removedVariants;
    bool sourceHashChanged = false;
    bool toolchainChanged = false;

    bool operator==(const ReportDiff&) const = default;
};

ReportDiff diffReports(const std::filesystem::path& currentReportsDir,
                       const std::filesystem::path& referenceReportsDir,
                       bool ignoreTimestamps = false);

bool hasRegression(const ReportDiff& diff);

std::string toJson(const ReportDiff& diff, bool pretty = true);

} // namespace hbg::analysis
