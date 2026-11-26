#pragma once

#include "sg/graph.h"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace sg {

struct NodeDef {
    std::string type;
    int typeVersion = 1;
    std::string title;
    std::string category;
    std::vector<std::string> synonyms;
    std::vector<SlotSpec> slots;
    std::vector<std::string> settings;
    std::vector<std::string> allowedTargets;
    Precision defaultPrecision = Precision::Inherit;
    bool hasPreview = true;
    bool isTargetSpecific = false;
    bool allowInSubGraph = true;
    bool allowInMainGraph = true;
    bool canPropagateFloatLiteral = false;

    std::string emitBody;
    std::string emitFunction;
    std::string emitFunctionName;
    std::string blockField;
    std::vector<std::string> requirements;
    bool blockNode = false;

    std::function<bool(NodeInstance&, int, std::string&)> migrate;
    std::function<bool(Graph&, NodeInstance&)> updateSlots;
};

class NodeRegistry {
public:
    bool add(NodeDef definition, Diagnostic* diagnostic = nullptr);
    const NodeDef* find(std::string_view type) const;
    NodeInstance instantiate(std::string_view type, NodeId id, Vec2 position = {}) const;
    NodeInstance instantiate(const NodeDef& definition, NodeId id, Vec2 position = {}) const;
    NodeInstance makeUnknown(NodeId id, std::string type, std::string serializedJson = {}) const;
    bool loadSpecs(std::string_view json, std::vector<Diagnostic>& diagnostics);
    std::size_t size() const noexcept { return definitions_.size(); }
    const std::map<std::string, NodeDef>& definitions() const noexcept { return definitions_; }

private:
    std::map<std::string, NodeDef> definitions_;
};

} // namespace sg
