#include "sg/validate.h"

#include "sg/concretize.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace sg {
namespace {

Diagnostic makeDiagnostic(Severity severity, const char* code, std::string message,
                          NodeId node = kInvalidNode, SlotId slot = kInvalidSlot) {
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.code = code;
    diagnostic.message = std::move(message);
    diagnostic.node = node;
    diagnostic.slot = slot;
    return diagnostic;
}

void addUnique(std::vector<Diagnostic>& diagnostics, Diagnostic diagnostic) {
    if (std::find(diagnostics.begin(), diagnostics.end(), diagnostic) == diagnostics.end()) {
        diagnostics.push_back(std::move(diagnostic));
    }
}

bool cycleVisit(const Graph& graph, NodeId id, std::map<NodeId, int>& colors,
                std::vector<Diagnostic>& diagnostics) {
    int& color = colors[id];
    if (color == 1) {
        addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorCycle,
                                              "graph contains a cycle", id, kInvalidSlot));
        return false;
    }
    if (color == 2) {
        return true;
    }
    color = 1;
    bool ok = true;
    for (const Edge& edge : graph.incomingEdges(id)) {
        const NodeInstance* source = graph.findNode(edge.fromNode);
        if (source != nullptr) {
            ok = cycleVisit(graph, source->id, colors, diagnostics) && ok;
        }
    }
    color = 2;
    return ok;
}

std::uint64_t variantCount(const Graph& graph) {
    std::uint64_t count = 1;
    for (const Keyword& keyword : graph.keywords) {
        std::uint64_t factor = 2;
        if (keyword.type == KeywordType::Enum) {
            factor = std::max<std::uint64_t>(1, keyword.entries.size());
        }
        if (count > 64 || factor > 64 / count) {
            return 65;
        }
        count *= factor;
    }
    return count;
}

const char* hlslReserved[] = {
    "AppendStructuredBuffer", "BlendState", "Buffer", "ByteAddressBuffer", "CompileShader",
    "ComputeShader", "ConsumeStructuredBuffer", "DepthStencilState", "DepthStencilView",
    "GeometryShader", "InputPatch", "LineStream", "OutputPatch", "PixelShader",
    "PointStream", "RasterizerState", "RenderTargetView", "RWBuffer", "RWByteAddressBuffer",
    "RWStructuredBuffer", "RWTexture1D", "RWTexture1DArray", "RWTexture2D", "RWTexture2DArray",
    "RWTexture3D", "SamplerState", "StructuredBuffer", "Texture1D", "Texture1DArray",
    "Texture2D", "Texture2DArray", "Texture2DMS", "Texture2DMSArray", "Texture3D",
    "TextureCube", "TextureCubeArray", "TriangleStream", "VertexShader", "asm", "bool",
    "break", "case", "cbuffer", "centroid", "class", "column_major", "const", "continue",
    "default", "discard", "do", "double", "else", "export", "extern", "false", "float",
    "for", "fxgroup", "half", "if", "in", "inline", "inout", "int", "interface", "line",
    "lineadj", "linear", "matrix", "namespace", "nointerpolation", "noperspective", "NULL",
    "out", "packoffset", "pass", "pixelfragment", "point", "precise", "return", "register",
    "row_major", "sample", "sampler", "shared", "snorm", "stateblock", "stateblock_state",
    "static", "string", "struct", "switch", "tbuffer", "technique", "technique10",
    "technique11", "texture", "true", "typedef", "triangle", "triangleadj", "uint",
    "uniform", "unorm", "unsigned", "vector", "vertexfragment", "void", "volatile", "while",
};

bool isReserved(std::string_view name) {
    for (const char* item : hlslReserved) {
        if (name == item) {
            return true;
        }
    }
    return false;
}

void validateNames(const Graph& graph, std::vector<Diagnostic>& diagnostics) {
    std::set<std::string> names;
    std::set<std::string> refs;
    for (const Property& property : graph.properties) {
        if (!names.insert(property.name).second) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorNameConflict,
                                                  "duplicate property name: " + property.name));
        }
        if (!refs.insert(property.refName).second) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorNameConflict,
                                                  "duplicate property refName: " + property.refName));
        }
        if (!isIdentifier(property.refName) || isReserved(property.refName)) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorNameConflict,
                                                  "invalid property refName: " + property.refName));
        }
        if (!property.defaultType.empty()) {
            const bool textureProperty =
                property.type == ValueType::Texture2D ||
                property.type == ValueType::Texture2DArray ||
                property.type == ValueType::Texture3D ||
                property.type == ValueType::Cubemap;
            if (!textureProperty ||
                !textureDefaultTypeFromString(property.defaultType)) {
                addUnique(diagnostics,
                          makeDiagnostic(Severity::Error, kErrorInvalidValue,
                                         "invalid texture defaultType: " +
                                             property.defaultType));
            }
        }
        if (property.type == ValueType::VirtualTexture) {
            if (!property.vtLayerRefNames.empty() &&
                property.vtLayerRefNames.size() != static_cast<std::size_t>(property.vtLayers)) {
                addUnique(diagnostics,
                          makeDiagnostic(Severity::Error, kErrorInvalidValue,
                                         "virtual texture layer refName count must match vtLayers"));
            }
            if (!property.vtLayerTextureTypes.empty() &&
                property.vtLayerTextureTypes.size() != static_cast<std::size_t>(property.vtLayers)) {
                addUnique(diagnostics,
                          makeDiagnostic(Severity::Error, kErrorInvalidValue,
                                         "virtual texture layer texture type count must match vtLayers"));
            }
            for (const std::string& layerRef : property.vtLayerRefNames) {
                if (!refs.insert(layerRef).second) {
                    addUnique(diagnostics,
                              makeDiagnostic(Severity::Error, kErrorNameConflict,
                                             "duplicate virtual texture layer refName: " + layerRef));
                }
                if (!isIdentifier(layerRef) || isReserved(layerRef)) {
                    addUnique(diagnostics,
                              makeDiagnostic(Severity::Error, kErrorNameConflict,
                                             "invalid virtual texture layer refName: " + layerRef));
                }
            }
            for (const std::string& layerType : property.vtLayerTextureTypes) {
                if (!virtualTextureLayerTextureTypeFromString(layerType)) {
                    addUnique(diagnostics,
                              makeDiagnostic(Severity::Error, kErrorInvalidValue,
                                             "invalid virtual texture layer texture type: " +
                                                 layerType));
                }
            }
        }
    }
    for (const Keyword& keyword : graph.keywords) {
        if (!names.insert(keyword.name).second) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorNameConflict,
                                                  "duplicate keyword name: " + keyword.name));
        }
        if (!refs.insert(keyword.refName).second) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorNameConflict,
                                                  "duplicate keyword refName: " + keyword.refName));
        }
        if (!isIdentifier(keyword.refName) || isReserved(keyword.refName)) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorNameConflict,
                                                  "invalid keyword refName: " + keyword.refName));
        }
        std::set<int> entryIds;
        for (const KeywordEntry& entry : keyword.entries) {
            if (!entryIds.insert(entry.id).second) {
                addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorDuplicateId,
                                                      "duplicate keyword entry id"));
            }
            if (!isIdentifier(entry.referenceName) || isReserved(entry.referenceName)) {
                addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorNameConflict,
                                                      "invalid keyword entry referenceName: " +
                                                          entry.referenceName));
            }
        }
    }
    for (const Dropdown& dropdown : graph.dropdowns) {
        if (!names.insert(dropdown.name).second) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorNameConflict,
                                                  "duplicate dropdown name: " + dropdown.name));
        }
        if (!refs.insert(dropdown.refName).second) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorNameConflict,
                                                  "duplicate dropdown refName: " + dropdown.refName));
        }
        if (!isIdentifier(dropdown.refName) || isReserved(dropdown.refName)) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorNameConflict,
                                                  "invalid dropdown refName: " + dropdown.refName));
        }
        if (dropdown.entries.empty()) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorInvalidValue,
                                                  "dropdown must have at least one entry"));
        } else if (dropdown.entries.size() > 16) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorInvalidValue,
                                                  "dropdown has more than 16 entries"));
        }
        std::set<int> entryIds;
        for (const DropdownEntry& entry : dropdown.entries) {
            if (entry.id <= 0 || entry.id > 16) {
                addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorInvalidValue,
                                                      "dropdown entry id must be in 1..16"));
            }
            if (!entryIds.insert(entry.id).second) {
                addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorDuplicateId,
                                                      "duplicate dropdown entry id"));
            }
        }
    }
}

void validateNode(const Graph& graph, const NodeRegistry& registry, NodeInstance& node,
                  std::vector<Diagnostic>& diagnostics) {
    std::set<SlotId> slotIds;
    for (const SlotInstance& slot : node.slots) {
        if (!slotIds.insert(slot.spec.id).second) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorDuplicateId,
                                                  "duplicate slot id", node.id, slot.spec.id));
        }
    }

    const NodeDef* definition = registry.find(node.type);
    if (definition == nullptr || node.unknown) {
        addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorUnknownNode,
                                              "unknown node type: " + node.type, node.id));
        node.unknown = true;
        return;
    }

    if (!definition->allowInMainGraph && graph.kind == GraphKind::Graph) {
        addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorTargetUnsupported,
                                              "node is not allowed in main graphs", node.id));
    }
    if (!definition->allowInSubGraph && graph.kind == GraphKind::SubGraph) {
        addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorTargetUnsupported,
                                              "node is not allowed in subgraphs", node.id));
    }
    if (node.type == "Dropdown") {
        std::string ref;
        for (const char* key : {"dropdown", "dropdownRef", "refName"}) {
            const Value* value = node.setting(key);
            if (value != nullptr) {
                if (const std::string* text = value->asString(); text != nullptr && !text->empty()) {
                    ref = *text;
                    break;
                }
            }
        }
        if (ref.empty() || graph.findDropdown(ref) == nullptr) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorMissingInput,
                                                  "Dropdown node does not reference a graph dropdown",
                                                  node.id));
        }
    }
    if (node.typeVersion > definition->typeVersion) {
        addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorUnknownNode,
                                              "node type version is newer than registry", node.id));
    }
    if (!definition->allowedTargets.empty()) {
        if (std::find(definition->allowedTargets.begin(), definition->allowedTargets.end(),
                      graph.target.id) == definition->allowedTargets.end()) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorTargetUnsupported,
                                                  "node is not supported by target " + graph.target.id,
                                                  node.id));
        }
    }

    for (SlotInstance& slot : node.slots) {
        if (slot.state.mode == SlotMode::Literal) {
            if (!slot.state.value) {
                addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorInvalidValue,
                                                      "literal slot has no value", node.id, slot.spec.id));
                slot.hasError = true;
            } else if (const auto concrete = concreteTypeOf(slot.state.value->type)) {
                if (!isCompatible(slot.spec.type, *concrete)) {
                    addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorTypeMismatch,
                                                          "literal value type does not match slot", node.id,
                                                          slot.spec.id));
                    slot.hasError = true;
                }
            } else if (slot.state.value->type != ValueType::Dynamic &&
                       slot.state.value->type != ValueType::DynamicVector &&
                       slot.state.value->type != ValueType::DynamicMatrix) {
                addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorInvalidValue,
                                                      "unsupported literal value type", node.id,
                                                      slot.spec.id));
                slot.hasError = true;
            }
        }
        if (slot.isInput()) {
            const Edge* edge = graph.incomingEdge(node.id, slot.spec.id);
            if (slot.spec.required && edge == nullptr && !slot.state.value.has_value() &&
                !slot.spec.defaultValue.has_value()) {
                addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorMissingInput,
                                                      "required input is not connected", node.id,
                                                      slot.spec.id));
                slot.hasError = true;
            }
        }
    }
}

void validateEdges(const Graph& graph, std::vector<Diagnostic>& diagnostics) {
    std::set<std::pair<NodeId, SlotId>> inputs;
    for (const Edge& edge : graph.edges) {
        const NodeInstance* fromNode = graph.findNode(edge.fromNode);
        const NodeInstance* toNode = graph.findNode(edge.toNode);
        if (fromNode == nullptr || toNode == nullptr) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorInvalidEdge,
                                                  "edge endpoint node does not exist", edge.toNode,
                                                  edge.toSlot));
            continue;
        }
        const SlotInstance* fromSlot = fromNode->findSlot(edge.fromSlot);
        const SlotInstance* toSlot = toNode->findSlot(edge.toSlot);
        if (fromSlot == nullptr || toSlot == nullptr) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorInvalidEdge,
                                                  "edge endpoint slot does not exist", edge.toNode,
                                                  edge.toSlot));
            continue;
        }
        if (!fromSlot->isOutput() || !toSlot->isInput() || !fromSlot->spec.allowConnection ||
            !toSlot->spec.allowConnection) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorInvalidEdge,
                                                  "edge has invalid slot direction or connection flag",
                                                  edge.toNode, edge.toSlot));
            continue;
        }
        if (edge.fromNode == edge.toNode) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorCycle,
                                                  "self edge is not allowed", edge.toNode, edge.toSlot));
        }
        if (!inputs.insert({edge.toNode, edge.toSlot}).second) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorInvalidEdge,
                                                  "input slot has multiple edges", edge.toNode,
                                                  edge.toSlot));
        }
        if (fromSlot->isDynamic() && fromSlot->concreteType == ConcreteValueType::External) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorTypeUnresolved,
                                                  "source slot has unresolved type", edge.fromNode,
                                                  edge.fromSlot));
        }
        if (!toSlot->spec.allowedConcreteTypes.empty() &&
            std::find(toSlot->spec.allowedConcreteTypes.begin(),
                      toSlot->spec.allowedConcreteTypes.end(),
                      fromSlot->concreteType) == toSlot->spec.allowedConcreteTypes.end()) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorTypeMismatch,
                                                  "source type is not allowed by slot", edge.toNode,
                                                  edge.toSlot));
        }
        if (toSlot->spec.requiresLiteral && !fromSlot->spec.supportsLiteral) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorTypeMismatch,
                                                  "literal input requires a literal source",
                                                  edge.toNode, edge.toSlot));
        }
        if (!isCompatible(toSlot->spec.type, fromSlot->concreteType, false)) {
            addUnique(diagnostics, makeDiagnostic(Severity::Error, kErrorTypeMismatch,
                                                  std::string("cannot connect ") +
                                                      toString(fromSlot->concreteType) + " to " +
                                                      toString(toSlot->spec.type),
                                                  edge.toNode, edge.toSlot));
        }
    }
}

} // namespace

bool hasError(const std::vector<Diagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& diagnostic) {
        return diagnostic.severity == Severity::Error;
    });
}

ValidationResult validate(Graph& graph, const NodeRegistry& registry, bool runConcretize) {
    ValidationResult result;
    if (runConcretize) {
        ConcretizeResult concrete = concretize(graph, registry);
        result.diagnostics.insert(result.diagnostics.end(), concrete.diagnostics.begin(),
                                  concrete.diagnostics.end());
    }

    std::map<NodeId, NodeId> nodeIds;
    for (const NodeInstance& node : graph.nodes) {
        if (!nodeIds.emplace(node.id, node.id).second) {
            addUnique(result.diagnostics, makeDiagnostic(Severity::Error, kErrorDuplicateId,
                                                         "duplicate node id", node.id));
        }
    }
    for (NodeInstance& node : graph.nodes) {
        validateNode(graph, registry, node, result.diagnostics);
    }
    validateNames(graph, result.diagnostics);
    validateEdges(graph, result.diagnostics);

    std::map<NodeId, int> colors;
    for (const NodeInstance& node : graph.nodes) {
        cycleVisit(graph, node.id, colors, result.diagnostics);
    }

    if (variantCount(graph) > 64) {
        addUnique(result.diagnostics, makeDiagnostic(Severity::Error, kErrorKeywordLimit,
                                                      "graph exceeds the 64 variant limit"));
    }
    if (graph.kind == GraphKind::SubGraph && graph.outputs.empty()) {
        addUnique(result.diagnostics, makeDiagnostic(Severity::Error, kErrorMissingInput,
                                                      "subgraph must declare at least one output"));
    }

    std::sort(result.diagnostics.begin(), result.diagnostics.end(), [](const Diagnostic& lhs,
                                                                        const Diagnostic& rhs) {
        if (lhs.node != rhs.node) {
            return lhs.node < rhs.node;
        }
        if (lhs.slot != rhs.slot) {
            return lhs.slot < rhs.slot;
        }
        if (lhs.code != rhs.code) {
            return lhs.code < rhs.code;
        }
        return lhs.message < rhs.message;
    });
    result.ok = !hasError(result.diagnostics);
    return result;
}

} // namespace sg
