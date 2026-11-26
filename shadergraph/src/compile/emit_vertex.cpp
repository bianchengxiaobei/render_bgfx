#include "compile_internal.h"

namespace sg::detail {

bool hasVertexDisplacement(const Graph& graph, const NodeRegistry& registry,
                           const ActiveGraph& active) {
    for (NodeId id : active.order) {
        const NodeInstance* node = graph.findNode(id);
        const NodeDef* definition = node == nullptr ? nullptr : registry.find(node->type);
        if (definition != nullptr && definition->blockNode &&
            definition->blockField == "positionOS") {
            return true;
        }
    }
    return false;
}

} // namespace sg::detail
