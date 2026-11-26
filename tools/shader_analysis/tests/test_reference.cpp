#include "hbg/analysis/reference.h"
#include "hbg/analysis/report_files.h"

#include "test_report_fixtures.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <stdexcept>
#include <string>

using namespace hbg::analysis;
using namespace hbg_test;

TEST_CASE("set-reference copies summaries and rebuilds the index") {
    TempDirectory temp;
    const fs::path from = temp.subdirectory("reports");
    const fs::path to = temp.subdirectory("reference");
    const ReportSummary summary =
        makeSummary("dxbc", "shaders/passes/forward.fs.sc", "fragment",
                    {makeVariant("aaaaaaaaaaaa", {"A=1"}, 10, 500)});
    writeSummaryTree(from, "dxbc/forward.fs.json", summary);
    writeRawFile(from / "dxbc" / "forward.fs" / "aaaaaaaaaaaa.json",
                 R"({"instructions":[]})");
    writeRawFile(from / "index.json", R"({"stale":true})");

    const ReferenceResult result = setReference(from, to);
    CHECK(result.summaryCount == 1);
    CHECK(result.indexEntryCount == 1);
    CHECK(fs::exists(to / "dxbc" / "forward.fs.json"));
    CHECK_FALSE(fs::exists(to / "dxbc" / "forward.fs" / "aaaaaaaaaaaa.json"));
    CHECK(fs::exists(to / "index.json"));

    const ReportSummary copied = parseSummary(readRawFile(to / "dxbc" / "forward.fs.json"));
    REQUIRE(copied.variants.size() == 1);
    CHECK_FALSE(copied.variants[0].detailPath.has_value());
    CHECK_FALSE(copied.variants[0].binaryPath.has_value());
    CHECK_FALSE(copied.variants[0].disasmPath.has_value());

    const ReportIndex index = parseIndex(readRawFile(to / "index.json"));
    REQUIRE(index.entries.size() == 1);
    CHECK(index.entries[0].backend == "dxbc");
    CHECK(index.entries[0].source == "shaders/passes/forward.fs.sc");
    CHECK(index.entries[0].summaryPath == "dxbc/forward.fs.json");
    CHECK(index.entries[0].variantCount == 1);
    CHECK(index.entries[0].okCount == 1);

    writeRawFile(to / "dxbc" / "stale.json", "{}");
    const ReferenceResult replaced = setReference(from, to);
    CHECK(replaced.summaryCount == 1);
    CHECK_FALSE(fs::exists(to / "dxbc" / "stale.json"));
}

TEST_CASE("set-reference rejects overlapping source and destination") {
    TempDirectory temp;
    const fs::path from = temp.subdirectory("reports");
    writeSummaryTree(from, "dxbc/forward.fs.json",
                     makeSummary("dxbc", "shaders/passes/forward.fs.sc", "fragment",
                                 {makeVariant("aaaaaaaaaaaa", {"A=1"}, 10, 500)}));

    CHECK_THROWS_AS(setReference(from, from), std::runtime_error);
    CHECK_THROWS_AS(setReference(from, from / "nested"), std::runtime_error);
    CHECK_THROWS_AS(setReference(from, from.parent_path()), std::runtime_error);
}
