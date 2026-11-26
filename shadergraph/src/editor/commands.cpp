#include "editor_internal.h"

#include "sg/validate.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace sg::editor {
namespace {

inline constexpr const char* kEditorNotFound = "SG_NOT_FOUND";

bool fail(CommandResult& result, std::string_view code, std::string message) {
    result.ok = false;
    result.code = std::string(code);
    result.message = std::move(message);
    return false;
}

bool getString(const Json& object, const char* key, std::string& value, CommandResult& result,
               bool required = true) {
    if (!object.contains(key)) {
        if (required) {
            return fail(result, kErrorInvalidJson, std::string("missing field: ") + key);
        }
        return true;
    }
    if (!object[key].is_string()) {
        return fail(result, kErrorInvalidJson, std::string("field must be a string: ") + key);
    }
    value = object[key].get<std::string>();
    return true;
}

bool getInteger(const Json& object, const char* key, int& value, CommandResult& result,
                bool required = true) {
    if (!object.contains(key)) {
        if (required) {
            return fail(result, kErrorInvalidJson, std::string("missing field: ") + key);
        }
        return true;
    }
    if (!object[key].is_number_integer() && !object[key].is_number_unsigned()) {
        return fail(result, kErrorInvalidJson, std::string("field must be an integer: ") + key);
    }
    value = object[key].get<int>();
    return true;
}

bool getFloat(const Json& object, const char* key, float& value, CommandResult& result,
              bool required = true) {
    if (!object.contains(key)) {
        if (required) {
            return fail(result, kErrorInvalidJson, std::string("missing field: ") + key);
        }
        return true;
    }
    if (!object[key].is_number()) {
        return fail(result, kErrorInvalidJson, std::string("field must be a number: ") + key);
    }
    value = object[key].get<float>();
    return true;
}

bool getVec2(const Json& object, const char* key, Vec2& value, CommandResult& result,
             bool required = true) {
    if (!object.contains(key)) {
        if (required) {
            return fail(result, kErrorInvalidJson, std::string("missing field: ") + key);
        }
        return true;
    }
    const Json& array = object[key];
    if (!array.is_array() || array.size() != 2 || !array[0].is_number() || !array[1].is_number()) {
        return fail(result, kErrorInvalidJson, std::string("field must be a two-number array: ") + key);
    }
    value = {array[0].get<float>(), array[1].get<float>()};
    return true;
}

bool getColor(const Json& object, const char* key, Vec4& value, CommandResult& result) {
    if (!object.contains(key)) {
        return true;
    }
    const Json& array = object[key];
    if (!array.is_array() || array.size() != 4) {
        return fail(result, kErrorInvalidJson, std::string("field must be a four-number array: ") + key);
    }
    for (const Json& item : array) {
        if (!item.is_number()) {
            return fail(result, kErrorInvalidJson, std::string("field must be a four-number array: ") + key);
        }
    }
    value = {array[0].get<float>(), array[1].get<float>(), array[2].get<float>(),
             array[3].get<float>()};
    return true;
}

bool readNodeId(const Json& object, const char* key, NodeId& value, CommandResult& result) {
    int parsed = 0;
    if (!getInteger(object, key, parsed, result)) {
        return false;
    }
    if (parsed < 0) {
        return fail(result, kErrorInvalidValue, std::string("node id must be non-negative: ") + key);
    }
    value = static_cast<NodeId>(parsed);
    return true;
}

bool readSlotId(const Json& object, const char* key, SlotId& value, CommandResult& result) {
    int parsed = 0;
    if (!getInteger(object, key, parsed, result)) {
        return false;
    }
    if (parsed < 0) {
        return fail(result, kErrorInvalidValue, std::string("slot id must be non-negative: ") + key);
    }
    value = static_cast<SlotId>(parsed);
    return true;
}

struct Endpoint {
    NodeId node = kInvalidNode;
    SlotId slot = kInvalidSlot;
};

bool readEndpoint(const Json& object, const char* key, Endpoint& endpoint, CommandResult& result) {
    if (!object.contains(key) || !object[key].is_object()) {
        return fail(result, kErrorInvalidJson, std::string("missing endpoint object: ") + key);
    }
    return readNodeId(object[key], "node", endpoint.node, result) &&
           readSlotId(object[key], "slot", endpoint.slot, result);
}

NodeInstance* findNode(DocumentState& state, NodeId id, CommandResult& result) {
    NodeInstance* node = state.graph.findNode(id);
    if (node == nullptr) {
        fail(result, kEditorNotFound, "node not found: " + std::to_string(id));
    }
    return node;
}

SlotInstance* findSlot(NodeInstance& node, SlotId id, CommandResult& result) {
    SlotInstance* slot = node.findSlot(id);
    if (slot == nullptr) {
        fail(result, kEditorNotFound, "slot not found: " + std::to_string(id));
    }
    return slot;
}

bool hasIncoming(const Graph& graph, NodeId node, SlotId slot) {
    return graph.incomingEdge(node, slot) != nullptr;
}

bool reaches(const Graph& graph, NodeId from, NodeId target) {
    std::vector<NodeId> stack{from};
    std::set<NodeId> visited;
    while (!stack.empty()) {
        const NodeId node = stack.back();
        stack.pop_back();
        if (node == target) {
            return true;
        }
        if (!visited.insert(node).second) {
            continue;
        }
        for (const Edge& edge : graph.edges) {
            if (edge.fromNode == node) {
                stack.push_back(edge.toNode);
            }
        }
    }
    return false;
}

std::optional<Value> defaultValueFor(ValueType type) {
    switch (type) {
    case ValueType::Float:
        return Value::makeFloat(0.0f);
    case ValueType::Integer:
        return Value::makeInteger(0);
    case ValueType::Boolean:
        return Value::makeBool(false);
    case ValueType::Vector2:
        return Value::makeVector2({0.0f, 0.0f});
    case ValueType::Vector3:
        return Value::makeVector3({0.0f, 0.0f, 0.0f});
    case ValueType::Vector4:
        return Value::makeVector4({0.0f, 0.0f, 0.0f, 0.0f});
    case ValueType::Color:
        return Value::makeColor({1.0f, 1.0f, 1.0f, 1.0f});
    case ValueType::Matrix2:
        return Value::makeMatrix2(Mat2::identity());
    case ValueType::Matrix3:
        return Value::makeMatrix3(Mat3::identity());
    case ValueType::Matrix4:
        return Value::makeMatrix4(Mat4::identity());
    case ValueType::Gradient:
        return Value::makeGradient(Gradient{});
    case ValueType::Texture2D:
    case ValueType::Texture2DArray:
    case ValueType::Texture3D:
    case ValueType::Cubemap:
    case ValueType::VirtualTexture:
    case ValueType::SamplerState:
    case ValueType::String:
        return Value::makeString(type, {});
    default:
        return std::nullopt;
    }
}

bool normalizeRefName(std::string name, bool prefixUnderscore, std::string& result,
                      CommandResult& commandResult) {
    if (name.empty()) {
        return fail(commandResult, kErrorInvalidValue, "name must not be empty");
    }
    for (char& character : name) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (!(std::isalnum(value) || character == '_')) {
            character = '_';
        }
    }
    if (prefixUnderscore && !name.empty() && name.front() != '_') {
        name.insert(name.begin(), '_');
    }
    if (!isIdentifier(name)) {
        return fail(commandResult, kErrorInvalidValue, "name is not a valid identifier");
    }
    result = std::move(name);
    return true;
}

bool parseValue(const Json& value, ValueType type, Value& result, CommandResult& commandResult) {
    std::string error;
    auto parsed = detail::parseValue(value, type, &error);
    if (!parsed) {
        return fail(commandResult, kErrorTypeMismatch, "invalid value: " + error);
    }
    result = std::move(*parsed);
    return true;
}

bool nodeTargetAllowed(const Graph& graph, const NodeDef& definition) {
    if (definition.allowedTargets.empty()) {
        return true;
    }
    return std::find(definition.allowedTargets.begin(), definition.allowedTargets.end(),
                     graph.target.id) != definition.allowedTargets.end();
}

bool applyNodeSettings(NodeInstance& node, const Json& settings, CommandResult& result) {
    if (!settings.is_object()) {
        return fail(result, kErrorInvalidJson, "settings must be an object");
    }
    for (const auto& [key, value] : settings.items()) {
        if (key == "precision") {
            if (!value.is_string()) {
                return fail(result, kErrorInvalidJson, "node precision must be a string");
            }
            const auto parsed = precisionFromString(value.get<std::string>());
            if (!parsed) {
                return fail(result, kErrorInvalidValue, "unknown node precision");
            }
            node.precision = *parsed;
            continue;
        }
        const Value* existing = node.setting(key);
        std::string error;
        auto parsed = detail::parseValue(value, existing == nullptr ? std::nullopt : std::optional<ValueType>{existing->type}, &error);
        if (!parsed) {
            return fail(result, kErrorTypeMismatch, "invalid node setting " + key + ": " + error);
        }
        node.settings.insert_or_assign(key, std::move(*parsed));
    }
    return true;
}

void eraseLayoutNode(DocumentState& state, NodeId id) {
    state.layout.nodes.erase(std::remove_if(state.layout.nodes.begin(), state.layout.nodes.end(),
                                            [id](const NodeLayout& node) { return node.id == id; }),
                             state.layout.nodes.end());
}

void eraseLayoutGroup(DocumentState& state, int id) {
    state.layout.groups.erase(std::remove_if(state.layout.groups.begin(), state.layout.groups.end(),
                                             [id](const GroupLayout& group) { return group.id == id; }),
                              state.layout.groups.end());
}

void eraseLayoutNote(DocumentState& state, int id) {
    state.layout.notes.erase(std::remove_if(state.layout.notes.begin(), state.layout.notes.end(),
                                            [id](const NoteLayout& note) { return note.id == id; }),
                             state.layout.notes.end());
}

std::string propertyRef(const NodeInstance& node) {
    for (const char* key : {"property", "propertyRef", "refName"}) {
        if (const Value* value = node.setting(key); value != nullptr) {
            if (const std::string* text = value->asString(); text != nullptr && !text->empty()) {
                return *text;
            }
        }
    }
    return {};
}

bool applyOne(DocumentState& state, const Json& command, CommandResult& result, bool& structural,
              bool& layoutChanged, Json& extra, std::set<NodeId>& affected);

} // namespace

#include "editor_commands.inl"

bool applyCommands(DocumentState& state, std::string_view requestJson, CommandResult& result) {
    Json request;
    try {
        request = Json::parse(requestJson.begin(), requestJson.end());
    } catch (const std::exception& exception) {
        return fail(result, kErrorInvalidJson, exception.what());
    }
    if (!request.is_object()) {
        return fail(result, kErrorInvalidJson, "request must be an object");
    }
    if (request.contains("baseRevision")) {
        int baseRevision = 0;
        if (!getInteger(request, "baseRevision", baseRevision, result)) {
            return false;
        }
        if (baseRevision < 0 || static_cast<std::uint64_t>(baseRevision) != state.revision) {
            result.code = "SG_STALE_REVISION";
            result.message = "baseRevision does not match the current revision";
            return false;
        }
    }
    if (!request.contains("commands") || !request["commands"].is_array()) {
        return fail(result, kErrorInvalidJson, "commands must be an array");
    }
    const Json& commands = request["commands"];
    if (commands.size() > 512) {
        return fail(result, kErrorInvalidValue, "command batch exceeds 512 commands");
    }
    bool atomic = true;
    if (request.contains("atomic")) {
        if (!request["atomic"].is_boolean()) {
            return fail(result, kErrorInvalidJson, "atomic must be a boolean");
        }
        atomic = request["atomic"].get<bool>();
    }

    Snapshot backup;
    backup.graph = state.graph;
    backup.layout = state.layout;
    const bool dirtyStructural = state.structuralDirty;
    const bool dirtyLayout = state.layoutDirty;

    int applied = 0;
    bool structural = false;
    bool layoutChanged = false;
    Json extra = Json::object();
    std::set<NodeId> affected;
    for (const Json& command : commands) {
        if (!command.is_object()) {
            if (atomic) {
                state.graph = backup.graph;
                state.layout = backup.layout;
                state.structuralDirty = dirtyStructural;
                state.layoutDirty = dirtyLayout;
                validateState(state);
                result.applied = 0;
            }
            return fail(result, kErrorInvalidJson, "command must be an object");
        }
        bool commandStructural = false;
        bool commandLayout = false;
        Json commandExtra = Json::object();
        std::set<NodeId> commandAffected;
        if (!applyOne(state, command, result, commandStructural, commandLayout, commandExtra,
                      commandAffected)) {
            if (atomic) {
                state.graph = backup.graph;
                state.layout = backup.layout;
                state.structuralDirty = dirtyStructural;
                state.layoutDirty = dirtyLayout;
                validateState(state);
                result.applied = 0;
                result.diagnostics = state.diagnostics;
            } else {
                if (structural) {
                    if (state.history.size() >= 200) {
                        state.history.erase(state.history.begin());
                    }
                    state.history.push_back(std::move(backup));
                    state.redo.clear();
                    state.structuralDirty = true;
                }
                if (layoutChanged) {
                    state.layoutDirty = true;
                }
                if (structural || layoutChanged) {
                    ++state.revision;
                    state.layout.revision = state.revision;
                    validateState(state);
                }
                result.applied = applied;
                result.diagnostics = state.diagnostics;
            }
            return false;
        }
        structural = structural || commandStructural;
        layoutChanged = layoutChanged || commandLayout;
        affected.insert(commandAffected.begin(), commandAffected.end());
        for (const auto& [key, value] : commandExtra.items()) {
            extra[key] = value;
        }
        ++applied;
    }

    if (structural) {
        if (state.history.size() >= 200) {
            state.history.erase(state.history.begin());
        }
        state.history.push_back(std::move(backup));
        state.redo.clear();
        state.structuralDirty = true;
    }
    if (layoutChanged) {
        state.layoutDirty = true;
    }
    if (structural || layoutChanged) {
        ++state.revision;
        state.layout.revision = state.revision;
        validateState(state);
    }

    Json data;
    data["applied"] = applied;
    data["dirty"] = state.structuralDirty || state.layoutDirty;
    data["affected"] = Json::array();
    for (NodeId id : affected) {
        data["affected"].push_back(id);
    }
    data["diagnostics"] = Json::parse(serializeDiagnostics(state.diagnostics));
    for (const auto& [key, value] : extra.items()) {
        data[key] = value;
    }
    result.ok = true;
    result.code.clear();
    result.message.clear();
    result.applied = applied;
    result.diagnostics = state.diagnostics;
    result.dataJson = data.dump();
    return true;
}

} // namespace sg::editor
