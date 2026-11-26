#include "hbg/analysis/report.h"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>
#include <utility>

namespace hbg::analysis {
namespace {

using Json = nlohmann::ordered_json;

[[noreturn]] void fail(const std::string& context, const std::string& message) {
    throw std::runtime_error(context + ": " + message);
}

const nlohmann::json& field(const nlohmann::json& object, const char* key,
                            const std::string& context) {
    if (!object.is_object()) {
        fail(context, "expected an object");
    }
    const auto it = object.find(key);
    if (it == object.end()) {
        fail(context, std::string("missing field '") + key + "'");
    }
    return *it;
}

std::string stringField(const nlohmann::json& object, const char* key,
                        const std::string& context) {
    const nlohmann::json& value = field(object, key, context);
    if (!value.is_string()) {
        fail(context, std::string("field '") + key + "' must be a string");
    }
    return value.get<std::string>();
}

int intField(const nlohmann::json& object, const char* key, const std::string& context) {
    const nlohmann::json& value = field(object, key, context);
    if (!value.is_number_integer()) {
        fail(context, std::string("field '") + key + "' must be an integer");
    }
    return value.get<int>();
}

bool boolField(const nlohmann::json& object, const char* key, const std::string& context) {
    const nlohmann::json& value = field(object, key, context);
    if (!value.is_boolean()) {
        fail(context, std::string("field '") + key + "' must be a boolean");
    }
    return value.get<bool>();
}

std::optional<std::int64_t> optionalIntField(const nlohmann::json& object, const char* key,
                                             const std::string& context) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return std::nullopt;
    }
    if (!it->is_number_integer()) {
        fail(context, std::string("field '") + key + "' must be an integer or null");
    }
    return it->get<std::int64_t>();
}

std::optional<int> optionalInt32Field(const nlohmann::json& object, const char* key,
                                      const std::string& context) {
    const std::optional<std::int64_t> value = optionalIntField(object, key, context);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return static_cast<int>(*value);
}

std::optional<bool> optionalBoolField(const nlohmann::json& object, const char* key,
                                      const std::string& context) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return std::nullopt;
    }
    if (!it->is_boolean()) {
        fail(context, std::string("field '") + key + "' must be a boolean or null");
    }
    return it->get<bool>();
}

std::optional<std::string> optionalStringField(const nlohmann::json& object, const char* key,
                                               const std::string& context) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return std::nullopt;
    }
    if (!it->is_string()) {
        fail(context, std::string("field '") + key + "' must be a string or null");
    }
    return it->get<std::string>();
}

std::vector<std::string> stringArray(const nlohmann::json& value, const std::string& context) {
    if (!value.is_array()) {
        fail(context, "expected an array");
    }
    std::vector<std::string> result;
    result.reserve(value.size());
    for (const nlohmann::json& element : value) {
        if (!element.is_string()) {
            fail(context, "array elements must be strings");
        }
        result.push_back(element.get<std::string>());
    }
    return result;
}

std::vector<std::string> stringArrayField(const nlohmann::json& object, const char* key,
                                          const std::string& context) {
    return stringArray(field(object, key, context), context + "." + key);
}

void writeOptionalInt(Json& object, const char* key,
                      const std::optional<std::int64_t>& value) {
    if (value.has_value()) {
        object[key] = *value;
    } else {
        object[key] = nullptr;
    }
}

void writeOptionalInt32(Json& object, const char* key, const std::optional<int>& value) {
    if (value.has_value()) {
        object[key] = *value;
    } else {
        object[key] = nullptr;
    }
}

void writeOptionalBool(Json& object, const char* key, const std::optional<bool>& value) {
    if (value.has_value()) {
        object[key] = *value;
    } else {
        object[key] = nullptr;
    }
}

void writeOptionalString(Json& object, const char* key,
                         const std::optional<std::string>& value) {
    if (value.has_value()) {
        object[key] = *value;
    } else {
        object[key] = nullptr;
    }
}

Json encodeLogEntry(const LogEntry& entry) {
    Json result = Json::object();
    result["message"] = entry.message;
    result["stacktrace"] = entry.stacktrace;
    return result;
}

Json encodeLogs(const std::vector<LogEntry>& warnings, const std::vector<LogEntry>& errors) {
    Json result = Json::object();
    result["warnings"] = Json::array();
    for (const LogEntry& entry : warnings) {
        result["warnings"].push_back(encodeLogEntry(entry));
    }
    result["errors"] = Json::array();
    for (const LogEntry& entry : errors) {
        result["errors"].push_back(encodeLogEntry(entry));
    }
    return result;
}

Json encodeMetrics(const Metrics& metrics) {
    Json result = Json::object();
    writeOptionalInt(result, "instructionCount", metrics.instructionCount);
    result["opcodeHistogram"] = metrics.opcodeHistogram;
    writeOptionalInt(result, "tempRegisterMax", metrics.tempRegisterMax);
    writeOptionalInt(result, "tempRegisterPeak", metrics.tempRegisterPeak);
    writeOptionalInt(result, "inputCount", metrics.inputCount);
    writeOptionalInt(result, "outputCount", metrics.outputCount);
    writeOptionalInt(result, "cbufferSlotCount", metrics.cbufferSlotCount);
    writeOptionalInt(result, "cbufferDwordsRead", metrics.cbufferDwordsRead);
    writeOptionalInt(result, "dynamicCBufferReadCount", metrics.dynamicCBufferReadCount);
    writeOptionalInt(result, "textureCount", metrics.textureCount);
    writeOptionalInt(result, "samplerCount", metrics.samplerCount);
    writeOptionalInt(result, "uavCount", metrics.uavCount);
    writeOptionalInt(result, "structuredBufferCount", metrics.structuredBufferCount);
    writeOptionalInt(result, "sampleCount", metrics.sampleCount);
    writeOptionalInt(result, "loadCount", metrics.loadCount);
    writeOptionalInt(result, "storeCount", metrics.storeCount);
    writeOptionalInt(result, "branchCount", metrics.branchCount);
    writeOptionalInt(result, "loopCount", metrics.loopCount);
    writeOptionalInt(result, "ssaValueCount", metrics.ssaValueCount);
    writeOptionalInt(result, "maxLiveSsaValues", metrics.maxLiveSsaValues);
    writeOptionalInt(result, "imageCount", metrics.imageCount);
    writeOptionalInt(result, "uniformBufferCount", metrics.uniformBufferCount);
    writeOptionalInt(result, "storageBufferCount", metrics.storageBufferCount);
    writeOptionalInt(result, "pushConstantCount", metrics.pushConstantCount);
    writeOptionalInt(result, "uniformBufferBytes", metrics.uniformBufferBytes);
    writeOptionalInt(result, "pushConstantBytes", metrics.pushConstantBytes);
    writeOptionalInt(result, "accessChainCount", metrics.accessChainCount);
    writeOptionalInt(result, "sourceLineCount", metrics.sourceLineCount);
    writeOptionalBool(result, "debugInfo", metrics.debugInfo);
    writeOptionalInt(result, "warningCount", metrics.warningCount);
    writeOptionalInt(result, "errorCount", metrics.errorCount);
    writeOptionalInt(result, "microCodeSize", metrics.microCodeSize);
    writeOptionalInt(result, "sgprCount", metrics.sgprCount);
    writeOptionalInt(result, "sgprUsedCount", metrics.sgprUsedCount);
    writeOptionalInt(result, "userSgprCount", metrics.userSgprCount);
    writeOptionalInt(result, "ldsSize", metrics.ldsSize);
    writeOptionalInt(result, "threadGroupWaves", metrics.threadGroupWaves);
    writeOptionalInt(result, "simdOccupancyCount", metrics.simdOccupancyCount);
    writeOptionalInt(result, "simdOccupancyMax", metrics.simdOccupancyMax);
    writeOptionalInt(result, "cuOccupancyCount", metrics.cuOccupancyCount);
    writeOptionalInt(result, "cuOccupancyMax", metrics.cuOccupancyMax);
    return result;
}

Json encodeVariant(const VariantReport& variant) {
    Json result = Json::object();
    result["variantId"] = variant.variantId;
    result["name"] = variant.name;
    result["defines"] = variant.defines;
    result["canonicalDefines"] = variant.canonicalDefines;
    result["keywords"] = variant.keywords;
    result["status"] = variantStatusToString(variant.status);
    result["cacheHit"] = variant.cacheHit;
    writeOptionalInt(result, "compileMs", variant.compileMs);
    writeOptionalInt(result, "binaryBytes", variant.binaryBytes);
    writeOptionalString(result, "binaryPath", variant.binaryPath);
    writeOptionalString(result, "disasmPath", variant.disasmPath);
    writeOptionalString(result, "detailPath", variant.detailPath);
    result["metrics"] = variant.metrics.has_value()
                            ? encodeMetrics(*variant.metrics)
                            : Json(nullptr);
    result["logs"] = encodeLogs(variant.warnings, variant.errors);
    return result;
}

Json encodeSummary(const ReportSummary& summary) {
    Json result = Json::object();
    result["schemaVersion"] = summary.schemaVersion;
    result["toolVersion"] = summary.toolVersion;
    result["generatedAt"] = summary.generatedAt;
    result["backend"] = summary.backend;
    result["profile"] = summary.profile;
    result["optimization"] = summary.optimization;
    result["debugInfo"] = summary.debugInfo;
    result["shaderc"] = Json::object();
    result["shaderc"]["path"] = summary.shaderc.path;
    result["shaderc"]["version"] = summary.shaderc.version;
    result["source"] = Json::object();
    result["source"]["path"] = summary.source.path;
    result["source"]["sha256"] = summary.source.sha256;
    result["source"]["language"] = summary.source.language;
    result["stage"] = summary.stage;
    result["variantCount"] = summary.variantCount;
    result["variants"] = Json::array();
    for (const VariantReport& variant : summary.variants) {
        result["variants"].push_back(encodeVariant(variant));
    }
    return result;
}

Json encodeRegisterRef(const RegisterRef& ref) {
    Json result = Json::object();
    result["class"] = ref.registerClass;
    result["index"] = ref.index;
    result["mask"] = ref.mask;
    return result;
}

Json encodeResourceRef(const ResourceRef& ref) {
    Json result = Json::object();
    result["kind"] = ref.kind;
    result["name"] = ref.name;
    result["index"] = ref.index;
    return result;
}

Json encodeCbufferRead(const CbufferRead& read) {
    Json result = Json::object();
    result["instruction"] = read.instruction;
    result["slot"] = read.slot;
    result["offset"] = read.offset;
    result["dwords"] = read.dwords;
    result["dynamic"] = read.dynamic;
    return result;
}

Json encodeInstruction(const Instruction& instruction) {
    Json result = Json::object();
    result["index"] = instruction.index;
    result["opcode"] = instruction.opcode;
    result["text"] = instruction.text;
    result["reads"] = Json::array();
    for (const RegisterRef& ref : instruction.reads) {
        result["reads"].push_back(encodeRegisterRef(ref));
    }
    result["writes"] = Json::array();
    for (const RegisterRef& ref : instruction.writes) {
        result["writes"].push_back(encodeRegisterRef(ref));
    }
    result["resourceRefs"] = Json::array();
    for (const ResourceRef& ref : instruction.resourceRefs) {
        result["resourceRefs"].push_back(encodeResourceRef(ref));
    }
    result["cbufferRead"] = instruction.cbufferRead.has_value()
                                ? encodeCbufferRead(*instruction.cbufferRead)
                                : Json(nullptr);
    result["pressure"] = Json::object();
    writeOptionalInt32(result["pressure"], "temp", instruction.pressure.temp);
    writeOptionalInt32(result["pressure"], "ssa", instruction.pressure.ssa);
    result["branch"] = Json::object();
    result["branch"]["kind"] = instruction.branch.kind;
    writeOptionalInt32(result["branch"], "targetIndex", instruction.branch.targetIndex);
    result["branch"]["indent"] = instruction.branch.indent;
    writeOptionalInt32(result, "sourceLine", instruction.sourceLine);
    return result;
}

Json encodeLiveness(const LivenessInterval& interval) {
    Json result = Json::object();
    result["class"] = interval.registerClass;
    result["index"] = interval.index;
    result["start"] = interval.start;
    result["end"] = interval.end;
    return result;
}

Json encodeResource(const Resource& resource) {
    Json result = Json::object();
    result["kind"] = resource.kind;
    result["name"] = resource.name;
    result["binding"] = resource.binding;
    writeOptionalInt32(result, "size", resource.size);
    return result;
}

Json encodeDetail(const DetailReport& detail) {
    Json result = Json::object();
    result["schemaVersion"] = detail.schemaVersion;
    result["source"] = detail.source;
    result["stage"] = detail.stage;
    result["backend"] = detail.backend;
    result["variantId"] = detail.variantId;
    result["instructions"] = Json::array();
    for (const Instruction& instruction : detail.instructions) {
        result["instructions"].push_back(encodeInstruction(instruction));
    }
    result["liveness"] = Json::array();
    for (const LivenessInterval& interval : detail.liveness) {
        result["liveness"].push_back(encodeLiveness(interval));
    }
    result["resources"] = Json::array();
    for (const Resource& resource : detail.resources) {
        result["resources"].push_back(encodeResource(resource));
    }
    result["cbufferReads"] = Json::array();
    for (const CbufferRead& read : detail.cbufferReads) {
        result["cbufferReads"].push_back(encodeCbufferRead(read));
    }
    return result;
}

Json encodeIndex(const ReportIndex& index) {
    Json result = Json::object();
    result["schemaVersion"] = index.schemaVersion;
    result["generatedAt"] = index.generatedAt;
    result["entries"] = Json::array();
    for (const ReportIndexEntry& entry : index.entries) {
        Json entryJson = Json::object();
        entryJson["backend"] = entry.backend;
        entryJson["source"] = entry.source;
        entryJson["stage"] = entry.stage;
        entryJson["summaryPath"] = entry.summaryPath;
        entryJson["variantCount"] = entry.variantCount;
        entryJson["okCount"] = entry.okCount;
        entryJson["failedCount"] = entry.failedCount;
        entryJson["sourceSha256"] = entry.sourceSha256;
        entryJson["generatedAt"] = entry.generatedAt;
        result["entries"].push_back(entryJson);
    }
    return result;
}

std::string dumpJson(const Json& json, bool pretty) {
    return (pretty ? json.dump(2) : json.dump()) + "\n";
}

nlohmann::json parseDocument(std::string_view text, const std::string& context) {
    try {
        return nlohmann::json::parse(text.begin(), text.end());
    } catch (const nlohmann::json::exception& error) {
        fail(context, std::string("invalid JSON: ") + error.what());
    }
}

Metrics parseMetrics(const nlohmann::json& object, const std::string& context) {
    Metrics metrics;
    metrics.instructionCount = optionalIntField(object, "instructionCount", context);
    const nlohmann::json& histogram = field(object, "opcodeHistogram", context);
    if (!histogram.is_object()) {
        fail(context, "field 'opcodeHistogram' must be an object");
    }
    for (auto it = histogram.begin(); it != histogram.end(); ++it) {
        if (!it.value().is_number_integer()) {
            fail(context, "opcodeHistogram values must be integers");
        }
        metrics.opcodeHistogram.emplace(it.key(), it.value().get<std::int64_t>());
    }
    metrics.tempRegisterMax = optionalIntField(object, "tempRegisterMax", context);
    metrics.tempRegisterPeak = optionalIntField(object, "tempRegisterPeak", context);
    metrics.inputCount = optionalIntField(object, "inputCount", context);
    metrics.outputCount = optionalIntField(object, "outputCount", context);
    metrics.cbufferSlotCount = optionalIntField(object, "cbufferSlotCount", context);
    metrics.cbufferDwordsRead = optionalIntField(object, "cbufferDwordsRead", context);
    metrics.dynamicCBufferReadCount =
        optionalIntField(object, "dynamicCBufferReadCount", context);
    metrics.textureCount = optionalIntField(object, "textureCount", context);
    metrics.samplerCount = optionalIntField(object, "samplerCount", context);
    metrics.uavCount = optionalIntField(object, "uavCount", context);
    metrics.structuredBufferCount = optionalIntField(object, "structuredBufferCount", context);
    metrics.sampleCount = optionalIntField(object, "sampleCount", context);
    metrics.loadCount = optionalIntField(object, "loadCount", context);
    metrics.storeCount = optionalIntField(object, "storeCount", context);
    metrics.branchCount = optionalIntField(object, "branchCount", context);
    metrics.loopCount = optionalIntField(object, "loopCount", context);
    metrics.ssaValueCount = optionalIntField(object, "ssaValueCount", context);
    metrics.maxLiveSsaValues = optionalIntField(object, "maxLiveSsaValues", context);
    metrics.imageCount = optionalIntField(object, "imageCount", context);
    metrics.uniformBufferCount = optionalIntField(object, "uniformBufferCount", context);
    metrics.storageBufferCount = optionalIntField(object, "storageBufferCount", context);
    metrics.pushConstantCount = optionalIntField(object, "pushConstantCount", context);
    metrics.uniformBufferBytes = optionalIntField(object, "uniformBufferBytes", context);
    metrics.pushConstantBytes = optionalIntField(object, "pushConstantBytes", context);
    metrics.accessChainCount = optionalIntField(object, "accessChainCount", context);
    metrics.sourceLineCount = optionalIntField(object, "sourceLineCount", context);
    metrics.debugInfo = optionalBoolField(object, "debugInfo", context);
    metrics.warningCount = optionalIntField(object, "warningCount", context);
    metrics.errorCount = optionalIntField(object, "errorCount", context);
    metrics.microCodeSize = optionalIntField(object, "microCodeSize", context);
    metrics.sgprCount = optionalIntField(object, "sgprCount", context);
    metrics.sgprUsedCount = optionalIntField(object, "sgprUsedCount", context);
    metrics.userSgprCount = optionalIntField(object, "userSgprCount", context);
    metrics.ldsSize = optionalIntField(object, "ldsSize", context);
    metrics.threadGroupWaves = optionalIntField(object, "threadGroupWaves", context);
    metrics.simdOccupancyCount = optionalIntField(object, "simdOccupancyCount", context);
    metrics.simdOccupancyMax = optionalIntField(object, "simdOccupancyMax", context);
    metrics.cuOccupancyCount = optionalIntField(object, "cuOccupancyCount", context);
    metrics.cuOccupancyMax = optionalIntField(object, "cuOccupancyMax", context);
    return metrics;
}

std::vector<LogEntry> parseLogs(const nlohmann::json& value, const std::string& context) {
    if (!value.is_array()) {
        fail(context, "expected an array");
    }
    std::vector<LogEntry> result;
    result.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        const std::string entryContext = context + "[" + std::to_string(i) + "]";
        LogEntry entry;
        entry.message = stringField(value[i], "message", entryContext);
        if (value[i].contains("stacktrace") && !value[i]["stacktrace"].is_null()) {
            entry.stacktrace = stringArray(value[i]["stacktrace"], entryContext + ".stacktrace");
        }
        result.push_back(std::move(entry));
    }
    return result;
}

VariantReport parseVariant(const nlohmann::json& object, const std::string& context) {
    VariantReport variant;
    variant.variantId = stringField(object, "variantId", context);
    variant.name = stringField(object, "name", context);
    variant.defines = stringArrayField(object, "defines", context);
    variant.canonicalDefines = stringArrayField(object, "canonicalDefines", context);
    variant.keywords = stringArrayField(object, "keywords", context);
    try {
        variant.status = variantStatusFromString(stringField(object, "status", context));
    } catch (const std::invalid_argument& error) {
        fail(context, error.what());
    }
    variant.cacheHit = boolField(object, "cacheHit", context);
    variant.compileMs = optionalIntField(object, "compileMs", context);
    variant.binaryBytes = optionalIntField(object, "binaryBytes", context);
    variant.binaryPath = optionalStringField(object, "binaryPath", context);
    variant.disasmPath = optionalStringField(object, "disasmPath", context);
    variant.detailPath = optionalStringField(object, "detailPath", context);

    const auto metrics = object.find("metrics");
    if (metrics != object.end() && !metrics->is_null()) {
        if (!metrics->is_object()) {
            fail(context, "field 'metrics' must be an object or null");
        }
        variant.metrics = parseMetrics(*metrics, context + ".metrics");
    }

    const nlohmann::json& logs = field(object, "logs", context);
    if (!logs.is_object()) {
        fail(context, "field 'logs' must be an object");
    }
    variant.warnings = parseLogs(field(logs, "warnings", context + ".logs"),
                                 context + ".logs.warnings");
    variant.errors = parseLogs(field(logs, "errors", context + ".logs"),
                               context + ".logs.errors");
    return variant;
}

RegisterRef parseRegisterRef(const nlohmann::json& object, const std::string& context) {
    RegisterRef ref;
    ref.registerClass = stringField(object, "class", context);
    ref.index = intField(object, "index", context);
    ref.mask = stringField(object, "mask", context);
    return ref;
}

ResourceRef parseResourceRef(const nlohmann::json& object, const std::string& context) {
    ResourceRef ref;
    ref.kind = stringField(object, "kind", context);
    ref.name = stringField(object, "name", context);
    ref.index = intField(object, "index", context);
    return ref;
}

CbufferRead parseCbufferRead(const nlohmann::json& object, const std::string& context) {
    CbufferRead read;
    read.instruction = intField(object, "instruction", context);
    read.slot = intField(object, "slot", context);
    read.offset = intField(object, "offset", context);
    read.dwords = intField(object, "dwords", context);
    read.dynamic = boolField(object, "dynamic", context);
    return read;
}

std::vector<RegisterRef> parseRegisterRefs(const nlohmann::json& value,
                                           const std::string& context) {
    if (!value.is_array()) {
        fail(context, "expected an array");
    }
    std::vector<RegisterRef> result;
    result.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        result.push_back(parseRegisterRef(value[i], context + "[" + std::to_string(i) + "]"));
    }
    return result;
}

std::vector<ResourceRef> parseResourceRefs(const nlohmann::json& value,
                                           const std::string& context) {
    if (!value.is_array()) {
        fail(context, "expected an array");
    }
    std::vector<ResourceRef> result;
    result.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        result.push_back(parseResourceRef(value[i], context + "[" + std::to_string(i) + "]"));
    }
    return result;
}

Instruction parseInstruction(const nlohmann::json& object, const std::string& context) {
    Instruction instruction;
    instruction.index = intField(object, "index", context);
    instruction.opcode = stringField(object, "opcode", context);
    instruction.text = stringField(object, "text", context);
    instruction.reads = parseRegisterRefs(field(object, "reads", context),
                                          context + ".reads");
    instruction.writes = parseRegisterRefs(field(object, "writes", context),
                                           context + ".writes");
    instruction.resourceRefs = parseResourceRefs(field(object, "resourceRefs", context),
                                                 context + ".resourceRefs");

    const auto cbufferRead = object.find("cbufferRead");
    if (cbufferRead != object.end() && !cbufferRead->is_null()) {
        if (!cbufferRead->is_object()) {
            fail(context, "field 'cbufferRead' must be an object or null");
        }
        instruction.cbufferRead = parseCbufferRead(*cbufferRead, context + ".cbufferRead");
    }

    const auto pressure = object.find("pressure");
    if (pressure != object.end() && !pressure->is_null()) {
        if (!pressure->is_object()) {
            fail(context, "field 'pressure' must be an object");
        }
        instruction.pressure.temp = optionalInt32Field(*pressure, "temp", context + ".pressure");
        instruction.pressure.ssa = optionalInt32Field(*pressure, "ssa", context + ".pressure");
    }

    const auto branch = object.find("branch");
    if (branch != object.end() && !branch->is_null()) {
        if (!branch->is_object()) {
            fail(context, "field 'branch' must be an object");
        }
        instruction.branch.kind = stringField(*branch, "kind", context + ".branch");
        instruction.branch.targetIndex =
            optionalInt32Field(*branch, "targetIndex", context + ".branch");
        instruction.branch.indent = intField(*branch, "indent", context + ".branch");
    }
    instruction.sourceLine = optionalInt32Field(object, "sourceLine", context);
    return instruction;
}

LivenessInterval parseLiveness(const nlohmann::json& object, const std::string& context) {
    LivenessInterval interval;
    interval.registerClass = stringField(object, "class", context);
    interval.index = intField(object, "index", context);
    interval.start = intField(object, "start", context);
    interval.end = intField(object, "end", context);
    return interval;
}

Resource parseResource(const nlohmann::json& object, const std::string& context) {
    Resource resource;
    resource.kind = stringField(object, "kind", context);
    resource.name = stringField(object, "name", context);
    resource.binding = intField(object, "binding", context);
    resource.size = optionalInt32Field(object, "size", context);
    return resource;
}

} // namespace

std::string variantStatusToString(VariantStatus status) {
    switch (status) {
    case VariantStatus::Ok:
        return "ok";
    case VariantStatus::Failed:
        return "failed";
    case VariantStatus::Skipped:
        return "skipped";
    }
    return "failed";
}

VariantStatus variantStatusFromString(std::string_view value) {
    if (value == "ok") {
        return VariantStatus::Ok;
    }
    if (value == "failed") {
        return VariantStatus::Failed;
    }
    if (value == "skipped") {
        return VariantStatus::Skipped;
    }
    throw std::invalid_argument("unknown variant status '" + std::string(value) + "'");
}

std::string toJson(const ReportSummary& summary, bool pretty) {
    return dumpJson(encodeSummary(summary), pretty);
}

std::string toJson(const DetailReport& detail, bool pretty) {
    return dumpJson(encodeDetail(detail), pretty);
}

std::string toJson(const ReportIndex& index, bool pretty) {
    return dumpJson(encodeIndex(index), pretty);
}

ReportSummary parseSummary(std::string_view jsonText) {
    const std::string context = "summary";
    const nlohmann::json document = parseDocument(jsonText, context);
    ReportSummary summary;
    summary.schemaVersion = intField(document, "schemaVersion", context);
    if (summary.schemaVersion != kReportSchemaVersion) {
        fail(context, "unsupported schemaVersion " + std::to_string(summary.schemaVersion));
    }
    summary.toolVersion = stringField(document, "toolVersion", context);
    summary.generatedAt = stringField(document, "generatedAt", context);
    summary.backend = stringField(document, "backend", context);
    summary.profile = stringField(document, "profile", context);
    summary.optimization = intField(document, "optimization", context);
    summary.debugInfo = boolField(document, "debugInfo", context);

    const nlohmann::json& shaderc = field(document, "shaderc", context);
    summary.shaderc.path = stringField(shaderc, "path", context + ".shaderc");
    summary.shaderc.version = stringField(shaderc, "version", context + ".shaderc");

    const nlohmann::json& source = field(document, "source", context);
    summary.source.path = stringField(source, "path", context + ".source");
    summary.source.sha256 = stringField(source, "sha256", context + ".source");
    summary.source.language = stringField(source, "language", context + ".source");

    summary.stage = stringField(document, "stage", context);
    summary.variantCount = intField(document, "variantCount", context);
    const nlohmann::json& variants = field(document, "variants", context);
    if (!variants.is_array()) {
        fail(context, "field 'variants' must be an array");
    }
    summary.variants.reserve(variants.size());
    for (size_t i = 0; i < variants.size(); ++i) {
        summary.variants.push_back(
            parseVariant(variants[i], context + ".variants[" + std::to_string(i) + "]"));
    }
    if (summary.variantCount != static_cast<int>(summary.variants.size())) {
        fail(context, "variantCount does not match variants array size");
    }
    return summary;
}

DetailReport parseDetail(std::string_view jsonText) {
    const std::string context = "detail";
    const nlohmann::json document = parseDocument(jsonText, context);
    DetailReport detail;
    detail.schemaVersion = intField(document, "schemaVersion", context);
    if (detail.schemaVersion != kReportSchemaVersion) {
        fail(context, "unsupported schemaVersion " + std::to_string(detail.schemaVersion));
    }
    detail.source = stringField(document, "source", context);
    detail.stage = stringField(document, "stage", context);
    detail.backend = stringField(document, "backend", context);
    detail.variantId = stringField(document, "variantId", context);

    const nlohmann::json& instructions = field(document, "instructions", context);
    if (!instructions.is_array()) {
        fail(context, "field 'instructions' must be an array");
    }
    detail.instructions.reserve(instructions.size());
    for (size_t i = 0; i < instructions.size(); ++i) {
        detail.instructions.push_back(parseInstruction(
            instructions[i], context + ".instructions[" + std::to_string(i) + "]"));
    }

    const nlohmann::json& liveness = field(document, "liveness", context);
    if (!liveness.is_array()) {
        fail(context, "field 'liveness' must be an array");
    }
    detail.liveness.reserve(liveness.size());
    for (size_t i = 0; i < liveness.size(); ++i) {
        detail.liveness.push_back(
            parseLiveness(liveness[i], context + ".liveness[" + std::to_string(i) + "]"));
    }

    const nlohmann::json& resources = field(document, "resources", context);
    if (!resources.is_array()) {
        fail(context, "field 'resources' must be an array");
    }
    detail.resources.reserve(resources.size());
    for (size_t i = 0; i < resources.size(); ++i) {
        detail.resources.push_back(
            parseResource(resources[i], context + ".resources[" + std::to_string(i) + "]"));
    }

    const nlohmann::json& cbufferReads = field(document, "cbufferReads", context);
    if (!cbufferReads.is_array()) {
        fail(context, "field 'cbufferReads' must be an array");
    }
    detail.cbufferReads.reserve(cbufferReads.size());
    for (size_t i = 0; i < cbufferReads.size(); ++i) {
        detail.cbufferReads.push_back(parseCbufferRead(
            cbufferReads[i], context + ".cbufferReads[" + std::to_string(i) + "]"));
    }
    return detail;
}

ReportIndex parseIndex(std::string_view jsonText) {
    const std::string context = "index";
    const nlohmann::json document = parseDocument(jsonText, context);
    ReportIndex index;
    index.schemaVersion = intField(document, "schemaVersion", context);
    if (index.schemaVersion != kReportSchemaVersion) {
        fail(context, "unsupported schemaVersion " + std::to_string(index.schemaVersion));
    }
    index.generatedAt = stringField(document, "generatedAt", context);
    const nlohmann::json& entries = field(document, "entries", context);
    if (!entries.is_array()) {
        fail(context, "field 'entries' must be an array");
    }
    index.entries.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        const std::string entryContext = context + ".entries[" + std::to_string(i) + "]";
        ReportIndexEntry entry;
        entry.backend = stringField(entries[i], "backend", entryContext);
        entry.source = stringField(entries[i], "source", entryContext);
        entry.stage = stringField(entries[i], "stage", entryContext);
        entry.summaryPath = stringField(entries[i], "summaryPath", entryContext);
        entry.variantCount = intField(entries[i], "variantCount", entryContext);
        entry.okCount = intField(entries[i], "okCount", entryContext);
        entry.failedCount = intField(entries[i], "failedCount", entryContext);
        entry.sourceSha256 = stringField(entries[i], "sourceSha256", entryContext);
        entry.generatedAt = stringField(entries[i], "generatedAt", entryContext);
        index.entries.push_back(std::move(entry));
    }
    return index;
}

} // namespace hbg::analysis
