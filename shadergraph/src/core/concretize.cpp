#include "sg/concretize.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <set>

namespace sg {
namespace {

std::optional<ConcreteValueType> slotConcrete(const Graph& graph, const Edge& edge) {
    const SlotInstance* slot = graph.findSlot(edge.fromNode, edge.fromSlot);
    if (slot == nullptr) {
        return std::nullopt;
    }
    return slot->concreteType;
}

bool isVectorConcrete(ConcreteValueType type) {
    return type == ConcreteValueType::Float || type == ConcreteValueType::Integer ||
           type == ConcreteValueType::Boolean || type == ConcreteValueType::Vector2 ||
           type == ConcreteValueType::Vector3 || type == ConcreteValueType::Vector4;
}

ConcreteValueType vectorResult(const std::vector<ConcreteValueType>& input) {
    std::vector<ConcreteValueType> distinct;
    for (ConcreteValueType type : input) {
        if (!isVectorConcrete(type)) {
            continue;
        }
        if (std::find(distinct.begin(), distinct.end(), type) == distinct.end()) {
            distinct.push_back(type);
        }
    }
    if (distinct.empty()) {
        return ConcreteValueType::Float;
    }
    if (distinct.size() == 1) {
        return distinct.front() == ConcreteValueType::Boolean ? ConcreteValueType::Float
                                                              : distinct.front();
    }
    std::vector<ConcreteValueType> candidates;
    for (ConcreteValueType type : distinct) {
        if (type != ConcreteValueType::Float && type != ConcreteValueType::Boolean) {
            candidates.push_back(type);
        }
    }
    if (candidates.empty()) {
        return ConcreteValueType::Float;
    }
    return *std::min_element(candidates.begin(), candidates.end(), [](ConcreteValueType lhs, ConcreteValueType rhs) {
        return channelCount(lhs) < channelCount(rhs);
    });
}

ConcreteValueType matrixResult(const std::vector<ConcreteValueType>& input) {
    std::vector<ConcreteValueType> distinct;
    for (ConcreteValueType type : input) {
        if (matrixDimension(type) == 0) {
            continue;
        }
        if (std::find(distinct.begin(), distinct.end(), type) == distinct.end()) {
            distinct.push_back(type);
        }
    }
    if (distinct.empty()) {
        return ConcreteValueType::Matrix2;
    }
    return *std::max_element(distinct.begin(), distinct.end(), [](ConcreteValueType lhs, ConcreteValueType rhs) {
        return matrixDimension(lhs) < matrixDimension(rhs);
    });
}

bool setConcrete(SlotInstance& slot, ConcreteValueType type) {
    if (slot.concreteType == type) {
        return false;
    }
    slot.concreteType = type;
    return true;
}

void markConnections(Graph& graph) {
    for (NodeInstance& node : graph.nodes) {
        for (SlotInstance& slot : node.slots) {
            slot.connected = false;
            slot.hasError = false;
        }
    }
    for (const Edge& edge : graph.edges) {
        SlotInstance* slot = graph.findSlot(edge.toNode, edge.toSlot);
        if (slot != nullptr) {
            slot->connected = true;
        }
    }
}

std::vector<NodeId> topologicalOrder(const Graph& graph) {
    std::vector<NodeId> order;
    std::set<NodeId> visiting;
    std::set<NodeId> visited;

    std::function<void(const NodeInstance&)> visit = [&](const NodeInstance& node) {
        if (visited.contains(node.id) || visiting.contains(node.id)) {
            return;
        }
        visiting.insert(node.id);
        for (const Edge& edge : graph.incomingEdges(node.id)) {
            const NodeInstance* source = graph.findNode(edge.fromNode);
            if (source != nullptr) {
                visit(*source);
            }
        }
        visiting.erase(node.id);
        visited.insert(node.id);
        order.push_back(node.id);
    };

    for (const NodeInstance& node : graph.nodes) {
        visit(node);
    }
    return order;
}

GraphPrecision precisionOf(const NodeInstance& node, const Graph& graph) {
    if (node.precision != Precision::Inherit) {
        return toGraphPrecision(node.precision, GraphPrecision::Graph);
    }
    const std::vector<const SlotInstance*> inputs = node.inputSlots();
    if (inputs.empty()) {
        return GraphPrecision::Graph;
    }
    GraphPrecision result = GraphPrecision::Half;
    for (const SlotInstance* input : inputs) {
        if (!input->connected) {
            result = static_cast<GraphPrecision>(
                std::min(static_cast<int>(result), static_cast<int>(GraphPrecision::Graph)));
            continue;
        }
        const Edge* edge = graph.incomingEdge(node.id, input->spec.id);
        if (edge == nullptr) {
            result = static_cast<GraphPrecision>(
                std::min(static_cast<int>(result), static_cast<int>(GraphPrecision::Graph)));
            continue;
        }
        const NodeInstance* source = graph.findNode(edge->fromNode);
        if (source == nullptr) {
            continue;
        }
        result = static_cast<GraphPrecision>(
            std::min(static_cast<int>(result), static_cast<int>(source->graphPrecision)));
    }
    return result;
}

} // namespace

ConcretizeResult concretize(Graph& graph, const NodeRegistry& registry, int maxIterations) {
    ConcretizeResult result;
    markConnections(graph);
    for (const NodeInstance& node : graph.nodes) {
        if (node.active && !node.unknown) {
            result.activeNodes.push_back(node.id);
        }
    }

    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        bool changed = false;

        for (NodeInstance& node : graph.nodes) {
            if (node.unknown) {
                continue;
            }
            const NodeDef* definition = registry.find(node.type);
            std::vector<ConcreteValueType> vectorInputs;
            std::vector<ConcreteValueType> matrixInputs;

            for (const SlotInstance& slot : node.slots) {
                if (!slot.isInput()) {
                    continue;
                }
                if (slot.isDynamicVector()) {
                    if (slot.state.mode == SlotMode::Literal && slot.state.value) {
                        if (const auto concrete = concreteTypeOf(slot.state.value->type)) {
                            if (isVectorConcrete(*concrete)) {
                                vectorInputs.push_back(*concrete);
                            }
                        }
                    }
                    const Edge* edge = graph.incomingEdge(node.id, slot.spec.id);
                    if (edge != nullptr) {
                        if (const auto concrete = slotConcrete(graph, *edge)) {
                            vectorInputs.push_back(*concrete);
                        }
                    }
                } else if (slot.isDynamicMatrix()) {
                    if (slot.state.mode == SlotMode::Literal && slot.state.value) {
                        if (const auto concrete = concreteTypeOf(slot.state.value->type)) {
                            if (matrixDimension(*concrete) != 0) {
                                matrixInputs.push_back(*concrete);
                            }
                        }
                    }
                    const Edge* edge = graph.incomingEdge(node.id, slot.spec.id);
                    if (edge != nullptr) {
                        if (const auto concrete = slotConcrete(graph, *edge)) {
                            matrixInputs.push_back(*concrete);
                        }
                    }
                }
            }

            ConcreteValueType vectorType = vectorResult(vectorInputs);
            ConcreteValueType matrixType = matrixResult(matrixInputs);
            if (vectorInputs.empty()) {
                for (const SlotInstance& slot : node.slots) {
                    if (slot.isDynamicVector() && slot.state.concreteType) {
                        vectorType = *slot.state.concreteType;
                        break;
                    }
                }
            }
            if (matrixInputs.empty()) {
                for (const SlotInstance& slot : node.slots) {
                    if (slot.isDynamicMatrix() && slot.state.concreteType) {
                        matrixType = *slot.state.concreteType;
                        break;
                    }
                }
            }

            for (SlotInstance& slot : node.slots) {
                if (slot.isDynamicVector()) {
                    changed |= setConcrete(slot, vectorType);
                } else if (slot.isDynamicMatrix()) {
                    changed |= setConcrete(slot, matrixType);
                }
            }

            if (definition != nullptr && definition->updateSlots) {
                if (definition->updateSlots(graph, node)) {
                    changed = true;
                }
            }
        }

        if (!changed) {
            result.ok = true;
            break;
        }
    }

    if (!result.ok) {
        Diagnostic diagnostic;
        diagnostic.severity = Severity::Error;
        diagnostic.code = kErrorTypeUnresolved;
        diagnostic.message = "graph concrete types did not converge";
        result.diagnostics.push_back(std::move(diagnostic));
    }

    const std::vector<NodeId> order = topologicalOrder(graph);
    for (NodeId id : order) {
        NodeInstance* node = graph.findNode(id);
        if (node == nullptr) {
            continue;
        }
        const GraphPrecision precision = precisionOf(*node, graph);
        const ConcretePrecision concrete = toConcretePrecision(precision, graph.defaultPrecision);
        node->graphPrecision = precision;
        node->concretePrecision = concrete;
        for (SlotInstance& slot : node->slots) {
            slot.graphPrecision = precision;
            slot.concretePrecision = concrete;
        }
    }

    return result;
}

} // namespace sg
