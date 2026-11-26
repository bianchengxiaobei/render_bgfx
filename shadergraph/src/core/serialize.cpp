#include "sg/serialize.h"

#include "sg/validate.h"

#include "migrate.h"
#include "value_json.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

namespace sg {
namespace {

using detail::Json;

std::string dumpJson(const Json& json, bool pretty) {
    return pretty ? json.dump(2) : json.dump();
}

std::optional<Value> defaultForType(ValueType type) {
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

Json encodeTarget(const Target& target, GraphPrecision defaultGraphPrecision,
                  ConcretePrecision defaultPrecision) {
    Json result;
    result["id"] = target.id;
    result["subTarget"] = target.subTarget;
    std::map<std::string, Value> values = target.settings;
    if (!values.contains("precision")) {
        if (defaultGraphPrecision == GraphPrecision::Half ||
            defaultPrecision == ConcretePrecision::Half) {
            values["precision"] = Value::makeString(ValueType::String, "half");
        } else if (defaultGraphPrecision == GraphPrecision::Graph) {
            values["precision"] = Value::makeString(ValueType::String, "graph");
        }
    }
    Json settings = Json::object();
    for (const auto& [name, value] : values) {
        settings[name] = detail::encodeValue(value);
    }
    result["settings"] = std::move(settings);
    return result;
}

Json encodeProperty(const Property& property) {
    Json result;
    result["name"] = property.name;
    result["refName"] = property.refName;
    result["type"] = toString(property.type);
    result["precision"] = toString(property.precision);
    result["exposed"] = property.exposed;
    result["default"] = detail::encodeValue(property.defaultValue);
    if (!property.category.empty()) {
        result["category"] = property.category;
    }
    if (property.floatMode != "default") {
        result["floatMode"] = property.floatMode;
    }
    if (!property.enumNames.empty()) {
        result["enumNames"] = property.enumNames;
        result["enumValues"] = property.enumValues;
    }
    if (!property.attributes.empty()) {
        result["attributes"] = property.attributes;
    }
    if (property.type == ValueType::VirtualTexture) {
        result["vtLayers"] = property.vtLayers;
        if (!property.vtLayerRefNames.empty()) {
            result["vtLayerRefNames"] = property.vtLayerRefNames;
        }
        if (!property.vtLayerTextureTypes.empty()) {
            std::vector<std::string> layerTypes;
            layerTypes.reserve(property.vtLayerTextureTypes.size());
            for (const std::string& type : property.vtLayerTextureTypes) {
                layerTypes.push_back(
                    virtualTextureLayerTextureTypeFromString(type).value_or(type));
            }
            result["vtLayerTextureTypes"] = std::move(layerTypes);
        }
    }
    if (isTextureType(property.type) || property.type == ValueType::SamplerState) {
        Json sampler = {{"filter", property.samplerFilter}, {"wrap", property.samplerWrap}};
        if (!property.samplerAnisotropic.empty() && property.samplerAnisotropic != "none") {
            sampler["anisotropic"] = property.samplerAnisotropic;
        }
        result["sampler"] = std::move(sampler);
        if (isTextureType(property.type) && !property.defaultType.empty()) {
            result["defaultType"] = property.defaultType;
        }
        if (isTextureType(property.type) && property.isHDR) {
            result["isHDR"] = true;
        }
        if (property.useTilingAndOffset) {
            result["useTilingAndOffset"] = true;
            result["tiling"] = {property.tiling.x, property.tiling.y};
            result["offset"] = {property.offset.x, property.offset.y};
        }
    }
    return result;
}

Json encodeKeyword(const Keyword& keyword) {
    Json result;
    result["name"] = keyword.name;
    result["refName"] = keyword.refName;
    result["type"] = toString(keyword.type);
    result["definition"] = toString(keyword.definition);
    result["scope"] = toString(keyword.scope);
    result["default"] = keyword.defaultValue;
    if (!keyword.category.empty()) {
        result["category"] = keyword.category;
    }
    if (!keyword.entries.empty()) {
        result["entries"] = Json::array();
        std::vector<KeywordEntry> entries = keyword.entries;
        std::sort(entries.begin(), entries.end(), [](const KeywordEntry& lhs, const KeywordEntry& rhs) {
            return lhs.id < rhs.id;
        });
        for (const KeywordEntry& entry : entries) {
            result["entries"].push_back(
                {{"id", entry.id}, {"displayName", entry.displayName}, {"referenceName", entry.referenceName}});
        }
    }
    return result;
}

Json encodeDropdown(const Dropdown& dropdown) {
    Json result;
    result["name"] = dropdown.name;
    result["refName"] = dropdown.refName;
    result["value"] = dropdown.value;
    if (!dropdown.category.empty()) {
        result["category"] = dropdown.category;
    }
    result["entries"] = Json::array();
    std::vector<DropdownEntry> entries = dropdown.entries;
    std::sort(entries.begin(), entries.end(), [](const DropdownEntry& lhs, const DropdownEntry& rhs) {
        return lhs.id < rhs.id;
    });
    for (const DropdownEntry& entry : entries) {
        result["entries"].push_back({{"id", entry.id}, {"displayName", entry.displayName}});
    }
    return result;
}

Json encodeNode(const NodeInstance& node, const SerializeOptions& options) {
    Json result;
    result["id"] = node.id;
    result["type"] = node.type;
    result["typeVersion"] = node.typeVersion;
    if (options.includePositions) {
        result["position"] = Json::array({node.position.x, node.position.y});
    }
    Json settings = Json::object();
    if (node.precision != Precision::Inherit) {
        settings["precision"] = toString(node.precision);
    }
    for (const auto& [name, value] : node.settings) {
        if (name == "precision") {
            continue;
        }
        settings[name] = detail::encodeValue(value);
    }
    result["settings"] = std::move(settings);
    Json slots = Json::object();
    std::vector<const SlotInstance*> sortedSlots;
    for (const SlotInstance& slot : node.slots) {
        sortedSlots.push_back(&slot);
    }
    std::sort(sortedSlots.begin(), sortedSlots.end(), [](const SlotInstance* lhs, const SlotInstance* rhs) {
        return lhs->spec.id < rhs->spec.id;
    });
    for (const SlotInstance* slot : sortedSlots) {
        Json state;
        state["mode"] = toString(slot->state.mode);
        if (slot->state.value) {
            state["value"] = detail::encodeValue(*slot->state.value);
        }
        if (slot->state.concreteType) {
            state["concreteType"] = toString(*slot->state.concreteType);
        }
        slots[std::to_string(slot->spec.id)] = std::move(state);
    }
    result["slots"] = std::move(slots);
    if (!node.unknownJson.empty()) {
        result["unknownData"] = node.unknownJson;
    }
    return result;
}

Json encodeGroup(const Group& group, bool includeLayout) {
    Json result;
    result["id"] = group.id;
    result["title"] = group.title;
    if (includeLayout) {
        result["position"] = Json::array({group.position.x, group.position.y});
        result["size"] = Json::array({group.size.x, group.size.y});
    }
    result["color"] = Json::array({group.color.x, group.color.y, group.color.z, group.color.w});
    std::vector<NodeId> nodes = group.nodes;
    std::sort(nodes.begin(), nodes.end());
    result["nodes"] = nodes;
    return result;
}

Json encodeNote(const Note& note, bool includeLayout) {
    Json result;
    result["id"] = note.id;
    result["title"] = note.title;
    if (includeLayout) {
        result["position"] = Json::array({note.position.x, note.position.y});
        result["size"] = Json::array({note.size.x, note.size.y});
    }
    result["text"] = note.text;
    result["theme"] = note.theme;
    return result;
}

Json encodeGraph(const Graph& graph, const SerializeOptions& options) {
    Json root;
    root["schemaVersion"] = graph.schemaVersion;
    root["kind"] = toString(graph.kind);
    root["name"] = graph.name;
    if (options.includeGeneratorVersion && !graph.generatorVersion.empty()) {
        root["generatorVersion"] = graph.generatorVersion;
    }
    if (graph.kind == GraphKind::Graph || !graph.target.id.empty()) {
        root["target"] = encodeTarget(graph.target, graph.defaultGraphPrecision, graph.defaultPrecision);
    }

    root["properties"] = Json::array();
    std::vector<const Property*> properties;
    for (const Property& property : graph.properties) {
        properties.push_back(&property);
    }
    std::sort(properties.begin(), properties.end(), [](const Property* lhs, const Property* rhs) {
        return lhs->refName < rhs->refName;
    });
    for (const Property* property : properties) {
        root["properties"].push_back(encodeProperty(*property));
    }

    root["keywords"] = Json::array();
    std::vector<const Keyword*> keywords;
    for (const Keyword& keyword : graph.keywords) {
        keywords.push_back(&keyword);
    }
    std::sort(keywords.begin(), keywords.end(), [](const Keyword* lhs, const Keyword* rhs) {
        return lhs->refName < rhs->refName;
    });
    for (const Keyword* keyword : keywords) {
        root["keywords"].push_back(encodeKeyword(*keyword));
    }

    if (!graph.dropdowns.empty()) {
        root["dropdowns"] = Json::array();
        std::vector<const Dropdown*> dropdowns;
        for (const Dropdown& dropdown : graph.dropdowns) {
            dropdowns.push_back(&dropdown);
        }
        std::sort(dropdowns.begin(), dropdowns.end(), [](const Dropdown* lhs, const Dropdown* rhs) {
            return lhs->refName < rhs->refName;
        });
        for (const Dropdown* dropdown : dropdowns) {
            root["dropdowns"].push_back(encodeDropdown(*dropdown));
        }
    }

    root["categories"] = Json::array();
    std::vector<const Category*> categories;
    for (const Category& category : graph.categories) {
        categories.push_back(&category);
    }
    std::sort(categories.begin(), categories.end(), [](const Category* lhs, const Category* rhs) {
        return lhs->name < rhs->name;
    });
    for (const Category* category : categories) {
        root["categories"].push_back({{"name", category->name}, {"children", category->children}});
    }

    root["nodes"] = Json::array();
    std::vector<const NodeInstance*> nodes;
    for (const NodeInstance& node : graph.nodes) {
        nodes.push_back(&node);
    }
    std::sort(nodes.begin(), nodes.end(), [](const NodeInstance* lhs, const NodeInstance* rhs) {
        return lhs->id < rhs->id;
    });
    for (const NodeInstance* node : nodes) {
        root["nodes"].push_back(encodeNode(*node, options));
    }

    root["edges"] = Json::array();
    std::vector<Edge> edges = graph.edges;
    std::sort(edges.begin(), edges.end(), [](const Edge& lhs, const Edge& rhs) {
        if (lhs.fromNode != rhs.fromNode) {
            return lhs.fromNode < rhs.fromNode;
        }
        if (lhs.fromSlot != rhs.fromSlot) {
            return lhs.fromSlot < rhs.fromSlot;
        }
        if (lhs.toNode != rhs.toNode) {
            return lhs.toNode < rhs.toNode;
        }
        return lhs.toSlot < rhs.toSlot;
    });
    for (const Edge& edge : edges) {
        root["edges"].push_back({{"from", {{"node", edge.fromNode}, {"slot", edge.fromSlot}}},
                                {"to", {{"node", edge.toNode}, {"slot", edge.toSlot}}}});
    }

    root["groups"] = Json::array();
    std::vector<const Group*> groups;
    for (const Group& group : graph.groups) {
        groups.push_back(&group);
    }
    std::sort(groups.begin(), groups.end(), [](const Group* lhs, const Group* rhs) {
        return lhs->id < rhs->id;
    });
    for (const Group* group : groups) {
        root["groups"].push_back(encodeGroup(*group, options.includePositions));
    }

    root["notes"] = Json::array();
    std::vector<const Note*> notes;
    for (const Note& note : graph.notes) {
        notes.push_back(&note);
    }
    std::sort(notes.begin(), notes.end(), [](const Note* lhs, const Note* rhs) {
        return lhs->id < rhs->id;
    });
    for (const Note* note : notes) {
        root["notes"].push_back(encodeNote(*note, options.includePositions));
    }

    if (graph.kind == GraphKind::SubGraph) {
        root["outputs"] = Json::array();
        for (const SubGraphOutput& output : graph.outputs) {
            root["outputs"].push_back(
                {{"name", output.name}, {"type", toString(output.type)}, {"slot", output.slot}});
        }
    }
    return root;
}

bool readString(const Json& object, const char* key, std::string& result, bool required,
                std::string& error) {
    if (!object.contains(key)) {
        if (required) {
            error = std::string("missing field: ") + key;
            return false;
        }
        return true;
    }
    if (!object[key].is_string()) {
        error = std::string("field must be a string: ") + key;
        return false;
    }
    result = object[key].get<std::string>();
    return true;
}

bool readBool(const Json& object, const char* key, bool& result, std::string& error) {
    if (!object.contains(key)) {
        return true;
    }
    if (!object[key].is_boolean()) {
        error = std::string("field must be a boolean: ") + key;
        return false;
    }
    result = object[key].get<bool>();
    return true;
}

bool readInt(const Json& object, const char* key, int& result, std::string& error) {
    if (!object.contains(key)) {
        return true;
    }
    if (!object[key].is_number_integer()) {
        error = std::string("field must be an integer: ") + key;
        return false;
    }
    result = object[key].get<int>();
    return true;
}

bool readPosition(const Json& object, const char* key, Vec2& result, std::string& error) {
    if (!object.contains(key)) {
        return true;
    }
    if (!object[key].is_array() || object[key].size() != 2 || !object[key][0].is_number() ||
        !object[key][1].is_number()) {
        error = std::string("field must be a two-number array: ") + key;
        return false;
    }
    result = {object[key][0].get<float>(), object[key][1].get<float>()};
    return true;
}

bool parseTarget(const Json& json, Target& target, GraphPrecision& defaultGraphPrecision,
                 ConcretePrecision& defaultPrecision, std::string& error) {
    if (!json.is_object()) {
        error = "target must be an object";
        return false;
    }
    if (!readString(json, "id", target.id, true, error) ||
        !readString(json, "subTarget", target.subTarget, false, error)) {
        return false;
    }
    if (json.contains("settings")) {
        if (!json["settings"].is_object()) {
            error = "target.settings must be an object";
            return false;
        }
        for (const auto& [key, value] : json["settings"].items()) {
            std::string valueError;
            auto parsed = detail::parseValue(value, std::nullopt, &valueError);
            if (!parsed) {
                error = "invalid target setting " + key + ": " + valueError;
                return false;
            }
            target.settings[key] = std::move(*parsed);
        }
    }
    if (const auto found = target.settings.find("precision"); found != target.settings.end()) {
        if (const auto* text = found->second.asString()) {
            if (const auto parsed = concretePrecisionFromString(*text)) {
                defaultPrecision = *parsed;
                defaultGraphPrecision = *parsed == ConcretePrecision::Half ? GraphPrecision::Half
                                                                          : GraphPrecision::Single;
            } else if (*text == "graph" || *text == "inherit") {
                defaultGraphPrecision = GraphPrecision::Graph;
            }
        }
    }
    return true;
}

bool parseProperty(const Json& json, Property& property, std::string& error) {
    if (!json.is_object()) {
        error = "property must be an object";
        return false;
    }
    if (!readString(json, "name", property.name, true, error) ||
        !readString(json, "refName", property.refName, false, error)) {
        return false;
    }
    if (property.refName.empty()) {
        property.refName = property.name;
        std::replace(property.refName.begin(), property.refName.end(), ' ', '_');
        if (!property.refName.empty() && property.refName.front() != '_') {
            property.refName.insert(property.refName.begin(), '_');
        }
    }
    std::string type = "float";
    if (!readString(json, "type", type, true, error)) {
        return false;
    }
    const auto parsedType = valueTypeFromString(type);
    if (!parsedType) {
        error = "unknown property type: " + type;
        return false;
    }
    property.type = *parsedType;
    std::string precision = "inherit";
    if (!readString(json, "precision", precision, false, error)) {
        return false;
    }
    if (const auto parsed = precisionFromString(precision)) {
        property.precision = *parsed;
    } else {
        error = "unknown property precision: " + precision;
        return false;
    }
    if (!readBool(json, "exposed", property.exposed, error) ||
        !readString(json, "category", property.category, false, error) ||
        !readString(json, "floatMode", property.floatMode, false, error) ||
        !readString(json, "defaultType", property.defaultType, false, error)) {
        return false;
    }
    if (json.contains("enumNames")) {
        if (!json["enumNames"].is_array()) {
            error = "property enumNames must be an array";
            return false;
        }
        for (const Json& name : json["enumNames"]) {
            if (!name.is_string()) {
                error = "property enumNames must contain strings";
                return false;
            }
            property.enumNames.push_back(name.get<std::string>());
        }
    }
    if (json.contains("enumValues")) {
        if (!json["enumValues"].is_array()) {
            error = "property enumValues must be an array";
            return false;
        }
        for (const Json& value : json["enumValues"]) {
            if (!value.is_number_integer()) {
                error = "property enumValues must contain integers";
                return false;
            }
            property.enumValues.push_back(value.get<int>());
        }
    }
    if (!property.enumValues.empty() && property.enumNames.empty()) {
        error = "property enumValues requires enumNames";
        return false;
    }
    if (property.enumValues.size() > property.enumNames.size()) {
        error = "property enumValues exceeds enumNames";
        return false;
    }
    while (property.enumValues.size() < property.enumNames.size()) {
        property.enumValues.push_back(static_cast<int>(property.enumValues.size()));
    }
    if (json.contains("attributes")) {
        if (!json["attributes"].is_array()) {
            error = "property attributes must be an array";
            return false;
        }
        for (const auto& attribute : json["attributes"]) {
            if (!attribute.is_string()) {
                error = "property attributes must contain strings";
                return false;
            }
            property.attributes.push_back(attribute.get<std::string>());
        }
    }
    if (json.contains("sampler")) {
        if (!json["sampler"].is_object()) {
            error = "property sampler must be an object";
            return false;
        }
        if (!readString(json["sampler"], "filter", property.samplerFilter, false, error) ||
            !readString(json["sampler"], "wrap", property.samplerWrap, false, error)) {
            return false;
        }
        if (json["sampler"].contains("anisotropic")) {
            std::string anisotropic;
            if (!readString(json["sampler"], "anisotropic", anisotropic, true, error)) {
                return false;
            }
            const std::string normalized = normalizeSamplerAnisotropy(anisotropic);
            if (normalized.empty()) {
                error = "unknown sampler anisotropic value: " + anisotropic;
                return false;
            }
            property.samplerAnisotropic = normalized;
        }
    }
    if (property.samplerAnisotropic != "none" &&
        !isTextureType(property.type) && property.type != ValueType::SamplerState) {
        error = "sampler anisotropic requires a texture or SamplerState property";
        return false;
    }
    if (!readBool(json, "useTilingAndOffset", property.useTilingAndOffset, error) ||
        !readPosition(json, "tiling", property.tiling, error) ||
        !readPosition(json, "offset", property.offset, error)) {
        return false;
    }
    if (property.useTilingAndOffset && property.type != ValueType::Texture2D) {
        error = "useTilingAndOffset requires a Texture2D property";
        return false;
    }
    if (!readBool(json, "isHDR", property.isHDR, error)) {
        return false;
    }
    if (property.isHDR && property.type != ValueType::Texture2D &&
        property.type != ValueType::Texture2DArray && property.type != ValueType::Texture3D &&
        property.type != ValueType::Cubemap) {
        error = "isHDR requires a Texture2D, Texture2DArray, Texture3D, or Cubemap property";
        return false;
    }
    if (!property.defaultType.empty()) {
        if (property.type != ValueType::Texture2D &&
            property.type != ValueType::Texture2DArray &&
            property.type != ValueType::Texture3D && property.type != ValueType::Cubemap) {
            error = "defaultType requires a Texture2D, Texture2DArray, Texture3D, or Cubemap property";
            return false;
        }
        const auto normalized = textureDefaultTypeFromString(property.defaultType);
        if (!normalized) {
            error = "unknown texture defaultType: " + property.defaultType;
            return false;
        }
        property.defaultType = *normalized;
    }
    if (!readInt(json, "vtLayers", property.vtLayers, error)) {
        return false;
    }
    if (property.type == ValueType::VirtualTexture &&
        (property.vtLayers < 1 || property.vtLayers > 4)) {
        error = "vtLayers must be between 1 and 4";
        return false;
    }
    if (json.contains("vtLayerRefNames")) {
        if (!json["vtLayerRefNames"].is_array()) {
            error = "property vtLayerRefNames must be an array";
            return false;
        }
        for (const Json& name : json["vtLayerRefNames"]) {
            if (!name.is_string() || name.get<std::string>().empty()) {
                error = "property vtLayerRefNames must contain non-empty strings";
                return false;
            }
            property.vtLayerRefNames.push_back(name.get<std::string>());
        }
    }
    if (json.contains("vtLayerTextureTypes")) {
        if (!json["vtLayerTextureTypes"].is_array()) {
            error = "property vtLayerTextureTypes must be an array";
            return false;
        }
        for (const Json& layerType : json["vtLayerTextureTypes"]) {
            if (!layerType.is_string()) {
                error = "property vtLayerTextureTypes must contain strings";
                return false;
            }
            const auto normalized =
                virtualTextureLayerTextureTypeFromString(layerType.get<std::string>());
            if (!normalized) {
                error = "unknown virtual texture layer texture type: " +
                        layerType.get<std::string>();
                return false;
            }
            property.vtLayerTextureTypes.push_back(*normalized);
        }
    }
    if (property.type != ValueType::VirtualTexture &&
        (!property.vtLayerRefNames.empty() || !property.vtLayerTextureTypes.empty())) {
        error = "virtual texture layer metadata requires a VirtualTexture property";
        return false;
    }
    if ((!property.vtLayerRefNames.empty() &&
         property.vtLayerRefNames.size() != static_cast<std::size_t>(property.vtLayers)) ||
        (!property.vtLayerTextureTypes.empty() &&
         property.vtLayerTextureTypes.size() != static_cast<std::size_t>(property.vtLayers))) {
        error = "virtual texture layer arrays must match vtLayers";
        return false;
    }
    if (json.contains("default")) {
        std::string valueError;
        auto value = detail::parseValue(json["default"], property.type, &valueError);
        if (!value) {
            error = "invalid property default: " + valueError;
            return false;
        }
        property.defaultValue = std::move(*value);
    } else {
        property.defaultValue = defaultForType(property.type).value_or(Value{});
    }
    return true;
}

bool parseKeyword(const Json& json, Keyword& keyword, std::string& error) {
    if (!json.is_object()) {
        error = "keyword must be an object";
        return false;
    }
    if (!readString(json, "name", keyword.name, true, error) ||
        !readString(json, "refName", keyword.refName, true, error)) {
        return false;
    }
    std::string type = "boolean";
    if (!readString(json, "type", type, false, error)) {
        return false;
    }
    if (const auto parsed = keywordTypeFromString(type)) {
        keyword.type = *parsed;
    } else {
        error = "unknown keyword type: " + type;
        return false;
    }
    std::string definition = "shaderfeature";
    std::string scope = "local";
    if (!readString(json, "definition", definition, false, error) ||
        !readString(json, "scope", scope, false, error)) {
        return false;
    }
    if (const auto parsed = keywordDefinitionFromString(definition)) {
        keyword.definition = *parsed;
    } else {
        error = "unknown keyword definition: " + definition;
        return false;
    }
    if (const auto parsed = keywordScopeFromString(scope)) {
        keyword.scope = *parsed;
    } else {
        error = "unknown keyword scope: " + scope;
        return false;
    }
    if (json.contains("default")) {
        if (keyword.type == KeywordType::Boolean && json["default"].is_boolean()) {
            keyword.defaultValue = json["default"].get<bool>() ? 1 : 0;
        } else if (!readInt(json, "default", keyword.defaultValue, error)) {
            return false;
        }
    }
    if (!readString(json, "category", keyword.category, false, error)) {
        return false;
    }
    if (json.contains("entries")) {
        if (!json["entries"].is_array()) {
            error = "keyword entries must be an array";
            return false;
        }
        for (const auto& entryJson : json["entries"]) {
            if (!entryJson.is_object()) {
                error = "keyword entry must be an object";
                return false;
            }
            KeywordEntry entry;
            if (!readInt(entryJson, "id", entry.id, error) ||
                !readString(entryJson, "displayName", entry.displayName, true, error) ||
                !readString(entryJson, "referenceName", entry.referenceName, true, error)) {
                return false;
            }
            keyword.entries.push_back(std::move(entry));
        }
    }
    return true;
}

bool parseDropdown(const Json& json, Dropdown& dropdown, std::string& error) {
    if (!json.is_object()) {
        error = "dropdown must be an object";
        return false;
    }
    if (!readString(json, "name", dropdown.name, true, error) ||
        !readString(json, "refName", dropdown.refName, true, error)) {
        return false;
    }
    if (json.contains("value") && !readInt(json, "value", dropdown.value, error)) {
        return false;
    }
    if (!readString(json, "category", dropdown.category, false, error)) {
        return false;
    }
    if (!json.contains("entries") || !json["entries"].is_array()) {
        error = "dropdown entries must be an array";
        return false;
    }
    for (const auto& entryJson : json["entries"]) {
        if (!entryJson.is_object()) {
            error = "dropdown entry must be an object";
            return false;
        }
        DropdownEntry entry;
        if (!readInt(entryJson, "id", entry.id, error) ||
            !readString(entryJson, "displayName", entry.displayName, true, error)) {
            return false;
        }
        dropdown.entries.push_back(std::move(entry));
    }
    return true;
}

bool parseNodeSlots(const Json& json, NodeInstance& node, const NodeDef& definition,
                    std::string& error, std::vector<Diagnostic>& diagnostics) {
    if (!json.is_object()) {
        error = "node slots must be an object";
        return false;
    }
    for (const auto& [key, value] : json.items()) {
        int slotId = 0;
        try {
            slotId = std::stoi(key);
        } catch (const std::exception&) {
            error = "node slot key is not an integer: " + key;
            return false;
        }
        SlotInstance* slot = node.findSlot(slotId);
        if (slot == nullptr) {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Warning;
            diagnostic.code = kErrorUnknownField;
            diagnostic.message = "unknown slot id " + key + " on node type " + definition.type;
            diagnostic.node = node.id;
            diagnostic.slot = slotId;
            diagnostics.push_back(std::move(diagnostic));
            continue;
        }
        if (!value.is_object()) {
            error = "node slot state must be an object";
            return false;
        }
        std::string mode = toString(slot->state.mode);
        if (!readString(value, "mode", mode, false, error)) {
            return false;
        }
        if (const auto parsed = slotModeFromString(mode)) {
            slot->state.mode = *parsed;
        } else {
            error = "unknown slot mode: " + mode;
            return false;
        }
        if (value.contains("concreteType")) {
            if (!value["concreteType"].is_string()) {
                error = "slot concreteType must be a string";
                return false;
            }
            const auto parsed = concreteTypeFromString(value["concreteType"].get<std::string>());
            if (!parsed) {
                error = "unknown slot concreteType";
                return false;
            }
            slot->state.concreteType = *parsed;
        }
        if (value.contains("value")) {
            std::string valueError;
            auto parsed = detail::parseValue(value["value"], slot->spec.type, &valueError);
            if (!parsed) {
                error = "invalid slot value: " + valueError;
                return false;
            }
            slot->state.value = std::move(*parsed);
        }
    }
    return true;
}

bool parseNode(const Json& json, const NodeRegistry& registry, NodeInstance& node,
               std::vector<Diagnostic>& diagnostics, std::string& error) {
    if (!json.is_object()) {
        error = "node must be an object";
        return false;
    }
    if (!readInt(json, "id", node.id, error) || !readString(json, "type", node.type, true, error) ||
        !readInt(json, "typeVersion", node.typeVersion, error) ||
        !readPosition(json, "position", node.position, error)) {
        return false;
    }
    const int requestedVersion = node.typeVersion;
    const NodeDef* definition = registry.find(node.type);
    if (definition == nullptr) {
        node = registry.makeUnknown(node.id, node.type, json.dump());
        return true;
    }
    node = registry.instantiate(*definition, node.id, node.position);
    node.typeVersion = requestedVersion;
    if (!json.contains("typeVersion")) {
        node.typeVersion = definition->typeVersion;
    }
    if (json.contains("typeVersion") && requestedVersion < definition->typeVersion &&
        definition->migrate) {
        std::string migrationError;
        if (!definition->migrate(node, requestedVersion, migrationError)) {
            error = migrationError;
            return false;
        }
        node.typeVersion = definition->typeVersion;
    }
    if (json.contains("settings")) {
        if (!json["settings"].is_object()) {
            error = "node settings must be an object";
            return false;
        }
        for (const auto& [key, value] : json["settings"].items()) {
            if (key == "precision") {
                if (!value.is_string()) {
                    error = "node precision must be a string";
                    return false;
                }
                if (const auto parsed = precisionFromString(value.get<std::string>())) {
                    node.precision = *parsed;
                } else {
                    error = "unknown node precision";
                    return false;
                }
                continue;
            }
            std::string valueError;
            auto parsed = detail::parseValue(value, std::nullopt, &valueError);
            if (!parsed) {
                error = "invalid node setting " + key + ": " + valueError;
                return false;
            }
            node.settings[key] = std::move(*parsed);
        }
    }
    if (json.contains("slots") && !parseNodeSlots(json["slots"], node, *definition, error, diagnostics)) {
        return false;
    }
    return true;
}

bool parseEdges(const Json& json, Graph& graph, std::vector<Diagnostic>& diagnostics,
                std::string& error) {
    if (!json.is_array()) {
        error = "edges must be an array";
        return false;
    }
    for (const auto& edgeJson : json) {
        if (!edgeJson.is_object() || !edgeJson.contains("from") || !edgeJson.contains("to") ||
            !edgeJson["from"].is_object() || !edgeJson["to"].is_object()) {
            error = "edge must have from/to objects";
            return false;
        }
        Edge edge;
        if (!readInt(edgeJson["from"], "node", edge.fromNode, error) ||
            !readInt(edgeJson["from"], "slot", edge.fromSlot, error) ||
            !readInt(edgeJson["to"], "node", edge.toNode, error) ||
            !readInt(edgeJson["to"], "slot", edge.toSlot, error)) {
            return false;
        }
        std::string edgeError;
        if (!graph.addEdge(edge, &edgeError)) {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorInvalidEdge;
            diagnostic.message = edgeError;
            diagnostic.node = edge.toNode;
            diagnostic.slot = edge.toSlot;
            diagnostics.push_back(std::move(diagnostic));
        }
    }
    return true;
}

bool parseGroups(const Json& json, Graph& graph, std::string& error) {
    if (!json.is_array()) {
        error = "groups must be an array";
        return false;
    }
    for (const auto& item : json) {
        if (!item.is_object()) {
            error = "group must be an object";
            return false;
        }
        Group group;
        if (!readInt(item, "id", group.id, error) ||
            !readString(item, "title", group.title, false, error) ||
            !readPosition(item, "position", group.position, error)) {
            return false;
        }
        if (item.contains("size") && item["size"].is_array() && item["size"].size() == 2 &&
            item["size"][0].is_number() && item["size"][1].is_number()) {
            group.size = {item["size"][0].get<float>(), item["size"][1].get<float>()};
        }
        if (item.contains("color") && item["color"].is_array() && item["color"].size() == 4) {
            group.color = {item["color"][0].get<float>(), item["color"][1].get<float>(),
                           item["color"][2].get<float>(), item["color"][3].get<float>()};
        }
        if (item.contains("nodes") && item["nodes"].is_array()) {
            for (const auto& nodeId : item["nodes"]) {
                if (nodeId.is_number_integer()) {
                    group.nodes.push_back(nodeId.get<NodeId>());
                }
            }
        }
        graph.groups.push_back(std::move(group));
    }
    return true;
}

bool parseNotes(const Json& json, Graph& graph, std::string& error) {
    if (!json.is_array()) {
        error = "notes must be an array";
        return false;
    }
    for (const auto& item : json) {
        if (!item.is_object()) {
            error = "note must be an object";
            return false;
        }
        Note note;
        if (!readInt(item, "id", note.id, error) ||
            !readString(item, "title", note.title, false, error) ||
            !readString(item, "text", note.text, false, error) ||
            !readString(item, "theme", note.theme, false, error) ||
            !readPosition(item, "position", note.position, error)) {
            return false;
        }
        if (item.contains("size") && item["size"].is_array() && item["size"].size() == 2 &&
            item["size"][0].is_number() && item["size"][1].is_number()) {
            note.size = {item["size"][0].get<float>(), item["size"][1].get<float>()};
        }
        graph.notes.push_back(std::move(note));
    }
    return true;
}

bool parseOutputs(const Json& json, Graph& graph, std::string& error) {
    if (!json.is_array()) {
        error = "outputs must be an array";
        return false;
    }
    for (const auto& item : json) {
        if (!item.is_object()) {
            error = "subgraph output must be an object";
            return false;
        }
        SubGraphOutput output;
        std::string type = "float";
        if (!readString(item, "name", output.name, true, error) ||
            !readString(item, "type", type, true, error) || !readInt(item, "slot", output.slot, error)) {
            return false;
        }
        const auto parsed = valueTypeFromString(type);
        if (!parsed) {
            error = "unknown subgraph output type: " + type;
            return false;
        }
        output.type = *parsed;
        graph.outputs.push_back(std::move(output));
    }
    return true;
}

void appendUnknownFieldDiagnostics(const Json& root, const std::set<std::string>& known,
                                   const LoadOptions& options, std::vector<Diagnostic>& diagnostics,
                                   bool& fatal, std::string& error) {
    for (const auto& [key, value] : root.items()) {
        if (known.contains(key)) {
            continue;
        }
        if (!options.allowUnknownFields) {
            fatal = true;
            error = "unknown top-level field: " + key;
            return;
        }
        if (options.warnUnknownFields) {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Warning;
            diagnostic.code = kErrorUnknownField;
            diagnostic.message = "ignored unknown top-level field: " + key;
            diagnostics.push_back(std::move(diagnostic));
        }
    }
}

} // namespace

std::string serialize(const Graph& graph, const SerializeOptions& options) {
    return dumpJson(encodeGraph(graph, options), options.pretty);
}

std::string serializeValueJson(const Value& value) {
    return detail::encodeValue(value).dump();
}

std::uint64_t contentHash(const Graph& graph) {
    SerializeOptions options;
    options.pretty = false;
    options.includePositions = false;
    const std::string canonical = serialize(graph, options);
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char byte : canonical) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

GraphLoadResult deserialize(std::string_view text, const NodeRegistry& registry, const LoadOptions& options) {
    GraphLoadResult result;
    Json root;
    try {
        root = detail::Json::parse(text.begin(), text.end());
    } catch (const std::exception& exception) {
        result.error = exception.what();
        Diagnostic diagnostic;
        diagnostic.severity = Severity::Error;
        diagnostic.code = kErrorInvalidJson;
        diagnostic.message = result.error;
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }

    try {
    if (!detail::migrateJson(root, result.diagnostics, result.error)) {
        return result;
    }

    const std::set<std::string> knownFields = {
        "schemaVersion", "kind", "name", "generatorVersion", "target", "properties", "keywords",
        "dropdowns", "categories", "nodes", "edges", "groups", "notes", "outputs", "customFunctions",
    };
    bool fatal = false;
    appendUnknownFieldDiagnostics(root, knownFields, options, result.diagnostics, fatal, result.error);
    if (fatal) {
        return result;
    }

    Graph& graph = result.graph;
    graph.schemaVersion = Graph::kCurrentSchemaVersion;
    std::string kind = "graph";
    if (!readString(root, "kind", kind, false, result.error) ||
        !readString(root, "name", graph.name, false, result.error) ||
        !readString(root, "generatorVersion", graph.generatorVersion, false, result.error)) {
        return result;
    }
    if (const auto parsed = graphKindFromString(kind)) {
        graph.kind = *parsed;
    } else {
        result.error = "unknown graph kind: " + kind;
        return result;
    }
    if (root.contains("target")) {
        if (!parseTarget(root["target"], graph.target, graph.defaultGraphPrecision,
                         graph.defaultPrecision, result.error)) {
            return result;
        }
    } else if (graph.kind == GraphKind::Graph) {
        result.error = "graph target is required";
        return result;
    }

    if (root.contains("properties")) {
        if (!root["properties"].is_array()) {
            result.error = "properties must be an array";
            return result;
        }
        for (const auto& item : root["properties"]) {
            Property property;
            if (!parseProperty(item, property, result.error)) {
                return result;
            }
            std::string addError;
            if (!graph.addProperty(std::move(property), &addError)) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorDuplicateId;
                diagnostic.message = addError;
                result.diagnostics.push_back(std::move(diagnostic));
            }
        }
    }
    if (root.contains("keywords")) {
        if (!root["keywords"].is_array()) {
            result.error = "keywords must be an array";
            return result;
        }
        for (const auto& item : root["keywords"]) {
            Keyword keyword;
            if (!parseKeyword(item, keyword, result.error)) {
                return result;
            }
            std::string addError;
            if (!graph.addKeyword(std::move(keyword), &addError)) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorDuplicateId;
                diagnostic.message = addError;
                result.diagnostics.push_back(std::move(diagnostic));
            }
        }
    }
    if (root.contains("dropdowns")) {
        if (!root["dropdowns"].is_array()) {
            result.error = "dropdowns must be an array";
            return result;
        }
        for (const auto& item : root["dropdowns"]) {
            Dropdown dropdown;
            if (!parseDropdown(item, dropdown, result.error)) {
                return result;
            }
            std::string addError;
            if (!graph.addDropdown(std::move(dropdown), &addError)) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorDuplicateId;
                diagnostic.message = addError;
                result.diagnostics.push_back(std::move(diagnostic));
            }
        }
    }
    if (root.contains("categories")) {
        if (!root["categories"].is_array()) {
            result.error = "categories must be an array";
            return result;
        }
        for (const auto& item : root["categories"]) {
            if (!item.is_object()) {
                result.error = "category must be an object";
                return result;
            }
            Category category;
            if (!readString(item, "name", category.name, true, result.error)) {
                return result;
            }
            if (item.contains("children") && item["children"].is_array()) {
                for (const auto& child : item["children"]) {
                    if (child.is_string()) {
                        category.children.push_back(child.get<std::string>());
                    }
                }
            }
            graph.categories.push_back(std::move(category));
        }
    }
    if (root.contains("nodes")) {
        if (!root["nodes"].is_array()) {
            result.error = "nodes must be an array";
            return result;
        }
        for (const auto& item : root["nodes"]) {
            NodeInstance node;
            if (!parseNode(item, registry, node, result.diagnostics, result.error)) {
                return result;
            }
            std::string addError;
            if (!graph.addNode(std::move(node), &addError)) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorDuplicateId;
                diagnostic.message = addError;
                result.diagnostics.push_back(std::move(diagnostic));
            }
        }
    }
    if (root.contains("edges") && !parseEdges(root["edges"], graph, result.diagnostics, result.error)) {
        return result;
    }
    if (root.contains("groups") && !parseGroups(root["groups"], graph, result.error)) {
        return result;
    }
    if (root.contains("notes") && !parseNotes(root["notes"], graph, result.error)) {
        return result;
    }
    if (root.contains("outputs") && !parseOutputs(root["outputs"], graph, result.error)) {
        return result;
    }

    result.ok = !hasError(result.diagnostics);
    return result;
    } catch (const std::exception& exception) {
        result.ok = false;
        result.error = exception.what();
        Diagnostic diagnostic;
        diagnostic.severity = Severity::Error;
        diagnostic.code = kErrorInvalidValue;
        diagnostic.message = result.error;
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }
}

bool saveGraph(const std::filesystem::path& path, const Graph& graph, const SerializeOptions& options,
               std::string* error) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        if (error != nullptr) {
            *error = "cannot open graph file for writing";
        }
        return false;
    }
    const std::string text = serialize(graph, options);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream) {
        if (error != nullptr) {
            *error = "cannot write graph file";
        }
        return false;
    }
    return true;
}

GraphLoadResult loadGraph(const std::filesystem::path& path, const NodeRegistry& registry,
                          const LoadOptions& options) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        GraphLoadResult result;
        result.error = "cannot open graph file";
        return result;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return deserialize(buffer.str(), registry, options);
}

std::string serializeLayout(const GraphLayout& layout) {
    Json root;
    root["schemaVersion"] = layout.schemaVersion;
    root["revision"] = layout.revision;
    root["view"] = {{"pan", Json::array({layout.pan.x, layout.pan.y})}, {"zoom", layout.zoom}};
    root["nodes"] = Json::array();
    std::vector<NodeLayout> nodes = layout.nodes;
    std::sort(nodes.begin(), nodes.end(), [](const NodeLayout& lhs, const NodeLayout& rhs) {
        return lhs.id < rhs.id;
    });
    for (const NodeLayout& node : nodes) {
        root["nodes"].push_back({{"id", node.id},
                                 {"position", Json::array({node.position.x, node.position.y})},
                                 {"collapsed", node.collapsed}});
    }
    root["groups"] = Json::array();
    std::vector<GroupLayout> groups = layout.groups;
    std::sort(groups.begin(), groups.end(), [](const GroupLayout& lhs, const GroupLayout& rhs) {
        return lhs.id < rhs.id;
    });
    for (const GroupLayout& group : groups) {
        root["groups"].push_back({{"id", group.id},
                                  {"position", Json::array({group.position.x, group.position.y})},
                                  {"size", Json::array({group.size.x, group.size.y})},
                                  {"collapsed", group.collapsed}});
    }
    root["notes"] = Json::array();
    std::vector<NoteLayout> notes = layout.notes;
    std::sort(notes.begin(), notes.end(), [](const NoteLayout& lhs, const NoteLayout& rhs) {
        return lhs.id < rhs.id;
    });
    for (const NoteLayout& note : notes) {
        root["notes"].push_back({{"id", note.id},
                                 {"position", Json::array({note.position.x, note.position.y})},
                                 {"size", Json::array({note.size.x, note.size.y})}});
    }
    return dumpJson(root, true);
}

LayoutLoadResult deserializeLayout(std::string_view text) {
    LayoutLoadResult result;
    Json root;
    try {
        root = detail::Json::parse(text.begin(), text.end());
    } catch (const std::exception& exception) {
        result.error = exception.what();
        return result;
    }
    if (!root.is_object()) {
        result.error = "layout root must be an object";
        return result;
    }
    if (!readInt(root, "schemaVersion", result.layout.schemaVersion, result.error) ||
        (root.contains("revision") && !root["revision"].is_number_unsigned() &&
         !(root["revision"].is_number_integer() &&
           root["revision"].get<std::int64_t>() >= 0))) {
        if (result.error.empty()) {
            result.error = "revision must be a non-negative integer";
        }
        return result;
    }
    if (root.contains("revision")) {
        result.layout.revision = root["revision"].get<std::uint64_t>();
    }
    if (result.layout.schemaVersion < 0 || result.layout.schemaVersion > 1) {
        result.error = "unsupported layout schemaVersion";
        return result;
    }
    if (root.contains("view") && root["view"].is_object()) {
        if (!readPosition(root["view"], "pan", result.layout.pan, result.error)) {
            return result;
        }
        if (root["view"].contains("zoom") && root["view"]["zoom"].is_number()) {
            result.layout.zoom = root["view"]["zoom"].get<float>();
        }
    }
    if (root.contains("nodes") && root["nodes"].is_array()) {
        for (const auto& item : root["nodes"]) {
            NodeLayout node;
            if (!item.is_object() || !readInt(item, "id", node.id, result.error) ||
                !readPosition(item, "position", node.position, result.error)) {
                if (result.error.empty()) {
                    result.error = "invalid node layout";
                }
                return result;
            }
            if (item.contains("collapsed") && item["collapsed"].is_boolean()) {
                node.collapsed = item["collapsed"].get<bool>();
            }
            result.layout.nodes.push_back(node);
        }
    }
    if (root.contains("groups") && root["groups"].is_array()) {
        for (const auto& item : root["groups"]) {
            GroupLayout group;
            if (!item.is_object() || !readInt(item, "id", group.id, result.error) ||
                !readPosition(item, "position", group.position, result.error)) {
                if (result.error.empty()) {
                    result.error = "invalid group layout";
                }
                return result;
            }
            if (item.contains("size") && item["size"].is_array() && item["size"].size() == 2) {
                group.size = {item["size"][0].get<float>(), item["size"][1].get<float>()};
            }
            if (item.contains("collapsed") && item["collapsed"].is_boolean()) {
                group.collapsed = item["collapsed"].get<bool>();
            }
            result.layout.groups.push_back(group);
        }
    }
    if (root.contains("notes") && root["notes"].is_array()) {
        for (const auto& item : root["notes"]) {
            NoteLayout note;
            if (!item.is_object() || !readInt(item, "id", note.id, result.error) ||
                !readPosition(item, "position", note.position, result.error)) {
                if (result.error.empty()) {
                    result.error = "invalid note layout";
                }
                return result;
            }
            if (item.contains("size") && item["size"].is_array() && item["size"].size() == 2) {
                note.size = {item["size"][0].get<float>(), item["size"][1].get<float>()};
            }
            result.layout.notes.push_back(note);
        }
    }
    result.ok = true;
    return result;
}

bool saveLayout(const std::filesystem::path& path, const GraphLayout& layout, std::string* error) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        if (error != nullptr) {
            *error = "cannot open layout file for writing";
        }
        return false;
    }
    const std::string text = serializeLayout(layout);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream) {
        if (error != nullptr) {
            *error = "cannot write layout file";
        }
        return false;
    }
    return true;
}

LayoutLoadResult loadLayout(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        LayoutLoadResult result;
        result.error = "cannot open layout file";
        return result;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return deserializeLayout(buffer.str());
}

} // namespace sg
