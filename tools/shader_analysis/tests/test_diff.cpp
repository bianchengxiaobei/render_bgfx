#include "hbg/analysis/diff.h"
#include "hbg/analysis/report_files.h"

#include "test_report_fixtures.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

using namespace hbg::analysis;
using namespace hbg_test;

TEST_CASE("diff matches variants by canonical defines and ignores volatile fields") {
    TempDirectory temp;
    const fs::path currentDir = temp.subdirectory("current");
    const fs::path referenceDir = temp.subdirectory("reference");

    ReportSummary current =
        makeSummary("dxbc", "shaders/passes/forward.fs.sc", "fragment",
                    {makeVariant("aaaaaaaaaaaa", {"B=2", "A=1"}, 10, 500)});
    ReportSummary reference = current;
    reference.generatedAt = "2026-11-22T12:00:00Z";
    reference.variants[0].cacheHit = true;
    reference.variants[0].compileMs = 999;
    reference.variants[0].defines = {"A=1", "B=2"};
    reference.variants[0].canonicalDefines = {"A=1", "B=2"};
    current.variants[0].defines = {"B=2", "A=1"};
    current.variants[0].canonicalDefines = {"A=1", "B=2"};
    current.variants[0].compileMs = 10;

    writeSummaryTree(currentDir, "dxbc/forward.fs.json", current);
    writeSummaryTree(referenceDir, "dxbc/forward.fs.json", reference);

    const ReportDiff diff = diffReports(currentDir, referenceDir);
    REQUIRE(diff.entries.size() == 1);
    CHECK(diff.entries[0].status == DiffStatus::Same);
    CHECK(diff.entries[0].variantId == "aaaaaaaaaaaa");
    CHECK(diff.entries[0].metricDeltas.empty());
    CHECK_FALSE(diff.sourceHashChanged);
    CHECK_FALSE(diff.toolchainChanged);
    CHECK(diff.addedVariants.empty());
    CHECK(diff.removedVariants.empty());
    CHECK(diff.current.generatedAt.has_value());
    CHECK(diff.reference.generatedAt.has_value());
    CHECK_FALSE(hasRegression(diff));

    const ReportDiff untimed = diffReports(currentDir, referenceDir, true);
    CHECK_FALSE(untimed.current.generatedAt.has_value());
    CHECK_FALSE(untimed.reference.generatedAt.has_value());
    CHECK(untimed.entries.size() == 1);
    CHECK(untimed.entries[0].status == DiffStatus::Same);
}

TEST_CASE("diff reports numeric metric deltas and regression status") {
    TempDirectory temp;
    const fs::path currentDir = temp.subdirectory("current");
    const fs::path referenceDir = temp.subdirectory("reference");

    ReportSummary current =
        makeSummary("dxbc", "shaders/passes/forward.fs.sc", "fragment",
                    {makeVariant("aaaaaaaaaaaa", {"A=1"}, 22, 872, 6)});
    ReportSummary reference =
        makeSummary("dxbc", "shaders/passes/forward.fs.sc", "fragment",
                    {makeVariant("aaaaaaaaaaaa", {"A=1"}, 10, 1000, 4)});
    current.variants[0].metrics->microCodeSize = 500;
    reference.variants[0].metrics->microCodeSize = 500;

    writeSummaryTree(currentDir, "dxbc/forward.fs.json", current);
    writeSummaryTree(referenceDir, "dxbc/forward.fs.json", reference);

    const ReportDiff diff = diffReports(currentDir, referenceDir);
    REQUIRE(diff.entries.size() == 1);
    CHECK(diff.entries[0].status == DiffStatus::Changed);
    CHECK(diff.entries[0].metricDeltas.at("instructionCount") == 12);
    CHECK(diff.entries[0].metricDeltas.at("tempRegisterPeak") == 2);
    CHECK(diff.entries[0].metricDeltas.at("binaryBytes") == -128);
    CHECK(hasRegression(diff));

    const std::string json = toJson(diff);
    const nlohmann::json document = nlohmann::json::parse(json);
    CHECK(document["schemaVersion"] == 1);
    CHECK(document["entries"][0]["status"] == "changed");
    CHECK(document["entries"][0]["metricDeltas"]["instructionCount"] == 12);
    CHECK(document["sourceHashChanged"] == false);
    CHECK(document["toolchainChanged"] == false);
}

TEST_CASE("diff reports added and removed variants") {
    TempDirectory temp;
    const fs::path currentDir = temp.subdirectory("current");
    const fs::path referenceDir = temp.subdirectory("reference");

    const ReportSummary current = makeSummary(
        "dxbc", "shaders/passes/forward.fs.sc", "fragment",
        {makeVariant("aaaaaaaaaaaa", {"A=1"}, 10, 500),
         makeVariant("bbbbbbbbbbbb", {"B=1"}, 20, 700)});
    const ReportSummary reference = makeSummary(
        "dxbc", "shaders/passes/forward.fs.sc", "fragment",
        {makeVariant("aaaaaaaaaaaa", {"A=1"}, 10, 500),
         makeVariant("cccccccccccc", {"C=1"}, 30, 900)});

    writeSummaryTree(currentDir, "dxbc/forward.fs.json", current);
    writeSummaryTree(referenceDir, "dxbc/forward.fs.json", reference);

    const ReportDiff diff = diffReports(currentDir, referenceDir);
    REQUIRE(diff.entries.size() == 1);
    CHECK(diff.entries[0].status == DiffStatus::Same);
    REQUIRE(diff.addedVariants.size() == 1);
    REQUIRE(diff.removedVariants.size() == 1);
    CHECK(diff.addedVariants[0].variantId == "bbbbbbbbbbbb");
    CHECK(diff.addedVariants[0].status == DiffStatus::Added);
    CHECK(diff.removedVariants[0].variantId == "cccccccccccc");
    CHECK(diff.removedVariants[0].status == DiffStatus::Removed);
}

TEST_CASE("diff marks source and toolchain changes incomparable") {
    TempDirectory temp;
    const fs::path currentDir = temp.subdirectory("current");
    const fs::path referenceDir = temp.subdirectory("reference");

    ReportSummary current =
        makeSummary("spirv", "shaders/passes/forward.fs.sc", "fragment",
                    {makeVariant("aaaaaaaaaaaa", {"A=1"}, 10, 500)}, "new-source",
                    "shaderc-2");
    ReportSummary reference =
        makeSummary("spirv", "shaders/passes/forward.fs.sc", "fragment",
                    {makeVariant("aaaaaaaaaaaa", {"A=1"}, 10, 500)}, "old-source",
                    "shaderc-1");

    writeSummaryTree(currentDir, "spirv/forward.fs.json", current);
    writeSummaryTree(referenceDir, "spirv/forward.fs.json", reference);

    const ReportDiff sourceDiff = diffReports(currentDir, referenceDir);
    REQUIRE(sourceDiff.entries.size() == 1);
    CHECK(sourceDiff.sourceHashChanged);
    CHECK(sourceDiff.toolchainChanged);
    CHECK(sourceDiff.entries[0].status == DiffStatus::Incomparable);
    CHECK(sourceDiff.entries[0].metricDeltas.empty());

    reference.source.sha256 = current.source.sha256;
    reference.shaderc.version = current.shaderc.version;
    writeSummaryTree(referenceDir, "spirv/forward.fs.json", reference);

    const ReportDiff sameDiff = diffReports(currentDir, referenceDir);
    REQUIRE(sameDiff.entries.size() == 1);
    CHECK_FALSE(sameDiff.sourceHashChanged);
    CHECK_FALSE(sameDiff.toolchainChanged);
    CHECK(sameDiff.entries[0].status == DiffStatus::Same);
}

TEST_CASE("report index rebuilds from summary files") {
    TempDirectory temp;
    const fs::path reportsDir = temp.subdirectory("reports");
    writeSummaryTree(reportsDir, "dxbc/passes/forward.fs.json",
                     makeSummary("dxbc", "shaders/passes/forward.fs.sc", "fragment",
                                 {makeVariant("aaaaaaaaaaaa", {"A=1"}, 10, 500)}));
    writeSummaryTree(reportsDir, "spirv/passes/forward.vs.json",
                     makeSummary("spirv", "shaders/passes/forward.vs.sc", "vertex",
                                 {makeVariant("bbbbbbbbbbbb", {"B=1"}, 20, 700)}));
    writeRawFile(reportsDir / "dxbc" / "passes" / "forward.fs" /
                     "aaaaaaaaaaaa.json",
                 "{}");

    const std::size_t entries = writeReportIndex(reportsDir);
    CHECK(entries == 2);
    const ReportIndex index = parseIndex(readRawFile(reportsDir / "index.json"));
    REQUIRE(index.entries.size() == 2);
    CHECK(index.entries[0].backend == "dxbc");
    CHECK(index.entries[0].summaryPath == "dxbc/passes/forward.fs.json");
    CHECK(index.entries[0].variantCount == 1);
    CHECK(index.entries[0].okCount == 1);
    CHECK(index.entries[1].backend == "spirv");
    CHECK_FALSE(index.generatedAt.empty());
}
