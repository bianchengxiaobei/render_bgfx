#pragma once

#include "sg/registry.h"

namespace sg {

void registerBuiltinNodes(NodeRegistry& registry);
NodeRegistry makeBuiltinNodeRegistry();

} // namespace sg
