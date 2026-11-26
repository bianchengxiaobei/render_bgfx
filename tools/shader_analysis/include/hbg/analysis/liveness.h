#pragma once

#include "hbg/analysis/report.h"

#include <vector>

namespace hbg::analysis {

struct LivenessAnalysis {
    std::vector<LivenessInterval> intervals;
    std::vector<int> pressure;
    int peak = 0;

    bool operator==(const LivenessAnalysis&) const = default;
};

LivenessAnalysis analyzeLiveness(const std::vector<Instruction>& instructions);

} // namespace hbg::analysis
