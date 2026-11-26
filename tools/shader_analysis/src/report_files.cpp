#include "hbg/analysis/report_files.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>

namespace hbg::analysis {
namespace {

namespace fs = std::filesystem;

std::string readTextFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open file: " + path.string());
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        throw std::runtime_error("cannot read file: " + path.string());
    }
    return contents.str();
}

void writeTextFile(const fs::path& path, std::string_view text) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error) {
        throw std::runtime_error("cannot create directory " + path.parent_path().string() +
                                 ": " + error.message());
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

bool isSafeRelativePath(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    const fs::path path(value);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const fs::path& part : path) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

bool isHexCharacter(char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f');
}

bool isVariantDetailFile(const fs::path& path) {
    std::string stem = path.stem().string();
    if (stem.size() == 18 && stem.ends_with("-debug")) {
        stem.resize(12);
    }
    if (stem.size() != 12) {
        return false;
    }
    return std::all_of(stem.begin(), stem.end(), isHexCharacter);
}

std::string currentTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_MSC_VER)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

LoadedSummary loadSummaryFile(const fs::path& path, const std::string& relativePath) {
    LoadedSummary loaded;
    loaded.path = path;
    loaded.relativePath = relativePath;
    loaded.summary = parseSummary(readTextFile(path));
    return loaded;
}

} // namespace

std::vector<LoadedSummary> loadReportSummaries(const fs::path& reportsDir) {
    if (!fs::is_directory(reportsDir)) {
        throw std::runtime_error("report directory not found: " + reportsDir.string());
    }

    std::vector<fs::path> summaryPaths;
    std::error_code error;
    for (const fs::directory_entry& backendEntry : fs::directory_iterator(reportsDir, error)) {
        if (error) {
            break;
        }
        if (!backendEntry.is_directory()) {
            continue;
        }
        for (const fs::directory_entry& entry :
             fs::recursive_directory_iterator(backendEntry.path(), error)) {
            if (error) {
                break;
            }
            if (!entry.is_regular_file() || entry.path().extension() != ".json" ||
                isVariantDetailFile(entry.path())) {
                continue;
            }
            summaryPaths.push_back(entry.path());
        }
        if (error) {
            break;
        }
    }
    if (error) {
        throw std::runtime_error("cannot scan report directory " + reportsDir.string() +
                                 ": " + error.message());
    }

    std::vector<LoadedSummary> result;
    result.reserve(summaryPaths.size());
    for (const fs::path& path : summaryPaths) {
        const std::string relative =
            fs::relative(path, reportsDir).generic_string();
        result.push_back(loadSummaryFile(path, relative));
    }
    std::sort(result.begin(), result.end(),
              [](const LoadedSummary& left, const LoadedSummary& right) {
                  return std::tie(left.summary.backend, left.summary.source.path,
                                  left.summary.stage, left.relativePath) <
                         std::tie(right.summary.backend, right.summary.source.path,
                                  right.summary.stage, right.relativePath);
              });
    return result;
}

std::vector<LoadedSummary> loadReportSummariesFromPath(const fs::path& path) {
    if (fs::is_directory(path)) {
        return loadReportSummaries(path);
    }
    if (!fs::is_regular_file(path)) {
        throw std::runtime_error("report path not found: " + path.string());
    }

    const std::string text = readTextFile(path);
    const nlohmann::json document = nlohmann::json::parse(text);
    if (document.contains("entries")) {
        const ReportIndex index = parseIndex(text);
        const fs::path base = path.parent_path();
        std::vector<LoadedSummary> result;
        result.reserve(index.entries.size());
        for (const ReportIndexEntry& entry : index.entries) {
            if (!isSafeRelativePath(entry.summaryPath)) {
                throw std::runtime_error("unsafe summaryPath: " + entry.summaryPath);
            }
            result.push_back(loadSummaryFile(base / entry.summaryPath, entry.summaryPath));
        }
        return result;
    }
    if (document.contains("variants")) {
        LoadedSummary loaded;
        loaded.path = path;
        loaded.relativePath = path.filename().generic_string();
        loaded.summary = parseSummary(text);
        return {std::move(loaded)};
    }
    throw std::runtime_error("unrecognized report JSON: " + path.string());
}

ReportIndex buildReportIndex(const std::vector<LoadedSummary>& summaries,
                             std::string generatedAt) {
    ReportIndex index;
    index.generatedAt = std::move(generatedAt);
    index.entries.reserve(summaries.size());
    for (const LoadedSummary& loaded : summaries) {
        const ReportSummary& summary = loaded.summary;
        ReportIndexEntry entry;
        entry.backend = summary.backend;
        entry.source = summary.source.path;
        entry.stage = summary.stage;
        entry.summaryPath = loaded.relativePath;
        entry.variantCount = summary.variantCount;
        entry.okCount = 0;
        entry.failedCount = 0;
        for (const VariantReport& variant : summary.variants) {
            if (variant.status == VariantStatus::Ok) {
                ++entry.okCount;
            } else if (variant.status == VariantStatus::Failed) {
                ++entry.failedCount;
            }
        }
        entry.sourceSha256 = summary.source.sha256;
        entry.generatedAt = summary.generatedAt;
        index.entries.push_back(std::move(entry));
    }
    std::sort(index.entries.begin(), index.entries.end(),
              [](const ReportIndexEntry& left, const ReportIndexEntry& right) {
                  return std::tie(left.backend, left.source, left.stage,
                                  left.summaryPath) <
                         std::tie(right.backend, right.source, right.stage,
                                  right.summaryPath);
              });
    return index;
}

std::size_t writeReportIndex(const fs::path& reportsDir) {
    std::error_code error;
    fs::create_directories(reportsDir, error);
    if (error) {
        throw std::runtime_error("cannot create report directory " + reportsDir.string() +
                                 ": " + error.message());
    }
    const std::vector<LoadedSummary> summaries = loadReportSummaries(reportsDir);
    const ReportIndex index = buildReportIndex(summaries, currentTimestamp());
    writeTextFile(reportsDir / "index.json", toJson(index));
    return index.entries.size();
}

} // namespace hbg::analysis
