#pragma once

#include "hbg/analysis/hashing.h"
#include "hbg/analysis/report.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace hbg_test {

namespace fs = std::filesystem;
using namespace hbg::analysis;

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = fs::temp_directory_path() /
                 ("hbg_shader_analysis_test_" + std::to_string(stamp) + "_" +
                  std::to_string(counter.fetch_add(1)));
        std::error_code error;
        fs::create_directories(m_path, error);
        if (error) {
            throw std::runtime_error("cannot create temp directory: " + error.message());
        }
    }

    ~TempDirectory() {
        std::error_code error;
        fs::remove_all(m_path, error);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    const fs::path& path() const { return m_path; }
    fs::path subdirectory(const std::string& name) const { return m_path / name; }

private:
    fs::path m_path;
};

inline void writeRawFile(const fs::path& path, std::string_view text) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    if (error) {
        throw std::runtime_error("cannot create test directory: " + error.message());
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("cannot write test file: " + path.string());
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

inline std::string readRawFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot read test file: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

inline VariantReport makeVariant(std::string variantId, std::vector<std::string> defines,
                                 std::int64_t instructionCount, std::int64_t binaryBytes,
                                 std::int64_t tempPeak = 4) {
    VariantReport variant;
    variant.variantId = std::move(variantId);
    variant.name = "forward.fs.0";
    variant.defines = std::move(defines);
    variant.canonicalDefines = canonicalDefines(variant.defines);
    variant.keywords = keywordNames(variant.defines);
    variant.status = VariantStatus::Ok;
    variant.cacheHit = false;
    variant.compileMs = 10;
    variant.binaryBytes = binaryBytes;
    Metrics metrics;
    metrics.instructionCount = instructionCount;
    metrics.opcodeHistogram = {{"mov", instructionCount}};
    metrics.tempRegisterMax = tempPeak + 1;
    metrics.tempRegisterPeak = tempPeak;
    metrics.samplerCount = 1;
    metrics.warningCount = 0;
    metrics.errorCount = 0;
    metrics.microCodeSize = binaryBytes;
    variant.metrics = metrics;
    return variant;
}

inline ReportSummary makeSummary(std::string backend, std::string source, std::string stage,
                                 std::vector<VariantReport> variants,
                                 std::string sourceHash = "source-hash",
                                 std::string shadercVersion = "shaderc-1",
                                 std::string generatedAt = "2026-11-23T12:00:00Z") {
    ReportSummary summary;
    summary.toolVersion = "test";
    summary.generatedAt = std::move(generatedAt);
    summary.backend = std::move(backend);
    summary.profile = "test-profile";
    summary.optimization = 3;
    summary.debugInfo = false;
    summary.shaderc = {"shaderc", std::move(shadercVersion)};
    summary.source = {std::move(source), std::move(sourceHash), "bgfx-sc"};
    summary.stage = std::move(stage);
    summary.variantCount = static_cast<int>(variants.size());
    summary.variants = std::move(variants);
    return summary;
}

inline fs::path writeSummaryTree(const fs::path& reportsDir,
                                 const std::string& relativePath,
                                 const ReportSummary& summary) {
    const fs::path path = reportsDir / relativePath;
    writeRawFile(path, toJson(summary));
    return path;
}

} // namespace hbg_test
