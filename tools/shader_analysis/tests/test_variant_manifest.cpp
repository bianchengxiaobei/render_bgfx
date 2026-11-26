#include "hbg/analysis/compile_command.h"
#include "hbg/analysis/filters.h"
#include "hbg/analysis/variant_manifest.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef HBG_ANALYSIS_TEST_ROOT
#define HBG_ANALYSIS_TEST_ROOT "."
#endif

using namespace hbg::analysis;
namespace fs = std::filesystem;

namespace {

fs::path testRoot() {
    return fs::path(HBG_ANALYSIS_TEST_ROOT);
}

nlohmann::json makeTask(const std::string& source, const std::string& output,
                        const std::string& backend = "dxbc",
                        const std::string& stage = "fragment",
                        const std::vector<std::string>& defines = {},
                        bool d3dSkipOptimization = false, int optimization = 3) {
    nlohmann::json task = nlohmann::json::object();
    task["source"] = source;
    task["output"] = output;
    task["stage"] = stage;
    task["backend"] = backend;
    task["defines"] = defines;
    task["varyingDef"] = nullptr;
    task["d3dSkipOptimization"] = d3dSkipOptimization;
    task["optimization"] = optimization;
    return task;
}

nlohmann::json makeManifest() {
    nlohmann::json manifest = nlohmann::json::object();
    manifest["version"] = 1;
    manifest["generatedBy"] = "test";
    manifest["optimization"] = 3;
    manifest["backends"] = {
        {"dxbc", {{"platform", "windows"}, {"profile", "s_5_0"}}},
        {"spirv", {{"platform", "linux"}, {"profile", "spirv"}}},
    };
    manifest["includeDirs"] = {"shaders", "shaders/include", "{bgfxSrc}/src"};
    manifest["tasks"] = nlohmann::json::array();
    return manifest;
}

const CompileTask& findTask(const VariantManifest& manifest, const std::string& output) {
    const auto it = std::find_if(manifest.tasks.begin(), manifest.tasks.end(),
                                 [&](const CompileTask& task) { return task.output == output; });
    REQUIRE(it != manifest.tasks.end());
    return *it;
}

bool containsArg(const std::vector<std::string>& args, const std::string& value) {
    return std::find(args.begin(), args.end(), value) != args.end();
}

fs::path appendComponents(const fs::path& base, const fs::path& relative) {
    fs::path result = base;
    for (const fs::path& part : relative) {
        result /= part;
    }
    return result;
}

} // namespace

TEST_CASE("real variants manifest loads and validates") {
    const VariantManifest manifest =
        VariantManifest::load(testRoot() / "shaders" / "variants.json");
    CHECK(manifest.version == 1);
    CHECK_FALSE(manifest.tasks.empty());
    const auto dxbc = std::count_if(manifest.tasks.begin(), manifest.tasks.end(),
                                    [](const CompileTask& task) {
                                        return task.backend == "dxbc";
                                    });
    const auto spirv = std::count_if(manifest.tasks.begin(), manifest.tasks.end(),
                                     [](const CompileTask& task) {
                                         return task.backend == "spirv";
                                     });
    CHECK(dxbc == spirv);
    for (const CompileTask& task : manifest.tasks) {
        CHECK(task.variantId().size() == 12);
    }
}

TEST_CASE("manifest rejects duplicate outputs and unsafe paths") {
    nlohmann::json duplicate = makeManifest();
    duplicate["tasks"].push_back(makeTask("shaders/a.fs.sc", "build/dxbc/a.fs.0.bin"));
    duplicate["tasks"].push_back(makeTask("shaders/b.fs.sc", "build/dxbc/a.fs.0.bin"));
    CHECK_THROWS_AS(VariantManifest::parse(duplicate.dump()), std::runtime_error);

    nlohmann::json unsafe = makeManifest();
    unsafe["tasks"].push_back(makeTask("../a.fs.sc", "build/dxbc/a.fs.0.bin"));
    CHECK_THROWS_AS(VariantManifest::parse(unsafe.dump()), std::runtime_error);

    nlohmann::json unsafeInclude = makeManifest();
    unsafeInclude["includeDirs"].push_back("../include");
    CHECK_THROWS_AS(VariantManifest::parse(unsafeInclude.dump()), std::runtime_error);

    nlohmann::json skip = makeManifest();
    skip["tasks"].push_back(
        makeTask("shaders/a.fs.sc", "build/spirv/a.fs.spv", "spirv", "fragment", {}, true, 0));
    CHECK_THROWS_AS(VariantManifest::parse(skip.dump()), std::runtime_error);
}

TEST_CASE("manifest selection applies backend stage pass keyword and define filters") {
    const VariantManifest manifest =
        VariantManifest::load(testRoot() / "shaders" / "variants.json");
    const PassFilter forward = PassFilter::parse("forward");
    const KeywordFilter noKeywords;
    const DefineFilter noDefines;

    const std::vector<CompileTask> selected =
        manifest.selectTasks("dxbc", "fragment", forward, noKeywords, noDefines);
    CHECK(selected.size() == 32);
    for (const CompileTask& task : selected) {
        CHECK(task.backend == "dxbc");
        CHECK(task.stage == "fragment");
        CHECK(task.passName() == "forward");
    }

    const std::vector<CompileTask> keywordSelected =
        manifest.selectTasks("dxbc", "fragment", forward,
                             KeywordFilter::parse("HAS_NORMAL_MAP&HBG_LIGHT_LIST_BUFFER"),
                             noDefines);
    CHECK(keywordSelected.size() == 8);

    const std::vector<CompileTask> defineSelected =
        manifest.selectTasks("dxbc", "fragment", forward, noKeywords,
                             DefineFilter::parse({"HBG_LIGHT_LIST_BUFFER=1"}));
    CHECK(defineSelected.size() == 16);

    const std::vector<CompileTask> spirvSelected =
        manifest.selectTasks("spirv", "vertex", forward, noKeywords, noDefines);
    CHECK(spirvSelected.size() == 1);
}

TEST_CASE("dry-run command matches the Python compile command fields") {
    const VariantManifest manifest =
        VariantManifest::load(testRoot() / "shaders" / "variants.json");
    const CompileTask& task = findTask(manifest, "build/dxbc/forward.fs.0.bin");
    REQUIRE(task.d3dSkipOptimization);

    const fs::path root = "repo";
    const fs::path bgfxSrc = "bgfx";
    const fs::path shaderc = "tools/shaderc.exe";
    const CompileCommand command =
        buildCompileCommand(manifest, task, root, bgfxSrc, shaderc);

    const std::vector<std::string> expected = {
        shaderc.string(),
        "-f",
        appendComponents(root, task.source).string(),
        "-o",
        appendComponents(root / "shaders", task.output).string(),
        "--type",
        "fragment",
        "-i",
        appendComponents(root, "shaders").string(),
        "-i",
        appendComponents(root, "shaders/include").string(),
        "-i",
        appendComponents(bgfxSrc, "src").string(),
        "--platform",
        "windows",
        "-p",
        "s_5_0",
        "--varyingdef",
        appendComponents(root, "shaders/include/varying.def.sc").string(),
        "--define",
        "HBG_SHADOW_POINT_SAMPLER=1;HBG_CONTACT_SHADOW_TEXTURE=1",
    };
    REQUIRE(command.args.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        INFO("argument " << i);
        CHECK(command.args[i] == expected[i]);
    }
}

TEST_CASE("optimized and debug commands add the expected options") {
    const VariantManifest manifest =
        VariantManifest::load(testRoot() / "shaders" / "variants.json");
    const CompileTask& vertex = findTask(manifest, "build/dxbc/forward.vs.bin");
    const CompileCommand vertexCommand = buildCompileCommand(
        manifest, vertex, "repo", "bgfx", "tools/shaderc.exe");
    const auto optimization = std::find(vertexCommand.args.begin(), vertexCommand.args.end(),
                                        "-O");
    REQUIRE(optimization != vertexCommand.args.end());
    REQUIRE(optimization + 1 != vertexCommand.args.end());
    CHECK(*(optimization + 1) == "3");
    CHECK(containsArg(vertexCommand.args, "--Werror"));

    const CompileTask& spirv = findTask(manifest, "build/spirv/forward.fs.0.bin");
    CompileCommandOptions debugOptions;
    debugOptions.debugInfo = true;
    const CompileCommand debugCommand = buildCompileCommand(
        manifest, spirv, "repo", "bgfx", "tools/shaderc.exe", debugOptions);
    CHECK(containsArg(debugCommand.args, "--debug"));
    const CompileCommand releaseCommand = buildCompileCommand(
        manifest, spirv, "repo", "bgfx", "tools/shaderc.exe");
    CHECK_FALSE(containsArg(releaseCommand.args, "--debug"));
}

TEST_CASE("include resolution requires bgfx source for the placeholder") {
    const VariantManifest manifest =
        VariantManifest::load(testRoot() / "shaders" / "variants.json");
    CHECK_THROWS_AS(resolveIncludeDirs(manifest, "repo", {}), std::runtime_error);
    const std::vector<fs::path> resolved =
        resolveIncludeDirs(manifest, "repo", "bgfx");
    REQUIRE(resolved.size() == 3);
    CHECK(resolved[0] == fs::path("repo") / "shaders");
    CHECK(resolved[1] == fs::path("repo") / "shaders" / "include");
    CHECK(resolved[2] == fs::path("bgfx") / "src");
}

TEST_CASE("analysis command options add disasm depends and output override") {
    const VariantManifest manifest =
        VariantManifest::load(testRoot() / "shaders" / "variants.json");
    const CompileTask& task = findTask(manifest, "build/dxbc/forward.vs.bin");
    CompileCommandOptions options;
    options.disasm = true;
    options.depends = true;
    options.outputPath = fs::path("tmp/forward.vs.bin");
    const CompileCommand command =
        buildCompileCommand(manifest, task, "repo", "bgfx", "tools/shaderc.exe", options);
    CHECK(command.output == "tmp/forward.vs.bin");
    CHECK(containsArg(command.args, "--disasm"));
    CHECK(containsArg(command.args, "--depends"));

    const CompileCommand defaultCommand =
        buildCompileCommand(manifest, task, "repo", "bgfx", "tools/shaderc.exe");
    CHECK_FALSE(containsArg(defaultCommand.args, "--disasm"));
    CHECK_FALSE(containsArg(defaultCommand.args, "--depends"));
}

TEST_CASE("command formatting quotes arguments with spaces") {
    CHECK(formatCommandLine({"a b", "c"}) == "\"a b\" c");
    CHECK(formatCommandLine({"a", "\"quoted\""}) == "a \"\\\"quoted\\\"\"");
    CHECK(formatCommandLine({"A=1;B=2"}) == "\"A=1;B=2\"");
}
