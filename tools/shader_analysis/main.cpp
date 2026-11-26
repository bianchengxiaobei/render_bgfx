#include "hbg/analysis/compile_command.h"
#include "hbg/analysis/compiler.h"
#include "hbg/analysis/diff.h"
#include "hbg/analysis/export_csv.h"
#include "hbg/analysis/filters.h"
#include "hbg/analysis/hashing.h"
#include "hbg/analysis/reference.h"
#include "hbg/analysis/report.h"
#include "hbg/analysis/variant_manifest.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifndef HBG_ANALYSIS_PROJECT_ROOT
#define HBG_ANALYSIS_PROJECT_ROOT ""
#endif

#ifndef HBG_ANALYSIS_SHADERC_DEFAULT
#define HBG_ANALYSIS_SHADERC_DEFAULT ""
#endif

#ifndef HBG_ANALYSIS_BGFX_SRC_DEFAULT
#define HBG_ANALYSIS_BGFX_SRC_DEFAULT ""
#endif

#ifndef HBG_ANALYSIS_VERSION
#define HBG_ANALYSIS_VERSION "0.0.0"
#endif

namespace {

namespace fs = std::filesystem;
using namespace hbg::analysis;

constexpr int kExitSuccess = 0;
constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;

class CliError : public std::runtime_error {
public:
    CliError(int code, const std::string& message) : std::runtime_error(message), m_code(code) {}
    int code() const { return m_code; }

private:
    int m_code;
};

void printUsage() {
    std::cout
        << "hbg_shader_analysis - shader build and analysis tool\n\n"
        << "Usage:\n"
        << "  hbg_shader_analysis build [--dry-run] [options]\n"
        << "  hbg_shader_analysis list --report <dir> [--backend <name>] [--json]\n"
        << "  hbg_shader_analysis validate --report <dir|file>\n"
        << "  hbg_shader_analysis diff <current> <reference> --out <file>\n"
        << "      [--fail-on-regression] [--ignore-timestamps]\n"
        << "  hbg_shader_analysis export --report <dir|file> --format csv --out <file>\n"
        << "  hbg_shader_analysis set-reference --from <dir> [--to <dir>]\n"
        << "  hbg_shader_analysis --self-test\n\n"
        << "Build options:\n"
        << "  --root <repo> --variants <file> --shaderc <path> --bgfx-src <path>\n"
        << "  --spirv-dis <path> --spirv-cross <path>\n"
        << "  --backend dxbc|spirv|all --stage vertex|fragment|compute\n"
        << "  --pass <filter> --variant-filter <expr> --define <NAME[=VALUE]>\n"
        << "  --jobs <n> --timeout <seconds> --retries <n> --fail-fast\n"
        << "  --force --no-cache --keep-temp --debug-info --out <dir>\n"
        << "  --progress plain|json|none\n";
}

fs::path defaultRoot() {
    const std::string value = HBG_ANALYSIS_PROJECT_ROOT;
    if (!value.empty()) {
        return fs::path(value);
    }
    return fs::current_path();
}

fs::path defaultShaderc() {
    return fs::path(HBG_ANALYSIS_SHADERC_DEFAULT);
}

fs::path defaultBgfxSrc() {
    return fs::path(HBG_ANALYSIS_BGFX_SRC_DEFAULT);
}

fs::path absolutePath(const fs::path& path) {
    fs::path result = path;
    if (result.is_relative()) {
        result = fs::absolute(result);
    }
    result = result.lexically_normal();
    result.make_preferred();
    return result;
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
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path(), error);
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

int parseInt(std::string_view value, const char* option) {
    int result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        throw CliError(kExitUsage, std::string("invalid integer for ") + option + ": " +
                                       std::string(value));
    }
    return result;
}

bool isValueOption(std::string_view key) {
    return key == "--root" || key == "--variants" || key == "--shaderc" ||
           key == "--bgfx-src" || key == "--backend" || key == "--pass" ||
           key == "--stage" || key == "--variant-filter" || key == "--define" ||
           key == "--jobs" || key == "--timeout" || key == "--retries" ||
           key == "--progress" || key == "--out" || key == "--spirv-dis" ||
           key == "--spirv-cross" || key == "--report";
}

struct BuildOptions {
    bool dryRun = false;
    fs::path root = defaultRoot();
    fs::path variants;
    fs::path shaderc = defaultShaderc();
    fs::path bgfxSrc = defaultBgfxSrc();
    std::string backend = "all";
    std::string stage;
    std::string passFilter;
    std::string variantFilter;
    std::vector<std::string> defines;
    std::string progress = "plain";
    int jobs = 8;
    int timeout = 300;
    int retries = 0;
    bool failFast = false;
    bool force = false;
    bool debugInfo = false;
    bool keepTemp = false;
    bool noCache = false;
    fs::path out;
    fs::path spirvDis;
    fs::path spirvCross;
};

BuildOptions parseBuildOptions(const std::vector<std::string>& args) {
    BuildOptions options;
    bool variantsSet = false;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& argument = args[i];
        if (argument == "--help" || argument == "-h") {
            printUsage();
            throw CliError(kExitSuccess, "");
        }
        if (argument.rfind("--", 0) != 0) {
            throw CliError(kExitUsage, "unexpected argument: " + argument);
        }
        std::string key = argument;
        std::optional<std::string> value;
        const size_t equals = argument.find('=');
        if (equals != std::string::npos) {
            key = argument.substr(0, equals);
            value = argument.substr(equals + 1);
        } else if (isValueOption(key)) {
            if (i + 1 >= args.size()) {
                throw CliError(kExitUsage, "missing value for " + key);
            }
            value = args[++i];
        }

        if (key == "--dry-run") {
            if (value.has_value()) {
                throw CliError(kExitUsage, "--dry-run does not take a value");
            }
            options.dryRun = true;
        } else if (key == "--fail-fast") {
            options.failFast = true;
        } else if (key == "--force") {
            options.force = true;
        } else if (key == "--debug-info") {
            options.debugInfo = true;
        } else if (key == "--keep-temp") {
            options.keepTemp = true;
        } else if (key == "--no-cache") {
            options.noCache = true;
        } else if (key == "--root") {
            options.root = *value;
        } else if (key == "--variants") {
            options.variants = *value;
            variantsSet = true;
        } else if (key == "--shaderc") {
            options.shaderc = *value;
        } else if (key == "--bgfx-src") {
            options.bgfxSrc = *value;
        } else if (key == "--backend") {
            options.backend = *value;
        } else if (key == "--stage") {
            options.stage = *value;
        } else if (key == "--pass") {
            options.passFilter = *value;
        } else if (key == "--variant-filter") {
            options.variantFilter = *value;
        } else if (key == "--define") {
            options.defines.push_back(*value);
        } else if (key == "--progress") {
            options.progress = *value;
        } else if (key == "--jobs") {
            options.jobs = parseInt(*value, "--jobs");
        } else if (key == "--timeout") {
            options.timeout = parseInt(*value, "--timeout");
        } else if (key == "--retries") {
            options.retries = parseInt(*value, "--retries");
        } else if (key == "--out") {
            options.out = *value;
        } else if (key == "--spirv-dis") {
            options.spirvDis = *value;
        } else if (key == "--spirv-cross") {
            options.spirvCross = *value;
        } else {
            throw CliError(kExitUsage, "unknown option: " + key);
        }
    }

    if (options.root.empty()) {
        throw CliError(kExitUsage, "--root must not be empty");
    }
    if (variantsSet && options.variants.empty()) {
        throw CliError(kExitUsage, "--variants must not be empty");
    }
    options.root = absolutePath(options.root);
    options.variants = variantsSet ? absolutePath(options.variants)
                                   : options.root / "shaders" / "variants.json";
    if (!options.shaderc.empty()) {
        options.shaderc = absolutePath(options.shaderc);
    }
    if (!options.bgfxSrc.empty()) {
        options.bgfxSrc = absolutePath(options.bgfxSrc);
    }
    if (!options.out.empty()) {
        options.out = absolutePath(options.out);
    }
    if (!options.spirvDis.empty()) {
        options.spirvDis = absolutePath(options.spirvDis);
    }
    if (!options.spirvCross.empty()) {
        options.spirvCross = absolutePath(options.spirvCross);
    }

    if (options.backend != "all" && options.backend != "dxbc" &&
        options.backend != "spirv") {
        throw CliError(kExitUsage, "invalid --backend: " + options.backend);
    }
    if (!options.stage.empty() && options.stage != "vertex" &&
        options.stage != "fragment" && options.stage != "compute") {
        throw CliError(kExitUsage, "invalid --stage: " + options.stage);
    }
    if (options.progress != "plain" && options.progress != "json" &&
        options.progress != "none") {
        throw CliError(kExitUsage, "invalid --progress: " + options.progress);
    }
    if (options.jobs <= 0) {
        throw CliError(kExitUsage, "--jobs must be positive");
    }
    if (options.timeout < 0) {
        throw CliError(kExitUsage, "--timeout must be non-negative");
    }
    if (options.retries < 0) {
        throw CliError(kExitUsage, "--retries must be non-negative");
    }
    return options;
}

int runBuild(const std::vector<std::string>& args) {
    const BuildOptions options = parseBuildOptions(args);
    if (!fs::is_directory(options.root)) {
        throw std::runtime_error("root directory not found: " + options.root.string());
    }
    if (options.shaderc.empty()) {
        throw std::runtime_error("shaderc path is required");
    }
    if (!fs::is_regular_file(options.shaderc)) {
        throw std::runtime_error("shaderc executable not found: " +
                                 options.shaderc.string());
    }
    if (options.bgfxSrc.empty()) {
        throw std::runtime_error("bgfx source path is required");
    }
    if (!fs::is_directory(options.bgfxSrc)) {
        throw std::runtime_error("bgfx source directory not found: " +
                                 options.bgfxSrc.string());
    }

    const VariantManifest manifest = VariantManifest::load(options.variants);
    const PassFilter passFilter = PassFilter::parse(options.passFilter);
    const KeywordFilter keywordFilter = KeywordFilter::parse(options.variantFilter);
    const DefineFilter defineFilter = DefineFilter::parse(options.defines);
    const std::vector<CompileTask> tasks = manifest.selectTasks(
        options.backend, options.stage, passFilter, keywordFilter, defineFilter);

    if (tasks.empty()) {
        if (options.progress != "none") {
            std::cerr << "build: no tasks matched\n";
        }
        return kExitSuccess;
    }

    if (options.dryRun) {
        CompileCommandOptions commandOptions;
        commandOptions.debugInfo = options.debugInfo;
        for (const CompileTask& task : tasks) {
            const CompileCommand command = buildCompileCommand(
                manifest, task, options.root, options.bgfxSrc, options.shaderc,
                commandOptions);
            if (options.progress == "json") {
                nlohmann::json event = nlohmann::json::object();
                event["event"] = "command";
                event["source"] = task.source;
                event["output"] = task.output;
                event["backend"] = task.backend;
                event["stage"] = task.stage;
                event["variantId"] = task.variantId();
                event["command"] = command.args;
                std::cout << event.dump() << '\n';
            } else {
                std::cout << formatCommandLine(command.args) << '\n';
            }
        }
        if (options.progress != "none") {
            std::cout.flush();
            std::cerr << "dry-run: " << tasks.size() << " task(s)\n";
        }
        return kExitSuccess;
    }

    if (options.debugInfo && options.backend != "spirv") {
        std::cerr << "warning: --debug-info is ignored for the dxbc backend\n";
    }

    CompilerOptions compilerOptions;
    compilerOptions.root = options.root;
    compilerOptions.reportsDir =
        options.out.empty() ? options.root / "shader_analysis" / "reports"
                            : options.out;
    compilerOptions.shaderc = options.shaderc;
    compilerOptions.bgfxSrc = options.bgfxSrc;
    compilerOptions.spirvDis = options.spirvDis;
    compilerOptions.spirvCross = options.spirvCross;
    compilerOptions.toolVersion = HBG_ANALYSIS_VERSION;
    compilerOptions.progress = options.progress;
    compilerOptions.jobs = options.jobs;
    compilerOptions.timeoutSeconds = options.timeout;
    compilerOptions.retries = options.retries;
    compilerOptions.force = options.force;
    compilerOptions.debugInfo = options.debugInfo;
    compilerOptions.keepTemp = options.keepTemp;
    compilerOptions.noCache = options.noCache;
    compilerOptions.failFast = options.failFast;

    const CompileRunResult run = compileVariants(manifest, tasks, compilerOptions);
    const size_t summaryCount =
        writeSummaries(manifest, tasks, run.variants, compilerOptions,
                       run.shadercVersion);
    size_t okCount = 0;
    size_t failedCount = 0;
    size_t skippedCount = 0;
    size_t cacheHits = 0;
    for (const VariantReport& variant : run.variants) {
        if (variant.status == VariantStatus::Ok) {
            ++okCount;
        } else if (variant.status == VariantStatus::Failed) {
            ++failedCount;
        } else {
            ++skippedCount;
        }
        if (variant.cacheHit) {
            ++cacheHits;
        }
    }
    if (options.progress != "none") {
        std::cerr << "build: " << tasks.size() << " variant(s), " << okCount
                  << " ok, " << failedCount << " failed, " << skippedCount
                  << " skipped, " << cacheHits << " cache hit(s), " << summaryCount
                  << " summary file(s)\n";
    }
    if (options.failFast && run.anyFailed) {
        return kExitFailure;
    }
    return kExitSuccess;
}
struct ReportOptions {
    fs::path report;
    std::string backend;
    bool json = false;
};

ReportOptions parseReportOptions(const std::vector<std::string>& args, bool allowJson) {
    ReportOptions options;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& argument = args[i];
        if (argument == "--help" || argument == "-h") {
            printUsage();
            throw CliError(kExitSuccess, "");
        }
        if (argument == "--json") {
            if (!allowJson) {
                throw CliError(kExitUsage, "unknown option: --json");
            }
            options.json = true;
            continue;
        }
        std::string key = argument;
        std::optional<std::string> value;
        const size_t equals = argument.find('=');
        if (equals != std::string::npos) {
            key = argument.substr(0, equals);
            value = argument.substr(equals + 1);
        } else if (key == "--report" || key == "--backend") {
            if (i + 1 >= args.size()) {
                throw CliError(kExitUsage, "missing value for " + key);
            }
            value = args[++i];
        }
        if (key == "--report") {
            options.report = *value;
        } else if (key == "--backend") {
            options.backend = *value;
        } else {
            throw CliError(kExitUsage, "unknown option: " + key);
        }
    }
    if (options.report.empty()) {
        throw CliError(kExitUsage, "--report is required");
    }
    options.report = absolutePath(options.report);
    return options;
}

fs::path indexPathFor(const fs::path& report) {
    if (fs::is_directory(report)) {
        return report / "index.json";
    }
    return report;
}

int runList(const std::vector<std::string>& args) {
    const ReportOptions options = parseReportOptions(args, true);
    const ReportIndex index = parseIndex(readTextFile(indexPathFor(options.report)));
    ReportIndex filtered;
    filtered.schemaVersion = index.schemaVersion;
    filtered.generatedAt = index.generatedAt;
    for (const ReportIndexEntry& entry : index.entries) {
        if (!options.backend.empty() && options.backend != "all" &&
            entry.backend != options.backend) {
            continue;
        }
        filtered.entries.push_back(entry);
    }
    if (options.json) {
        std::cout << toJson(filtered);
        return kExitSuccess;
    }
    for (const ReportIndexEntry& entry : filtered.entries) {
        std::cout << entry.backend << '\t' << entry.stage << '\t' << entry.variantCount
                  << '\t' << entry.okCount << '\t' << entry.failedCount << '\t'
                  << entry.source << '\n';
    }
    return kExitSuccess;
}

void validateSummaryAgainstIndex(const ReportIndexEntry& entry, const ReportSummary& summary) {
    if (summary.backend != entry.backend || summary.source.path != entry.source ||
        summary.stage != entry.stage) {
        throw std::runtime_error("summary identity mismatch for " + entry.summaryPath);
    }
    if (summary.variantCount != entry.variantCount) {
        throw std::runtime_error("variant count mismatch for " + entry.summaryPath);
    }
    if (!entry.sourceSha256.empty() && summary.source.sha256 != entry.sourceSha256) {
        throw std::runtime_error("source hash mismatch for " + entry.summaryPath);
    }
    int okCount = 0;
    int failedCount = 0;
    for (const VariantReport& variant : summary.variants) {
        if (variant.status == VariantStatus::Ok) {
            ++okCount;
        } else if (variant.status == VariantStatus::Failed) {
            ++failedCount;
        }
    }
    if (okCount != entry.okCount || failedCount != entry.failedCount) {
        throw std::runtime_error("variant status counts mismatch for " + entry.summaryPath);
    }
}

int runValidate(const std::vector<std::string>& args) {
    const ReportOptions options = parseReportOptions(args, false);
    if (!fs::exists(options.report)) {
        throw std::runtime_error("report path not found: " + options.report.string());
    }
    if (fs::is_directory(options.report)) {
        const ReportIndex index =
            parseIndex(readTextFile(options.report / "index.json"));
        for (const ReportIndexEntry& entry : index.entries) {
            if (!isSafeRelativePath(entry.summaryPath)) {
                throw std::runtime_error("unsafe summaryPath: " + entry.summaryPath);
            }
            const fs::path summaryPath = options.report / entry.summaryPath;
            const ReportSummary summary = parseSummary(readTextFile(summaryPath));
            validateSummaryAgainstIndex(entry, summary);
        }
        std::cout << "valid: " << index.entries.size() << " summary file(s)\n";
        return kExitSuccess;
    }

    const std::string text = readTextFile(options.report);
    const nlohmann::json document = nlohmann::json::parse(text);
    if (document.contains("entries")) {
        const ReportIndex index = parseIndex(text);
        std::cout << "valid: index with " << index.entries.size() << " entr(y/ies)\n";
    } else if (document.contains("variants")) {
        const ReportSummary summary = parseSummary(text);
        std::cout << "valid: summary with " << summary.variants.size() << " variant(s)\n";
    } else if (document.contains("instructions")) {
        const DetailReport detail = parseDetail(text);
        std::cout << "valid: detail with " << detail.instructions.size()
                  << " instruction(s)\n";
    } else {
        throw std::runtime_error("unrecognized report JSON");
    }
    return kExitSuccess;
}

int runDiff(const std::vector<std::string>& args) {
    std::vector<std::string> positional;
    fs::path out;
    bool failOnRegression = false;
    bool ignoreTimestamps = false;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& argument = args[i];
        if (argument == "--help" || argument == "-h") {
            printUsage();
            throw CliError(kExitSuccess, "");
        }
        if (argument.rfind("--", 0) != 0) {
            positional.push_back(argument);
            continue;
        }
        std::string key = argument;
        std::optional<std::string> value;
        const size_t equals = argument.find('=');
        if (equals != std::string::npos) {
            key = argument.substr(0, equals);
            value = argument.substr(equals + 1);
        } else if (key == "--out") {
            if (i + 1 >= args.size()) {
                throw CliError(kExitUsage, "missing value for --out");
            }
            value = args[++i];
        }
        if (key == "--out") {
            if (!value.has_value() || value->empty()) {
                throw CliError(kExitUsage, "--out must not be empty");
            }
            out = *value;
        } else if (key == "--fail-on-regression") {
            if (value.has_value()) {
                throw CliError(kExitUsage, "--fail-on-regression does not take a value");
            }
            failOnRegression = true;
        } else if (key == "--ignore-timestamps") {
            if (value.has_value()) {
                throw CliError(kExitUsage, "--ignore-timestamps does not take a value");
            }
            ignoreTimestamps = true;
        } else {
            throw CliError(kExitUsage, "unknown option: " + key);
        }
    }
    if (positional.size() != 2) {
        throw CliError(kExitUsage,
                       "diff requires <currentReportsDir> <referenceReportsDir>");
    }
    if (out.empty()) {
        throw CliError(kExitUsage, "--out is required");
    }

    const fs::path current = absolutePath(positional[0]);
    const fs::path reference = absolutePath(positional[1]);
    const ReportDiff diff = diffReports(current, reference, ignoreTimestamps);
    const fs::path outPath = absolutePath(out);
    writeTextFile(outPath, toJson(diff));

    size_t same = 0;
    size_t changed = 0;
    size_t incomparable = 0;
    for (const DiffEntry& entry : diff.entries) {
        if (entry.status == DiffStatus::Same) {
            ++same;
        } else if (entry.status == DiffStatus::Changed) {
            ++changed;
        } else {
            ++incomparable;
        }
    }
    const bool regression = hasRegression(diff);
    std::cout << "diff: " << diff.entries.size() << " matched, " << same << " same, "
              << changed << " changed, " << incomparable << " incomparable, "
              << diff.addedVariants.size() << " added, "
              << diff.removedVariants.size() << " removed, regression="
              << (regression ? "true" : "false") << ", wrote " << outPath.string()
              << '\n';
    if (failOnRegression && regression) {
        std::cerr << "diff: regression detected\n";
        return kExitFailure;
    }
    return kExitSuccess;
}

int runExport(const std::vector<std::string>& args) {
    fs::path report;
    fs::path out;
    std::string format = "csv";
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& argument = args[i];
        if (argument == "--help" || argument == "-h") {
            printUsage();
            throw CliError(kExitSuccess, "");
        }
        if (argument.rfind("--", 0) != 0) {
            throw CliError(kExitUsage, "unexpected argument: " + argument);
        }
        std::string key = argument;
        std::optional<std::string> value;
        const size_t equals = argument.find('=');
        if (equals != std::string::npos) {
            key = argument.substr(0, equals);
            value = argument.substr(equals + 1);
        } else if (key == "--report" || key == "--format" || key == "--out") {
            if (i + 1 >= args.size()) {
                throw CliError(kExitUsage, "missing value for " + key);
            }
            value = args[++i];
        }
        if (key == "--report") {
            report = *value;
        } else if (key == "--format") {
            format = *value;
        } else if (key == "--out") {
            out = *value;
        } else {
            throw CliError(kExitUsage, "unknown option: " + key);
        }
    }
    if (report.empty()) {
        throw CliError(kExitUsage, "--report is required");
    }
    if (out.empty()) {
        throw CliError(kExitUsage, "--out is required");
    }
    if (format != "csv") {
        throw CliError(kExitUsage, "unsupported export format: " + format);
    }
    const size_t rows = exportCsv(absolutePath(report), absolutePath(out));
    std::cout << "csv: " << rows << " row(s) -> " << absolutePath(out).string() << '\n';
    return kExitSuccess;
}

int runSetReference(const std::vector<std::string>& args) {
    fs::path from;
    fs::path to;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& argument = args[i];
        if (argument == "--help" || argument == "-h") {
            printUsage();
            throw CliError(kExitSuccess, "");
        }
        if (argument.rfind("--", 0) != 0) {
            throw CliError(kExitUsage, "unexpected argument: " + argument);
        }
        std::string key = argument;
        std::optional<std::string> value;
        const size_t equals = argument.find('=');
        if (equals != std::string::npos) {
            key = argument.substr(0, equals);
            value = argument.substr(equals + 1);
        } else if (key == "--from" || key == "--to") {
            if (i + 1 >= args.size()) {
                throw CliError(kExitUsage, "missing value for " + key);
            }
            value = args[++i];
        }
        if (key == "--from") {
            from = *value;
        } else if (key == "--to") {
            to = *value;
        } else {
            throw CliError(kExitUsage, "unknown option: " + key);
        }
    }
    if (from.empty()) {
        throw CliError(kExitUsage, "--from is required");
    }
    if (to.empty()) {
        to = defaultRoot() / "shader_analysis" / "reference";
    }
    const ReferenceResult result =
        setReference(absolutePath(from), absolutePath(to));
    std::cout << "reference: " << result.summaryCount << " summary file(s), "
              << result.indexEntryCount << " index entr(y/ies) -> "
              << absolutePath(to).string() << '\n';
    return kExitSuccess;
}

int runSelfTest() {
    const std::string emptyHash =
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    const std::string abcHash =
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    if (sha256Hex("") != emptyHash || sha256Hex("abc") != abcHash) {
        std::cerr << "self-test: SHA-256 mismatch\n";
        return kExitFailure;
    }
    const std::vector<std::string> canonical = canonicalDefines({"B=2", "A=1", "A=1"});
    if (canonical != std::vector<std::string>{"A=1", "B=2"}) {
        std::cerr << "self-test: canonical defines mismatch\n";
        return kExitFailure;
    }
    if (makeVariantId("shaders/passes/forward.fs.sc", "fragment", "dxbc",
                      {"B=2", "A=1"}) != "27a361396d8d") {
        std::cerr << "self-test: variant id mismatch\n";
        return kExitFailure;
    }
    if (!globMatch("deferred*", "deferredLighting") ||
        !PassFilter::parse("forward").matches("forward", "forward.fs", "fragment")) {
        std::cerr << "self-test: filter mismatch\n";
        return kExitFailure;
    }
    if (KeywordFilter::parse("A&B|C").matches({"A=1", "B=2"}) == false ||
        KeywordFilter::parse("A&B|C").matches({"C=1"}) == false ||
        KeywordFilter::parse("A&B|C").matches({"A=1"}) == true) {
        std::cerr << "self-test: keyword filter mismatch\n";
        return kExitFailure;
    }
    const fs::path root = defaultRoot();
    const fs::path variants = root / "shaders" / "variants.json";
    if (fs::exists(variants)) {
        const VariantManifest manifest = VariantManifest::load(variants);
        if (manifest.tasks.empty()) {
            std::cerr << "self-test: manifest has no tasks\n";
            return kExitFailure;
        }
        std::cout << "self-test: OK (" << manifest.tasks.size() << " task(s))\n";
        return kExitSuccess;
    }
    std::cout << "self-test: OK\n";
    return kExitSuccess;
}

int run(const std::vector<std::string>& args) {
    if (args.empty()) {
        printUsage();
        return kExitUsage;
    }
    const std::string& command = args.front();
    if (command == "--help" || command == "-h" || command == "help") {
        printUsage();
        return kExitSuccess;
    }
    if (command == "--self-test") {
        return runSelfTest();
    }
    const std::vector<std::string> commandArgs(args.begin() + 1, args.end());
    if (command == "build") {
        return runBuild(commandArgs);
    }
    if (command == "list") {
        return runList(commandArgs);
    }
    if (command == "validate") {
        return runValidate(commandArgs);
    }
    if (command == "diff") {
        return runDiff(commandArgs);
    }
    if (command == "export") {
        return runExport(commandArgs);
    }
    if (command == "set-reference") {
        return runSetReference(commandArgs);
    }
    throw CliError(kExitUsage, "unknown command: " + command);
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::vector<std::string> args;
        args.reserve(static_cast<size_t>(argc > 0 ? argc - 1 : 0));
        for (int i = 1; i < argc; ++i) {
            args.emplace_back(argv[i]);
        }
        return run(args);
    } catch (const CliError& error) {
        if (error.code() != kExitSuccess) {
            std::cerr << error.what() << '\n';
        }
        return error.code();
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return kExitFailure;
    }
}
