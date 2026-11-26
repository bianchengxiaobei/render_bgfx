#pragma once

#include "sg/graph.h"
#include "sg/registry.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sg {

struct SubGraphLibrary {
    std::map<std::string, Graph> graphs;

    const Graph* find(std::string_view name) const;
    void add(std::string name, Graph graph);
};

struct SubGraphExpansion {
    bool ok = false;
    Graph graph;
    std::vector<Diagnostic> diagnostics;
    std::string error;
};

SubGraphExpansion expandSubgraphs(const Graph& graph, const NodeRegistry& registry,
                                 const SubGraphLibrary& library, int maxDepth = 16);

} // namespace sg
