#include "sg/subgraph.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <optional>
#include <set>

namespace sg {
namespace {

constexpr const char* kSubGraphType = "SubGraph";
constexpr const char* kSubGraphOutputType = "SubGraphOutput";
constexpr const char* kPropertyType = "Property";

bool isSubGraphNode(const NodeInstance& node) {
    return node.type == kSubGraphType || node.type == "Sub-graph" ||
           node.type == "Sub Graph" || node.type == "SubGraphNode" ||
           node.type == "Subgraph";
}

std::string settingString(const NodeInstance& node, std::string_view name) {
    const Value* value = node.setting(name);
    if (value == nullptr) {
        return {};
    }
    if (const auto* text = value->asString()) {
        return *text;
    }
    return {};
}

std::string namespacedRef(NodeId nodeId, std::string_view refName) {
    return "sg" + std::to_string(nodeId) + "_" + std::string(refName);
}

std::string propertyRef(const NodeInstance& node) {
    for (const char* key : {"property", "propertyRef", "refName"}) {
        const std::string value = settingString(node, key);
        if (!value.empty()) {
            return value;
        }
    }
    return {};
}

std::string dropdownRef(const NodeInstance& node) {
    for (const char* key : {"dropdown", "dropdownRef", "refName"}) {
        const std::string value = settingString(node, key);
        if (!value.empty()) {
            return value;
        }
    }
    return {};
}

const Value* subGraphDropdownSetting(const NodeInstance& node, std::string_view refName) {
    const std::string key = "dropdown." + std::string(refName);
    const auto found = node.settings.find(key);
    return found == node.settings.end() ? nullptr : &found->second;
}

std::optional<int> dropdownSelectionIndex(const Dropdown& dropdown, const Value& value) {
    if (const auto* text = value.asString()) {
        if (text->empty()) {
            return std::nullopt;
        }
        for (std::size_t index = 0; index < dropdown.entries.size(); ++index) {
            if (dropdown.entries[index].displayName == *text) {
                return static_cast<int>(index);
            }
        }
        int parsed = -1;
        const char* begin = text->data();
        const char* end = begin + text->size();
        const auto result = std::from_chars(begin, end, parsed);
        if (result.ec == std::errc{} && result.ptr == end && parsed >= 0 &&
            static_cast<std::size_t>(parsed) < dropdown.entries.size()) {
            return parsed;
        }
        return std::nullopt;
    }
    if (const auto* index = value.asInteger()) {
        if (*index >= 0 && static_cast<std::size_t>(*index) < dropdown.entries.size()) {
            return static_cast<int>(*index);
        }
    }
    return std::nullopt;
}

void addDiagnostic(std::vector<Diagnostic>& diagnostics, Severity severity, const char* code,
                   std::string message, NodeId node = kInvalidNode, SlotId slot = kInvalidSlot) {
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.code = code;
    diagnostic.message = std::move(message);
    diagnostic.node = node;
    diagnostic.slot = slot;
    diagnostics.push_back(std::move(diagnostic));
}

bool expandGraph(Graph& graph, const NodeRegistry& registry, const SubGraphLibrary& library,
                 std::vector<std::string>& stack, int depth, int maxDepth,
                 std::vector<Diagnostic>& diagnostics, std::string& error) {
    while (true) {
        const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(), isSubGraphNode);
        if (found == graph.nodes.end()) {
            return true;
        }
        const NodeId subNodeId = found->id;
        NodeInstance subNode = *found;
        const std::string name = settingString(subNode, "subgraph");
        if (name.empty()) {
            addDiagnostic(diagnostics, Severity::Error, kErrorInvalidValue,
                          "SubGraph node is missing the subgraph setting", subNodeId);
            return false;
        }
        if (depth >= maxDepth) {
            addDiagnostic(diagnostics, Severity::Error, kErrorSubGraphDepth,
                          "subgraph expansion exceeded maximum depth", subNodeId);
            return false;
        }
        if (std::find(stack.begin(), stack.end(), name) != stack.end()) {
            addDiagnostic(diagnostics, Severity::Error, kErrorSubGraphDepth,
                          "recursive subgraph reference: " + name, subNodeId);
            return false;
        }
        const Graph* source = library.find(name);
        if (source == nullptr) {
            addDiagnostic(diagnostics, Severity::Error, kErrorUnknownNode,
                          "subgraph not found: " + name, subNodeId);
            return false;
        }

        stack.push_back(name);
        Graph expanded = *source;
        if (!expandGraph(expanded, registry, library, stack, depth + 1, maxDepth, diagnostics, error)) {
            stack.pop_back();
            return false;
        }
        stack.pop_back();

        std::map<NodeId, NodeId> idMap;
        NodeId nextId = graph.nextNodeId();
        std::set<NodeId> removedPropertyNodes;
        std::map<std::string, Edge> externalSource;
        std::map<std::string, std::string> promotedRefs;
        std::map<std::string, std::string> promotedDropdowns;
        for (const Dropdown& dropdown : expanded.dropdowns) {
            Dropdown promoted = dropdown;
            promoted.refName = namespacedRef(subNodeId, dropdown.refName);
            while (graph.findDropdown(promoted.refName) != nullptr) {
                promoted.refName += "_";
            }
            const std::string sourceRef = dropdown.refName;
            const std::string targetRef = promoted.refName;
            promoted.name = dropdown.name + " (" + targetRef + ")";
            if (const Value* selection = subGraphDropdownSetting(subNode, sourceRef)) {
                if (const auto index = dropdownSelectionIndex(dropdown, *selection)) {
                    promoted.value = *index;
                } else {
                    addDiagnostic(diagnostics, Severity::Warning, kErrorInvalidValue,
                                  "subgraph dropdown selection for '" + sourceRef +
                                      "' is invalid; using the graph value",
                                  subNodeId);
                }
            }
            std::string addError;
            if (!graph.addDropdown(std::move(promoted), &addError)) {
                addDiagnostic(diagnostics, Severity::Error, kErrorNameConflict, addError, subNodeId);
                return false;
            }
            promotedDropdowns[sourceRef] = targetRef;
        }

        std::vector<const SlotInstance*> inputSlots;
        std::vector<const SlotInstance*> outputSlots;
        for (const SlotInstance& slot : subNode.slots) {
            if (slot.isInput()) {
                inputSlots.push_back(&slot);
            } else {
                outputSlots.push_back(&slot);
            }
        }
        const auto bySlotId = [](const SlotInstance* lhs, const SlotInstance* rhs) {
            return lhs->spec.id < rhs->spec.id;
        };
        std::sort(inputSlots.begin(), inputSlots.end(), bySlotId);
        std::sort(outputSlots.begin(), outputSlots.end(), bySlotId);

        std::set<SlotId> usedInputSlots;
        std::size_t nextInput = 0;
        for (const Property& property : expanded.properties) {
            const SlotInstance* inputSlot = subNode.findSlot(property.name);
            if (inputSlot == nullptr || !inputSlot->isInput()) {
                inputSlot = subNode.findSlot(property.refName);
            }
            if (inputSlot == nullptr || !inputSlot->isInput() ||
                usedInputSlots.contains(inputSlot->spec.id)) {
                while (nextInput < inputSlots.size() &&
                       usedInputSlots.contains(inputSlots[nextInput]->spec.id)) {
                    ++nextInput;
                }
                inputSlot = nextInput < inputSlots.size() ? inputSlots[nextInput] : nullptr;
            }
            if (inputSlot != nullptr && inputSlot->isInput() &&
                usedInputSlots.insert(inputSlot->spec.id).second) {
                if (const Edge* edge = graph.incomingEdge(subNodeId, inputSlot->spec.id)) {
                    externalSource[property.refName] = *edge;
                }
            }
        }
        for (const Property& property : expanded.properties) {
            if (externalSource.contains(property.refName)) {
                continue;
            }
            const std::string ref = namespacedRef(subNodeId, property.refName);
            if (graph.findProperty(ref) != nullptr) {
                addDiagnostic(diagnostics, Severity::Error, kErrorNameConflict,
                              "subgraph property conflicts with parent: " + ref, subNodeId);
                return false;
            }
            promotedRefs[property.refName] = ref;
            Property promoted = property;
            promoted.name = name + " " + property.name;
            promoted.refName = ref;
            std::string addError;
            if (!graph.addProperty(std::move(promoted), &addError)) {
                addDiagnostic(diagnostics, Severity::Error, kErrorNameConflict, addError, subNodeId);
                return false;
            }
        }

        for (const NodeInstance& node : expanded.nodes) {
            if (node.type == kSubGraphOutputType) {
                continue;
            }
            if (node.type == kPropertyType && externalSource.contains(propertyRef(node))) {
                removedPropertyNodes.insert(node.id);
                continue;
            }
            NodeInstance clone = node;
            clone.id = nextId++;
            idMap[node.id] = clone.id;
            if (clone.type == kPropertyType) {
                const std::string ref = propertyRef(clone);
                if (!ref.empty() && promotedRefs.contains(ref)) {
                    clone.settings.erase("property");
                    clone.settings.erase("propertyRef");
                    clone.settings.erase("refName");
                    clone.setSetting("property",
                                     Value::makeString(ValueType::String, promotedRefs[ref]));
                }
            } else if (clone.type == "Dropdown") {
                const std::string ref = dropdownRef(clone);
                if (!ref.empty() && promotedDropdowns.contains(ref)) {
                    clone.settings.erase("dropdown");
                    clone.settings.erase("dropdownRef");
                    clone.settings.erase("refName");
                    clone.setSetting("dropdown", Value::makeString(ValueType::String,
                                                                   promotedDropdowns[ref]));
                }
            }
            std::string addError;
            if (!graph.addNode(std::move(clone), &addError)) {
                addDiagnostic(diagnostics, Severity::Error, kErrorDuplicateId, addError, subNodeId);
                return false;
            }
        }

        for (const Edge& edge : expanded.edges) {
            if (edge.fromNode == edge.toNode && !idMap.contains(edge.fromNode)) {
                continue;
            }
            if (!idMap.contains(edge.fromNode) || !idMap.contains(edge.toNode)) {
                continue;
            }
            if (removedPropertyNodes.contains(edge.fromNode)) {
                continue;
            }
            Edge mapped{idMap[edge.fromNode], edge.fromSlot, idMap[edge.toNode], edge.toSlot};
            std::string addError;
            if (!graph.addEdge(mapped, &addError)) {
                addDiagnostic(diagnostics, Severity::Error, kErrorInvalidEdge, addError,
                              mapped.toNode, mapped.toSlot);
            }
        }

        for (const auto& [propertyRefName, sourceEdge] : externalSource) {
            for (const Edge& consumer : expanded.edges) {
                if (!removedPropertyNodes.contains(consumer.fromNode)) {
                    continue;
                }
                const NodeInstance* propertyNode = expanded.findNode(consumer.fromNode);
                if (propertyNode == nullptr || propertyRef(*propertyNode) != propertyRefName) {
                    continue;
                }
                if (!idMap.contains(consumer.toNode)) {
                    continue;
                }
                Edge mapped{sourceEdge.fromNode, sourceEdge.fromSlot, idMap[consumer.toNode],
                            consumer.toSlot};
                std::string addError;
                if (!graph.addEdge(mapped, &addError)) {
                    addDiagnostic(diagnostics, Severity::Error, kErrorInvalidEdge, addError,
                                  mapped.toNode, mapped.toSlot);
                }
            }
            graph.removeEdge(sourceEdge);
        }

        std::set<SlotId> usedOutputSlots;
        std::size_t nextOutput = 0;
        for (const SubGraphOutput& output : expanded.outputs) {
            const SlotInstance* outputSlot = subNode.findSlot(output.slot);
            if (outputSlot == nullptr || !outputSlot->isOutput()) {
                outputSlot = subNode.findSlot(output.name);
            }
            if (outputSlot == nullptr || !outputSlot->isOutput() ||
                usedOutputSlots.contains(outputSlot->spec.id)) {
                while (nextOutput < outputSlots.size() &&
                       usedOutputSlots.contains(outputSlots[nextOutput]->spec.id)) {
                    ++nextOutput;
                }
                outputSlot = nextOutput < outputSlots.size() ? outputSlots[nextOutput] : nullptr;
            }
            if (outputSlot == nullptr || !outputSlot->isOutput() ||
                !usedOutputSlots.insert(outputSlot->spec.id).second) {
                continue;
            }
            const NodeInstance* outputNode = nullptr;
            SlotId sourceSlot = kInvalidSlot;
            for (const NodeInstance& node : expanded.nodes) {
                if (node.type != kSubGraphOutputType) {
                    continue;
                }
                const SlotInstance* input = node.findSlot(output.slot);
                if (input == nullptr || !input->isInput()) {
                    input = node.findSlot(output.name);
                }
                if (input == nullptr || !input->isInput()) {
                    continue;
                }
                if (const Edge* edge = expanded.incomingEdge(node.id, input->spec.id)) {
                    outputNode = expanded.findNode(edge->fromNode);
                    sourceSlot = edge->fromSlot;
                }
                break;
            }
            if (outputNode == nullptr) {
                continue;
            }

            NodeId sourceNode = kInvalidNode;
            if (removedPropertyNodes.contains(outputNode->id)) {
                const auto external = externalSource.find(propertyRef(*outputNode));
                if (external == externalSource.end()) {
                    continue;
                }
                sourceNode = external->second.fromNode;
                sourceSlot = external->second.fromSlot;
            } else {
                const auto sourceMapped = idMap.find(outputNode->id);
                if (sourceMapped == idMap.end()) {
                    continue;
                }
                sourceNode = sourceMapped->second;
            }

            const std::vector<Edge> consumers = graph.outgoingEdges(subNodeId, outputSlot->spec.id);
            for (const Edge& consumer : consumers) {
                graph.removeEdge(consumer);
                Edge mapped{sourceNode, sourceSlot, consumer.toNode, consumer.toSlot};
                std::string addError;
                if (!graph.addEdge(mapped, &addError)) {
                    addDiagnostic(diagnostics, Severity::Error, kErrorInvalidEdge, addError,
                                  mapped.toNode, mapped.toSlot);
                }
            }
        }

        for (const Edge& edge : graph.incomingEdges(subNodeId)) {
            addDiagnostic(diagnostics, Severity::Warning, kErrorUnknownField,
                          "unmatched SubGraph input is dropped", subNodeId, edge.toSlot);
        }
        graph.removeNode(subNodeId);
    }
}

} // namespace

const Graph* SubGraphLibrary::find(std::string_view name) const {
    const auto found = graphs.find(std::string(name));
    return found == graphs.end() ? nullptr : &found->second;
}

void SubGraphLibrary::add(std::string name, Graph graph) {
    graphs.insert_or_assign(std::move(name), std::move(graph));
}

SubGraphExpansion expandSubgraphs(const Graph& graph, const NodeRegistry& registry,
                                  const SubGraphLibrary& library, int maxDepth) {
    SubGraphExpansion result;
    result.graph = graph;
    std::vector<std::string> stack;
    result.ok = expandGraph(result.graph, registry, library, stack, 0, maxDepth,
                            result.diagnostics, result.error);
    if (!result.ok && result.error.empty()) {
        result.error = "subgraph expansion failed";
    }
    return result;
}

} // namespace sg
