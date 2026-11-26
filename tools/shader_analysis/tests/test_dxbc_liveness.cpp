#include "hbg/analysis/dxbc_parser.h"
#include "hbg/analysis/liveness.h"

#include <doctest/doctest.h>

#include <utility>
#include <vector>

using namespace hbg::analysis;

namespace {

RegisterRef ref(std::string registerClass, int index, std::string mask = "xyzw") {
    return RegisterRef{std::move(registerClass), index, std::move(mask)};
}

Instruction instruction(int index, std::vector<RegisterRef> reads = {},
                        std::vector<RegisterRef> writes = {}) {
    Instruction result;
    result.index = index;
    result.reads = std::move(reads);
    result.writes = std::move(writes);
    return result;
}

} // namespace

TEST_CASE("dxbc liveness matches Unity read-before-write scan") {
    const std::vector<Instruction> instructions = {
        instruction(0, {ref("r", 0, "x")}),
        instruction(1, {}, {ref("r", 0, "y")}),
        instruction(2, {ref("r", 0, "zw")}),
    };
    const LivenessAnalysis analysis = analyzeLiveness(instructions);
    CHECK(analysis.intervals == std::vector<LivenessInterval>{
                                     {"r", 0, 0, 0},
                                     {"r", 0, 1, 2},
                                 });
    CHECK(analysis.pressure == std::vector<int>{1, 1, 1});
    CHECK(analysis.peak == 1);
}

TEST_CASE("dxbc liveness drops definitions overwritten before any read") {
    const std::vector<Instruction> instructions = {
        instruction(0, {}, {ref("r", 0)}),
        instruction(1, {}, {ref("r", 0)}),
        instruction(2, {ref("r", 0)}),
    };
    const LivenessAnalysis analysis = analyzeLiveness(instructions);
    CHECK(analysis.intervals == std::vector<LivenessInterval>{{"r", 0, 1, 2}});
    CHECK(analysis.pressure == std::vector<int>{0, 1, 1});
    CHECK(analysis.peak == 1);
}

TEST_CASE("dxbc liveness keeps read-only registers alive from the start") {
    const std::vector<Instruction> instructions = {
        instruction(0, {ref("r", 0)}),
        instruction(1, {ref("r", 0)}),
        instruction(2, {ref("r", 0)}),
    };
    const LivenessAnalysis analysis = analyzeLiveness(instructions);
    CHECK(analysis.intervals == std::vector<LivenessInterval>{{"r", 0, 0, 2}});
    CHECK(analysis.pressure == std::vector<int>{1, 1, 1});
    CHECK(analysis.peak == 1);
}

TEST_CASE("dxbc liveness ignores dead writes and non-temp registers") {
    const std::vector<Instruction> instructions = {
        instruction(0, {ref("v", 0)}, {ref("r", 0)}),
        instruction(1, {ref("v", 0), ref("o", 0)}),
    };
    const LivenessAnalysis analysis = analyzeLiveness(instructions);
    CHECK(analysis.intervals.empty());
    CHECK(analysis.pressure == std::vector<int>{0, 0});
    CHECK(analysis.peak == 0);
}

TEST_CASE("dxbc liveness counts unique registers and x class") {
    const std::vector<Instruction> instructions = {
        instruction(0, {}, {ref("r", 0), ref("x", 2)}),
        instruction(1, {ref("r", 0)}, {ref("r", 1)}),
        instruction(2, {ref("r", 1), ref("r", 0), ref("x", 2)}),
    };
    const LivenessAnalysis analysis = analyzeLiveness(instructions);
    CHECK(analysis.intervals == std::vector<LivenessInterval>{
                                     {"r", 0, 0, 2},
                                     {"r", 1, 1, 2},
                                     {"x", 2, 0, 2},
                                 });
    CHECK(analysis.pressure == std::vector<int>{2, 3, 3});
    CHECK(analysis.peak == 3);
}

TEST_CASE("applyLiveness writes pressure back to DXBC instructions") {
    DxbcProgram program;
    program.instructions = {
        instruction(0, {}, {ref("r", 0)}),
        instruction(1, {ref("r", 0)}),
    };
    const LivenessAnalysis analysis = analyzeLiveness(program.instructions);
    applyLiveness(program, analysis);
    REQUIRE(program.instructions.size() == 2);
    CHECK(program.instructions[0].pressure.temp == 1);
    CHECK(program.instructions[1].pressure.temp == 1);
    CHECK(program.tempRegisterPeak == 1);
    CHECK(program.liveness == analysis.intervals);
}
