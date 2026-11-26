#include "hbg/analysis/dxbc_parser.h"
#include "hbg/analysis/liveness.h"
#include "hbg/analysis/report.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using namespace hbg::analysis;

namespace {

std::string fixture(const char* name) {
    const std::string path =
        std::string(HBG_ANALYSIS_TEST_ROOT) + "/tools/shader_analysis/tests/fixtures/" +
        name;
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

bool hasRead(const Instruction& instruction, const std::string& registerClass,
             int index) {
    for (const RegisterRef& ref : instruction.reads) {
        if (ref.registerClass == registerClass && ref.index == index) {
            return true;
        }
    }
    return false;
}

bool hasWrite(const Instruction& instruction, const std::string& registerClass,
              int index) {
    for (const RegisterRef& ref : instruction.writes) {
        if (ref.registerClass == registerClass && ref.index == index) {
            return true;
        }
    }
    return false;
}

bool hasResource(const Instruction& instruction, const std::string& kind,
                 const std::string& name) {
    for (const ResourceRef& ref : instruction.resourceRefs) {
        if (ref.kind == kind && ref.name == name) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("parses the real forward vertex disassembly") {
    const DxbcProgram program =
        parseDxbcDisassembly(fixture("forward.vs.disasm"));
    REQUIRE(program.instructions.size() == 32);
    CHECK(program.stats.declaredTemps == 3);
    CHECK(program.stats.maxTempIndex == 2);
    CHECK(program.stats.inputCount == 5);
    CHECK(program.stats.outputCount == 6);
    CHECK(program.stats.cbufferSlotCount == 1);
    CHECK(program.stats.textureCount == 0);
    CHECK(program.stats.samplerCount == 0);
    CHECK(program.resources.empty());
    REQUIRE(program.cbufferReads.size() == 16);
    CHECK(program.cbufferReads.front() == CbufferRead{0, 0, 16, 4, false});
    CHECK(program.cbufferReads.back() == CbufferRead{29, 0, 136, 4, false});

    const Instruction& first = program.instructions.front();
    CHECK(first.index == 0);
    CHECK(first.opcode == "mul");
    CHECK(first.text == "mul r0.xyzw, v1.yyyy, cb0[1].xyzw");
    CHECK(hasRead(first, "v", 1));
    CHECK(hasWrite(first, "r", 0));
    REQUIRE(first.cbufferRead.has_value());
    CHECK(first.cbufferRead->slot == 0);
    CHECK(first.cbufferRead->offset == 16);
    CHECK(first.cbufferRead->dwords == 4);
    CHECK_FALSE(first.cbufferRead->dynamic);

    const LivenessAnalysis liveness = analyzeLiveness(program.instructions);
    CHECK(liveness.peak > 0);
    CHECK(liveness.pressure.size() == program.instructions.size());

    DxbcProgram analyzed = program;
    applyLiveness(analyzed, liveness);
    CHECK(analyzed.tempRegisterPeak == liveness.peak);
    CHECK(analyzed.instructions[0].pressure.temp == liveness.pressure[0]);

    const DetailReport detail =
        buildDxbcDetail(analyzed, "shaders/passes/forward.vs.sc", "vertex",
                        "27a361396d8d");
    CHECK(detail.instructions.size() == 32);
    CHECK(detail.liveness == liveness.intervals);
    CHECK(detail.liveness.front().registerClass == "r");
    CHECK(parseDetail(toJson(detail)) == detail);

    const Metrics metrics = buildDxbcMetrics(analyzed);
    CHECK(metrics.instructionCount == 32);
    CHECK(metrics.tempRegisterMax == 3);
    CHECK(metrics.tempRegisterPeak == liveness.peak);
    CHECK(metrics.inputCount == 5);
    CHECK(metrics.outputCount == 6);
    CHECK(metrics.cbufferSlotCount == 1);
    CHECK(metrics.cbufferDwordsRead == 64);
    CHECK(metrics.dynamicCBufferReadCount == 0);
    CHECK(metrics.sampleCount == 0);
    CHECK(metrics.loadCount == 0);
    CHECK(metrics.storeCount == 0);
    CHECK(metrics.opcodeHistogram.at("mul") > 0);
}

TEST_CASE("parses a real forward fragment disassembly excerpt") {
    const DxbcProgram program =
        parseDxbcDisassembly(fixture("forward.fs.sample.disasm"));
    CHECK(program.instructions.size() == 83);
    CHECK(program.stats.declaredTemps == 58);
    CHECK(program.stats.inputCount >= 4);
    CHECK(program.stats.outputCount == 1);
    CHECK(program.stats.textureCount > 0);
    CHECK(program.stats.samplerCount > 0);
    CHECK(program.stats.sampleCount == 5);
    CHECK(program.stats.loopCount == 0);
    CHECK(program.stats.branchCount > 0);
    CHECK_FALSE(program.resources.empty());
    CHECK_FALSE(program.cbufferReads.empty());
    CHECK(program.cbufferReads.front().slot == 0);
    CHECK(program.cbufferReads.front().offset == 13 * 16);
    CHECK(program.instructions.front().opcode == "mov");
    CHECK(program.instructions.back().opcode == "sample_indexable");
    CHECK(std::any_of(program.instructions.begin(), program.instructions.end(),
                      [](const Instruction& instruction) {
                          return instruction.branch.kind == "if";
                      }));
}
TEST_CASE("parses declared resources, cbuffers and control flow") {
    const std::string text = R"(ps_5_0
dcl_constantbuffer CB0[54], immediateIndexed
dcl_constantbuffer CB2[8], immediateIndexed
dcl_sampler s0, mode_default
dcl_resource_texture2d (float,float,float,float) t0
dcl_resource_structured t9, 16
dcl_uav_typed (float,float,float,float) u2
dcl_input_ps linear v4.xy
dcl_output o0.xyzw
dcl_temps 8
dcl_indexableTemp x0[16], 4
sample_indexable(texture2d)(float,float,float,float) r2.xyzw, v4.xyxx, t0.xyzw, s0
mov r5.x, -r4.w
lt r0.w, r2.w, cb0[28].w
if_nz r0.w
  mov x0[r1.w + 0].xyzw, r2.xyzw
  ld_indexable(buffer)(float,float,float,float) r3.xyzw, r2.xxxx, t9.xyzw
  loop
    breakc_z r15.w
    add r1.x, cb0[r2.x + 4].xy, r3.x
    continue
  endloop
else
  store_uav_typed u2.xyzw, r2.xyzw
endif
label_1:
call label_1
imul null, r6.x, r1.x, l(10)
ret
)";
    const DxbcProgram program = parseDxbcDisassembly(text);
    REQUIRE(program.instructions.size() == 17);
    CHECK(program.stats.declaredTemps == 8);
    CHECK(program.stats.inputCount == 1);
    CHECK(program.stats.outputCount == 1);
    CHECK(program.stats.cbufferSlotCount == 2);
    CHECK(program.stats.textureCount == 1);
    CHECK(program.stats.samplerCount == 1);
    CHECK(program.stats.uavCount == 1);
    CHECK(program.stats.structuredBufferCount == 1);
    REQUIRE(program.resources.size() == 4);

    const Instruction& sample = program.instructions[0];
    CHECK(sample.opcode == "sample_indexable");
    CHECK(hasRead(sample, "v", 4));
    CHECK(hasWrite(sample, "r", 2));
    CHECK(hasResource(sample, "texture2d", "t0"));
    CHECK(hasResource(sample, "sampler", "s0"));

    const Instruction& indexableWrite = program.instructions[4];
    CHECK(hasWrite(indexableWrite, "x", 0));
    CHECK(hasRead(indexableWrite, "r", 1));
    CHECK(hasRead(indexableWrite, "r", 2));

    const Instruction& load = program.instructions[5];
    CHECK(load.opcode == "ld_indexable");
    CHECK(hasWrite(load, "r", 3));
    CHECK(hasResource(load, "structured_buffer", "t9"));

    const Instruction& dynamicCbuffer = program.instructions[8];
    REQUIRE(dynamicCbuffer.cbufferRead.has_value());
    CHECK(dynamicCbuffer.cbufferRead->offset == 64);
    CHECK(dynamicCbuffer.cbufferRead->dwords == 2);
    CHECK(dynamicCbuffer.cbufferRead->dynamic);
    CHECK(hasRead(dynamicCbuffer, "r", 2));

    const Instruction& staticCbuffer = program.instructions[2];
    REQUIRE(staticCbuffer.cbufferRead.has_value());
    CHECK(staticCbuffer.cbufferRead->offset == 460);
    CHECK(staticCbuffer.cbufferRead->dwords == 1);
    CHECK_FALSE(staticCbuffer.cbufferRead->dynamic);

    const Instruction& store = program.instructions[12];
    CHECK(store.opcode == "store_uav_typed");
    CHECK(store.writes.empty());
    CHECK(hasRead(store, "r", 2));
    CHECK(hasResource(store, "uav", "u2"));

    const Instruction& call = program.instructions[14];
    CHECK(call.branch.kind == "call");
    CHECK(call.branch.targetIndex == 14);

    const Instruction& imul = program.instructions[15];
    CHECK(hasWrite(imul, "r", 6));
    CHECK_FALSE(hasWrite(imul, "r", 1));

    CHECK(program.instructions[3].branch.kind == "if");
    CHECK(program.instructions[3].branch.indent == 0);
    CHECK(program.instructions[4].branch.indent == 1);
    CHECK(program.instructions[6].branch.kind == "loop");
    CHECK(program.instructions[6].branch.indent == 1);
    CHECK(program.instructions[7].branch.kind == "break");
    CHECK(program.instructions[10].branch.kind == "endloop");
    CHECK(program.instructions[10].branch.indent == 1);
    CHECK(program.instructions[11].branch.kind == "else");
    CHECK(program.instructions[11].branch.indent == 0);
    CHECK(program.instructions[13].branch.kind == "endif");
    CHECK(program.instructions[13].branch.indent == 0);

    const Metrics metrics = buildDxbcMetrics(program);
    CHECK(metrics.tempRegisterMax == 8);
    CHECK(metrics.cbufferDwordsRead == 3);
    CHECK(metrics.dynamicCBufferReadCount == 1);
    CHECK(metrics.sampleCount == 1);
    CHECK(metrics.loadCount == 1);
    CHECK(metrics.storeCount == 1);
    CHECK(metrics.textureCount == 1);
    CHECK(metrics.samplerCount == 1);
    CHECK(metrics.uavCount == 1);
    CHECK(metrics.structuredBufferCount == 1);
}

TEST_CASE("parses predicates and switch/case indentation") {
    const std::string text = R"(ps_5_0
(p0) mov r0.x, r1.x
switch r0.x
case 0
  break
default:
  break
endswitch
ret
)";
    const DxbcProgram program = parseDxbcDisassembly(text);
    REQUIRE(program.instructions.size() == 8);
    CHECK(program.instructions[0].reads.size() == 2);
    CHECK(hasRead(program.instructions[0], "p", 0));
    CHECK(hasRead(program.instructions[0], "r", 1));
    CHECK(hasWrite(program.instructions[0], "r", 0));
    CHECK(program.instructions[1].branch.kind == "switch");
    CHECK(program.instructions[1].branch.indent == 0);
    CHECK(program.instructions[2].branch.kind == "case");
    CHECK(program.instructions[2].branch.indent == 0);
    CHECK(program.instructions[3].branch.indent == 1);
    CHECK(program.instructions[4].branch.kind == "default");
    CHECK(program.instructions[4].branch.indent == 0);
    CHECK(program.instructions[6].branch.kind == "endswitch");
    CHECK(program.instructions[6].branch.indent == 0);
    CHECK(program.instructions[7].branch.kind == "ret");
}
