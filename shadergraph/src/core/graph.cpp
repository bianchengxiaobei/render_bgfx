#include "sg/graph.h"

#include <algorithm>
#include <limits>
#include <set>

namespace sg {
namespace {

ConcreteValueType defaultConcrete(ValueType type) {
    if (const auto concrete = concreteTypeOf(type)) {
        return *concrete;
    }
    switch (type) {
    case ValueType::DynamicVector:
        return ConcreteValueType::Float;
    case ValueType::DynamicMatrix:
        return ConcreteValueType::Matrix2;
    case ValueType::Dynamic:
        return ConcreteValueType::Vector4;
    default:
        return ConcreteValueType::Vector4;
    }
}

std::string makeError(std::string message) {
    return message;
}

} // namespace

SlotInstance makeSlotInstance(const SlotSpec& spec, const SlotState& state) {
    SlotInstance slot;
    slot.spec = spec;
    slot.state = state;
    if (spec.type == ValueType::DynamicVector) {
        slot.concreteType = state.concreteType.value_or(ConcreteValueType::Float);
        slot.concreteExplicit = state.concreteType.has_value();
    } else if (spec.type == ValueType::DynamicMatrix) {
        slot.concreteType = state.concreteType.value_or(ConcreteValueType::Matrix2);
        slot.concreteExplicit = state.concreteType.has_value();
    } else if (spec.type == ValueType::Dynamic) {
        slot.concreteType = state.concreteType.value_or(ConcreteValueType::Vector4);
        slot.concreteExplicit = state.concreteType.has_value();
    } else {
        slot.concreteType = concreteTypeOf(spec.type).value_or(ConcreteValueType::Vector4);
    }
    return slot;
}

const Value* SlotInstance::effectiveValue() const {
    if (state.value) {
        return &*state.value;
    }
    return spec.defaultValue ? &*spec.defaultValue : nullptr;
}

bool SlotInstance::hasExplicitValue() const {
    return state.mode == SlotMode::Literal && state.value.has_value();
}

SlotInstance* NodeInstance::findSlot(SlotId slotId) {
    const auto found = std::find_if(slots.begin(), slots.end(), [slotId](const SlotInstance& slot) {
        return slot.spec.id == slotId;
    });
    return found == slots.end() ? nullptr : &*found;
}

const SlotInstance* NodeInstance::findSlot(SlotId slotId) const {
    const auto found = std::find_if(slots.begin(), slots.end(), [slotId](const SlotInstance& slot) {
        return slot.spec.id == slotId;
    });
    return found == slots.end() ? nullptr : &*found;
}

SlotInstance* NodeInstance::findSlot(std::string_view name) {
    const auto found = std::find_if(slots.begin(), slots.end(), [name](const SlotInstance& slot) {
        return slot.spec.name == name;
    });
    return found == slots.end() ? nullptr : &*found;
}

const SlotInstance* NodeInstance::findSlot(std::string_view name) const {
    const auto found = std::find_if(slots.begin(), slots.end(), [name](const SlotInstance& slot) {
        return slot.spec.name == name;
    });
    return found == slots.end() ? nullptr : &*found;
}

std::vector<SlotInstance*> NodeInstance::inputSlots() {
    std::vector<SlotInstance*> result;
    for (SlotInstance& slot : slots) {
        if (slot.isInput()) {
            result.push_back(&slot);
        }
    }
    return result;
}

std::vector<const SlotInstance*> NodeInstance::inputSlots() const {
    std::vector<const SlotInstance*> result;
    for (const SlotInstance& slot : slots) {
        if (slot.isInput()) {
            result.push_back(&slot);
        }
    }
    return result;
}

std::vector<SlotInstance*> NodeInstance::outputSlots() {
    std::vector<SlotInstance*> result;
    for (SlotInstance& slot : slots) {
        if (slot.isOutput()) {
            result.push_back(&slot);
        }
    }
    return result;
}

std::vector<const SlotInstance*> NodeInstance::outputSlots() const {
    std::vector<const SlotInstance*> result;
    for (const SlotInstance& slot : slots) {
        if (slot.isOutput()) {
            result.push_back(&slot);
        }
    }
    return result;
}

const Value* NodeInstance::setting(std::string_view name) const {
    const auto found = settings.find(std::string(name));
    return found == settings.end() ? nullptr : &found->second;
}

void NodeInstance::setSetting(std::string name, Value value) {
    settings.insert_or_assign(std::move(name), std::move(value));
}

NodeInstance* Graph::findNode(NodeId id) {
    const auto found = std::find_if(nodes.begin(), nodes.end(), [id](const NodeInstance& node) {
        return node.id == id;
    });
    return found == nodes.end() ? nullptr : &*found;
}

const NodeInstance* Graph::findNode(NodeId id) const {
    const auto found = std::find_if(nodes.begin(), nodes.end(), [id](const NodeInstance& node) {
        return node.id == id;
    });
    return found == nodes.end() ? nullptr : &*found;
}

SlotInstance* Graph::findSlot(NodeId nodeId, SlotId slotId) {
    NodeInstance* node = findNode(nodeId);
    return node == nullptr ? nullptr : node->findSlot(slotId);
}

const SlotInstance* Graph::findSlot(NodeId nodeId, SlotId slotId) const {
    const NodeInstance* node = findNode(nodeId);
    return node == nullptr ? nullptr : node->findSlot(slotId);
}

const Edge* Graph::incomingEdge(NodeId nodeId, SlotId slotId) const {
    const auto found = std::find_if(edges.begin(), edges.end(), [nodeId, slotId](const Edge& edge) {
        return edge.toNode == nodeId && edge.toSlot == slotId;
    });
    return found == edges.end() ? nullptr : &*found;
}

Edge* Graph::incomingEdge(NodeId nodeId, SlotId slotId) {
    const auto found = std::find_if(edges.begin(), edges.end(), [nodeId, slotId](const Edge& edge) {
        return edge.toNode == nodeId && edge.toSlot == slotId;
    });
    return found == edges.end() ? nullptr : &*found;
}

std::vector<Edge> Graph::outgoingEdges(NodeId nodeId, SlotId slotId) const {
    std::vector<Edge> result;
    for (const Edge& edge : edges) {
        if (edge.fromNode == nodeId && edge.fromSlot == slotId) {
            result.push_back(edge);
        }
    }
    return result;
}

std::vector<Edge> Graph::incomingEdges(NodeId nodeId) const {
    std::vector<Edge> result;
    for (const Edge& edge : edges) {
        if (edge.toNode == nodeId) {
            result.push_back(edge);
        }
    }
    return result;
}

const Property* Graph::findProperty(std::string_view refName) const {
    const auto found = std::find_if(properties.begin(), properties.end(), [refName](const Property& property) {
        return property.refName == refName;
    });
    return found == properties.end() ? nullptr : &*found;
}

Property* Graph::findProperty(std::string_view refName) {
    const auto found = std::find_if(properties.begin(), properties.end(), [refName](const Property& property) {
        return property.refName == refName;
    });
    return found == properties.end() ? nullptr : &*found;
}

const Keyword* Graph::findKeyword(std::string_view refName) const {
    const auto found = std::find_if(keywords.begin(), keywords.end(), [refName](const Keyword& keyword) {
        return keyword.refName == refName;
    });
    return found == keywords.end() ? nullptr : &*found;
}

Keyword* Graph::findKeyword(std::string_view refName) {
    const auto found = std::find_if(keywords.begin(), keywords.end(), [refName](const Keyword& keyword) {
        return keyword.refName == refName;
    });
    return found == keywords.end() ? nullptr : &*found;
}

const Dropdown* Graph::findDropdown(std::string_view refName) const {
    const auto found = std::find_if(dropdowns.begin(), dropdowns.end(), [refName](const Dropdown& dropdown) {
        return dropdown.refName == refName;
    });
    return found == dropdowns.end() ? nullptr : &*found;
}

Dropdown* Graph::findDropdown(std::string_view refName) {
    const auto found = std::find_if(dropdowns.begin(), dropdowns.end(), [refName](const Dropdown& dropdown) {
        return dropdown.refName == refName;
    });
    return found == dropdowns.end() ? nullptr : &*found;
}

const Category* Graph::findCategory(std::string_view categoryName) const {
    const auto found = std::find_if(categories.begin(), categories.end(), [categoryName](const Category& category) {
        return category.name == categoryName;
    });
    return found == categories.end() ? nullptr : &*found;
}

Category* Graph::findCategory(std::string_view categoryName) {
    const auto found = std::find_if(categories.begin(), categories.end(), [categoryName](const Category& category) {
        return category.name == categoryName;
    });
    return found == categories.end() ? nullptr : &*found;
}

NodeId Graph::nextNodeId() const {
    NodeId result = 0;
    for (const NodeInstance& node : nodes) {
        if (node.id >= result) {
            result = node.id + 1;
        }
    }
    return result;
}

int Graph::nextGroupId() const {
    int result = 0;
    for (const Group& group : groups) {
        if (group.id >= result) {
            result = group.id + 1;
        }
    }
    return result;
}

int Graph::nextNoteId() const {
    int result = 0;
    for (const Note& note : notes) {
        if (note.id >= result) {
            result = note.id + 1;
        }
    }
    return result;
}

bool Graph::addNode(NodeInstance node, std::string* error) {
    if (node.id < 0) {
        if (error != nullptr) {
            *error = makeError("node id must be non-negative");
        }
        return false;
    }
    if (findNode(node.id) != nullptr) {
        if (error != nullptr) {
            *error = makeError("duplicate node id");
        }
        return false;
    }
    std::set<SlotId> slotIds;
    for (SlotInstance& slot : node.slots) {
        if (slot.spec.id < 0 || !slotIds.insert(slot.spec.id).second) {
            if (error != nullptr) {
                *error = makeError("duplicate or invalid slot id");
            }
            return false;
        }
        if (slot.spec.type == ValueType::Dynamic) {
            if (slot.state.concreteType) {
                slot.concreteType = *slot.state.concreteType;
                slot.concreteExplicit = true;
            } else {
                slot.concreteType = defaultConcrete(slot.spec.type);
            }
        } else if (slot.spec.type == ValueType::DynamicVector) {
            slot.concreteType = slot.state.concreteType.value_or(ConcreteValueType::Float);
            slot.concreteExplicit = slot.state.concreteType.has_value();
        } else if (slot.spec.type == ValueType::DynamicMatrix) {
            slot.concreteType = slot.state.concreteType.value_or(ConcreteValueType::Matrix2);
            slot.concreteExplicit = slot.state.concreteType.has_value();
        } else {
            slot.concreteType = concreteTypeOf(slot.spec.type).value_or(ConcreteValueType::Vector4);
        }
    }
    nodes.push_back(std::move(node));
    return true;
}

bool Graph::removeNode(NodeId id, std::string* error) {
    const auto found = std::find_if(nodes.begin(), nodes.end(), [id](const NodeInstance& node) {
        return node.id == id;
    });
    if (found == nodes.end()) {
        if (error != nullptr) {
            *error = makeError("node not found");
        }
        return false;
    }
    edges.erase(std::remove_if(edges.begin(), edges.end(), [id](const Edge& edge) {
                    return edge.fromNode == id || edge.toNode == id;
                }),
                edges.end());
    nodes.erase(found);
    for (NodeInstance& node : nodes) {
        for (SlotInstance& slot : node.slots) {
            slot.connected = slot.isInput() && incomingEdge(node.id, slot.spec.id) != nullptr;
        }
    }
    for (Group& group : groups) {
        group.nodes.erase(std::remove(group.nodes.begin(), group.nodes.end(), id), group.nodes.end());
    }
    return true;
}

bool Graph::addEdge(Edge edge, std::string* error) {
    const NodeInstance* fromNode = findNode(edge.fromNode);
    const NodeInstance* toNode = findNode(edge.toNode);
    if (fromNode == nullptr || toNode == nullptr) {
        if (error != nullptr) {
            *error = makeError("edge endpoint node not found");
        }
        return false;
    }
    const SlotInstance* fromSlot = fromNode->findSlot(edge.fromSlot);
    const SlotInstance* toSlot = toNode->findSlot(edge.toSlot);
    if (fromSlot == nullptr || toSlot == nullptr) {
        if (error != nullptr) {
            *error = makeError("edge endpoint slot not found");
        }
        return false;
    }
    if (!fromSlot->isOutput() || !toSlot->isInput()) {
        if (error != nullptr) {
            *error = makeError("edge must connect output to input");
        }
        return false;
    }
    if (!fromSlot->spec.allowConnection || !toSlot->spec.allowConnection) {
        if (error != nullptr) {
            *error = makeError("slot does not allow connections");
        }
        return false;
    }
    if (edge.fromNode == edge.toNode) {
        if (error != nullptr) {
            *error = makeError("self edges are not allowed");
        }
        return false;
    }
    if (incomingEdge(edge.toNode, edge.toSlot) != nullptr) {
        if (error != nullptr) {
            *error = makeError("input slot already connected");
        }
        return false;
    }
    const auto duplicate = std::find(edges.begin(), edges.end(), edge);
    if (duplicate != edges.end()) {
        if (error != nullptr) {
            *error = makeError("edge already exists");
        }
        return false;
    }
    edges.push_back(edge);
    NodeInstance* mutableTo = findNode(edge.toNode);
    if (mutableTo != nullptr) {
        SlotInstance* slot = mutableTo->findSlot(edge.toSlot);
        if (slot != nullptr) {
            slot->connected = true;
            slot->state.mode = SlotMode::Connected;
        }
    }
    return true;
}

bool Graph::removeEdge(const Edge& edge) {
    const auto found = std::find(edges.begin(), edges.end(), edge);
    if (found == edges.end()) {
        return false;
    }
    edges.erase(found);
    NodeInstance* node = findNode(edge.toNode);
    if (node != nullptr) {
        SlotInstance* slot = node->findSlot(edge.toSlot);
        if (slot != nullptr) {
            slot->connected = incomingEdge(edge.toNode, edge.toSlot) != nullptr;
            if (!slot->connected) {
                slot->state.mode = slot->state.value ? SlotMode::Literal : SlotMode::Default;
            }
        }
    }
    return true;
}

bool Graph::addProperty(Property property, std::string* error) {
    if (property.refName.empty() || findProperty(property.refName) != nullptr) {
        if (error != nullptr) {
            *error = makeError("duplicate or empty property refName");
        }
        return false;
    }
    properties.push_back(std::move(property));
    return true;
}

bool Graph::removeProperty(std::string_view refName) {
    const auto found = std::find_if(properties.begin(), properties.end(), [refName](const Property& property) {
        return property.refName == refName;
    });
    if (found == properties.end()) {
        return false;
    }
    properties.erase(found);
    return true;
}

bool Graph::addKeyword(Keyword keyword, std::string* error) {
    if (keyword.refName.empty() || findKeyword(keyword.refName) != nullptr) {
        if (error != nullptr) {
            *error = makeError("duplicate or empty keyword refName");
        }
        return false;
    }
    keywords.push_back(std::move(keyword));
    return true;
}

bool Graph::removeKeyword(std::string_view refName) {
    const auto found = std::find_if(keywords.begin(), keywords.end(), [refName](const Keyword& keyword) {
        return keyword.refName == refName;
    });
    if (found == keywords.end()) {
        return false;
    }
    keywords.erase(found);
    return true;
}

bool Graph::addDropdown(Dropdown dropdown, std::string* error) {
    if (dropdown.refName.empty() || findDropdown(dropdown.refName) != nullptr) {
        if (error != nullptr) {
            *error = makeError("duplicate or empty dropdown refName");
        }
        return false;
    }
    dropdowns.push_back(std::move(dropdown));
    return true;
}

bool Graph::removeDropdown(std::string_view refName) {
    const auto found = std::find_if(dropdowns.begin(), dropdowns.end(), [refName](const Dropdown& dropdown) {
        return dropdown.refName == refName;
    });
    if (found == dropdowns.end()) {
        return false;
    }
    dropdowns.erase(found);
    return true;
}

bool Graph::setSlotValue(NodeId nodeId, SlotId slotId, SlotMode mode, std::optional<Value> value,
                         std::string* error) {
    SlotInstance* slot = findSlot(nodeId, slotId);
    if (slot == nullptr) {
        if (error != nullptr) {
            *error = makeError("slot not found");
        }
        return false;
    }
    if (mode == SlotMode::Literal && !value.has_value()) {
        if (error != nullptr) {
            *error = makeError("literal slot requires a value");
        }
        return false;
    }
    slot->state.mode = mode;
    slot->state.value = std::move(value);
    return true;
}

bool Graph::setSlotConcreteType(NodeId nodeId, SlotId slotId, ConcreteValueType type,
                                std::string* error) {
    SlotInstance* slot = findSlot(nodeId, slotId);
    if (slot == nullptr) {
        if (error != nullptr) {
            *error = makeError("slot not found");
        }
        return false;
    }
    if (!slot->isDynamic()) {
        if (error != nullptr) {
            *error = makeError("slot is not dynamic");
        }
        return false;
    }
    const bool matrix = slot->isDynamicMatrix();
    const bool dynamic = slot->spec.type == ValueType::Dynamic;
    const bool validMatrix = (matrix || dynamic) && (type == ConcreteValueType::Matrix2 ||
                                                     type == ConcreteValueType::Matrix3 ||
                                                     type == ConcreteValueType::Matrix4);
    const bool validVector = !matrix && type != ConcreteValueType::Matrix2 &&
                             type != ConcreteValueType::Matrix3 &&
                             type != ConcreteValueType::Matrix4;
    if (!validMatrix && !validVector) {
        if (error != nullptr) {
            *error = makeError("concrete type is not valid for the dynamic slot");
        }
        return false;
    }
    slot->state.concreteType = type;
    slot->concreteType = type;
    slot->concreteExplicit = true;
    return true;
}

} // namespace sg
