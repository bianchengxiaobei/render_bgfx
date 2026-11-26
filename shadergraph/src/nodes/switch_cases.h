#pragma once

#include "sg/graph.h"

#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sg {

struct SwitchCase {
    std::string name;
    std::string comparison = "equal";
    float threshold = 0.0f;
};

inline std::string trimSwitchText(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

inline std::string normalizeSwitchComparison(std::string_view text) {
    const std::string trimmed = trimSwitchText(text);
    if (trimmed == "!=") {
        return "notEqual";
    }
    if (trimmed == "==") {
        return "equal";
    }
    if (trimmed == "<") {
        return "less";
    }
    if (trimmed == "<=") {
        return "lessOrEqual";
    }
    if (trimmed == ">") {
        return "greater";
    }
    if (trimmed == ">=") {
        return "greaterOrEqual";
    }

    std::string key;
    for (char character : trimmed) {
        const unsigned char value = static_cast<unsigned char>(character);
        if (std::isalnum(value) != 0) {
            key.push_back(static_cast<char>(std::tolower(value)));
        }
    }
    if (key == "notequal") {
        return "notEqual";
    }
    if (key == "lessorequal") {
        return "lessOrEqual";
    }
    if (key == "greaterorequal") {
        return "greaterOrEqual";
    }
    if (key == "less") {
        return "less";
    }
    if (key == "greater") {
        return "greater";
    }
    return "equal";
}

inline const char* switchComparisonOperator(std::string_view comparison) {
    if (comparison == "notEqual") {
        return "!=";
    }
    if (comparison == "less") {
        return "<";
    }
    if (comparison == "lessOrEqual") {
        return "<=";
    }
    if (comparison == "greater") {
        return ">";
    }
    if (comparison == "greaterOrEqual") {
        return ">=";
    }
    return "==";
}

inline std::vector<SwitchCase> parseSwitchCases(std::string_view serialized) {
    std::vector<SwitchCase> cases;
    std::size_t start = 0;
    while (start <= serialized.size()) {
        const std::size_t end = serialized.find(';', start);
        const std::string_view part = serialized.substr(
            start, end == std::string_view::npos ? serialized.size() - start : end - start);
        if (!trimSwitchText(part).empty()) {
            SwitchCase entry;
            std::size_t fieldStart = 0;
            std::vector<std::string> fields;
            while (fieldStart <= part.size()) {
                const std::size_t fieldEnd = part.find('|', fieldStart);
                fields.push_back(trimSwitchText(part.substr(
                    fieldStart,
                    fieldEnd == std::string_view::npos ? part.size() - fieldStart
                                                       : fieldEnd - fieldStart)));
                if (fieldEnd == std::string_view::npos) {
                    break;
                }
                fieldStart = fieldEnd + 1;
            }
            entry.name = fields.empty() ? std::string() : fields[0];
            if (fields.size() > 1) {
                entry.comparison = normalizeSwitchComparison(fields[1]);
            }
            if (fields.size() > 2 && !fields[2].empty()) {
                char* parsedEnd = nullptr;
                const float threshold = std::strtof(fields[2].c_str(), &parsedEnd);
                if (parsedEnd != fields[2].c_str()) {
                    entry.threshold = threshold;
                }
            }
            if (entry.name.empty()) {
                const std::size_t index = cases.size();
                entry.name = index < 26
                                 ? std::string(1, static_cast<char>('A' + index))
                                 : "Case " + std::to_string(index + 1);
            }
            cases.push_back(std::move(entry));
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return cases;
}

struct EffectiveSwitchCases {
    std::vector<SwitchCase> cases;
    bool fromProperty = false;
};

inline const Property* switchEnumProperty(const Graph& graph, const NodeInstance& node) {
    const Edge* edge = graph.incomingEdge(node.id, 0);
    if (edge == nullptr) {
        return nullptr;
    }
    const NodeInstance* source = graph.findNode(edge->fromNode);
    if (source == nullptr || source->type != "Property") {
        return nullptr;
    }
    std::string reference;
    for (const char* key : {"property", "propertyRef", "refName"}) {
        const Value* value = source->setting(key);
        if (value == nullptr) {
            continue;
        }
        if (const std::string* text = value->asString(); text != nullptr && !text->empty()) {
            reference = *text;
            break;
        }
    }
    if (reference.empty()) {
        return nullptr;
    }
    const Property* property = graph.findProperty(reference);
    if (property == nullptr || property->type != ValueType::Float ||
        property->floatMode != "enum" || property->enumNames.empty()) {
        return nullptr;
    }
    return property;
}

inline EffectiveSwitchCases effectiveSwitchCases(const Graph& graph, const NodeInstance& node) {
    EffectiveSwitchCases result;
    if (const Property* property = switchEnumProperty(graph, node); property != nullptr) {
        result.fromProperty = true;
        for (std::size_t index = 0; index < property->enumNames.size(); ++index) {
            SwitchCase entry;
            entry.name = property->enumNames[index];
            entry.comparison = "equal";
            entry.threshold = index < property->enumValues.size()
                                  ? static_cast<float>(property->enumValues[index])
                                  : static_cast<float>(index);
            result.cases.push_back(std::move(entry));
        }
        return result;
    }

    std::string encoded;
    if (const Value* value = node.setting("cases")) {
        if (const std::string* text = value->asString()) {
            encoded = *text;
        }
    }
    result.cases = parseSwitchCases(encoded);
    return result;
}

} // namespace sg
