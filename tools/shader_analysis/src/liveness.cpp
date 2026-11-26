#include "hbg/analysis/liveness.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace hbg::analysis {
namespace {

using RegisterKey = std::pair<std::string, int>;

bool isTempRegister(const RegisterRef& ref) {
    return ref.registerClass == "r" || ref.registerClass == "x";
}

RegisterKey keyOf(const RegisterRef& ref) {
    return {ref.registerClass, ref.index};
}

bool intervalLess(const LivenessInterval& left, const LivenessInterval& right) {
    if (left.registerClass != right.registerClass) {
        return left.registerClass < right.registerClass;
    }
    if (left.index != right.index) {
        return left.index < right.index;
    }
    if (left.start != right.start) {
        return left.start < right.start;
    }
    return left.end < right.end;
}

} // namespace

LivenessAnalysis analyzeLiveness(const std::vector<Instruction>& instructions) {
    LivenessAnalysis analysis;
    analysis.pressure.assign(instructions.size(), 0);
    if (instructions.empty()) {
        return analysis;
    }

    std::map<RegisterKey, int> lastRead;
    for (int index = static_cast<int>(instructions.size()) - 1; index >= 0; --index) {
        const Instruction& instruction = instructions[static_cast<size_t>(index)];

        for (const RegisterRef& write : instruction.writes) {
            if (!isTempRegister(write)) {
                continue;
            }
            const RegisterKey key = keyOf(write);
            const auto read = lastRead.find(key);
            if (read == lastRead.end()) {
                continue;
            }
            analysis.intervals.push_back(
                LivenessInterval{key.first, key.second, index, read->second});
            lastRead.erase(read);
        }

        for (const RegisterRef& read : instruction.reads) {
            if (!isTempRegister(read)) {
                continue;
            }
            lastRead.try_emplace(keyOf(read), index);
        }
    }

    for (const auto& [key, readIndex] : lastRead) {
        analysis.intervals.push_back(
            LivenessInterval{key.first, key.second, 0, readIndex});
    }

    std::sort(analysis.intervals.begin(), analysis.intervals.end(), intervalLess);

    const size_t count = instructions.size();
    std::vector<std::vector<size_t>> starts(count);
    std::vector<std::vector<size_t>> ends(count);
    for (size_t i = 0; i < analysis.intervals.size(); ++i) {
        const LivenessInterval& interval = analysis.intervals[i];
        int start = std::clamp(interval.start, 0, static_cast<int>(count) - 1);
        int end = std::clamp(interval.end, 0, static_cast<int>(count) - 1);
        if (start > end) {
            std::swap(start, end);
        }
        starts[static_cast<size_t>(start)].push_back(i);
        ends[static_cast<size_t>(end)].push_back(i);
    }

    std::map<RegisterKey, int> active;
    int peak = 0;
    for (size_t i = 0; i < count; ++i) {
        for (const size_t intervalIndex : starts[i]) {
            const LivenessInterval& interval = analysis.intervals[intervalIndex];
            ++active[{interval.registerClass, interval.index}];
        }
        analysis.pressure[i] = static_cast<int>(active.size());
        peak = std::max(peak, analysis.pressure[i]);
        for (const size_t intervalIndex : ends[i]) {
            const LivenessInterval& interval = analysis.intervals[intervalIndex];
            const auto it = active.find({interval.registerClass, interval.index});
            if (it != active.end() && --it->second == 0) {
                active.erase(it);
            }
        }
    }
    analysis.peak = peak;
    return analysis;
}

} // namespace hbg::analysis
