#pragma once

#include "hbg/analysis/liveness.h"
#include "hbg/analysis/report.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace hbg::analysis {

struct DxbcProgramStats {
    int declaredTemps = 0;
    int maxTempIndex = -1;
    int inputCount = 0;
    int outputCount = 0;
    int cbufferSlotCount = 0;
    int textureCount = 0;
    int samplerCount = 0;
    int uavCount = 0;
    int structuredBufferCount = 0;
    int sampleCount = 0;
    int loadCount = 0;
    int storeCount = 0;
    int branchCount = 0;
    int loopCount = 0;
    std::map<std::string, std::int64_t> opcodeHistogram;

    bool operator==(const DxbcProgramStats&) const = default;
};

struct DxbcProgram {
    std::vector<Instruction> instructions;
    std::vector<LivenessInterval> liveness;
    std::vector<Resource> resources;
    std::vector<CbufferRead> cbufferReads;
    DxbcProgramStats stats;
    int tempRegisterPeak = 0;

    bool operator==(const DxbcProgram&) const = default;
};

DxbcProgram parseDxbcDisassembly(std::string_view text);
void applyLiveness(DxbcProgram& program, const LivenessAnalysis& analysis);

DetailReport buildDxbcDetail(const DxbcProgram& program, std::string source,
                             std::string stage, std::string variantId);
Metrics buildDxbcMetrics(const DxbcProgram& program);

} // namespace hbg::analysis
