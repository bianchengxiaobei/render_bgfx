#include "hbg/analysis/report.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

using namespace hbg::analysis;

namespace {

ReportSummary makeSummary() {
    ReportSummary summary;
    summary.toolVersion = "0.1.0";
    summary.generatedAt = "2026-11-23T12:34:56Z";
    summary.backend = "dxbc";
    summary.profile = "s_5_0";
    summary.optimization = 3;
    summary.debugInfo = false;
    summary.shaderc = {"tools/shaderc.exe", "1.0"};
    summary.source = {"shaders/passes/forward.fs.sc", "abc123", "bgfx-sc"};
    summary.stage = "fragment";

    VariantReport ok;
    ok.variantId = "27a361396d8d";
    ok.name = "forward.fs.0";
    ok.defines = {"B=2", "A=1"};
    ok.canonicalDefines = {"A=1", "B=2"};
    ok.keywords = {"A", "B"};
    ok.status = VariantStatus::Ok;
    ok.cacheHit = true;
    ok.compileMs = 0;
    ok.binaryBytes = 4567;
    ok.binaryPath = "shader_analysis/cache/dxbc/forward.fs/27a361396d8d/output.bin";
    ok.disasmPath = "shader_analysis/cache/dxbc/forward.fs/27a361396d8d/output.bin.disasm";
    ok.detailPath = "shader_analysis/reports/dxbc/forward.fs/27a361396d8d.json";
    Metrics metrics;
    metrics.instructionCount = 12;
    metrics.opcodeHistogram = {{"mov", 3}, {"sample", 1}};
    metrics.tempRegisterMax = 4;
    metrics.tempRegisterPeak = 2;
    metrics.samplerCount = 1;
    metrics.warningCount = 0;
    metrics.errorCount = 0;
    ok.metrics = metrics;
    ok.warnings = {{"warning text", {"frame 1"}}};
    summary.variants.push_back(ok);

    VariantReport failed;
    failed.variantId = "aaaaaaaabbbb";
    failed.name = "forward.fs.1";
    failed.status = VariantStatus::Failed;
    failed.errors = {{"compile error", {}}};
    summary.variants.push_back(failed);
    summary.variantCount = static_cast<int>(summary.variants.size());
    return summary;
}

DetailReport makeDetail() {
    DetailReport detail;
    detail.source = "shaders/passes/forward.fs.sc";
    detail.stage = "fragment";
    detail.backend = "dxbc";
    detail.variantId = "27a361396d8d";

    Instruction instruction;
    instruction.index = 0;
    instruction.opcode = "mov";
    instruction.text = "mov r0.xy, v0.xy";
    instruction.reads = {{"v", 0, "xy"}};
    instruction.writes = {{"r", 0, "xy"}};
    instruction.resourceRefs = {{"texture2d", "s_albedo", 0}};
    instruction.cbufferRead = CbufferRead{0, 1, 16, 4, false};
    instruction.pressure = Pressure{1, std::nullopt};
    instruction.branch = BranchInfo{"none", std::nullopt, 0};
    instruction.sourceLine = 42;
    detail.instructions.push_back(instruction);

    detail.liveness = {{"r", 0, 0, 0}};
    detail.resources = {{"texture2d", "s_albedo", 0, std::nullopt}};
    detail.cbufferReads = {CbufferRead{0, 1, 16, 4, true}};
    return detail;
}

} // namespace

TEST_CASE("summary JSON roundtrips") {
    const ReportSummary summary = makeSummary();
    const std::string json = toJson(summary);
    const ReportSummary parsed = parseSummary(json);
    CHECK(parsed == summary);
    CHECK(json.find("\"schemaVersion\": 1") != std::string::npos);
    CHECK(json.back() == '\n');
}

TEST_CASE("detail JSON roundtrips") {
    const DetailReport detail = makeDetail();
    const DetailReport parsed = parseDetail(toJson(detail));
    CHECK(parsed == detail);
}

TEST_CASE("index JSON roundtrips") {
    ReportIndex index;
    index.generatedAt = "2026-11-23T12:34:56Z";
    index.entries.push_back(ReportIndexEntry{
        "dxbc", "shaders/passes/forward.fs.sc", "fragment", "dxbc/passes/forward.fs.json",
        2, 1, 1, "abc123", "2026-11-23T12:34:56Z"});
    const ReportIndex parsed = parseIndex(toJson(index));
    CHECK(parsed == index);
}

TEST_CASE("report parsing rejects unsupported schema versions") {
    const ReportSummary summary = makeSummary();
    nlohmann::json document = nlohmann::json::parse(toJson(summary));
    document["schemaVersion"] = 2;
    CHECK_THROWS_AS(parseSummary(document.dump()), std::runtime_error);
}

TEST_CASE("report parsing rejects unknown statuses") {
    const ReportSummary summary = makeSummary();
    nlohmann::json document = nlohmann::json::parse(toJson(summary));
    document["variants"][0]["status"] = "bogus";
    CHECK_THROWS_AS(parseSummary(document.dump()), std::runtime_error);
}

TEST_CASE("report parsing rejects malformed JSON") {
    CHECK_THROWS_AS(parseSummary("{"), std::runtime_error);
    CHECK_THROWS_AS(parseDetail("[]"), std::runtime_error);
    CHECK_THROWS_AS(parseIndex("null"), std::runtime_error);
}
