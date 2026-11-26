#pragma once

#include "sg/graph.h"
#include "sg/registry.h"

#include <vector>

namespace sg {

struct ValidationResult {
    bool ok = false;
    std::vector<Diagnostic> diagnostics;
};

ValidationResult validate(Graph& graph, const NodeRegistry& registry, bool runConcretize = true);

bool hasError(const std::vector<Diagnostic>& diagnostics);

} // namespace sg
