#include "compile_internal.h"

#include <set>

namespace sg::detail {

const std::vector<StageBinding>& stageMap() {
    static const std::vector<StageBinding> stages = {
        {"graph0", 0}, {"graph1", 1}, {"graph2", 2}, {"graph3", 3}, {"graph4", 4},
        {"lightData", 5}, {"directionalShadow", 6}, {"lightList", 7}, {"shadowContext", 8},
        {"punctualShadow", 9}, {"environment", 10}, {"contactShadow", 11},
        {"pointImsShadow", 12}, {"pointAreaDepth", 13}, {"probeMaps", 14}, {"cookieAtlas", 15},
    };
    return stages;
}

bool validatePassPlan(const TargetSpec& target, std::string& error) {
    if (target.passes.empty()) {
        error = "target has no passes";
        return false;
    }
    std::set<std::string> passNames;
    for (const PassSpec& pass : target.passes) {
        if (pass.id.empty() || !passNames.insert(pass.id).second) {
            error = "target has a duplicate or empty pass id";
            return false;
        }
        if (pass.vertexTemplate.empty() || pass.fragmentTemplate.empty()) {
            error = "pass has no vertex or fragment template";
            return false;
        }
    }
    std::set<int> stages;
    for (const StageBinding& binding : stageMap()) {
        if (binding.stage < 0 || binding.stage > 15 || !stages.insert(binding.stage).second) {
            error = "stage map has an invalid or duplicate stage";
            return false;
        }
    }
    return true;
}

} // namespace sg::detail
