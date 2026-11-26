#include "hbg/analysis/variant_manifest.h"

#include "hbg/analysis/hashing.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace hbg::analysis {
namespace {

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

std::vector<std::string> stringArrayField(const nlohmann::json& object, const char* key,
                                          const std::string& context) {
    const nlohmann::json& value = field(object, key, context);
    if (!value.is_array()) {
        fail(context, std::string("field '") + key + "' must be an array");
    }
    std::vector<std::string> result;
    result.reserve(value.size());
    for (const nlohmann::json& element : value) {
        if (!element.is_string()) {
            fail(context, std::string("field '") + key + "' must contain strings");
        }
        result.push_back(element.get<std::string>());
    }
    return result;
}

bool isSafeRelativePath(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    const std::filesystem::path path(value);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const std::filesystem::path& part : path) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

bool isKnownStage(std::string_view stage) {
    return stage == "vertex" || stage == "fragment" || stage == "compute";
}

std::string stageExtension(std::string_view stage) {
    if (stage == "vertex") {
        return "vs";
    }
    if (stage == "fragment") {
        return "fs";
    }
    if (stage == "compute") {
        return "cs";
    }
    return {};
}

bool endsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::filesystem::path appendComponents(const std::filesystem::path& base,
                                       const std::filesystem::path& relative) {
    std::filesystem::path result = base;
    for (const std::filesystem::path& part : relative) {
        result /= part;
    }
    return result;
}

} // namespace

std::string CompileTask::fullStem() const {
    std::string name = std::filesystem::path(source).filename().string();
    if (endsWith(name, ".sc")) {
        name.resize(name.size() - 3);
    }
    return name;
}

std::string CompileTask::passName() const {
    std::string name = fullStem();
    const std::string extension = stageExtension(stage);
    if (!extension.empty()) {
        const std::string suffix = "." + extension;
        if (endsWith(name, suffix)) {
            name.resize(name.size() - suffix.size());
        }
    }
    return name;
}

std::string CompileTask::variantId() const {
    return makeVariantId(source, stage, backend, defines);
}

VariantManifest VariantManifest::parse(std::string_view jsonText, std::string_view origin) {
    const std::string context(origin.empty() ? "variants.json" : origin);
    nlohmann::json document;
    try {
        document = nlohmann::json::parse(jsonText.begin(), jsonText.end());
    } catch (const nlohmann::json::exception& error) {
        fail(context, std::string("invalid JSON: ") + error.what());
    }
    if (!document.is_object()) {
        fail(context, "expected a JSON object");
    }

    VariantManifest manifest;
    manifest.version = intField(document, "version", context);
    manifest.generatedBy = stringField(document, "generatedBy", context);
    manifest.optimization = intField(document, "optimization", context);

    const nlohmann::json& backends = field(document, "backends", context);
    if (!backends.is_object() || backends.empty()) {
        fail(context, "field 'backends' must be a non-empty object");
    }
    for (auto it = backends.begin(); it != backends.end(); ++it) {
        const std::string backendContext = context + " backends[" + it.key() + "]";
        BackendInfo info;
        info.platform = stringField(it.value(), "platform", backendContext);
        info.profile = stringField(it.value(), "profile", backendContext);
        manifest.backends.emplace(it.key(), std::move(info));
    }

    manifest.includeDirs = stringArrayField(document, "includeDirs", context);

    const nlohmann::json& tasks = field(document, "tasks", context);
    if (!tasks.is_array()) {
        fail(context, "field 'tasks' must be an array");
    }
    manifest.tasks.reserve(tasks.size());
    for (size_t i = 0; i < tasks.size(); ++i) {
        const std::string taskContext =
            context + " tasks[" + std::to_string(i) + "]";
        const nlohmann::json& taskJson = tasks[i];
        CompileTask task;
        task.source = stringField(taskJson, "source", taskContext);
        task.output = stringField(taskJson, "output", taskContext);
        task.stage = stringField(taskJson, "stage", taskContext);
        task.backend = stringField(taskJson, "backend", taskContext);
        task.defines = stringArrayField(taskJson, "defines", taskContext);
        const nlohmann::json& varying = field(taskJson, "varyingDef", taskContext);
        if (!varying.is_null()) {
            if (!varying.is_string()) {
                fail(taskContext, "field 'varyingDef' must be a string or null");
            }
            task.varyingDef = varying.get<std::string>();
        }
        task.d3dSkipOptimization = boolField(taskJson, "d3dSkipOptimization", taskContext);
        task.optimization = intField(taskJson, "optimization", taskContext);
        manifest.tasks.push_back(std::move(task));
    }

    manifest.validate();
    return manifest;
}

VariantManifest VariantManifest::load(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open variants file: " + path.string());
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        throw std::runtime_error("cannot read variants file: " + path.string());
    }
    return parse(contents.str(), path.string());
}

void VariantManifest::validate() const {
    if (version != 1) {
        throw std::runtime_error("variants.json: unsupported version " +
                                 std::to_string(version));
    }
    if (generatedBy.empty()) {
        throw std::runtime_error("variants.json: generatedBy must not be empty");
    }
    if (optimization < 0) {
        throw std::runtime_error("variants.json: optimization must be non-negative");
    }
    if (backends.empty()) {
        throw std::runtime_error("variants.json: backends must not be empty");
    }
    for (const auto& [name, info] : backends) {
        if (name.empty() || info.platform.empty() || info.profile.empty()) {
            throw std::runtime_error("variants.json: backend '" + name +
                                     "' must have platform and profile");
        }
    }
    for (const std::string& includeDir : includeDirs) {
        if (!isSafeRelativePath(includeDir)) {
            throw std::runtime_error(
                "variants.json: includeDirs must contain relative paths");
        }
    }

    std::set<std::string> outputs;
    std::set<std::string> variants;
    for (size_t i = 0; i < tasks.size(); ++i) {
        const CompileTask& task = tasks[i];
        const std::string context = "variants.json tasks[" + std::to_string(i) + "]";
        if (!isSafeRelativePath(task.source)) {
            throw std::runtime_error(context + ": source must be a relative path");
        }
        if (!isSafeRelativePath(task.output)) {
            throw std::runtime_error(context + ": output must be a relative path");
        }
        if (!isKnownStage(task.stage)) {
            throw std::runtime_error(context + ": unknown stage '" + task.stage + "'");
        }
        if (!backends.contains(task.backend)) {
            throw std::runtime_error(context + ": unknown backend '" + task.backend + "'");
        }
        for (const std::string& define : task.defines) {
            if (define.empty()) {
                throw std::runtime_error(context + ": defines must not contain empty entries");
            }
        }
        if (task.varyingDef.has_value() && !isSafeRelativePath(*task.varyingDef)) {
            throw std::runtime_error(context + ": varyingDef must be a relative path");
        }
        if (task.optimization < 0) {
            throw std::runtime_error(context + ": optimization must be non-negative");
        }
        if (task.d3dSkipOptimization) {
            if (task.backend != "dxbc") {
                throw std::runtime_error(context + ": d3dSkipOptimization requires dxbc");
            }
            if (task.optimization != 0) {
                throw std::runtime_error(context + ": d3dSkipOptimization requires optimization 0");
            }
        }
        const std::string outputKey = task.backend + ":" + task.output;
        if (!outputs.insert(outputKey).second) {
            throw std::runtime_error(context + ": duplicate output '" + task.output + "'");
        }
        const std::string variantKey = task.backend + ":" + task.variantId();
        if (!variants.insert(variantKey).second) {
            throw std::runtime_error(context + ": duplicate variant id for source '" +
                                     task.source + "'");
        }
    }
}

std::vector<CompileTask> VariantManifest::selectTasks(
    std::string_view backend, std::string_view stage, const PassFilter& passFilter,
    const KeywordFilter& keywordFilter, const DefineFilter& defineFilter) const {
    if (!backend.empty() && backend != "all" && !backends.contains(std::string(backend))) {
        throw std::runtime_error("unknown backend '" + std::string(backend) + "'");
    }
    if (!stage.empty() && !isKnownStage(stage)) {
        throw std::runtime_error("unknown stage '" + std::string(stage) + "'");
    }

    std::vector<CompileTask> selected;
    for (const CompileTask& task : tasks) {
        if (!backend.empty() && backend != "all" && task.backend != backend) {
            continue;
        }
        if (!stage.empty() && task.stage != stage) {
            continue;
        }
        if (!passFilter.matches(task.passName(), task.fullStem(), task.stage)) {
            continue;
        }
        if (!keywordFilter.matches(task.defines)) {
            continue;
        }
        if (!defineFilter.matches(task.defines)) {
            continue;
        }
        selected.push_back(task);
    }
    return selected;
}

std::vector<std::filesystem::path> resolveIncludeDirs(
    const VariantManifest& manifest, const std::filesystem::path& root,
    const std::filesystem::path& bgfxSrc) {
    std::vector<std::filesystem::path> result;
    result.reserve(manifest.includeDirs.size());
    for (const std::string& includeDir : manifest.includeDirs) {
        constexpr std::string_view placeholder = "{bgfxSrc}";
        if (includeDir == placeholder) {
            if (bgfxSrc.empty()) {
                throw std::runtime_error("--bgfx-src is required by include directory " +
                                         includeDir);
            }
            result.push_back(bgfxSrc);
        } else if (includeDir.rfind(std::string(placeholder) + "/", 0) == 0 ||
                   includeDir.rfind(std::string(placeholder) + "\\", 0) == 0) {
            if (bgfxSrc.empty()) {
                throw std::runtime_error("--bgfx-src is required by include directory " +
                                         includeDir);
            }
            result.push_back(
                appendComponents(bgfxSrc, includeDir.substr(placeholder.size() + 1)));
        } else {
            result.push_back(appendComponents(root, includeDir));
        }
    }
    return result;
}

} // namespace hbg::analysis
