#include "hbg/analysis/spirv_parser.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using namespace hbg::analysis;
namespace fs = std::filesystem;

constexpr const char* kSyntheticShader = R"(OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %100 "main"
OpSource HLSL 500
OpName %100 "main"
OpName %20 "ubo"
OpName %24 "tex"
OpName %27 "samp"
OpName %200 "synthetic.fs"
%201 = OpExtInstImport "NonSemantic.Shader.DebugInfo.100"
%202 = OpString "synthetic-debug.fs"
%203 = OpConstant %8 77
OpDecorate %20 Binding 2
OpDecorate %20 DescriptorSet 0
OpDecorate %24 Binding 0
OpDecorate %24 DescriptorSet 0
OpDecorate %27 Binding 1
OpDecorate %27 DescriptorSet 0
OpDecorate %13 Block
OpMemberDecorate %13 0 Offset 0
OpMemberDecorate %13 1 Offset 16
%1 = OpTypeFloat 32
%2 = OpTypeVector %1 4
%3 = OpTypeImage %1 2D 0 0 0 1 Unknown
%4 = OpTypeSampler
%5 = OpTypeSampledImage %3
%6 = OpTypeVoid
%7 = OpTypeFunction %6
%8 = OpTypeInt 32 1
%9 = OpTypeBool
%10 = OpConstant %8 0
%11 = OpConstant %8 1
%12 = OpConstantTrue %9
%13 = OpTypeStruct %2 %2
%14 = OpTypePointer Uniform %13
%15 = OpTypePointer Uniform %2
%16 = OpTypePointer UniformConstant %3
%17 = OpTypePointer UniformConstant %4
%20 = OpVariable %14 Uniform
%24 = OpVariable %16 UniformConstant
%27 = OpVariable %17 UniformConstant
%100 = OpFunction %6 None %7
%101 = OpFunctionParameter %8
%102 = OpLabel
%103 = OpAccessChain %15 %20 %10
%104 = OpLoad %2 %103
%105 = OpAccessChain %15 %20 %11
%106 = OpLoad %2 %105
%107 = OpAccessChain %15 %20 %101
%108 = OpLoad %2 %107
%109 = OpLoad %3 %24
%110 = OpLoad %4 %27
%111 = OpSampledImage %5 %109 %110
%112 = OpImageSampleImplicitLod %2 %111 %104
OpLine %200 42 0
%204 = OpExtInst %1 %201 DebugLine %202 %203 %203 %10 %10
%113 = OpCompositeExtract %1 %112 0
OpSelectionMerge %120 None
OpBranchConditional %12 %115 %120
%115 = OpLabel
%116 = OpFAdd %1 %113 %113
OpBranch %120
%120 = OpLabel
%121 = OpPhi %1 %116 %115 %113 %102
OpLoopMerge %140 %141 None
OpBranch %130
%130 = OpLabel
%131 = OpFAdd %1 %121 %121
OpBranch %141
%141 = OpLabel
OpBranch %130
%140 = OpLabel
OpReturn
OpFunctionEnd
)";

std::string readFixture(const char* name) {
    const fs::path path = fs::path(HBG_ANALYSIS_TEST_ROOT) / "tools" /
                          "shader_analysis" / "tests" / "fixtures" / name;
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.good());
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

} // namespace

TEST_CASE("SPIR-V disassembly extracts SSA liveness, resources and control flow") {
    const SpirvAnalysisResult result = analyzeSpirvDisassembly(
        kSyntheticShader, SpirvReflection{}, SpirvAnalysisOptions{}, "a.fs.sc",
        "fragment", "variant");
    REQUIRE(result.detail.instructions.size() > 0);
    CHECK(result.detail.backend == "spirv");
    CHECK(result.metrics.instructionCount.has_value());
    CHECK(*result.metrics.instructionCount > 0);
    CHECK(result.metrics.ssaValueCount.has_value());
    CHECK(*result.metrics.ssaValueCount == 15);
    CHECK(result.metrics.maxLiveSsaValues.has_value());
    CHECK(*result.metrics.maxLiveSsaValues > 0);
    CHECK(result.metrics.opcodeHistogram.count("OpExtInst") == 0);
    CHECK(result.metrics.loadCount == 5);
    CHECK(result.metrics.storeCount == 0);
    CHECK(result.metrics.accessChainCount == 3);
    CHECK(result.metrics.cbufferSlotCount == 1);
    CHECK(result.metrics.cbufferDwordsRead == 12);
    CHECK(result.metrics.dynamicCBufferReadCount == 1);
    CHECK(result.metrics.branchCount == 6);
    CHECK(result.metrics.loopCount == 1);
    CHECK(result.metrics.sourceLineCount == 2);
    CHECK(result.metrics.debugInfo.value_or(false));

    REQUIRE(result.detail.resources.size() == 3);
    CHECK(result.detail.resources[0].kind == "image");
    CHECK(result.detail.resources[1].kind == "sampler");
    CHECK(result.detail.resources[2].kind == "uniform_buffer");
    CHECK(result.detail.resources[2].name == "ubo");
    CHECK(result.detail.resources[2].binding == 2);
    REQUIRE(result.detail.cbufferReads.size() == 3);
    CHECK(result.detail.cbufferReads[0].offset == 0);
    CHECK(result.detail.cbufferReads[1].offset == 16);
    CHECK(result.detail.cbufferReads[2].dynamic);
    CHECK(result.detail.cbufferReads[2].offset == 0);

    bool foundSourceLine = false;
    bool foundSelection = false;
    bool foundLoop = false;
    for (const Instruction& instruction : result.detail.instructions) {
        if (instruction.sourceLine.has_value()) {
            foundSourceLine = true;
            CHECK(*instruction.sourceLine == 77);
        }
        if (instruction.opcode == "OpSelectionMerge") {
            foundSelection = true;
            CHECK(instruction.branch.kind == "selectionMerge");
            CHECK(instruction.branch.targetIndex.has_value());
        }
        if (instruction.opcode == "OpLoopMerge") {
            foundLoop = true;
            CHECK(instruction.branch.kind == "loopMerge");
            CHECK(instruction.branch.targetIndex.has_value());
        }
    }
    CHECK(foundSourceLine);
    CHECK(foundSelection);
    CHECK(foundLoop);
    CHECK_FALSE(result.detail.liveness.empty());
}

TEST_CASE("SPIR-V reflection resources are normalized") {
    const SpirvReflection reflection = parseSpirvReflectionJson(R"({
        "separate_images": [{"name": "tex", "binding": 0, "set": 0}],
        "separate_samplers": [{"name": "samp", "binding": 1, "set": 0}],
        "ubos": [{"name": "UniformBlock", "binding": 2, "set": 0, "block_size": 32}],
        "ssbos": [{"name": "storage", "binding": 3, "set": 0, "block_size": 0}],
        "push_constants": [{"name": "push", "block_size": 16}]
    })");
    CHECK(reflection.available);
    REQUIRE(reflection.resources.size() == 5);
    CHECK(reflection.resources[0].kind == "image");
    CHECK(reflection.resources[1].kind == "sampler");
    CHECK(reflection.resources[2].kind == "uniform_buffer");
    CHECK(reflection.resources[2].size == 32);
    CHECK(reflection.resources[3].kind == "storage_buffer");
    CHECK_FALSE(reflection.resources[3].size.has_value());
    CHECK(reflection.resources[4].kind == "push_constant");
    CHECK(reflection.uniformBufferBytes == 32);
    CHECK(reflection.pushConstantBytes == 16);
}

TEST_CASE("forward.vs SPIR-V fixture matches real reflection") {
    const SpirvReflection reflection =
        parseSpirvReflectionJson(readFixture("forward.vs.spv.reflect.json"));
    REQUIRE(reflection.available);
    const SpirvAnalysisResult result = analyzeSpirvDisassembly(
        readFixture("forward.vs.spv.asm.txt"), reflection, SpirvAnalysisOptions{},
        "shaders/passes/forward.vs.sc", "vertex", "e5b566abd182");
    REQUIRE(result.detail.resources.size() == 1);
    CHECK(result.detail.resources[0].kind == "uniform_buffer");
    CHECK(result.detail.resources[0].name == "UniformBlock");
    CHECK(result.detail.resources[0].binding == 0);
    CHECK(result.detail.resources[0].size == 144);
    CHECK(result.metrics.instructionCount.value_or(0) > 0);
    CHECK(result.metrics.maxLiveSsaValues.value_or(0) > 0);
    CHECK(result.metrics.uniformBufferCount == 1);
    CHECK(result.metrics.uniformBufferBytes == 144);
    REQUIRE(result.detail.cbufferReads.size() == 3);
    CHECK(result.detail.cbufferReads[0].offset == 0);
    CHECK(result.detail.cbufferReads[1].offset == 64);
    CHECK(result.detail.cbufferReads[2].offset == 128);
}
