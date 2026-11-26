#include "compile_internal.h"

#include "sg/validate.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <set>
#include <string>

namespace sg::detail {
namespace {

bool fieldActive(const TargetSpec& target, std::string_view field) {
    return std::find(target.blocks.begin(), target.blocks.end(), field) != target.blocks.end();
}

std::string targetString(const Graph& graph, const char* key, std::string fallback) {
    const auto found = graph.target.settings.find(key);
    if (found == graph.target.settings.end()) {
        return fallback;
    }
    if (const std::string* value = found->second.asString()) {
        return *value;
    }
    return fallback;
}

void visitUpstream(const Graph& graph, const std::set<NodeId>& allowed, NodeId id,
                   std::set<NodeId>& visited, std::vector<NodeId>& order) {
    if (id == kInvalidNode || !allowed.contains(id) || visited.contains(id)) {
        return;
    }
    visited.insert(id);
    for (const Edge& edge : graph.incomingEdges(id)) {
        visitUpstream(graph, allowed, edge.fromNode, visited, order);
    }
    order.push_back(id);
}

} // namespace

bool preparePropertyNodes(Graph& graph, std::vector<Diagnostic>& diagnostics) {
    bool ok = true;
    for (NodeInstance& node : graph.nodes) {
        if (node.type != "Property" || node.unknown) {
            continue;
        }
        std::optional<std::string> ref;
        for (const char* key : {"property", "propertyRef", "refName"}) {
            const auto found = node.settings.find(key);
            if (found == node.settings.end()) {
                continue;
            }
            if (const std::string* value = found->second.asString(); value != nullptr && !value->empty()) {
                ref = *value;
                break;
            }
        }
        const Property* property = ref ? graph.findProperty(*ref) : nullptr;
        if (property == nullptr) {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorMissingInput;
            diagnostic.message = "Property node does not reference a graph property";
            diagnostic.node = node.id;
            diagnostics.push_back(std::move(diagnostic));
            ok = false;
            continue;
        }

        node.settings.erase("propertyRef");
        node.settings.erase("refName");
        node.settings.insert_or_assign(
            "property", Value::makeString(ValueType::String, property->refName));
        node.precision = property->precision;

        ValueType outputType = property->type;
        if (outputType == ValueType::Color) {
            outputType = ValueType::Vector4;
        }

        const bool valueOutput =
            outputType == ValueType::Boolean || outputType == ValueType::Float ||
            outputType == ValueType::Integer || outputType == ValueType::Vector2 ||
            outputType == ValueType::Vector3 || outputType == ValueType::Vector4 ||
            outputType == ValueType::Gradient || outputType == ValueType::Texture2D ||
            outputType == ValueType::Texture2DArray || outputType == ValueType::Texture3D ||
            outputType == ValueType::Cubemap || outputType == ValueType::SamplerState ||
            outputType == ValueType::VirtualTexture || isMatrixType(outputType);
        if (!valueOutput) {
            const bool connected =
                std::any_of(graph.edges.begin(), graph.edges.end(),
                            [&](const Edge& edge) { return edge.fromNode == node.id; });
            if (connected) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorTypeMismatch;
                diagnostic.message =
                    std::string("Property node type '") + toString(property->type) +
                    "' cannot be wired as a value output";
                diagnostic.node = node.id;
                diagnostics.push_back(std::move(diagnostic));
                ok = false;
            }
            continue;
        }

        if (node.slots.empty()) {
            continue;
        }
        SlotInstance& slot = node.slots.front();
        slot.spec.type = outputType;
        slot.spec.displayName = property->name;
        if (const auto concrete = concreteTypeOf(outputType)) {
            slot.state.concreteType = *concrete;
            slot.concreteType = *concrete;
        }
    }
    return ok;
}

ActiveGraph collectActiveNodes(Graph& graph, const NodeRegistry& registry, const TargetSpec& target) {
    ActiveGraph result;
    std::set<NodeId> allowed;
    std::set<NodeId> roots;
    bool hasBlockNodes = false;
    for (const NodeInstance& node : graph.nodes) {
        if (node.unknown || !node.active) {
            continue;
        }
        const NodeDef* definition = registry.find(node.type);
        if (definition == nullptr) {
            continue;
        }
        if (!definition->blockNode) {
            allowed.insert(node.id);
            continue;
        }
        hasBlockNodes = true;
        const std::string& field = definition->blockField;
        if (field == "normalTangent" || field == "normalObject" || field == "normalWorld") {
            const std::string space = targetString(graph, "normalSpace", "tangent");
            if ((field == "normalTangent" && space == "tangent") ||
                (field == "normalObject" && space == "object") ||
                (field == "normalWorld" && space == "world")) {
                allowed.insert(node.id);
                roots.insert(node.id);
            }
            continue;
        }
        if (fieldActive(target, field)) {
            allowed.insert(node.id);
            roots.insert(node.id);
        }
    }

    if (roots.empty()) {
        if (!hasBlockNodes) {
            for (NodeId id : allowed) {
                visitUpstream(graph, allowed, id, result.nodes, result.order);
            }
        }
    } else {
        for (NodeId root : roots) {
            visitUpstream(graph, allowed, root, result.nodes, result.order);
        }
    }
    result.ok = true;
    return result;
}

} // namespace sg::detail
