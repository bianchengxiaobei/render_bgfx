#pragma once

#include "sg/types.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sg {

struct SlotSpec {
    SlotId id = kInvalidSlot;
    std::string name;
    std::string displayName;
    SlotDirection direction = SlotDirection::Input;
    ValueType type = ValueType::Float;
    std::optional<Value> defaultValue;
    bool required = false;
    bool hidden = false;
    bool allowConnection = true;
    bool supportsLiteral = false;
    bool requiresLiteral = false;
    bool canPropagateLiteral = false;
    std::vector<ConcreteValueType> allowedConcreteTypes;

    const std::string& label() const { return displayName.empty() ? name : displayName; }
};

struct SlotState {
    SlotMode mode = SlotMode::Default;
    std::optional<Value> value;
    std::optional<ConcreteValueType> concreteType;
};

struct SlotInstance {
    SlotSpec spec;
    SlotState state;
    ConcreteValueType concreteType = ConcreteValueType::Vector4;
    GraphPrecision graphPrecision = GraphPrecision::Single;
    ConcretePrecision concretePrecision = ConcretePrecision::Single;
    bool concreteExplicit = false;
    bool hasError = false;
    bool connected = false;

    bool isInput() const { return spec.direction == SlotDirection::Input; }
    bool isOutput() const { return spec.direction == SlotDirection::Output; }
    bool isDynamic() const { return isDynamicType(spec.type); }
    bool isDynamicVector() const { return spec.type == ValueType::DynamicVector; }
    bool isDynamicMatrix() const { return spec.type == ValueType::DynamicMatrix; }
    const Value* effectiveValue() const;
    bool hasExplicitValue() const;
};

SlotInstance makeSlotInstance(const SlotSpec& spec, const SlotState& state = {});

struct NodeInstance {
    NodeId id = kInvalidNode;
    std::string type;
    int typeVersion = 1;
    Vec2 position;
    Precision precision = Precision::Inherit;
    GraphPrecision graphPrecision = GraphPrecision::Single;
    ConcretePrecision concretePrecision = ConcretePrecision::Single;
    std::map<std::string, Value> settings;
    std::vector<::sg::SlotInstance> slots;
    bool active = true;
    bool unknown = false;
    std::string unknownJson;

    ::sg::SlotInstance* findSlot(SlotId slotId);
    const ::sg::SlotInstance* findSlot(SlotId slotId) const;
    ::sg::SlotInstance* findSlot(std::string_view name);
    const ::sg::SlotInstance* findSlot(std::string_view name) const;
    std::vector<::sg::SlotInstance*> inputSlots();
    std::vector<const ::sg::SlotInstance*> inputSlots() const;
    std::vector<::sg::SlotInstance*> outputSlots();
    std::vector<const ::sg::SlotInstance*> outputSlots() const;
    const Value* setting(std::string_view name) const;
    void setSetting(std::string name, Value value);
};

struct Edge {
    NodeId fromNode = kInvalidNode;
    SlotId fromSlot = kInvalidSlot;
    NodeId toNode = kInvalidNode;
    SlotId toSlot = kInvalidSlot;

    bool operator==(const Edge& other) const = default;
};

struct Property {
    std::string name;
    std::string refName;
    ValueType type = ValueType::Float;
    Precision precision = Precision::Inherit;
    bool exposed = true;
    Value defaultValue;
    std::string category;
    std::string floatMode = "default";
    std::vector<std::string> enumNames;
    std::vector<int> enumValues;
    std::vector<std::string> attributes;
    std::string samplerFilter = "linear";
    std::string samplerWrap = "repeat";
    std::string samplerAnisotropic = "none";
    std::string defaultType;
    bool isHDR = false;
    bool useTilingAndOffset = false;
    Vec2 tiling{1.0f, 1.0f};
    Vec2 offset{0.0f, 0.0f};
    int vtLayers = 2;
    std::vector<std::string> vtLayerRefNames;
    std::vector<std::string> vtLayerTextureTypes;
};

struct KeywordEntry {
    int id = 0;
    std::string displayName;
    std::string referenceName;
};

struct Keyword {
    std::string name;
    std::string refName;
    KeywordType type = KeywordType::Boolean;
    KeywordDefinition definition = KeywordDefinition::ShaderFeature;
    KeywordScope scope = KeywordScope::Local;
    int defaultValue = 0;
    std::string category;
    std::vector<KeywordEntry> entries;
};

struct DropdownEntry {
    int id = 0;
    std::string displayName;
};

struct Dropdown {
    std::string name;
    std::string refName;
    int value = 0;
    std::string category;
    std::vector<DropdownEntry> entries;
};

struct Category {
    std::string name;
    std::vector<std::string> children;
};

struct Target {
    std::string id;
    std::string subTarget;
    std::map<std::string, Value> settings;
};

struct Group {
    int id = 0;
    std::string title;
    Vec2 position;
    Vec2 size{300.0f, 200.0f};
    Vec4 color{0.2f, 0.3f, 0.4f, 1.0f};
    std::vector<NodeId> nodes;
};

struct Note {
    int id = 0;
    std::string title;
    Vec2 position;
    Vec2 size{200.0f, 120.0f};
    std::string text;
    std::string theme = "classic";
};

struct SubGraphOutput {
    std::string name;
    ValueType type = ValueType::Float;
    SlotId slot = kInvalidSlot;
};

struct Graph {
    static constexpr int kCurrentSchemaVersion = 1;

    int schemaVersion = kCurrentSchemaVersion;
    GraphKind kind = GraphKind::Graph;
    std::string name;
    std::string generatorVersion;
    Target target;
    GraphPrecision defaultGraphPrecision = GraphPrecision::Single;
    ConcretePrecision defaultPrecision = ConcretePrecision::Single;
    std::vector<Property> properties;
    std::vector<Keyword> keywords;
    std::vector<Dropdown> dropdowns;
    std::vector<Category> categories;
    std::vector<NodeInstance> nodes;
    std::vector<Edge> edges;
    std::vector<Group> groups;
    std::vector<Note> notes;
    std::vector<SubGraphOutput> outputs;

    NodeInstance* findNode(NodeId id);
    const NodeInstance* findNode(NodeId id) const;
    SlotInstance* findSlot(NodeId nodeId, SlotId slotId);
    const SlotInstance* findSlot(NodeId nodeId, SlotId slotId) const;
    const Edge* incomingEdge(NodeId nodeId, SlotId slotId) const;
    Edge* incomingEdge(NodeId nodeId, SlotId slotId);
    std::vector<Edge> outgoingEdges(NodeId nodeId, SlotId slotId) const;
    std::vector<Edge> incomingEdges(NodeId nodeId) const;
    const Property* findProperty(std::string_view refName) const;
    Property* findProperty(std::string_view refName);
    const Keyword* findKeyword(std::string_view refName) const;
    Keyword* findKeyword(std::string_view refName);
    const Dropdown* findDropdown(std::string_view refName) const;
    Dropdown* findDropdown(std::string_view refName);
    const Category* findCategory(std::string_view name) const;
    Category* findCategory(std::string_view name);
    NodeId nextNodeId() const;
    int nextGroupId() const;
    int nextNoteId() const;
    bool addNode(NodeInstance node, std::string* error = nullptr);
    bool removeNode(NodeId id, std::string* error = nullptr);
    bool addEdge(Edge edge, std::string* error = nullptr);
    bool removeEdge(const Edge& edge);
    bool addProperty(Property property, std::string* error = nullptr);
    bool removeProperty(std::string_view refName);
    bool addKeyword(Keyword keyword, std::string* error = nullptr);
    bool removeKeyword(std::string_view refName);
    bool addDropdown(Dropdown dropdown, std::string* error = nullptr);
    bool removeDropdown(std::string_view refName);
    bool setSlotValue(NodeId nodeId, SlotId slotId, SlotMode mode, std::optional<Value> value,
                      std::string* error = nullptr);
    bool setSlotConcreteType(NodeId nodeId, SlotId slotId, ConcreteValueType type,
                             std::string* error = nullptr);
};

} // namespace sg
