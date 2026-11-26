#include "hbg/analysis/export_csv.h"

#include "hbg/analysis/report_files.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace hbg::analysis {
namespace {

std::string quote(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (const char character : value) {
        if (character == '"') {
            result.push_back('"');
        }
        result.push_back(character);
    }
    result.push_back('"');
    return result;
}

void appendStringField(std::string& row, std::string_view value) {
    row += quote(value);
    row.push_back(';');
}

void appendIntField(std::string& row, const std::optional<std::int64_t>& value) {
    if (value.has_value()) {
        row += std::to_string(*value);
    }
    row.push_back(';');
}

std::string joinDefines(const std::vector<std::string>& defines) {
    std::string result;
    for (size_t i = 0; i < defines.size(); ++i) {
        if (i != 0) {
            result += "\r\n";
        }
        result += defines[i];
    }
    return result;
}

std::string metricsInt(const VariantReport& variant,
                       std::optional<std::int64_t> Metrics::* member) {
    if (!variant.metrics.has_value()) {
        return {};
    }
    const std::optional<std::int64_t>& value = (*variant.metrics).*member;
    return value.has_value() ? std::to_string(*value) : std::string{};
}

void appendMetricsInt(std::string& row, const VariantReport& variant,
                      std::optional<std::int64_t> Metrics::* member) {
    row += metricsInt(variant, member);
    row.push_back(';');
}

void appendVariant(std::string& output, const ReportSummary& summary,
                   const VariantReport& variant) {
    appendStringField(output, summary.source.path);
    appendStringField(output, summary.stage);
    appendStringField(output, summary.backend);
    appendStringField(output, variant.variantId);
    appendStringField(output, joinDefines(variant.canonicalDefines));
    appendStringField(output, variantStatusToString(variant.status));
    appendIntField(output, variant.compileMs);
    appendIntField(output, variant.binaryBytes);
    appendMetricsInt(output, variant, &Metrics::instructionCount);
    appendMetricsInt(output, variant, &Metrics::tempRegisterMax);
    appendMetricsInt(output, variant, &Metrics::tempRegisterPeak);
    appendMetricsInt(output, variant, &Metrics::ssaValueCount);
    appendMetricsInt(output, variant, &Metrics::maxLiveSsaValues);
    appendMetricsInt(output, variant, &Metrics::textureCount);
    appendMetricsInt(output, variant, &Metrics::samplerCount);
    appendMetricsInt(output, variant, &Metrics::uavCount);
    appendMetricsInt(output, variant, &Metrics::uniformBufferCount);
    appendMetricsInt(output, variant, &Metrics::uniformBufferBytes);
    appendMetricsInt(output, variant, &Metrics::storageBufferCount);
    appendMetricsInt(output, variant, &Metrics::cbufferDwordsRead);
    appendMetricsInt(output, variant, &Metrics::dynamicCBufferReadCount);
    appendMetricsInt(output, variant, &Metrics::warningCount);
    appendMetricsInt(output, variant, &Metrics::errorCount);
    output += "\r\n";
}

void writeTextFile(const std::filesystem::path& path, std::string_view text) {
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            throw std::runtime_error("cannot create directory " +
                                     path.parent_path().string() + ": " +
                                     error.message());
        }
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("cannot write file: " + path.string());
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream.good()) {
        throw std::runtime_error("cannot write file: " + path.string());
    }
}

} // namespace

std::string buildCsv(const std::vector<ReportSummary>& summaries) {
    static constexpr const char* kColumns[] = {
        "Source",
        "Stage",
        "Backend",
        "VariantId",
        "Defines",
        "Status",
        "CompileMs",
        "BinaryBytes",
        "InstructionCount",
        "TempRegisterMax",
        "TempRegisterPeak",
        "SsaValueCount",
        "MaxLiveSsaValues",
        "TextureCount",
        "SamplerCount",
        "UavCount",
        "UniformBufferCount",
        "UniformBufferBytes",
        "StorageBufferCount",
        "CbufferDwordsRead",
        "DynamicCBufferReadCount",
        "WarningCount",
        "ErrorCount",
    };

    std::string output;
    for (const char* column : kColumns) {
        output += quote(column);
        output.push_back(';');
    }
    output += "\r\n";

    std::vector<const ReportSummary*> sorted;
    sorted.reserve(summaries.size());
    for (const ReportSummary& summary : summaries) {
        sorted.push_back(&summary);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const ReportSummary* left, const ReportSummary* right) {
                  return std::tie(left->backend, left->source.path, left->stage) <
                         std::tie(right->backend, right->source.path, right->stage);
              });

    for (const ReportSummary* summary : sorted) {
        std::vector<const VariantReport*> variants;
        variants.reserve(summary->variants.size());
        for (const VariantReport& variant : summary->variants) {
            variants.push_back(&variant);
        }
        std::sort(variants.begin(), variants.end(),
                  [](const VariantReport* left, const VariantReport* right) {
                      return left->variantId < right->variantId;
                  });
        for (const VariantReport* variant : variants) {
            appendVariant(output, *summary, *variant);
        }
    }
    return output;
}

std::size_t exportCsv(const std::filesystem::path& reportPath,
                      const std::filesystem::path& outputPath) {
    const std::vector<LoadedSummary> loaded =
        loadReportSummariesFromPath(reportPath);
    std::vector<ReportSummary> summaries;
    summaries.reserve(loaded.size());
    std::size_t rows = 0;
    for (const LoadedSummary& entry : loaded) {
        summaries.push_back(entry.summary);
        rows += entry.summary.variants.size();
    }
    writeTextFile(outputPath, buildCsv(summaries));
    return rows;
}

} // namespace hbg::analysis
