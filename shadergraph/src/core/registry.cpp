#include "sg/registry.h"

#include "value_json.h"

#include <algorithm>
#include <set>

namespace sg {

bool NodeRegistry::add(NodeDef definition, Diagnostic* diagnostic) {
    const auto fail = [diagnostic](std::string message) {
        if (diagnostic != nullptr) {
            diagnostic->severity = Severity::Error;
            diagnostic->code = kErrorDuplicateType;
            diagnostic->message = std::move(message);
        }
        return false;
    };
    if (definition.type.empty()) {
        return fail("node type must not be empty");
    }
    if (definitions_.find(definition.type) != definitions_.end()) {
        return fail("duplicate node type: " + definition.type);
    }
    std::set<SlotId> ids;
    for (const SlotSpec& slot : definition.slots) {
        if (slot.id < 0 || !ids.insert(slot.id).second) {
            return fail("duplicate or invalid slot id on node type: " + definition.type);
        }
    }
    const std::string type = definition.type;
    definitions_.insert_or_assign(type, std::move(definition));
    return true;
}

const NodeDef* NodeRegistry::find(std::string_view type) const {
    const auto found = definitions_.find(std::string(type));
    return found == definitions_.end() ? nullptr : &found->second;
}

NodeInstance NodeRegistry::instantiate(const NodeDef& definition, NodeId id, Vec2 position) const {
    NodeInstance node;
    node.id = id;
    node.type = definition.type;
    node.typeVersion = definition.typeVersion;
    node.position = position;
    node.precision = definition.defaultPrecision;
    for (const SlotSpec& spec : definition.slots) {
        node.slots.push_back(makeSlotInstance(spec));
    }
    return node;
}

NodeInstance NodeRegistry::instantiate(std::string_view type, NodeId id, Vec2 position) const {
    if (const NodeDef* definition = find(type)) {
        return instantiate(*definition, id, position);
    }
    return makeUnknown(id, std::string(type));
}

NodeInstance NodeRegistry::makeUnknown(NodeId id, std::string type, std::string serializedJson) const {
    NodeInstance node;
    node.id = id;
    node.type = std::move(type);
    node.unknown = true;
    node.unknownJson = std::move(serializedJson);
    node.active = false;
    return node;
}

namespace {

std::string child(std::string_view base, std::string_view name) {
    if (base.empty()) {
        return std::string(name);
    }
    return std::string(base) + "." + std::string(name);
}

bool readString(const detail::Json& object, const char* key, std::string& value, std::string& error) {
    if (!object.contains(key)) {
        return true;
    }
    if (!object[key].is_string()) {
        error = std::string("field must be a string: ") + key;
        return false;
    }
    value = object[key].get<std::string>();
    return true;
}

bool readBool(const detail::Json& object, const char* key, bool& value, std::string& error) {
    if (!object.contains(key)) {
        return true;
    }
    if (!object[key].is_boolean()) {
        error = std::string("field must be a boolean: ") + key;
        return false;
    }
    value = object[key].get<bool>();
    return true;
}

bool readInt(const detail::Json& object, const char* key, int& value, std::string& error) {
    if (!object.contains(key)) {
        return true;
    }
    if (!object[key].is_number_integer()) {
        error = std::string("field must be an integer: ") + key;
        return false;
    }
    value = object[key].get<int>();
    return true;
}

bool parseSlot(const detail::Json& json, std::size_t index, std::string_view parent,
               SlotSpec& slot, std::string& error) {
    if (!json.is_object()) {
        error = child(parent, "slots[" + std::to_string(index) + "]") + " must be an object";
        return false;
    }
    slot.id = static_cast<SlotId>(index);
    if (!readInt(json, "id", slot.id, error)) {
        return false;
    }
    if (!readString(json, "name", slot.name, error) ||
        !readString(json, "displayName", slot.displayName, error)) {
        return false;
    }
    slot.displayName = slot.displayName.empty() ? slot.name : slot.displayName;
    std::string direction = toString(slot.direction);
    if (!readString(json, "dir", direction, error)) {
        return false;
    }
    if (const auto parsed = slotDirectionFromString(direction)) {
        slot.direction = *parsed;
    } else {
        error = "invalid slot direction: " + direction;
        return false;
    }
    std::string type = toString(slot.type);
    if (!readString(json, "type", type, error)) {
        return false;
    }
    if (const auto parsed = valueTypeFromString(type)) {
        slot.type = *parsed;
    } else {
        error = "invalid slot type: " + type;
        return false;
    }
    if (!readBool(json, "required", slot.required, error) ||
        !readBool(json, "hidden", slot.hidden, error) ||
        !readBool(json, "allowConnection", slot.allowConnection, error) ||
        !readBool(json, "supportsLiteral", slot.supportsLiteral, error) ||
        !readBool(json, "requiresLiteral", slot.requiresLiteral, error) ||
        !readBool(json, "canPropagateLiteral", slot.canPropagateLiteral, error)) {
        return false;
    }
    if (json.contains("default") || json.contains("defaultValue")) {
        const char* key = json.contains("default") ? "default" : "defaultValue";
        std::string valueError;
        auto value = detail::parseValue(json[key], slot.type, &valueError);
        if (!value) {
            error = "invalid default value for slot " + std::to_string(slot.id) + ": " + valueError;
            return false;
        }
        slot.defaultValue = std::move(*value);
    }
    if (json.contains("allowed")) {
        if (!json["allowed"].is_array()) {
            error = "slot.allowed must be an array";
            return false;
        }
        for (const auto& item : json["allowed"]) {
            if (!item.is_string()) {
                error = "slot.allowed entries must be strings";
                return false;
            }
            const auto typeValue = concreteTypeFromString(item.get<std::string>());
            if (!typeValue) {
                error = "unknown allowed concrete type: " + item.get<std::string>();
                return false;
            }
            slot.allowedConcreteTypes.push_back(*typeValue);
        }
    }
    return true;
}

bool parseDefinition(const detail::Json& json, NodeDef& definition, std::string& error) {
    if (!json.is_object()) {
        error = "node spec must be an object";
        return false;
    }
    if (!readString(json, "type", definition.type, error) ||
        !readString(json, "title", definition.title, error) ||
        !readString(json, "category", definition.category, error)) {
        return false;
    }
    if (definition.type.empty()) {
        error = "node spec type must not be empty";
        return false;
    }
    if (!readInt(json, "typeVersion", definition.typeVersion, error)) {
        return false;
    }
    if (json.contains("synonyms")) {
        if (!json["synonyms"].is_array()) {
            error = "synonyms must be an array";
            return false;
        }
        for (const auto& item : json["synonyms"]) {
            if (!item.is_string()) {
                error = "synonyms must contain strings";
                return false;
            }
            definition.synonyms.push_back(item.get<std::string>());
        }
    }
    if (json.contains("settings")) {
        if (!json["settings"].is_array()) {
            error = "settings must be an array";
            return false;
        }
        for (const auto& item : json["settings"]) {
            if (item.is_string()) {
                definition.settings.push_back(item.get<std::string>());
            } else if (item.is_object() && item.contains("name") && item["name"].is_string()) {
                definition.settings.push_back(item["name"].get<std::string>());
            } else {
                error = "settings entries must be strings or objects with a name";
                return false;
            }
        }
    }
    if (json.contains("allowedTargets")) {
        if (!json["allowedTargets"].is_array()) {
            error = "allowedTargets must be an array";
            return false;
        }
        for (const auto& item : json["allowedTargets"]) {
            if (!item.is_string()) {
                error = "allowedTargets must contain strings";
                return false;
            }
            definition.allowedTargets.push_back(item.get<std::string>());
        }
    }
    if (json.contains("emit")) {
        if (!json["emit"].is_object()) {
            error = "emit must be an object";
            return false;
        }
        if (!readString(json["emit"], "body", definition.emitBody, error) ||
            !readString(json["emit"], "function", definition.emitFunction, error) ||
            !readString(json["emit"], "functionName", definition.emitFunctionName, error)) {
            return false;
        }
    }
    if (json.contains("requirements")) {
        if (!json["requirements"].is_array()) {
            error = "requirements must be an array";
            return false;
        }
        for (const auto& item : json["requirements"]) {
            if (!item.is_string()) {
                error = "requirements must contain strings";
                return false;
            }
            definition.requirements.push_back(item.get<std::string>());
        }
    }
    if (json.contains("block")) {
        if (json["block"].is_string()) {
            definition.blockField = json["block"].get<std::string>();
            definition.blockNode = true;
        } else {
            error = "block must be a string field name";
            return false;
        }
    }
    if (!readBool(json, "hasPreview", definition.hasPreview, error) ||
        !readBool(json, "isTargetSpecific", definition.isTargetSpecific, error) ||
        !readBool(json, "allowInSubGraph", definition.allowInSubGraph, error) ||
        !readBool(json, "allowInMainGraph", definition.allowInMainGraph, error) ||
        !readBool(json, "canPropagateFloatLiteral", definition.canPropagateFloatLiteral, error)) {
        return false;
    }
    std::string precision = toString(definition.defaultPrecision);
    if (!readString(json, "precision", precision, error)) {
        return false;
    }
    if (const auto parsed = precisionFromString(precision)) {
        definition.defaultPrecision = *parsed;
    } else {
        error = "invalid default precision: " + precision;
        return false;
    }
    if (json.contains("slots")) {
        if (!json["slots"].is_array()) {
            error = "slots must be an array";
            return false;
        }
        std::size_t index = 0;
        for (const auto& item : json["slots"]) {
            SlotSpec slot;
            if (!parseSlot(item, index, definition.type, slot, error)) {
                return false;
            }
            definition.slots.push_back(std::move(slot));
            ++index;
        }
    }
    return true;
}

} // namespace

bool NodeRegistry::loadSpecs(std::string_view json, std::vector<Diagnostic>& diagnostics) {
    detail::Json root;
    try {
        root = detail::Json::parse(json.begin(), json.end());
    } catch (const std::exception& exception) {
        Diagnostic diagnostic;
        diagnostic.severity = Severity::Error;
        diagnostic.code = kErrorInvalidJson;
        diagnostic.message = exception.what();
        diagnostics.push_back(std::move(diagnostic));
        return false;
    }

    try {
    std::vector<const detail::Json*> entries;
    if (root.is_array()) {
        for (const auto& item : root) {
            entries.push_back(&item);
        }
    } else if (root.is_object() && root.contains("nodes") && root["nodes"].is_array()) {
        for (const auto& item : root["nodes"]) {
            entries.push_back(&item);
        }
    } else {
        entries.push_back(&root);
    }

    bool ok = true;
    for (const detail::Json* entry : entries) {
        NodeDef definition;
        std::string error;
        if (!parseDefinition(*entry, definition, error)) {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorInvalidValue;
            diagnostic.message = error;
            diagnostics.push_back(std::move(diagnostic));
            ok = false;
            continue;
        }
        Diagnostic diagnostic;
        if (!add(std::move(definition), &diagnostic)) {
            diagnostics.push_back(std::move(diagnostic));
            ok = false;
        }
    }
    return ok;
    } catch (const std::exception& exception) {
        Diagnostic diagnostic;
        diagnostic.severity = Severity::Error;
        diagnostic.code = kErrorInvalidValue;
        diagnostic.message = exception.what();
        diagnostics.push_back(std::move(diagnostic));
        return false;
    }
}

} // namespace sg
