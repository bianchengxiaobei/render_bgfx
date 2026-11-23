#pragma once

#include "vfx/asset.h"

#include <vector>

namespace vfx::detail {

bool flattenSubgraphs(Graph& graph, std::vector<ParseError>& errors);

} // namespace vfx::detail
