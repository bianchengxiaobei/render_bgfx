#pragma once

#include "sg/graph.h"
#include "sg/registry.h"

#include <vector>

namespace sg {

struct ConcretizeResult {
    bool ok = false;
    std::vector<NodeId> activeNodes;
    std::vector<Diagnostic> diagnostics;
};

ConcretizeResult concretize(Graph& graph, const NodeRegistry& registry, int maxIterations = 1024);

} // namespace sg
