#pragma once

#include "hbg/analysis/filters.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hbg::analysis {

struct BackendInfo {
    std::string platform;
    std::string profile;

    bool operator==(const BackendInfo&) const = default;
};

struct CompileTask {
    std::string source;
    std::string output;
    std::string stage;
    std::string backend;
    std::vector<std::string> defines;
    std::optional<std::string> varyingDef;
    bool d3dSkipOptimization = false;
    int optimization = 3;

    std::string fullStem() const;
    std::string passName() const;
    std::string variantId() const;

    bool operator==(const CompileTask&) const = default;
};

struct VariantManifest {
    int version = 1;
    std::string generatedBy;
    int optimization = 3;
    std::map<std::string, BackendInfo> backends;
    std::vector<std::string> includeDirs;
    std::vector<CompileTask> tasks;

    static VariantManifest parse(std::string_view jsonText,
                                 std::string_view origin = "variants.json");
    static VariantManifest load(const std::filesystem::path& path);
    void validate() const;

    std::vector<CompileTask> selectTasks(std::string_view backend, std::string_view stage,
                                         const PassFilter& passFilter,
                                         const KeywordFilter& keywordFilter,
                                         const DefineFilter& defineFilter) const;
};

std::vector<std::filesystem::path> resolveIncludeDirs(
    const VariantManifest& manifest, const std::filesystem::path& root,
    const std::filesystem::path& bgfxSrc);

} // namespace hbg::analysis
