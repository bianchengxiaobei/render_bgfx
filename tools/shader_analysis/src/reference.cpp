#include "hbg/analysis/reference.h"

#include "hbg/analysis/report_files.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace hbg::analysis {
namespace {

namespace fs = std::filesystem;

fs::path normalized(const fs::path& path) {
    std::error_code error;
    fs::path result = fs::weakly_canonical(path, error);
    if (error) {
        result = fs::absolute(path);
    }
    result = result.lexically_normal();
    result.make_preferred();
    return result;
}

bool isWithin(const fs::path& child, const fs::path& parent) {
    auto childIt = child.begin();
    for (auto parentIt = parent.begin(); parentIt != parent.end(); ++parentIt, ++childIt) {
        if (childIt == child.end() || *childIt != *parentIt) {
            return false;
        }
    }
    return true;
}

void writeTextFile(const fs::path& path, std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("cannot write reference summary: " + path.string());
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream.good()) {
        throw std::runtime_error("cannot write reference summary: " + path.string());
    }
}

} // namespace

ReferenceResult setReference(const fs::path& fromReportsDir,
                             const fs::path& toReferenceDir) {
    const fs::path from = normalized(fromReportsDir);
    const fs::path to = normalized(toReferenceDir);
    if (!fs::is_directory(from)) {
        throw std::runtime_error("reference source not found: " + from.string());
    }
    if (to.empty() || to.filename().empty()) {
        throw std::runtime_error("invalid reference destination: " + to.string());
    }
    if (isWithin(to, from) || isWithin(from, to)) {
        throw std::runtime_error("reference source and destination must not overlap");
    }

    const std::vector<LoadedSummary> summaries = loadReportSummaries(from);
    std::error_code error;
    fs::remove_all(to, error);
    if (error) {
        throw std::runtime_error("cannot clear reference directory " + to.string() +
                                 ": " + error.message());
    }
    fs::create_directories(to, error);
    if (error) {
        throw std::runtime_error("cannot create reference directory " + to.string() +
                                 ": " + error.message());
    }

    for (const LoadedSummary& entry : summaries) {
        const fs::path destination = to / entry.relativePath;
        fs::create_directories(destination.parent_path(), error);
        if (error) {
            throw std::runtime_error("cannot create reference directory " +
                                     destination.parent_path().string() + ": " +
                                     error.message());
        }
        ReportSummary portable = entry.summary;
        for (VariantReport& variant : portable.variants) {
            variant.binaryPath.reset();
            variant.disasmPath.reset();
            variant.detailPath.reset();
        }
        writeTextFile(destination, toJson(portable));
    }

    ReferenceResult result;
    result.summaryCount = summaries.size();
    result.indexEntryCount = writeReportIndex(to);
    return result;
}

} // namespace hbg::analysis
