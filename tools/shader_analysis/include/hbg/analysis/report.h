#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hbg::analysis {

inline constexpr int kReportSchemaVersion = 1;

struct ShadercInfo {
    std::string path;
    std::string version;

    bool operator==(const ShadercInfo&) const = default;
};

struct SourceInfo {
    std::string path;
    std::string sha256;
    std::string language;

    bool operator==(const SourceInfo&) const = default;
};

struct LogEntry {
    std::string message;
    std::vector<std::string> stacktrace;

    bool operator==(const LogEntry&) const = default;
};

enum class VariantStatus {
    Ok,
    Failed,
    Skipped,
};

std::string variantStatusToString(VariantStatus status);
VariantStatus variantStatusFromString(std::string_view value);

struct Metrics {
    std::optional<std::int64_t> instructionCount;
    std::map<std::string, std::int64_t> opcodeHistogram;
    std::optional<std::int64_t> tempRegisterMax;
    std::optional<std::int64_t> tempRegisterPeak;
    std::optional<std::int64_t> inputCount;
    std::optional<std::int64_t> outputCount;
    std::optional<std::int64_t> cbufferSlotCount;
    std::optional<std::int64_t> cbufferDwordsRead;
    std::optional<std::int64_t> dynamicCBufferReadCount;
    std::optional<std::int64_t> textureCount;
    std::optional<std::int64_t> samplerCount;
    std::optional<std::int64_t> uavCount;
    std::optional<std::int64_t> structuredBufferCount;
    std::optional<std::int64_t> sampleCount;
    std::optional<std::int64_t> loadCount;
    std::optional<std::int64_t> storeCount;
    std::optional<std::int64_t> branchCount;
    std::optional<std::int64_t> loopCount;
    std::optional<std::int64_t> ssaValueCount;
    std::optional<std::int64_t> maxLiveSsaValues;
    std::optional<std::int64_t> imageCount;
    std::optional<std::int64_t> uniformBufferCount;
    std::optional<std::int64_t> storageBufferCount;
    std::optional<std::int64_t> pushConstantCount;
    std::optional<std::int64_t> uniformBufferBytes;
    std::optional<std::int64_t> pushConstantBytes;
    std::optional<std::int64_t> accessChainCount;
    std::optional<std::int64_t> sourceLineCount;
    std::optional<bool> debugInfo;
    std::optional<std::int64_t> warningCount;
    std::optional<std::int64_t> errorCount;
    std::optional<std::int64_t> microCodeSize;
    std::optional<std::int64_t> sgprCount;
    std::optional<std::int64_t> sgprUsedCount;
    std::optional<std::int64_t> userSgprCount;
    std::optional<std::int64_t> ldsSize;
    std::optional<std::int64_t> threadGroupWaves;
    std::optional<std::int64_t> simdOccupancyCount;
    std::optional<std::int64_t> simdOccupancyMax;
    std::optional<std::int64_t> cuOccupancyCount;
    std::optional<std::int64_t> cuOccupancyMax;

    bool operator==(const Metrics&) const = default;
};

struct VariantReport {
    std::string variantId;
    std::string name;
    std::vector<std::string> defines;
    std::vector<std::string> canonicalDefines;
    std::vector<std::string> keywords;
    VariantStatus status = VariantStatus::Ok;
    bool cacheHit = false;
    std::optional<std::int64_t> compileMs;
    std::optional<std::int64_t> binaryBytes;
    std::optional<std::string> binaryPath;
    std::optional<std::string> disasmPath;
    std::optional<std::string> detailPath;
    std::optional<Metrics> metrics;
    std::vector<LogEntry> warnings;
    std::vector<LogEntry> errors;

    bool operator==(const VariantReport&) const = default;
};

struct ReportSummary {
    int schemaVersion = kReportSchemaVersion;
    std::string toolVersion;
    std::string generatedAt;
    std::string backend;
    std::string profile;
    int optimization = 0;
    bool debugInfo = false;
    ShadercInfo shaderc;
    SourceInfo source;
    std::string stage;
    int variantCount = 0;
    std::vector<VariantReport> variants;

    bool operator==(const ReportSummary&) const = default;
};

struct RegisterRef {
    std::string registerClass;
    int index = 0;
    std::string mask;

    bool operator==(const RegisterRef&) const = default;
};

struct ResourceRef {
    std::string kind;
    std::string name;
    int index = -1;

    bool operator==(const ResourceRef&) const = default;
};

struct CbufferRead {
    int instruction = 0;
    int slot = 0;
    int offset = 0;
    int dwords = 0;
    bool dynamic = false;

    bool operator==(const CbufferRead&) const = default;
};

struct Pressure {
    std::optional<int> temp;
    std::optional<int> ssa;

    bool operator==(const Pressure&) const = default;
};

struct BranchInfo {
    std::string kind = "none";
    std::optional<int> targetIndex;
    int indent = 0;

    bool operator==(const BranchInfo&) const = default;
};

struct Instruction {
    int index = 0;
    std::string opcode;
    std::string text;
    std::vector<RegisterRef> reads;
    std::vector<RegisterRef> writes;
    std::vector<ResourceRef> resourceRefs;
    std::optional<CbufferRead> cbufferRead;
    Pressure pressure;
    BranchInfo branch;
    std::optional<int> sourceLine;

    bool operator==(const Instruction&) const = default;
};

struct LivenessInterval {
    std::string registerClass;
    int index = 0;
    int start = 0;
    int end = 0;

    bool operator==(const LivenessInterval&) const = default;
};

struct Resource {
    std::string kind;
    std::string name;
    int binding = -1;
    std::optional<int> size;

    bool operator==(const Resource&) const = default;
};

struct DetailReport {
    int schemaVersion = kReportSchemaVersion;
    std::string source;
    std::string stage;
    std::string backend;
    std::string variantId;
    std::vector<Instruction> instructions;
    std::vector<LivenessInterval> liveness;
    std::vector<Resource> resources;
    std::vector<CbufferRead> cbufferReads;

    bool operator==(const DetailReport&) const = default;
};

struct ReportIndexEntry {
    std::string backend;
    std::string source;
    std::string stage;
    std::string summaryPath;
    int variantCount = 0;
    int okCount = 0;
    int failedCount = 0;
    std::string sourceSha256;
    std::string generatedAt;

    bool operator==(const ReportIndexEntry&) const = default;
};

struct ReportIndex {
    int schemaVersion = kReportSchemaVersion;
    std::string generatedAt;
    std::vector<ReportIndexEntry> entries;

    bool operator==(const ReportIndex&) const = default;
};

std::string toJson(const ReportSummary& summary, bool pretty = true);
std::string toJson(const DetailReport& detail, bool pretty = true);
std::string toJson(const ReportIndex& index, bool pretty = true);

ReportSummary parseSummary(std::string_view jsonText);
DetailReport parseDetail(std::string_view jsonText);
ReportIndex parseIndex(std::string_view jsonText);

} // namespace hbg::analysis
