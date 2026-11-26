#include "hbg/analysis/diff.h"

#include "hbg/analysis/hashing.h"
#include "hbg/analysis/report_files.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace hbg::analysis {
namespace {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;

using OptionalInt = std::optional<std::int64_t> Metrics::*;

struct MetricField {
    const char* name;
    OptionalInt member;
};

const MetricField kMetricFields[] = {
    {"instructionCount", &Metrics::instructionCount},
    {"tempRegisterMax", &Metrics::tempRegisterMax},
    {"tempRegisterPeak", &Metrics::tempRegisterPeak},
    {"inputCount", &Metrics::inputCount},
    {"outputCount", &Metrics::outputCount},
    {"cbufferSlotCount", &Metrics::cbufferSlotCount},
    {"cbufferDwordsRead", &Metrics::cbufferDwordsRead},
    {"dynamicCBufferReadCount", &Metrics::dynamicCBufferReadCount},
    {"textureCount", &Metrics::textureCount},
    {"samplerCount", &Metrics::samplerCount},
    {"uavCount", &Metrics::uavCount},
    {"structuredBufferCount", &Metrics::structuredBufferCount},
    {"sampleCount", &Metrics::sampleCount},
    {"loadCount", &Metrics::loadCount},
    {"storeCount", &Metrics::storeCount},
    {"branchCount", &Metrics::branchCount},
    {"loopCount", &Metrics::loopCount},
    {"ssaValueCount", &Metrics::ssaValueCount},
    {"maxLiveSsaValues", &Metrics::maxLiveSsaValues},
    {"imageCount", &Metrics::imageCount},
    {"uniformBufferCount", &Metrics::uniformBufferCount},
    {"storageBufferCount", &Metrics::storageBufferCount},
    {"pushConstantCount", &Metrics::pushConstantCount},
    {"uniformBufferBytes", &Metrics::uniformBufferBytes},
    {"pushConstantBytes", &Metrics::pushConstantBytes},
    {"accessChainCount", &Metrics::accessChainCount},
    {"sourceLineCount", &Metrics::sourceLineCount},
    {"warningCount", &Metrics::warningCount},
    {"errorCount", &Metrics::errorCount},
    {"microCodeSize", &Metrics::microCodeSize},
    {"sgprCount", &Metrics::sgprCount},
    {"sgprUsedCount", &Metrics::sgprUsedCount},
    {"userSgprCount", &Metrics::userSgprCount},
    {"ldsSize", &Metrics::ldsSize},
    {"threadGroupWaves", &Metrics::threadGroupWaves},
    {"simdOccupancyCount", &Metrics::simdOccupancyCount},
    {"simdOccupancyMax", &Metrics::simdOccupancyMax},
    {"cuOccupancyCount", &Metrics::cuOccupancyCount},
    {"cuOccupancyMax", &Metrics::cuOccupancyMax},
};

struct ProgramKey {
    std::string backend;
    std::string source;
    std::string stage;

    bool operator<(const ProgramKey& other) const {
        return std::tie(backend, source, stage) <
               std::tie(other.backend, other.source, other.stage);
    }
};

struct ProgramData {
    const ReportSummary* summary = nullptr;
    std::map<std::string, const VariantReport*> variants;
};

using ProgramMap = std::map<ProgramKey, ProgramData>;

std::string variantKey(const VariantReport& variant) {
    return sha256Hex(joinCanonicalDefines(variant.canonicalDefines)).substr(0, 12);
}

ProgramMap indexPrograms(const std::vector<LoadedSummary>& summaries) {
    ProgramMap programs;
    for (const LoadedSummary& loaded : summaries) {
        const ProgramKey key{loaded.summary.backend, loaded.summary.source.path,
                             loaded.summary.stage};
        const auto [it, inserted] = programs.emplace(key, ProgramData{});
        if (!inserted) {
            throw std::runtime_error("duplicate report program: " + key.backend + "/" +
                                     key.source + "/" + key.stage);
        }
        ProgramData& data = it->second;
        data.summary = &loaded.summary;
        for (const VariantReport& variant : loaded.summary.variants) {
            const std::string id = variantKey(variant);
            if (!data.variants.emplace(id, &variant).second) {
                throw std::runtime_error("duplicate variant defines in " + key.source + "/" +
                                         key.stage + "/" + key.backend);
            }
        }
    }
    return programs;
}

std::optional<std::string> latestTimestamp(
    const std::vector<LoadedSummary>& summaries) {
    std::optional<std::string> latest;
    for (const LoadedSummary& loaded : summaries) {
        if (loaded.summary.generatedAt.empty()) {
            continue;
        }
        if (!latest.has_value() || *latest < loaded.summary.generatedAt) {
            latest = loaded.summary.generatedAt;
        }
    }
    return latest;
}

std::string normalizedRoot(const fs::path& path) {
    return fs::absolute(path).lexically_normal().generic_string();
}

bool compareMetrics(const VariantReport& current, const VariantReport& reference,
                    DiffEntry& entry) {
    if (!current.metrics.has_value() || !reference.metrics.has_value()) {
        return false;
    }
    const Metrics& left = *current.metrics;
    const Metrics& right = *reference.metrics;
    bool changed = false;

    for (const MetricField& field : kMetricFields) {
        const std::optional<std::int64_t> l = left.*field.member;
        const std::optional<std::int64_t> r = right.*field.member;
        if (l.has_value() != r.has_value()) {
            changed = true;
        } else if (l.has_value() && *l != *r) {
            entry.metricDeltas[field.name] = *l - *r;
            changed = true;
        }
    }

    if (current.binaryBytes.has_value() != reference.binaryBytes.has_value()) {
        changed = true;
    } else if (current.binaryBytes.has_value() &&
               *current.binaryBytes != *reference.binaryBytes) {
        entry.metricDeltas["binaryBytes"] =
            *current.binaryBytes - *reference.binaryBytes;
        changed = true;
    }

    if (left.debugInfo != right.debugInfo ||
        left.opcodeHistogram != right.opcodeHistogram) {
        changed = true;
    }
    return changed;
}

DiffVariantRef makeRef(const ProgramKey& key, const VariantReport& variant,
                       DiffStatus status) {
    DiffVariantRef ref;
    ref.backend = key.backend;
    ref.source = key.source;
    ref.stage = key.stage;
    ref.variantId = variant.variantId;
    ref.canonicalDefines = variant.canonicalDefines;
    ref.status = status;
    return ref;
}

Json encodeSide(const DiffReportSide& side) {
    Json result = Json::object();
    result["root"] = side.root;
    result["generatedAt"] = side.generatedAt.has_value()
                                ? Json(*side.generatedAt)
                                : Json(nullptr);
    return result;
}

Json encodeVariantRef(const DiffVariantRef& ref) {
    Json result = Json::object();
    result["backend"] = ref.backend;
    result["source"] = ref.source;
    result["stage"] = ref.stage;
    result["variantId"] = ref.variantId;
    result["canonicalDefines"] = ref.canonicalDefines;
    result["status"] = diffStatusToString(ref.status);
    return result;
}

bool isRegressionMetric(std::string_view name) {
    return name == "binaryBytes" || name == "instructionCount" ||
           name == "tempRegisterMax" || name == "tempRegisterPeak" ||
           name == "ssaValueCount" || name == "maxLiveSsaValues" ||
           name == "cbufferDwordsRead";
}

} // namespace

std::string diffStatusToString(DiffStatus status) {
    switch (status) {
    case DiffStatus::Same:
        return "same";
    case DiffStatus::Changed:
        return "changed";
    case DiffStatus::Added:
        return "added";
    case DiffStatus::Removed:
        return "removed";
    case DiffStatus::Incomparable:
        return "incomparable";
    }
    return "incomparable";
}

DiffStatus diffStatusFromString(std::string_view value) {
    if (value == "same") {
        return DiffStatus::Same;
    }
    if (value == "changed") {
        return DiffStatus::Changed;
    }
    if (value == "added") {
        return DiffStatus::Added;
    }
    if (value == "removed") {
        return DiffStatus::Removed;
    }
    if (value == "incomparable") {
        return DiffStatus::Incomparable;
    }
    throw std::invalid_argument("unknown diff status '" + std::string(value) + "'");
}

ReportDiff diffReports(const fs::path& currentReportsDir,
                       const fs::path& referenceReportsDir, bool ignoreTimestamps) {
    const std::vector<LoadedSummary> currentSummaries =
        loadReportSummaries(currentReportsDir);
    const std::vector<LoadedSummary> referenceSummaries =
        loadReportSummaries(referenceReportsDir);

    ReportDiff diff;
    diff.current.root = normalizedRoot(currentReportsDir);
    diff.reference.root = normalizedRoot(referenceReportsDir);
    if (!ignoreTimestamps) {
        diff.current.generatedAt = latestTimestamp(currentSummaries);
        diff.reference.generatedAt = latestTimestamp(referenceSummaries);
    }

    const ProgramMap current = indexPrograms(currentSummaries);
    const ProgramMap reference = indexPrograms(referenceSummaries);

    std::set<ProgramKey> keys;
    for (const auto& [key, value] : current) {
        keys.insert(key);
    }
    for (const auto& [key, value] : reference) {
        keys.insert(key);
    }

    for (const ProgramKey& key : keys) {
        const auto currentIt = current.find(key);
        const auto referenceIt = reference.find(key);
        if (currentIt == current.end()) {
            for (const auto& variant : referenceIt->second.variants) {
                diff.removedVariants.push_back(
                    makeRef(key, *variant.second, DiffStatus::Removed));
            }
            continue;
        }
        if (referenceIt == reference.end()) {
            for (const auto& variant : currentIt->second.variants) {
                diff.addedVariants.push_back(
                    makeRef(key, *variant.second, DiffStatus::Added));
            }
            continue;
        }

        const ReportSummary& currentSummary = *currentIt->second.summary;
        const ReportSummary& referenceSummary = *referenceIt->second.summary;
        const bool sourceHashChanged =
            currentSummary.source.sha256 != referenceSummary.source.sha256;
        const bool programToolchainChanged =
            currentSummary.shaderc.version != referenceSummary.shaderc.version ||
            currentSummary.toolVersion != referenceSummary.toolVersion ||
            currentSummary.profile != referenceSummary.profile ||
            currentSummary.optimization != referenceSummary.optimization ||
            currentSummary.debugInfo != referenceSummary.debugInfo;
        diff.sourceHashChanged = diff.sourceHashChanged || sourceHashChanged;
        diff.toolchainChanged = diff.toolchainChanged || programToolchainChanged;

        for (const auto& referenceVariant : referenceIt->second.variants) {
            if (currentIt->second.variants.find(referenceVariant.first) ==
                currentIt->second.variants.end()) {
                diff.removedVariants.push_back(
                    makeRef(key, *referenceVariant.second, DiffStatus::Removed));
            }
        }

        for (const auto& [id, currentVariant] : currentIt->second.variants) {
            const auto referenceVariantIt =
                referenceIt->second.variants.find(id);
            if (referenceVariantIt == referenceIt->second.variants.end()) {
                diff.addedVariants.push_back(
                    makeRef(key, *currentVariant, DiffStatus::Added));
                continue;
            }

            const VariantReport& referenceVariant = *referenceVariantIt->second;
            DiffEntry entry;
            entry.backend = key.backend;
            entry.source = key.source;
            entry.stage = key.stage;
            entry.variantId = currentVariant->variantId;
            entry.canonicalDefines = currentVariant->canonicalDefines;

            if (sourceHashChanged || programToolchainChanged ||
                currentVariant->status != VariantStatus::Ok ||
                referenceVariant.status != VariantStatus::Ok ||
                !currentVariant->metrics.has_value() ||
                !referenceVariant.metrics.has_value()) {
                entry.status = DiffStatus::Incomparable;
            } else if (compareMetrics(*currentVariant, referenceVariant, entry)) {
                entry.status = DiffStatus::Changed;
            } else {
                entry.status = DiffStatus::Same;
            }
            diff.entries.push_back(std::move(entry));
        }
    }

    std::sort(diff.entries.begin(), diff.entries.end(),
              [](const DiffEntry& left, const DiffEntry& right) {
                  return std::tie(left.backend, left.source, left.stage,
                                  left.variantId) <
                         std::tie(right.backend, right.source, right.stage,
                                  right.variantId);
              });
    const auto refLess = [](const DiffVariantRef& left, const DiffVariantRef& right) {
        return std::tie(left.backend, left.source, left.stage, left.variantId) <
               std::tie(right.backend, right.source, right.stage, right.variantId);
    };
    std::sort(diff.addedVariants.begin(), diff.addedVariants.end(), refLess);
    std::sort(diff.removedVariants.begin(), diff.removedVariants.end(), refLess);
    return diff;
}

bool hasRegression(const ReportDiff& diff) {
    for (const DiffEntry& entry : diff.entries) {
        if (entry.status != DiffStatus::Changed) {
            continue;
        }
        for (const auto& [name, delta] : entry.metricDeltas) {
            if (delta > 0 && isRegressionMetric(name)) {
                return true;
            }
        }
    }
    return false;
}

std::string toJson(const ReportDiff& diff, bool pretty) {
    Json result = Json::object();
    result["schemaVersion"] = diff.schemaVersion;
    result["current"] = encodeSide(diff.current);
    result["reference"] = encodeSide(diff.reference);
    result["entries"] = Json::array();
    for (const DiffEntry& entry : diff.entries) {
        Json value = Json::object();
        value["backend"] = entry.backend;
        value["source"] = entry.source;
        value["stage"] = entry.stage;
        value["variantId"] = entry.variantId;
        value["canonicalDefines"] = entry.canonicalDefines;
        value["metricDeltas"] = Json::object();
        for (const auto& [name, delta] : entry.metricDeltas) {
            value["metricDeltas"][name] = delta;
        }
        value["status"] = diffStatusToString(entry.status);
        result["entries"].push_back(std::move(value));
    }
    result["addedVariants"] = Json::array();
    for (const DiffVariantRef& ref : diff.addedVariants) {
        result["addedVariants"].push_back(encodeVariantRef(ref));
    }
    result["removedVariants"] = Json::array();
    for (const DiffVariantRef& ref : diff.removedVariants) {
        result["removedVariants"].push_back(encodeVariantRef(ref));
    }
    result["sourceHashChanged"] = diff.sourceHashChanged;
    result["toolchainChanged"] = diff.toolchainChanged;
    return (pretty ? result.dump(2) : result.dump()) + "\n";
}

} // namespace hbg::analysis
