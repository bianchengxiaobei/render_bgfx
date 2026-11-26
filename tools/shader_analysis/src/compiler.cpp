#include "hbg/analysis/compiler.h"

#include "hbg/analysis/compile_command.h"
#include "hbg/analysis/dxbc_parser.h"
#include "hbg/analysis/hashing.h"
#include "hbg/analysis/liveness.h"
#include "hbg/analysis/process_runner.h"
#include "hbg/analysis/report_files.h"
#include "hbg/analysis/spirv_blob.h"
#include "hbg/analysis/spirv_parser.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace hbg::analysis {
namespace {

using Json = nlohmann::json;
namespace fs = std::filesystem;

constexpr int kCacheMetaVersion = 1;
std::atomic<std::uint64_t> g_tempCounter{0};

std::string trimCopy(std::string_view text) {
    while (!text.empty() &&
           (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
            text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
            text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

std::string lowerCopy(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return result;
}

template <typename Function>
void forEachLine(std::string_view text, Function&& function) {
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find('\n', start);
        std::string_view line = text.substr(
            start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        function(line);
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
}

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
    const fs::path temporary = path.string() + ".tmp." +
                               std::to_string(++g_tempCounter);
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("cannot write file: " + temporary.string());
        }
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        stream.flush();
        if (!stream) {
            fs::remove(temporary);
            throw std::runtime_error("cannot write file: " + temporary.string());
        }
    }
    fs::remove(path, error);
    error.clear();
    fs::rename(temporary, path, error);
    if (error) {
        fs::remove(temporary);
        throw std::runtime_error("cannot publish file " + path.string() + ": " +
                                 error.message());
    }
}

void writeBinaryFile(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
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
    if (!bytes.empty()) {
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    stream.flush();
    if (!stream) {
        throw std::runtime_error("cannot write file: " + path.string());
    }
}

bool isNonEmptyFile(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error) && fs::file_size(path, error) > 0 &&
           !error;
}

std::string relativeToRoot(const fs::path& path, const fs::path& root) {
    std::error_code error;
    const fs::path absolute = fs::absolute(path, error).lexically_normal();
    const fs::path absoluteRoot = fs::absolute(root, error).lexically_normal();
    if (error) {
        return path.generic_string();
    }
    const fs::path relative = fs::relative(absolute, absoluteRoot, error);
    if (!error && !relative.empty()) {
        bool escapes = false;
        for (const fs::path& part : relative) {
            if (part == "..") {
                escapes = true;
                break;
            }
        }
        if (!escapes) {
            return relative.generic_string();
        }
    }
    return absolute.generic_string();
}

std::string sourceRelativeStem(const CompileTask& task) {
    fs::path path(task.source);
    path.replace_extension();
    std::string stem = path.generic_string();
    const std::string prefix = "shaders/";
    if (stem.rfind(prefix, 0) == 0) {
        stem.erase(0, prefix.size());
    }
    if (stem.empty() || stem.rfind("../", 0) == 0) {
        stem = fs::path(task.source).stem().generic_string();
    }
    return stem;
}

fs::path taskOutputPath(const fs::path& root, const CompileTask& task) {
    return root / "shaders" / task.output;
}

fs::path cacheDirectory(const fs::path& root, const CompileTask& task,
                        bool debugInfo) {
    std::string variant = task.variantId();
    if (debugInfo) {
        variant += "-debug";
    }
    return root / "shader_analysis" / "cache" / task.backend / sourceRelativeStem(task) /
           variant;
}

fs::path tempPathFor(const fs::path& output) {
    return fs::path(output.string() + ".tmp." + std::to_string(++g_tempCounter));
}

void publishFile(const fs::path& source, const fs::path& destination) {
    std::error_code error;
    fs::create_directories(destination.parent_path(), error);
    if (error) {
        throw std::runtime_error("cannot create directory " +
                                 destination.parent_path().string() + ": " +
                                 error.message());
    }
    fs::remove(destination, error);
    error.clear();
    fs::rename(source, destination, error);
    if (!error) {
        return;
    }
    error.clear();
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing, error);
    if (error) {
        throw std::runtime_error("cannot publish " + destination.string() + ": " +
                                 error.message());
    }
    fs::remove(source, error);
}

void copyFileTo(const fs::path& source, const fs::path& destination) {
    std::error_code error;
    fs::create_directories(destination.parent_path(), error);
    if (error) {
        throw std::runtime_error("cannot create directory " +
                                 destination.parent_path().string() + ": " +
                                 error.message());
    }
    const fs::path temporary = destination.string() + ".tmp." +
                               std::to_string(++g_tempCounter);
    fs::copy_file(source, temporary, fs::copy_options::overwrite_existing, error);
    if (error) {
        throw std::runtime_error("cannot copy " + source.string() + " to " +
                                 destination.string() + ": " + error.message());
    }
    publishFile(temporary, destination);
}

std::string outputStem(const CompileTask& task) {
    return fs::path(task.output).filename().stem().string();
}

std::string formatTimestamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

std::string queryShadercVersionText(std::string_view standardOutput,
                                    std::string_view standardError);

std::string queryShadercVersion(const CompilerOptions& options) {
    if (options.shaderc.empty()) {
        return "unknown";
    }
    const ProcessResult result =
        runProcess(options.shaderc, {"--version"}, options.root, 10000);
    if (!result.launched || result.exitCode != 0) {
        return "unknown";
    }
    const std::string output =
        queryShadercVersionText(result.stdoutText, result.stderrText);
    return output.empty() ? std::string("unknown") : output;
}

std::string queryShadercVersionText(std::string_view standardOutput,
                                    std::string_view standardError) {
    std::string output = trimCopy(standardOutput);
    if (output.empty()) {
        output = trimCopy(standardError);
    }
    const size_t newline = output.find('\n');
    if (newline != std::string::npos) {
        output.resize(newline);
        output = trimCopy(output);
    }
    return output;
}

std::vector<LogEntry> parseLogArray(const Json& array) {
    std::vector<LogEntry> result;
    if (!array.is_array()) {
        return result;
    }
    for (const Json& value : array) {
        if (!value.is_object()) {
            continue;
        }
        LogEntry entry;
        if (value.contains("message") && value["message"].is_string()) {
            entry.message = value["message"].get<std::string>();
        }
        if (value.contains("stacktrace") && value["stacktrace"].is_array()) {
            for (const Json& line : value["stacktrace"]) {
                if (line.is_string()) {
                    entry.stacktrace.push_back(line.get<std::string>());
                }
            }
        }
        result.push_back(std::move(entry));
    }
    return result;
}

Json encodeLogArray(const std::vector<LogEntry>& entries) {
    Json result = Json::array();
    for (const LogEntry& entry : entries) {
        Json value = Json::object();
        value["message"] = entry.message;
        value["stacktrace"] = entry.stacktrace;
        result.push_back(std::move(value));
    }
    return result;
}

void addDiagnostic(ShadercLogs& logs, std::string_view line, bool succeeded,
                   std::set<std::string>& seen) {
    const std::string text = trimCopy(line);
    if (text.empty() || !seen.insert(text).second) {
        return;
    }
    const std::string lower = lowerCopy(text);
    const bool warning = lower.find(": warning") != std::string::npos ||
                         lower.rfind("warning", 0) == 0;
    const bool error = lower.find(": error") != std::string::npos ||
                       lower.rfind("error", 0) == 0 ||
                       lower.find("fatal error") != std::string::npos;
    if (!warning && !error) {
        if (!succeeded) {
            logs.errors.push_back({text, {}});
        }
        return;
    }

    LogEntry entry;
    entry.message = text;
    const size_t open = text.find('(');
    const size_t close = open == std::string::npos ? std::string::npos
                                                   : text.find(')', open + 1);
    if (open != std::string::npos && close != std::string::npos && close > open + 1) {
        const std::string lineNumber = text.substr(open + 1, close - open - 1);
        if (!lineNumber.empty() &&
            std::all_of(lineNumber.begin(), lineNumber.end(),
                        [](unsigned char character) {
                            return std::isdigit(character) != 0;
                        })) {
            entry.stacktrace.push_back(text.substr(0, open) + ":" + lineNumber);
        }
    }
    if (error) {
        logs.errors.push_back(std::move(entry));
    } else {
        logs.warnings.push_back(std::move(entry));
    }
}

std::string normalizeDependencyPath(const fs::path& path, const fs::path& root) {
    std::error_code error;
    fs::path absolute = path.is_absolute() ? path : root / path;
    absolute = absolute.lexically_normal();
    return relativeToRoot(absolute, root);
}

fs::path dependencyPath(const std::string& stored, const fs::path& root) {
    const fs::path path(stored);
    return path.is_absolute() ? path : root / path;
}

std::vector<std::string> parseDependencyFile(std::string_view text,
                                             const fs::path& root) {
    const size_t separator = text.find(" : ");
    if (separator == std::string_view::npos) {
        return {};
    }
    std::string cleaned;
    for (size_t i = separator + 3; i < text.size(); ++i) {
        if (text[i] == '\\') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
                continue;
            }
            if (i + 2 < text.size() && text[i + 1] == '\r' && text[i + 2] == '\n') {
                i += 2;
                continue;
            }
        }
        cleaned.push_back(text[i]);
    }

    std::vector<std::string> result;
    std::set<std::string> seen;
    std::istringstream stream(cleaned);
    std::string token;
    while (stream >> token) {
        if (token.size() >= 2 && token.front() == '"' && token.back() == '"') {
            token = token.substr(1, token.size() - 2);
        }
        if (token.empty()) {
            continue;
        }
        const std::string normalized = normalizeDependencyPath(fs::path(token), root);
        if (seen.insert(normalized).second) {
            result.push_back(normalized);
        }
    }
    return result;
}

struct Dependency {
    std::string path;
    std::string sha256;
};

std::vector<fs::path> fallbackDependencyPaths(const VariantManifest& manifest,
                                              const CompileTask& task,
                                              const CompilerOptions& options) {
    std::vector<fs::path> result;
    result.push_back(options.root / task.source);
    if (task.varyingDef.has_value()) {
        result.push_back(options.root / *task.varyingDef);
    }
    for (const fs::path& includeDir :
         resolveIncludeDirs(manifest, options.root, options.bgfxSrc)) {
        std::error_code error;
        if (!fs::is_directory(includeDir, error)) {
            continue;
        }
        fs::recursive_directory_iterator iterator(
            includeDir, fs::directory_options::skip_permission_denied, error);
        const fs::recursive_directory_iterator end;
        for (; iterator != end; iterator.increment(error)) {
            if (error) {
                error.clear();
                continue;
            }
            if (iterator->is_regular_file(error) && !error) {
                result.push_back(iterator->path());
            }
            error.clear();
        }
    }
    return result;
}

std::vector<Dependency> hashPaths(const std::vector<fs::path>& paths,
                                  const fs::path& root) {
    std::map<std::string, std::string> values;
    for (const fs::path& path : paths) {
        if (!isNonEmptyFile(path)) {
            continue;
        }
        const std::string stored = normalizeDependencyPath(path, root);
        values[stored] = sha256File(path);
    }
    std::vector<Dependency> result;
    result.reserve(values.size());
    for (const auto& [path, hash] : values) {
        result.push_back({path, hash});
    }
    return result;
}

bool effectiveDebugInfo(const CompileTask& task, const CompilerOptions& options) {
    return options.debugInfo && task.backend == "spirv";
}

std::vector<Dependency> dependenciesForTask(const VariantManifest& manifest,
                                            const CompileTask& task,
                                            const CompilerOptions& options,
                                            const fs::path& dependsPath) {
    std::vector<std::string> storedPaths;
    if (isNonEmptyFile(dependsPath)) {
        storedPaths = parseDependencyFile(readTextFile(dependsPath), options.root);
    }
    if (storedPaths.empty()) {
        return hashPaths(fallbackDependencyPaths(manifest, task, options), options.root);
    }

    std::set<std::string> seen;
    std::vector<fs::path> paths;
    const auto append = [&](const fs::path& path) {
        const std::string stored = normalizeDependencyPath(path, options.root);
        if (seen.insert(stored).second) {
            paths.push_back(path);
        }
    };
    append(options.root / task.source);
    if (task.varyingDef.has_value()) {
        append(options.root / *task.varyingDef);
    }
    for (const std::string& stored : storedPaths) {
        append(dependencyPath(stored, options.root));
    }
    return hashPaths(paths, options.root);
}

std::string cacheKey(const VariantManifest& manifest, const CompileTask& task,
                     const CompilerOptions& options, std::string_view shadercVersion,
                     const std::vector<Dependency>& dependencies) {
    if (task.backend != "dxbc" && task.backend != "spirv") {
        throw std::runtime_error("unknown backend '" + task.backend + "'");
    }
    const auto backend = manifest.backends.find(task.backend);
    if (backend == manifest.backends.end()) {
        throw std::runtime_error("manifest has no backend '" + task.backend + "'");
    }
    std::string material = "shader-analysis-cache-v1\n";
    material += shadercVersion;
    material.push_back('\n');
    material += task.backend;
    material.push_back('\n');
    material += backend->second.profile;
    material.push_back('\n');
    material += task.stage;
    material.push_back('\n');
    material += std::to_string(task.optimization);
    material.push_back('\n');
    material += effectiveDebugInfo(task, options) ? '1' : '0';
    material.push_back('\n');
    material += task.source;
    material.push_back('\n');
    for (const std::string& define : task.defines) {
        material += define;
        material.push_back('\n');
    }
    for (const std::string& includeDir : manifest.includeDirs) {
        material += includeDir;
        material.push_back('\n');
    }
    material += "dependencies\n";
    for (const Dependency& dependency : dependencies) {
        material += dependency.path;
        material.push_back('\n');
        material += dependency.sha256;
        material.push_back('\n');
    }
    return sha256Hex(material);
}

struct CacheEntry {
    fs::path directory;
    fs::path output;
    fs::path disasm;
    fs::path spirv;
    std::string key;
    std::vector<LogEntry> warnings;
    std::vector<LogEntry> errors;
};

Json cacheMetaFor(const CompileTask& task, const VariantManifest& manifest,
                  const CompilerOptions& options, std::string_view shadercVersion,
                  std::string_view key, const std::vector<Dependency>& dependencies,
                  const ShadercLogs& logs, std::uintmax_t outputBytes) {
    const auto backend = manifest.backends.find(task.backend);
    Json meta = Json::object();
    meta["cacheVersion"] = kCacheMetaVersion;
    meta["key"] = key;
    meta["shadercVersion"] = shadercVersion;
    meta["shadercPath"] = relativeToRoot(options.shaderc, options.root);
    meta["backend"] = task.backend;
    meta["profile"] = backend == manifest.backends.end() ? "" : backend->second.profile;
    meta["stage"] = task.stage;
    meta["source"] = task.source;
    meta["variantId"] = task.variantId();
    meta["optimization"] = task.optimization;
    meta["debugInfo"] = effectiveDebugInfo(task, options);
    meta["defines"] = task.defines;
    meta["outputBytes"] = outputBytes;
    meta["dependencies"] = Json::array();
    for (const Dependency& dependency : dependencies) {
        Json value = Json::object();
        value["path"] = dependency.path;
        value["sha256"] = dependency.sha256;
        meta["dependencies"].push_back(std::move(value));
    }
    meta["logs"] = Json::object();
    meta["logs"]["warnings"] = encodeLogArray(logs.warnings);
    meta["logs"]["errors"] = encodeLogArray(logs.errors);
    return meta;
}

std::optional<CacheEntry> loadCache(const VariantManifest& manifest,
                                    const CompileTask& task,
                                    const CompilerOptions& options,
                                    std::string_view shadercVersion) {
    const fs::path directory = cacheDirectory(
        options.root, task, options.debugInfo && task.backend == "spirv");
    const fs::path output = directory / "output.bin";
    const fs::path disasm = directory / "output.bin.disasm";
    const fs::path spirv = directory / "spirv.bin";
    const fs::path metaPath = directory / "meta.json";
    if (!isNonEmptyFile(metaPath) || !isNonEmptyFile(output)) {
        return std::nullopt;
    }
    if (task.backend == "dxbc" && !isNonEmptyFile(disasm)) {
        return std::nullopt;
    }

    try {
        const Json meta = Json::parse(readTextFile(metaPath));
        if (!meta.is_object() || meta.value("cacheVersion", 0) != kCacheMetaVersion ||
            meta.value("variantId", std::string{}) != task.variantId() ||
            meta.value("source", std::string{}) != task.source ||
            meta.value("backend", std::string{}) != task.backend ||
            meta.value("stage", std::string{}) != task.stage) {
            return std::nullopt;
        }
        const auto dependenciesJson = meta.find("dependencies");
        if (dependenciesJson == meta.end() || !dependenciesJson->is_array()) {
            return std::nullopt;
        }
        std::vector<Dependency> dependencies;
        for (const Json& value : *dependenciesJson) {
            if (!value.is_object() || !value.contains("path") ||
                !value["path"].is_string()) {
                return std::nullopt;
            }
            const std::string stored = value["path"].get<std::string>();
            const fs::path path = dependencyPath(stored, options.root);
            if (!isNonEmptyFile(path)) {
                return std::nullopt;
            }
            dependencies.push_back({stored, sha256File(path)});
        }
        if (dependencies.empty() ||
            cacheKey(manifest, task, options, shadercVersion, dependencies) !=
                meta.value("key", std::string{})) {
            return std::nullopt;
        }

        ShadercLogs logs;
        if (meta.contains("logs") && meta["logs"].is_object()) {
            const auto& logObject = meta["logs"];
            if (logObject.contains("warnings")) {
                logs.warnings = parseLogArray(logObject["warnings"]);
            }
            if (logObject.contains("errors")) {
                logs.errors = parseLogArray(logObject["errors"]);
            }
        }

        if (task.backend == "spirv") {
            try {
                const SpirvBlob blob = extractSpirvBlob(output);
                writeBinaryFile(spirv, blob.bytes);
            } catch (const std::exception&) {
                return std::nullopt;
            }
        }
        return CacheEntry{directory, output, disasm, spirv, meta.value("key", ""),
                          logs.warnings, logs.errors};
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void copyCachedArtifacts(const CacheEntry& cache, const CompileTask& task,
                         const fs::path& output, const fs::path& disasm) {
    copyFileTo(cache.output, output);
    if (task.backend == "dxbc") {
        copyFileTo(cache.disasm, disasm);
    }
}

void writeCache(const VariantManifest& manifest, const CompileTask& task,
                const CompilerOptions& options, std::string_view shadercVersion,
                std::string_view key, const std::vector<Dependency>& dependencies,
                const ShadercLogs& logs, const fs::path& output,
                const fs::path& disasm, const std::optional<SpirvBlob>& spirv) {
    const fs::path directory = cacheDirectory(
        options.root, task, options.debugInfo && task.backend == "spirv");
    std::error_code error;
    fs::create_directories(directory, error);
    if (error) {
        throw std::runtime_error("cannot create cache directory " +
                                 directory.string() + ": " + error.message());
    }
    const fs::path cachedOutput = directory / "output.bin";
    const fs::path cachedDisasm = directory / "output.bin.disasm";
    copyFileTo(output, cachedOutput);
    if (task.backend == "dxbc" && isNonEmptyFile(disasm)) {
        copyFileTo(disasm, cachedDisasm);
    }
    if (task.backend == "spirv" && spirv.has_value()) {
        writeBinaryFile(directory / "spirv.bin", spirv->bytes);
    }
    const Json meta = cacheMetaFor(task, manifest, options, shadercVersion, key,
                                   dependencies, logs, fs::file_size(output));
    writeTextFile(directory / "meta.json", meta.dump(2) + "\n");
}

VariantReport baseVariant(const CompileTask& task) {
    VariantReport variant;
    variant.variantId = task.variantId();
    variant.name = outputStem(task);
    variant.defines = task.defines;
    variant.canonicalDefines = canonicalDefines(task.defines);
    variant.keywords = keywordNames(task.defines);
    return variant;
}

void applyLogMetrics(VariantReport& variant, const ShadercLogs& logs,
                     bool debugInfoEnabled) {
    Metrics metrics;
    metrics.debugInfo = debugInfoEnabled;
    metrics.warningCount = static_cast<std::int64_t>(logs.warnings.size());
    metrics.errorCount = static_cast<std::int64_t>(logs.errors.size());
    if (variant.binaryBytes.has_value()) {
        metrics.microCodeSize = variant.binaryBytes;
    }
    variant.metrics = std::move(metrics);
    variant.warnings = logs.warnings;
    variant.errors = logs.errors;
}

fs::path detailPathFor(const CompileTask& task, const CompilerOptions& options) {
    std::string variant = task.variantId();
    if (task.backend == "spirv" && options.debugInfo) {
        variant += "-debug";
    }
    return options.reportsDir / task.backend / fs::path(sourceRelativeStem(task)) /
           (variant + ".json");
}

void mergeAnalysisMetrics(VariantReport& variant, Metrics analysis) {
    if (variant.metrics.has_value()) {
        const Metrics& previous = *variant.metrics;
        analysis.warningCount = previous.warningCount;
        analysis.errorCount = previous.errorCount;
        analysis.microCodeSize = previous.microCodeSize;
        if (!analysis.debugInfo.value_or(false)) {
            analysis.debugInfo = previous.debugInfo;
        }
    } else {
        analysis.microCodeSize = variant.binaryBytes;
    }
    variant.metrics = std::move(analysis);
}

void analyzeDxbcVariant(const fs::path& disassembly, const CompileTask& task,
                        const CompilerOptions& options, VariantReport& variant) {
    if (task.backend != "dxbc") {
        return;
    }
    try {
        const DxbcProgram program = parseDxbcDisassembly(readTextFile(disassembly));
        const LivenessAnalysis liveness = analyzeLiveness(program.instructions);

        DxbcProgram analyzed = program;
        applyLiveness(analyzed, liveness);

        const DetailReport detail =
            buildDxbcDetail(analyzed, task.source, task.stage, task.variantId());
        const fs::path detailPath = detailPathFor(task, options);
        std::error_code error;
        fs::create_directories(detailPath.parent_path(), error);
        if (error) {
            throw std::runtime_error("cannot create detail directory " +
                                     detailPath.parent_path().string() + ": " +
                                     error.message());
        }
        writeTextFile(detailPath, toJson(detail));

        mergeAnalysisMetrics(variant, buildDxbcMetrics(analyzed));
        variant.detailPath = relativeToRoot(detailPath, options.root);
    } catch (const std::exception& error) {
        variant.status = VariantStatus::Failed;
        variant.detailPath = std::nullopt;
        variant.errors.push_back(
            {std::string("DXBC analysis failed: ") + error.what(), {}});
        if (variant.metrics.has_value()) {
            variant.metrics->errorCount =
                static_cast<std::int64_t>(variant.errors.size());
        }
    }
}

void analyzeSpirvVariant(const fs::path& spirv, const CompileTask& task,
                         const CompilerOptions& options, VariantReport& variant) {
    if (task.backend != "spirv") {
        return;
    }
    std::vector<LogEntry> analysisWarnings;
    try {
        const fs::path disassembler = findSpirvTool(options.spirvDis, "spirv-dis");
        if (disassembler.empty()) {
            throw std::runtime_error(
                "spirv-dis not found; pass --spirv-dis or install the Vulkan SDK");
        }
        const int timeoutMs =
            options.timeoutSeconds > 0 ? options.timeoutSeconds * 1000 : 0;
        const ProcessResult disassembly = runProcess(
            disassembler,
            {spirv.string(), "--no-color", "--raw-id", "--no-header"},
            options.root, timeoutMs);
        if (!disassembly.launched || disassembly.timedOut || disassembly.cancelled ||
            disassembly.exitCode != 0 || trimCopy(disassembly.stdoutText).empty()) {
            const std::string detail = trimCopy(disassembly.stderrText);
            throw std::runtime_error(
                "spirv-dis failed" + (detail.empty() ? std::string{}
                                                    : ": " + detail));
        }

        SpirvReflection reflection;
        const fs::path reflector = findSpirvTool(options.spirvCross, "spirv-cross");
        if (!reflector.empty()) {
            const ProcessResult reflected = runProcess(
                reflector, {spirv.string(), "--reflect"}, options.root, timeoutMs);
            if (reflected.launched && !reflected.timedOut && !reflected.cancelled &&
                reflected.exitCode == 0) {
                reflection = parseSpirvReflectionJson(reflected.stdoutText);
            } else {
                const std::string detail = trimCopy(reflected.stderrText);
                analysisWarnings.push_back(
                    {"spirv-cross reflection failed" +
                         (detail.empty() ? std::string{} : ": " + detail),
                     {}});
            }
        }

        SpirvAnalysisOptions analysisOptions;
        analysisOptions.debugInfo = options.debugInfo;
        SpirvAnalysisResult analyzed = analyzeSpirvDisassembly(
            disassembly.stdoutText, reflection, analysisOptions, task.source,
            task.stage, task.variantId());
        const fs::path detailPath = detailPathFor(task, options);
        std::error_code error;
        fs::create_directories(detailPath.parent_path(), error);
        if (error) {
            throw std::runtime_error("cannot create detail directory " +
                                     detailPath.parent_path().string() + ": " +
                                     error.message());
        }
        writeTextFile(detailPath, toJson(analyzed.detail));
        mergeAnalysisMetrics(variant, std::move(analyzed.metrics));
        variant.detailPath = relativeToRoot(detailPath, options.root);
        variant.warnings.insert(variant.warnings.end(), analysisWarnings.begin(),
                                analysisWarnings.end());
        if (variant.metrics.has_value()) {
            variant.metrics->warningCount =
                static_cast<std::int64_t>(variant.warnings.size());
        }
    } catch (const std::exception& error) {
        variant.status = VariantStatus::Failed;
        variant.detailPath = std::nullopt;
        variant.errors.push_back(
            {std::string("SPIR-V analysis failed: ") + error.what(), {}});
        if (variant.metrics.has_value()) {
            variant.metrics->errorCount =
                static_cast<std::int64_t>(variant.errors.size());
        }
    }
}

VariantReport failedVariant(const CompileTask& task, const CompilerOptions& options,
                            const ShadercLogs& logs, std::int64_t compileMs) {
    VariantReport variant = baseVariant(task);
    variant.status = VariantStatus::Failed;
    variant.compileMs = compileMs;
    variant.cacheHit = false;
    applyLogMetrics(variant, logs,
                    options.debugInfo && task.backend == "spirv");
    return variant;
}

VariantReport skippedVariant(const CompileTask& task) {
    VariantReport variant = baseVariant(task);
    variant.status = VariantStatus::Skipped;
    variant.compileMs = 0;
    variant.cacheHit = false;
    Metrics metrics;
    metrics.debugInfo = false;
    metrics.warningCount = 0;
    metrics.errorCount = 0;
    variant.metrics = metrics;
    return variant;
}

std::string compileFailureMessage(const ProcessResult& result) {
    if (!result.launchError.empty()) {
        return result.launchError;
    }
    if (result.timedOut) {
        return "shaderc timed out";
    }
    if (result.cancelled) {
        return "shaderc cancelled";
    }
    return "shaderc exited with code " + std::to_string(result.exitCode);
}

ShadercLogs processLogs(const ProcessResult& result) {
    const bool succeeded = result.launched && !result.timedOut &&
                           !result.cancelled && result.exitCode == 0;
    ShadercLogs logs =
        parseShadercLogs(result.stdoutText, result.stderrText, succeeded);
    if (!result.launched && !result.launchError.empty()) {
        logs.errors.push_back({result.launchError, {}});
    } else if (result.timedOut || result.cancelled) {
        logs.errors.push_back({compileFailureMessage(result), {}});
    } else if (result.exitCode != 0 && logs.errors.empty()) {
        logs.errors.push_back({compileFailureMessage(result), {}});
    }
    return logs;
}

VariantReport compileOne(const VariantManifest& manifest, const CompileTask& task,
                         const CompilerOptions& options,
                         std::string_view shadercVersion,
                         const std::atomic<bool>& cancelFlag) {
    const fs::path output = taskOutputPath(options.root, task);
    const fs::path disasm = fs::path(output.string() + ".disasm");
    const fs::path cache = cacheDirectory(
        options.root, task, options.debugInfo && task.backend == "spirv");

    if (!options.force && !options.noCache) {
        if (const std::optional<CacheEntry> entry =
                loadCache(manifest, task, options, shadercVersion)) {
            try {
                copyCachedArtifacts(*entry, task, output, disasm);
                VariantReport variant = baseVariant(task);
                variant.status = entry->errors.empty() ? VariantStatus::Ok
                                                        : VariantStatus::Failed;
                variant.cacheHit = true;
                variant.compileMs = 0;
                variant.binaryBytes = static_cast<std::int64_t>(
                    fs::file_size(entry->output));
                variant.binaryPath = relativeToRoot(entry->output, options.root);
                if (task.backend == "dxbc") {
                    variant.disasmPath = relativeToRoot(entry->disasm, options.root);
                }
                variant.warnings = entry->warnings;
                variant.errors = entry->errors;
                Metrics metrics;
                metrics.debugInfo = options.debugInfo && task.backend == "spirv";
                metrics.warningCount =
                    static_cast<std::int64_t>(entry->warnings.size());
                metrics.errorCount = static_cast<std::int64_t>(entry->errors.size());
                metrics.microCodeSize = variant.binaryBytes;
                variant.metrics = metrics;
                if (variant.status == VariantStatus::Ok) {
                    if (task.backend == "dxbc") {
                        analyzeDxbcVariant(entry->disasm, task, options, variant);
                    } else {
                        analyzeSpirvVariant(entry->spirv, task, options, variant);
                    }
                }
                return variant;
            } catch (const std::exception&) {
            }
        }
    }

    ShadercLogs lastLogs;
    std::int64_t compileMs = 0;
    std::vector<Dependency> dependencies;
    std::string key;
    std::optional<SpirvBlob> spirv;

    const int attempts = options.retries + 1;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (cancelFlag.load()) {
            return skippedVariant(task);
        }
        const auto started = std::chrono::steady_clock::now();
        const fs::path temporary = tempPathFor(output);
        const fs::path temporaryDisasm = fs::path(temporary.string() + ".disasm");
        const fs::path temporaryDepends = fs::path(temporary.string() + ".d");

        try {
            fs::create_directories(output.parent_path());
            CompileCommandOptions commandOptions;
            commandOptions.debugInfo = options.debugInfo;
            commandOptions.disasm = task.backend == "dxbc";
            commandOptions.depends = true;
            commandOptions.outputPath = temporary;
            const CompileCommand command = buildCompileCommand(
                manifest, task, options.root, options.bgfxSrc, options.shaderc,
                commandOptions);
            std::vector<std::string> arguments(command.args.begin() + 1,
                                               command.args.end());
            const ProcessResult result = runProcess(
                options.shaderc, arguments, options.root,
                options.timeoutSeconds > 0 ? options.timeoutSeconds * 1000 : 0,
                &cancelFlag);
            compileMs = static_cast<std::int64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count());
            lastLogs = processLogs(result);
            const bool success = result.launched && !result.timedOut &&
                                 !result.cancelled && result.exitCode == 0 &&
                                 lastLogs.errors.empty();
            if (!success) {
                if (!result.launched && lastLogs.errors.empty()) {
                    lastLogs.errors.push_back({compileFailureMessage(result), {}});
                }
                if (options.keepTemp) {
                    continue;
                }
                std::error_code ignore;
                fs::remove(temporary, ignore);
                fs::remove(temporaryDisasm, ignore);
                fs::remove(temporaryDepends, ignore);
                continue;
            }
            if (!isNonEmptyFile(temporary)) {
                lastLogs.errors.push_back({"shaderc produced an empty output", {}});
                if (options.keepTemp) {
                    continue;
                }
                std::error_code ignore;
                fs::remove(temporary, ignore);
                fs::remove(temporaryDisasm, ignore);
                fs::remove(temporaryDepends, ignore);
                continue;
            }
            if (task.backend == "dxbc" && !isNonEmptyFile(temporaryDisasm)) {
                lastLogs.errors.push_back(
                    {"shaderc did not produce the requested DXBC disassembly", {}});
                if (options.keepTemp) {
                    continue;
                }
                std::error_code ignore;
                fs::remove(temporary, ignore);
                fs::remove(temporaryDisasm, ignore);
                fs::remove(temporaryDepends, ignore);
                continue;
            }
            if (task.backend == "spirv") {
                try {
                    spirv = extractSpirvBlob(temporary);
                } catch (const std::exception& error) {
                    lastLogs.errors.push_back({error.what(), {}});
                    if (options.keepTemp) {
                        continue;
                    }
                    std::error_code ignore;
                    fs::remove(temporary, ignore);
                    fs::remove(temporaryDisasm, ignore);
                    fs::remove(temporaryDepends, ignore);
                    continue;
                }
            }
            try {
                dependencies = dependenciesForTask(manifest, task, options,
                                                   temporaryDepends);
            } catch (const std::exception& error) {
                lastLogs.warnings.push_back(
                    {std::string("dependency scan failed; using source only: ") +
                         error.what(),
                     {}});
                dependencies = hashPaths({options.root / task.source}, options.root);
            }
            key = cacheKey(manifest, task, options, shadercVersion, dependencies);

            publishFile(temporary, output);
            if (task.backend == "dxbc") {
                publishFile(temporaryDisasm, disasm);
            }
            std::error_code ignore;
            fs::remove(temporaryDepends, ignore);

            VariantReport variant = baseVariant(task);
            variant.status = VariantStatus::Ok;
            variant.cacheHit = false;
            variant.compileMs = compileMs;
            variant.binaryBytes =
                static_cast<std::int64_t>(fs::file_size(output));
            variant.binaryPath = relativeToRoot(output, options.root);
            if (task.backend == "dxbc") {
                variant.disasmPath = relativeToRoot(disasm, options.root);
            }
            applyLogMetrics(variant, lastLogs,
                            options.debugInfo && task.backend == "spirv");
            if (!options.noCache) {
                try {
                    writeCache(manifest, task, options, shadercVersion, key,
                               dependencies, lastLogs, output, disasm, spirv);
                    variant.binaryPath =
                        relativeToRoot(cache / "output.bin", options.root);
                    if (task.backend == "dxbc") {
                        variant.disasmPath =
                            relativeToRoot(cache / "output.bin.disasm", options.root);
                    }
                } catch (const std::exception& error) {
                    variant.warnings.push_back(
                        {std::string("cache write failed: ") + error.what(), {}});
                    if (variant.metrics.has_value()) {
                        variant.metrics->warningCount =
                            static_cast<std::int64_t>(variant.warnings.size());
                    }
                }
            }
            if (variant.status == VariantStatus::Ok) {
                if (task.backend == "dxbc") {
                    analyzeDxbcVariant(disasm, task, options, variant);
                } else if (spirv.has_value()) {
                    fs::path analysisPath = cache / "spirv.bin";
                    std::optional<fs::path> temporarySpirv;
                    if (options.noCache || !isNonEmptyFile(analysisPath)) {
                        temporarySpirv = tempPathFor(output);
                        writeBinaryFile(*temporarySpirv, spirv->bytes);
                        analysisPath = *temporarySpirv;
                    }
                    analyzeSpirvVariant(analysisPath, task, options, variant);
                    if (temporarySpirv.has_value()) {
                        std::error_code removeError;
                        fs::remove(*temporarySpirv, removeError);
                    }
                }
            }
            return variant;
        } catch (const std::exception& error) {
            lastLogs.errors.push_back({error.what(), {}});
            if (options.keepTemp) {
                continue;
            }
            std::error_code ignore;
            fs::remove(temporary, ignore);
            fs::remove(temporaryDisasm, ignore);
            fs::remove(temporaryDepends, ignore);
        }
    }

    compileMs = std::max<std::int64_t>(compileMs, 0);
    VariantReport variant = failedVariant(task, options, lastLogs, compileMs);
    variant.binaryPath = std::nullopt;
    variant.disasmPath = std::nullopt;
    return variant;
}

void printProgress(const CompilerOptions& options, size_t index, size_t total,
                   const CompileTask& task, const VariantReport& variant,
                   std::mutex& outputMutex) {
    if (options.progress == "none") {
        return;
    }
    std::lock_guard<std::mutex> lock(outputMutex);
    if (options.progress == "json") {
        Json event = Json::object();
        event["event"] = "variant";
        event["source"] = task.source;
        event["backend"] = task.backend;
        event["stage"] = task.stage;
        event["status"] = variantStatusToString(variant.status);
        event["index"] = index;
        event["total"] = total;
        event["cacheHit"] = variant.cacheHit;
        event["compileMs"] = variant.compileMs.value_or(0);
        std::cout << event.dump() << '\n';
        std::cout.flush();
        return;
    }
    const char* status = variant.status == VariantStatus::Ok
                             ? "ok"
                             : (variant.status == VariantStatus::Skipped ? "skipped"
                                                                         : "FAILED");
    std::cerr << "shader analysis [" << (index + 1) << "/" << total << "] "
              << task.source << " (" << task.backend << ", " << task.stage << ") "
              << status << " " << variant.compileMs.value_or(0) << "ms";
    if (variant.cacheHit) {
        std::cerr << " cache=hit";
    }
    std::cerr << '\n';
}

std::string sourceSha256(const fs::path& path) {
    try {
        return sha256File(path);
    } catch (const std::exception&) {
        return {};
    }
}

} // namespace

ShadercLogs parseShadercLogs(std::string_view stdoutText, std::string_view stderrText,
                             bool succeeded) {
    ShadercLogs logs;
    std::set<std::string> seen;
    forEachLine(stdoutText, [&](std::string_view line) {
        addDiagnostic(logs, line, succeeded, seen);
    });
    forEachLine(stderrText, [&](std::string_view line) {
        addDiagnostic(logs, line, succeeded, seen);
    });
    if (!succeeded && logs.errors.empty()) {
        std::string raw = trimCopy(stdoutText);
        const std::string errorText = trimCopy(stderrText);
        if (!raw.empty() && !errorText.empty()) {
            raw += "\n";
        }
        raw += errorText;
        if (!raw.empty()) {
            logs.errors.push_back({raw, {}});
        }
    }
    return logs;
}

CompileRunResult compileVariants(const VariantManifest& manifest,
                                 const std::vector<CompileTask>& tasks,
                                 const CompilerOptions& options) {
    CompileRunResult run;
    run.shadercVersion = queryShadercVersion(options);
    run.variants.resize(tasks.size());
    if (tasks.empty()) {
        return run;
    }

    std::atomic<size_t> next{0};
    std::atomic<bool> cancel{false};
    std::atomic<bool> anyFailed{false};
    std::mutex outputMutex;
    const size_t workerCount =
        std::min<size_t>(static_cast<size_t>(std::max(1, options.jobs)), tasks.size());
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (size_t worker = 0; worker < workerCount; ++worker) {
        workers.emplace_back([&]() {
            for (;;) {
                const size_t index = next.fetch_add(1);
                if (index >= tasks.size()) {
                    return;
                }
                const CompileTask& task = tasks[index];
                VariantReport report;
                if (cancel.load()) {
                    report = skippedVariant(task);
                } else {
                    try {
                        report = compileOne(manifest, task, options,
                                            run.shadercVersion, cancel);
                    } catch (const std::exception& error) {
                        ShadercLogs logs;
                        logs.errors.push_back({error.what(), {}});
                        report = failedVariant(task, options, logs, 0);
                        report.binaryPath = std::nullopt;
                        report.disasmPath = std::nullopt;
                    }
                }
                if (report.status == VariantStatus::Failed) {
                    anyFailed = true;
                    if (options.failFast) {
                        cancel = true;
                    }
                }
                run.variants[index] = std::move(report);
                printProgress(options, index, tasks.size(), task, run.variants[index],
                              outputMutex);
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    run.cancelled = cancel.load();
    run.anyFailed = anyFailed.load();
    return run;
}

std::size_t writeSummaries(const VariantManifest& manifest,
                           const std::vector<CompileTask>& tasks,
                           const std::vector<VariantReport>& variants,
                           const CompilerOptions& options,
                           std::string_view shadercVersion) {
    if (tasks.size() != variants.size()) {
        throw std::runtime_error("summary task/variant count mismatch");
    }

    struct GroupKey {
        std::string backend;
        std::string source;
        std::string stage;

        bool operator<(const GroupKey& other) const {
            return std::tie(backend, source, stage) <
                   std::tie(other.backend, other.source, other.stage);
        }
    };

    std::map<GroupKey, std::vector<size_t>> groups;
    for (size_t i = 0; i < tasks.size(); ++i) {
        groups[{tasks[i].backend, tasks[i].source, tasks[i].stage}].push_back(i);
    }

    const std::string generatedAt = formatTimestamp();
    std::map<std::string, std::string> sourceHashes;
    size_t written = 0;
    for (const auto& [key, indices] : groups) {
        const auto backend = manifest.backends.find(key.backend);
        if (backend == manifest.backends.end()) {
            throw std::runtime_error("manifest has no backend '" + key.backend + "'");
        }
        ReportSummary summary;
        summary.toolVersion = options.toolVersion;
        summary.generatedAt = generatedAt;
        summary.backend = key.backend;
        summary.profile = backend->second.profile;
        summary.optimization = manifest.optimization;
        summary.debugInfo = options.debugInfo && key.backend == "spirv";
        summary.shaderc.path = relativeToRoot(options.shaderc, options.root);
        summary.shaderc.version =
            shadercVersion.empty() ? "unknown" : std::string(shadercVersion);
        summary.source.path = key.source;
        const auto hash = sourceHashes.find(key.source);
        if (hash == sourceHashes.end()) {
            const std::string value =
                sourceSha256(options.root / key.source);
            sourceHashes.emplace(key.source, value);
            summary.source.sha256 = value;
        } else {
            summary.source.sha256 = hash->second;
        }
        summary.source.language = "bgfx-sc";
        summary.stage = key.stage;
        summary.variants.reserve(indices.size());
        for (const size_t index : indices) {
            summary.variants.push_back(variants[index]);
        }
        std::sort(summary.variants.begin(), summary.variants.end(),
                  [](const VariantReport& left, const VariantReport& right) {
                      return left.variantId < right.variantId;
                  });
        summary.variantCount = static_cast<int>(summary.variants.size());

        const fs::path path =
            options.reportsDir / key.backend / (sourceRelativeStem(tasks[indices[0]]) +
                                                 ".json");
        writeTextFile(path, toJson(summary));
        ++written;
    }
    writeReportIndex(options.reportsDir);
    return written;
}

} // namespace hbg::analysis
