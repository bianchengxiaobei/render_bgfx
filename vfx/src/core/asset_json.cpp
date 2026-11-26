#include "vfx/asset.h"

#include "block_catalog.h"
#include "custom_hlsl.h"
#include "subgraph.h"
#include "vfx/attribute_layout.h"
#include "vfx/expression.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vfx {
namespace {

using Json = nlohmann::json;
using detail::BlockSpec;
using detail::FieldSpec;
using detail::findBlockSpec;

constexpr std::size_t kMaxJsonDepth = 64;
constexpr std::uint32_t kMaxSortedCapacity = 262144u;

std::string child(std::string_view base, std::string_view name) {
    if (base.empty()) {
        return std::string(name);
    }
    return std::string(base) + "." + std::string(name);
}

std::string element(std::string_view base, std::size_t index) {
    return std::string(base) + "[" + std::to_string(index) + "]";
}

bool isIdentifier(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    const auto isAlpha = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    const auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
    if (!isAlpha(name.front())) {
        return false;
    }
    return std::all_of(name.begin() + 1, name.end(), [&](char c) {
        return isAlpha(c) || isDigit(c);
    });
}

bool isKnownExpressionOp(std::string_view op) {
    static constexpr std::string_view kOps[] = {
        "attr",
        "attribute",
        "property",
        "time",
        "dt",
        "lifetime_ratio",
        "add",
        "sub",
        "mul",
        "div",
        "min",
        "max",
        "clamp",
        "lerp",
        "remap",
        "sin",
        "cos",
        "abs",
        "saturate",
        "random",
        "noise",
        "sample_curve",
        "sample_gradient",
        "sample_texture",
        "select",
        "swizzle",
        "subgraph",
        "input",
        "arg",
    };
    return std::find(std::begin(kOps), std::end(kOps), op) != std::end(kOps);
}

struct JsonPathAccess {
    bool isIndex = false;
    std::string key;
    std::size_t index = 0;
};

bool parseJsonPath(std::string_view path, std::vector<JsonPathAccess>& access) {
    std::size_t cursor = 0;
    if (!path.empty() && path.front() == '$') {
        ++cursor;
    }
    while (cursor < path.size()) {
        if (path[cursor] == '.') {
            ++cursor;
            continue;
        }
        if (path[cursor] == '[') {
            ++cursor;
            const std::size_t start = cursor;
            std::size_t index = 0;
            while (cursor < path.size() && path[cursor] >= '0' && path[cursor] <= '9') {
                index = index * 10u + static_cast<std::size_t>(path[cursor] - '0');
                ++cursor;
            }
            if (cursor == start || cursor >= path.size() || path[cursor] != ']') {
                return false;
            }
            ++cursor;
            access.push_back(JsonPathAccess{true, {}, index});
            continue;
        }
        const std::size_t start = cursor;
        while (cursor < path.size() && path[cursor] != '.' && path[cursor] != '[') {
            ++cursor;
        }
        if (cursor == start) {
            return false;
        }
        access.push_back(JsonPathAccess{false, std::string(path.substr(start, cursor - start)), 0});
    }
    return true;
}

class JsonLocationFinder {
  public:
    explicit JsonLocationFinder(std::string_view text) : m_text(text) {}

    bool locate(std::string_view path, std::uint32_t& line, std::uint32_t& column) const {
        std::size_t cursor = skipWhitespace(0);
        if (cursor >= m_text.size()) {
            return false;
        }
        if (path.empty() || path == "$") {
            return setLocation(cursor, line, column);
        }
        std::vector<JsonPathAccess> access;
        if (!parseJsonPath(path, access) || access.empty()) {
            return false;
        }
        return findValue(cursor, access, 0, line, column);
    }

    void rootLocation(std::uint32_t& line, std::uint32_t& column) const {
        const std::size_t cursor = skipWhitespace(0);
        if (!setLocation(cursor, line, column)) {
            line = 1;
            column = 1;
        }
    }

  private:
    std::size_t skipWhitespace(std::size_t cursor) const {
        if (cursor == 0 && m_text.size() >= 3 && static_cast<unsigned char>(m_text[0]) == 0xefu &&
            static_cast<unsigned char>(m_text[1]) == 0xbbu && static_cast<unsigned char>(m_text[2]) == 0xbfu) {
            cursor = 3;
        }
        while (cursor < m_text.size()) {
            const char c = m_text[cursor];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++cursor;
                continue;
            }
            break;
        }
        return cursor;
    }

    bool setLocation(std::size_t offset, std::uint32_t& line, std::uint32_t& column) const {
        if (offset > m_text.size()) {
            return false;
        }
        std::uint32_t currentLine = 1;
        std::size_t lineStart = 0;
        for (std::size_t i = 0; i < offset; ++i) {
            if (m_text[i] == '\n') {
                ++currentLine;
                lineStart = i + 1;
            }
        }
        line = currentLine;
        column = static_cast<std::uint32_t>(offset - lineStart + 1u);
        return true;
    }

    bool parseString(std::size_t& cursor, std::string& out) const {
        if (cursor >= m_text.size() || m_text[cursor] != '"') {
            return false;
        }
        ++cursor;
        out.clear();
        while (cursor < m_text.size()) {
            const char c = m_text[cursor++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (cursor >= m_text.size()) {
                return false;
            }
            const char escaped = m_text[cursor++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                out.push_back(escaped);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u':
                if (cursor + 4 > m_text.size()) {
                    return false;
                }
                out.push_back('?');
                cursor += 4;
                break;
            default:
                return false;
            }
        }
        return false;
    }

    bool skipObject(std::size_t& cursor) const {
        ++cursor;
        for (;;) {
            cursor = skipWhitespace(cursor);
            if (cursor >= m_text.size()) {
                return false;
            }
            if (m_text[cursor] == '}') {
                ++cursor;
                return true;
            }
            std::string key;
            if (!parseString(cursor, key)) {
                return false;
            }
            cursor = skipWhitespace(cursor);
            if (cursor >= m_text.size() || m_text[cursor] != ':') {
                return false;
            }
            ++cursor;
            if (!skipValue(cursor)) {
                return false;
            }
            cursor = skipWhitespace(cursor);
            if (cursor < m_text.size() && m_text[cursor] == ',') {
                ++cursor;
                continue;
            }
            if (cursor < m_text.size() && m_text[cursor] == '}') {
                ++cursor;
                return true;
            }
            return false;
        }
    }

    bool skipArray(std::size_t& cursor) const {
        ++cursor;
        for (;;) {
            cursor = skipWhitespace(cursor);
            if (cursor >= m_text.size()) {
                return false;
            }
            if (m_text[cursor] == ']') {
                ++cursor;
                return true;
            }
            if (!skipValue(cursor)) {
                return false;
            }
            cursor = skipWhitespace(cursor);
            if (cursor < m_text.size() && m_text[cursor] == ',') {
                ++cursor;
                continue;
            }
            if (cursor < m_text.size() && m_text[cursor] == ']') {
                ++cursor;
                return true;
            }
            return false;
        }
    }

    bool skipValue(std::size_t& cursor) const {
        cursor = skipWhitespace(cursor);
        if (cursor >= m_text.size()) {
            return false;
        }
        const char c = m_text[cursor];
        if (c == '"') {
            std::string ignored;
            return parseString(cursor, ignored);
        }
        if (c == '{') {
            return skipObject(cursor);
        }
        if (c == '[') {
            return skipArray(cursor);
        }
        const std::size_t start = cursor;
        while (cursor < m_text.size()) {
            const char value = m_text[cursor];
            if (value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == ',' || value == ']' ||
                value == '}') {
                break;
            }
            ++cursor;
        }
        return cursor > start;
    }

    bool findValue(std::size_t& cursor, const std::vector<JsonPathAccess>& access, std::size_t index,
                   std::uint32_t& line, std::uint32_t& column) const {
        cursor = skipWhitespace(cursor);
        if (cursor >= m_text.size()) {
            return false;
        }
        const JsonPathAccess& current = access[index];
        if (current.isIndex) {
            if (m_text[cursor] != '[') {
                return false;
            }
            ++cursor;
            for (std::size_t item = 0;; ++item) {
                cursor = skipWhitespace(cursor);
                if (cursor >= m_text.size() || m_text[cursor] == ']') {
                    return false;
                }
                if (item == current.index) {
                    if (index + 1 == access.size()) {
                        return setLocation(cursor, line, column);
                    }
                    return findValue(cursor, access, index + 1, line, column);
                }
                if (!skipValue(cursor)) {
                    return false;
                }
                cursor = skipWhitespace(cursor);
                if (cursor < m_text.size() && m_text[cursor] == ',') {
                    ++cursor;
                    continue;
                }
                if (cursor < m_text.size() && m_text[cursor] == ']') {
                    return false;
                }
                return false;
            }
        }

        if (m_text[cursor] != '{') {
            return false;
        }
        ++cursor;
        for (;;) {
            cursor = skipWhitespace(cursor);
            if (cursor >= m_text.size() || m_text[cursor] == '}') {
                return false;
            }
            const std::size_t keyStart = cursor;
            std::string key;
            if (!parseString(cursor, key)) {
                return false;
            }
            cursor = skipWhitespace(cursor);
            if (cursor >= m_text.size() || m_text[cursor] != ':') {
                return false;
            }
            ++cursor;
            if (key == current.key) {
                if (index + 1 == access.size()) {
                    return setLocation(keyStart, line, column);
                }
                return findValue(cursor, access, index + 1, line, column);
            }
            if (!skipValue(cursor)) {
                return false;
            }
            cursor = skipWhitespace(cursor);
            if (cursor < m_text.size() && m_text[cursor] == ',') {
                ++cursor;
                continue;
            }
            if (cursor < m_text.size() && m_text[cursor] == '}') {
                return false;
            }
            return false;
        }
    }

    std::string_view m_text;
};

void jsonParseErrorLocation(std::string_view message, std::uint32_t& line, std::uint32_t& column) {
    line = 0;
    column = 0;
    const std::string_view marker = "parse error at line ";
    const std::size_t markerPos = message.find(marker);
    if (markerPos == std::string_view::npos) {
        return;
    }
    std::size_t cursor = markerPos + marker.size();
    std::uint32_t parsedLine = 0;
    while (cursor < message.size() && message[cursor] >= '0' && message[cursor] <= '9') {
        parsedLine = parsedLine * 10u + static_cast<std::uint32_t>(message[cursor] - '0');
        ++cursor;
    }
    const std::string_view columnMarker = ", column ";
    if (cursor + columnMarker.size() > message.size() || message.substr(cursor, columnMarker.size()) != columnMarker) {
        return;
    }
    cursor += columnMarker.size();
    std::uint32_t parsedColumn = 0;
    while (cursor < message.size() && message[cursor] >= '0' && message[cursor] <= '9') {
        parsedColumn = parsedColumn * 10u + static_cast<std::uint32_t>(message[cursor] - '0');
        ++cursor;
    }
    line = parsedLine;
    column = parsedColumn;
}

void validateExpressionValue(const Value& value, const ExpressionContext& context, const std::string& path,
                             std::vector<ParseError>& errors) {
    if (!errors.empty()) {
        return;
    }
    if (value.kind == Value::Kind::Expression) {
        const CompiledExpression compiled = compileExpression(value, context, path);
        if (!compiled.ok()) {
            if (!compiled.errors.empty()) {
                errors.push_back(ParseError{compiled.errors.front().path, compiled.errors.front().message});
            } else {
                errors.push_back(ParseError{path, "failed to compile expression"});
            }
        }
        return;
    }
    if (value.kind == Value::Kind::Array) {
        for (std::size_t i = 0; i < value.array.size() && errors.empty(); ++i) {
            validateExpressionValue(value.array[i], context, path + "[" + std::to_string(i) + "]", errors);
        }
    } else if (value.kind == Value::Kind::Object) {
        for (const auto& [name, member] : value.object) {
            if (!errors.empty()) {
                break;
            }
            validateExpressionValue(member, context, child(path, name), errors);
        }
    }
}

void validateBlockExpressions(const std::vector<Block>& blocks, const std::string& path,
                              const ExpressionContext& context, std::vector<ParseError>& errors) {
    for (std::size_t i = 0; i < blocks.size() && errors.empty(); ++i) {
        const Block& block = blocks[i];
        const std::string blockPath = element(path, i);
        if (block.parameters.kind != Value::Kind::Object) {
            continue;
        }
        for (const auto& [name, value] : block.parameters.object) {
            if (!errors.empty()) {
                break;
            }
            validateExpressionValue(value, context, child(blockPath, name), errors);
        }
    }
}

void validateCustomHlslBlocks(const std::vector<Block>& blocks, const std::string& path, const Graph& graph,
                              std::vector<ParseError>& errors) {
    for (std::size_t i = 0; i < blocks.size() && errors.empty(); ++i) {
        if (blocks[i].type != "custom_hlsl") {
            continue;
        }
        CustomHlslBlock program;
        CustomHlslError error;
        if (readCustomHlslBlock(blocks[i], graph, element(path, i), program, error)) {
            continue;
        }
        ParseError diagnostic;
        diagnostic.path = std::move(error.path);
        diagnostic.message = std::move(error.message);
        diagnostic.line = error.line;
        diagnostic.column = error.column;
        if (error.line != 0) {
            diagnostic.location = ErrorLocationKind::Hlsl;
        }
        errors.push_back(std::move(diagnostic));
    }
}

bool validateGraphCustomHlsl(const Graph& graph, std::vector<ParseError>& errors) {
    for (std::size_t i = 0; i < graph.subgraphs.size() && errors.empty(); ++i) {
        const std::string subgraphPath = element("subgraphs", i);
        validateCustomHlslBlocks(graph.subgraphs[i].initBlocks, child(child(subgraphPath, "init"), "blocks"), graph,
                                 errors);
        validateCustomHlslBlocks(graph.subgraphs[i].updateBlocks, child(child(subgraphPath, "update"), "blocks"), graph,
                                 errors);
    }
    for (std::size_t i = 0; i < graph.systems.size() && errors.empty(); ++i) {
        const std::string systemPath = element("systems", i);
        validateCustomHlslBlocks(graph.systems[i].initBlocks, child(child(systemPath, "init"), "blocks"), graph,
                                 errors);
        validateCustomHlslBlocks(graph.systems[i].updateBlocks, child(child(systemPath, "update"), "blocks"), graph,
                                 errors);
    }
    return errors.empty();
}

bool validateGraphExpressions(const Graph& graph, std::vector<ParseError>& errors) {
    const ExpressionContext context(graph);
    for (std::size_t i = 0; i < graph.subgraphs.size() && errors.empty(); ++i) {
        const SubgraphDesc& subgraph = graph.subgraphs[i];
        const std::string subgraphPath = element("subgraphs", i);
        validateBlockExpressions(subgraph.initBlocks, child(child(subgraphPath, "init"), "blocks"), context,
                                 errors);
        validateBlockExpressions(subgraph.updateBlocks, child(child(subgraphPath, "update"), "blocks"), context,
                                 errors);
    }
    for (std::size_t i = 0; i < graph.systems.size() && errors.empty(); ++i) {
        const SystemDesc& system = graph.systems[i];
        const std::string systemPath = element("systems", i);
        validateBlockExpressions(system.initBlocks, child(child(systemPath, "init"), "blocks"), context, errors);
        validateBlockExpressions(system.updateBlocks, child(child(systemPath, "update"), "blocks"), context, errors);
        const std::string eventsPath = child(systemPath, "events");
        for (std::size_t j = 0; j < system.events.receive.size() && errors.empty(); ++j) {
            const std::string eventPath = child(element(child(eventsPath, "receive"), j), "attributes");
            validateExpressionValue(system.events.receive[j].attributes, context, eventPath, errors);
        }
        for (std::size_t j = 0; j < system.events.emit.size() && errors.empty(); ++j) {
            const std::string eventPath = child(element(child(eventsPath, "emit"), j), "attributes");
            validateExpressionValue(system.events.emit[j].attributes, context, eventPath, errors);
        }
    }
    return errors.empty();
}

class GraphParser {
public:
    GraphParser(const Json& root, std::vector<ParseError>& errors, GraphParseMode mode)
        : m_root(root), m_errors(errors), m_mode(mode) {}

    std::unique_ptr<Graph> parse() {
        if (!m_root.is_object()) {
            fail("$", "root must be an object");
            return nullptr;
        }

        static constexpr const char* kRootFields[] = {
            "version", "name", "culling", "properties", "attributes", "subgraphs", "systems",
        };
        if (!checkKnownFields(m_root, "$", kRootFields)) {
            return nullptr;
        }

        auto graph = std::make_unique<Graph>();
        if (!readRequiredUint(m_root, "version", publicPath("version"), graph->version)) {
            return nullptr;
        }
        if (graph->version != 1) {
            fail(publicPath("version"), "unsupported asset version " + std::to_string(graph->version));
            return nullptr;
        }
        if (!readRequiredString(m_root, "name", publicPath("name"), graph->name)) {
            return nullptr;
        }
        if (graph->name.empty()) {
            fail(publicPath("name"), "must not be empty");
            return nullptr;
        }

        const auto culling = m_root.find("culling");
        if (culling != m_root.end()) {
            if (!culling->is_string()) {
                fail(publicPath("culling"), "must be a string");
                return nullptr;
            }
            const std::string name = culling->get<std::string>();
            if (name == "none") {
                graph->cullingFlags = 0u;
            } else if (name == "simulation") {
                graph->cullingFlags = kCullSimulation;
            } else if (name == "visible") {
                graph->cullingFlags = kCullSimulation | kCullBoundsUpdate;
            } else {
                fail(publicPath("culling"), "unknown culling mode '" + name + "'");
                return nullptr;
            }
        }

        const auto properties = m_root.find("properties");
        if (properties != m_root.end()) {
            if (!properties->is_array()) {
                fail(publicPath("properties"), "must be an array");
                return nullptr;
            }
            graph->properties.reserve(properties->size());
            for (std::size_t i = 0; i < properties->size(); ++i) {
                Property property;
                const std::string path = element("properties", i);
                if (!parseProperty((*properties)[i], path, property)) {
                    return nullptr;
                }
                if (findProperty(graph->properties, property.name) != nullptr) {
                    fail(child(path, "name"), "duplicate property '" + property.name + "'");
                    return nullptr;
                }
                if (findBuiltInAttribute(property.name) != nullptr) {
                    fail(child(path, "name"), "property name '" + property.name + "' is reserved by a built-in attribute");
                    return nullptr;
                }
                graph->properties.push_back(std::move(property));
            }
        }

        const auto attributes = m_root.find("attributes");
        if (attributes != m_root.end()) {
            if (!attributes->is_array()) {
                fail(publicPath("attributes"), "must be an array");
                return nullptr;
            }
            if (attributes->size() > 8) {
                fail(publicPath("attributes"), "at most 8 custom attributes are supported");
                return nullptr;
            }
            graph->attributes.reserve(attributes->size());
            for (std::size_t i = 0; i < attributes->size(); ++i) {
                AttributeDesc attribute;
                const std::string path = element("attributes", i);
                if (!parseAttribute((*attributes)[i], path, attribute)) {
                    return nullptr;
                }
                if (findBuiltInAttribute(attribute.name) != nullptr) {
                    fail(child(path, "name"), "custom attribute '" + attribute.name + "' conflicts with a built-in attribute");
                    return nullptr;
                }
                if (findAttribute(graph->attributes, attribute.name) != nullptr) {
                    fail(child(path, "name"), "duplicate custom attribute '" + attribute.name + "'");
                    return nullptr;
                }
                graph->attributes.push_back(std::move(attribute));
            }
        }

        const auto subgraphs = m_root.find("subgraphs");
        if (subgraphs != m_root.end()) {
            if (!subgraphs->is_array()) {
                fail(publicPath("subgraphs"), "must be an array");
                return nullptr;
            }
            graph->subgraphs.reserve(subgraphs->size());
            for (std::size_t i = 0; i < subgraphs->size(); ++i) {
                SubgraphDesc subgraph;
                const std::string path = element("subgraphs", i);
                if (!parseSubgraph((*subgraphs)[i], path, subgraph)) {
                    return nullptr;
                }
                if (std::any_of(graph->subgraphs.begin(), graph->subgraphs.end(),
                                [&](const SubgraphDesc& existing) { return existing.name == subgraph.name; })) {
                    fail(child(path, "name"), "duplicate subgraph '" + subgraph.name + "'");
                    return nullptr;
                }
                graph->subgraphs.push_back(std::move(subgraph));
            }
        }

        const auto systems = m_root.find("systems");
        if (systems == m_root.end() || !systems->is_array() || systems->empty()) {
            fail(publicPath("systems"), "must be a non-empty array");
            return nullptr;
        }
        graph->systems.reserve(systems->size());
        for (std::size_t i = 0; i < systems->size(); ++i) {
            SystemDesc system;
            const std::string path = element("systems", i);
            if (!parseSystem((*systems)[i], path, system, graph->properties)) {
                return nullptr;
            }
            if (std::any_of(graph->systems.begin(), graph->systems.end(),
                            [&](const SystemDesc& existing) { return existing.name == system.name; })) {
                fail(child(path, "name"), "duplicate system '" + system.name + "'");
                return nullptr;
            }
            graph->systems.push_back(std::move(system));
        }

        if (graph->cullingFlags != 0u &&
            std::any_of(graph->systems.begin(), graph->systems.end(), [](const SystemDesc& system) {
                return system.bounds.mode == BoundsMode::Automatic;
            })) {
            fail(publicPath("culling"), "cannot cull automatic bounds systems");
            return nullptr;
        }

        if (m_mode == GraphParseMode::PreserveSubgraphs) {
            Graph flattened = *graph;
            if (!detail::flattenSubgraphs(flattened, m_errors)) {
                return nullptr;
            }
            if (!validateGraphCustomHlsl(*graph, m_errors)) {
                return nullptr;
            }
            if (!validateGraphExpressions(flattened, m_errors)) {
                return nullptr;
            }
        } else {
            if (!detail::flattenSubgraphs(*graph, m_errors)) {
                return nullptr;
            }
            if (!validateGraphCustomHlsl(*graph, m_errors)) {
                return nullptr;
            }
            if (!validateGraphExpressions(*graph, m_errors)) {
                return nullptr;
            }
        }
        if (!m_errors.empty()) {
            return nullptr;
        }
        return graph;
    }

private:
    bool fail(std::string path, std::string message) {
        m_errors.push_back(ParseError{std::move(path), std::move(message)});
        return false;
    }

    static std::string publicPath(std::string_view path) {
        return path == "$" ? std::string() : std::string(path);
    }

    template <std::size_t N>
    bool checkKnownFields(const Json& object, const std::string& path, const char* const (&fields)[N]) {
        for (const auto& [name, value] : object.items()) {
            (void)value;
            bool known = false;
            for (const char* field : fields) {
                if (name == field) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                return fail(path == "$" ? name : child(path, name), "unknown field");
            }
        }
        return true;
    }

    bool readRequiredString(const Json& object, const char* key, const std::string& path, std::string& out) {
        const auto found = object.find(key);
        if (found == object.end()) {
            return fail(path, "is required");
        }
        if (!found->is_string()) {
            return fail(path, "must be a string");
        }
        out = found->get<std::string>();
        return true;
    }

    bool readRequiredUint(const Json& object, const char* key, const std::string& path, std::uint32_t& out) {
        const auto found = object.find(key);
        if (found == object.end()) {
            return fail(path, "is required");
        }
        return readUint(*found, path, out);
    }

    bool readUint(const Json& value, const std::string& path, std::uint32_t& out) {
        if (!value.is_number()) {
            return fail(path, "must be an integer");
        }
        const double number = value.get<double>();
        if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number ||
            number > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
            return fail(path, "must be a non-negative 32-bit integer");
        }
        out = static_cast<std::uint32_t>(number);
        return true;
    }

    bool readInt(const Json& value, const std::string& path, std::int32_t& out) {
        if (!value.is_number()) {
            return fail(path, "must be an integer");
        }
        const double number = value.get<double>();
        if (!std::isfinite(number) || std::floor(number) != number ||
            number < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
            number > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
            return fail(path, "must be a 32-bit integer");
        }
        out = static_cast<std::int32_t>(number);
        return true;
    }

    bool readFloat(const Json& value, const std::string& path, float& out) {
        if (!value.is_number()) {
            return fail(path, "must be a finite number");
        }
        const double number = value.get<double>();
        if (!std::isfinite(number) || number < -std::numeric_limits<float>::max() ||
            number > std::numeric_limits<float>::max()) {
            return fail(path, "must be a finite float");
        }
        out = static_cast<float>(number);
        return true;
    }

    bool readBool(const Json& object, const char* key, const std::string& path, bool& out) {
        const auto found = object.find(key);
        if (found == object.end()) {
            return true;
        }
        if (!found->is_boolean()) {
            return fail(path, "must be a boolean");
        }
        out = found->get<bool>();
        return true;
    }

    bool readFloatArray(const Json& value, const std::string& path, std::size_t count,
                        std::array<float, 4>& out) {
        if (!value.is_array() || value.size() != count) {
            return fail(path, "must be an array of " + std::to_string(count) + " numbers");
        }
        for (std::size_t i = 0; i < count; ++i) {
            if (!readFloat(value[i], element(path, i), out[i])) {
                return false;
            }
        }
        return true;
    }

    bool readVec3(const Json& value, const std::string& path, Vec3& out) {
        std::array<float, 4> values{};
        if (!readFloatArray(value, path, 3, values)) {
            return false;
        }
        out = Vec3{values[0], values[1], values[2]};
        return true;
    }

    const Value* parseValue(const Json& value, const std::string& path, std::size_t depth,
                            std::optional<Value>& out) {
        if (depth > kMaxJsonDepth) {
            fail(path, "JSON nesting depth exceeds 64");
            return nullptr;
        }

        Value result;
        if (value.is_number()) {
            const double number = value.get<double>();
            if (!std::isfinite(number)) {
                fail(path, "number must be finite");
                return nullptr;
            }
            result.kind = Value::Kind::Number;
            result.number = number;
        } else if (value.is_boolean()) {
            result.kind = Value::Kind::Bool;
            result.boolean = value.get<bool>();
        } else if (value.is_string()) {
            result.kind = Value::Kind::String;
            result.string = value.get<std::string>();
        } else if (value.is_array()) {
            result.kind = Value::Kind::Array;
            result.array.reserve(value.size());
            for (std::size_t i = 0; i < value.size(); ++i) {
                std::optional<Value> item;
                if (parseValue(value[i], element(path, i), depth + 1, item) == nullptr) {
                    return nullptr;
                }
                result.array.push_back(std::move(*item));
            }
        } else if (value.is_object()) {
            const auto op = value.find("op");
            if (op != value.end()) {
                if (!op->is_string()) {
                    fail(child(path, "op"), "must be a string");
                    return nullptr;
                }
                const std::string opName = op->get<std::string>();
                if (!isKnownExpressionOp(opName)) {
                    fail(child(path, "op"), "unknown expression op '" + opName + "'");
                    return nullptr;
                }
                auto expression = std::make_shared<Expression>();
                expression->op = opName;
                for (const auto& [name, nested] : value.items()) {
                    if (name == "op") {
                        continue;
                    }
                    std::optional<Value> argument;
                    if (parseValue(nested, child(path, name), depth + 1, argument) == nullptr) {
                        return nullptr;
                    }
                    expression->arguments.emplace_back(name, std::move(*argument));
                }
                result.kind = Value::Kind::Expression;
                result.expression = std::move(expression);
            } else {
                result.kind = Value::Kind::Object;
                for (const auto& [name, nested] : value.items()) {
                    std::optional<Value> member;
                    if (parseValue(nested, child(path, name), depth + 1, member) == nullptr) {
                        return nullptr;
                    }
                    result.object.emplace_back(name, std::move(*member));
                }
            }
        } else {
            fail(path, "unsupported JSON value");
            return nullptr;
        }

        out = std::move(result);
        return &*out;
    }

    bool parseValueChecked(const Json& value, const std::string& path, Value& out) {
        std::optional<Value> parsed;
        if (parseValue(value, path, 0, parsed) == nullptr) {
            return false;
        }
        out = std::move(*parsed);
        return true;
    }

    bool parseCurveKeys(const Json& keys, const std::string& path, std::vector<CurveKey>& out) {
        if (!keys.is_array() || keys.empty()) {
            return fail(path, "must be a non-empty array");
        }
        out.reserve(keys.size());
        for (std::size_t i = 0; i < keys.size(); ++i) {
            const auto& key = keys[i];
            const std::string keyPath = element(path, i);
            if (!key.is_array() || key.size() != 2) {
                return fail(keyPath, "must be [time, value]");
            }
            CurveKey parsed;
            if (!readFloat(key[0], element(keyPath, 0), parsed.time) ||
                !readFloat(key[1], element(keyPath, 1), parsed.value)) {
                return false;
            }
            out.push_back(parsed);
        }
        std::stable_sort(out.begin(), out.end(), [](const CurveKey& a, const CurveKey& b) {
            return a.time < b.time;
        });
        for (std::size_t i = 1; i < out.size(); ++i) {
            if (out[i].time == out[i - 1].time) {
                return fail(path, "curve keys must have unique times");
            }
        }
        return true;
    }

    bool parseGradientKeys(const Json& keys, const std::string& path, std::vector<GradientKey>& out) {
        if (!keys.is_array() || keys.empty()) {
            return fail(path, "must be a non-empty array");
        }
        out.reserve(keys.size());
        for (std::size_t i = 0; i < keys.size(); ++i) {
            const auto& key = keys[i];
            const std::string keyPath = element(path, i);
            if (!key.is_object()) {
                return fail(keyPath, "must be an object");
            }
            const auto t = key.find("t");
            const auto color = key.find("color");
            if (t == key.end()) {
                return fail(child(keyPath, "t"), "is required");
            }
            if (color == key.end()) {
                return fail(child(keyPath, "color"), "is required");
            }
            GradientKey parsed;
            std::array<float, 4> colorValues{};
            if (!readFloat(*t, child(keyPath, "t"), parsed.time) ||
                !readFloatArray(*color, child(keyPath, "color"), 4, colorValues)) {
                return false;
            }
            parsed.color = Vec4{colorValues[0], colorValues[1], colorValues[2], colorValues[3]};
            for (const auto& [name, value] : key.items()) {
                (void)value;
                if (name != "t" && name != "color") {
                    return fail(child(keyPath, name), "unknown field");
                }
            }
            out.push_back(parsed);
        }
        std::stable_sort(out.begin(), out.end(), [](const GradientKey& a, const GradientKey& b) {
            return a.time < b.time;
        });
        for (std::size_t i = 1; i < out.size(); ++i) {
            if (out[i].time == out[i - 1].time) {
                return fail(path, "gradient keys must have unique times");
            }
        }
        return true;
    }

    bool parseProperty(const Json& value, const std::string& path, Property& out) {
        if (!value.is_object()) {
            return fail(path, "must be an object");
        }
        static constexpr const char* kFields[] = {"name", "type", "default", "keys"};
        if (!checkKnownFields(value, path, kFields)) {
            return false;
        }
        if (!readRequiredString(value, "name", child(path, "name"), out.name)) {
            return false;
        }
        if (!isIdentifier(out.name)) {
            return fail(child(path, "name"), "must be a valid identifier");
        }
        if (findBuiltInAttribute(out.name) != nullptr) {
            return fail(child(path, "name"), "property name is reserved by a built-in attribute");
        }

        std::string typeName;
        if (!readRequiredString(value, "type", child(path, "type"), typeName)) {
            return false;
        }
        if (typeName == "float") {
            out.type = PropertyType::Float;
        } else if (typeName == "int") {
            out.type = PropertyType::Int;
        } else if (typeName == "bool") {
            out.type = PropertyType::Bool;
        } else if (typeName == "vector2") {
            out.type = PropertyType::Vector2;
        } else if (typeName == "vector3") {
            out.type = PropertyType::Vector3;
        } else if (typeName == "vector4") {
            out.type = PropertyType::Vector4;
        } else if (typeName == "color") {
            out.type = PropertyType::Color;
        } else if (typeName == "texture") {
            out.type = PropertyType::Texture;
        } else if (typeName == "curve") {
            out.type = PropertyType::Curve;
        } else if (typeName == "gradient") {
            out.type = PropertyType::Gradient;
        } else {
            return fail(child(path, "type"), "unknown property type '" + typeName + "'");
        }

        const auto defaultIt = value.find("default");
        const auto keysIt = value.find("keys");
        if (out.type == PropertyType::Curve) {
            if (defaultIt != value.end()) {
                return fail(child(path, "default"), "curve properties use 'keys', not 'default'");
            }
            if (keysIt == value.end()) {
                return fail(child(path, "keys"), "is required for curve properties");
            }
            if (!parseCurveKeys(*keysIt, child(path, "keys"), out.curve)) {
                return false;
            }
            return true;
        }
        if (out.type == PropertyType::Gradient) {
            if (defaultIt != value.end()) {
                return fail(child(path, "default"), "gradient properties use 'keys', not 'default'");
            }
            if (keysIt == value.end()) {
                return fail(child(path, "keys"), "is required for gradient properties");
            }
            if (!parseGradientKeys(*keysIt, child(path, "keys"), out.gradient)) {
                return false;
            }
            return true;
        }
        if (keysIt != value.end()) {
            return fail(child(path, "keys"), "only curve/gradient properties use 'keys'");
        }
        if (defaultIt == value.end()) {
            return fail(child(path, "default"), "is required for this property type");
        }

        switch (out.type) {
        case PropertyType::Float:
            return readFloat(*defaultIt, child(path, "default"), out.defaultFloat[0]);
        case PropertyType::Int: {
            std::int32_t integer = 0;
            if (!readInt(*defaultIt, child(path, "default"), integer)) {
                return false;
            }
            out.defaultFloat[0] = static_cast<float>(integer);
            return true;
        }
        case PropertyType::Bool:
            if (!defaultIt->is_boolean()) {
                return fail(child(path, "default"), "must be a boolean");
            }
            out.defaultFloat[0] = defaultIt->get<bool>() ? 1.0f : 0.0f;
            return true;
        case PropertyType::Vector2:
            return readFloatArray(*defaultIt, child(path, "default"), 2, out.defaultFloat);
        case PropertyType::Vector3:
            return readFloatArray(*defaultIt, child(path, "default"), 3, out.defaultFloat);
        case PropertyType::Vector4:
        case PropertyType::Color:
            return readFloatArray(*defaultIt, child(path, "default"), 4, out.defaultFloat);
        case PropertyType::Texture:
            if (!defaultIt->is_string()) {
                return fail(child(path, "default"), "must be a string path");
            }
            out.defaultTexture = defaultIt->get<std::string>();
            return true;
        case PropertyType::Curve:
        case PropertyType::Gradient:
            break;
        }
        return true;
    }

    bool parseAttribute(const Json& value, const std::string& path, AttributeDesc& out) {
        if (!value.is_object()) {
            return fail(path, "must be an object");
        }
        static constexpr const char* kFields[] = {"name", "type", "default"};
        if (!checkKnownFields(value, path, kFields)) {
            return false;
        }
        if (!readRequiredString(value, "name", child(path, "name"), out.name)) {
            return false;
        }
        if (!isIdentifier(out.name)) {
            return fail(child(path, "name"), "must be a valid identifier");
        }

        std::string typeName;
        if (!readRequiredString(value, "type", child(path, "type"), typeName)) {
            return false;
        }
        if (typeName == "float") {
            out.type = AttributeType::Float;
        } else if (typeName == "float2") {
            out.type = AttributeType::Float2;
        } else if (typeName == "float3") {
            out.type = AttributeType::Float3;
        } else if (typeName == "float4") {
            out.type = AttributeType::Float4;
        } else if (typeName == "uint") {
            out.type = AttributeType::UInt;
        } else if (typeName == "int") {
            out.type = AttributeType::Int;
        } else if (typeName == "bool") {
            out.type = AttributeType::Bool;
        } else {
            return fail(child(path, "type"), "unknown attribute type '" + typeName + "'");
        }

        const auto defaultIt = value.find("default");
        if (defaultIt == value.end()) {
            return true;
        }
        const std::string defaultPath = child(path, "default");
        switch (out.type) {
        case AttributeType::Float:
            return readFloat(*defaultIt, defaultPath, out.defaultFloat[0]);
        case AttributeType::Float2:
            return readFloatArray(*defaultIt, defaultPath, 2, out.defaultFloat);
        case AttributeType::Float3:
            return readFloatArray(*defaultIt, defaultPath, 3, out.defaultFloat);
        case AttributeType::Float4:
            return readFloatArray(*defaultIt, defaultPath, 4, out.defaultFloat);
        case AttributeType::UInt: {
            std::uint32_t integer = 0;
            if (!readUint(*defaultIt, defaultPath, integer)) {
                return false;
            }
            out.defaultUint[0] = integer;
            return true;
        }
        case AttributeType::Int: {
            std::int32_t integer = 0;
            if (!readInt(*defaultIt, defaultPath, integer)) {
                return false;
            }
            out.defaultUint[0] = static_cast<std::uint32_t>(integer);
            return true;
        }
        case AttributeType::Bool:
            if (!defaultIt->is_boolean()) {
                return fail(defaultPath, "must be a boolean");
            }
            out.defaultUint[0] = defaultIt->get<bool>() ? 1u : 0u;
            return true;
        }
        return true;
    }

    bool parseBlocks(const Json& value, const std::string& path, bool update, std::vector<Block>& out) {
        if (!value.is_array()) {
            return fail(path, "must be an array");
        }
        out.reserve(value.size());
        for (std::size_t i = 0; i < value.size(); ++i) {
            const auto& item = value[i];
            const std::string blockPath = element(path, i);
            if (!item.is_object()) {
                return fail(blockPath, "must be an object");
            }
            const auto typeIt = item.find("type");
            if (typeIt == item.end()) {
                return fail(child(blockPath, "type"), "is required");
            }
            if (!typeIt->is_string()) {
                return fail(child(blockPath, "type"), "must be a string");
            }
            Block block;
            block.type = typeIt->get<std::string>();
            const BlockSpec* spec = findBlockSpec(block.type, update);
            if (spec == nullptr) {
                return fail(child(blockPath, "type"), "unknown " + std::string(update ? "update" : "init") +
                                                          " block type '" + block.type + "'");
            }

            if (!readBool(item, "enabled", child(blockPath, "enabled"), block.enabled)) {
                return false;
            }

            block.parameters.kind = Value::Kind::Object;
            for (const auto& [name, nested] : item.items()) {
                if (name == "type" || name == "enabled") {
                    continue;
                }
                const auto field = std::find_if(spec->fields.begin(), spec->fields.end(),
                                                [&](const FieldSpec& fieldSpec) {
                                                    return name == fieldSpec.name;
                                                });
                if (field == spec->fields.end()) {
                    return fail(child(blockPath, name), "unknown field for block '" + block.type + "'");
                }
                std::optional<Value> parameter;
                if (parseValue(nested, child(blockPath, name), 0, parameter) == nullptr) {
                    return false;
                }
                block.parameters.object.emplace_back(name, std::move(*parameter));
            }
            for (const FieldSpec& field : spec->fields) {
                if (field.required && block.parameter(field.name) == nullptr) {
                    return fail(child(blockPath, field.name), "is required for block '" + block.type + "'");
                }
            }
            if (!validateBlockFieldShapes(block, blockPath)) {
                return false;
            }
            out.push_back(std::move(block));
        }
        return true;
    }

    bool validateBlockFieldShapes(const Block& block, const std::string& path) {
        const auto vector3 = [&](std::string_view field) {
            const Value* value = block.parameter(field);
            if (value == nullptr || value->isExpression()) {
                return true;
            }
            if (!value->array.empty()) {
                if (value->array.size() != 3) {
                    return fail(child(child(path, field), "length"), "must contain 3 components");
                }
                return true;
            }
            return fail(child(path, field), "must be a float3");
        };
        const auto vector3or4 = [&](std::string_view field) {
            const Value* value = block.parameter(field);
            if (value == nullptr || value->isExpression()) {
                return true;
            }
            if (!value->array.empty()) {
                if (value->array.size() != 3 && value->array.size() != 4) {
                    return fail(child(path, field), "must contain 3 or 4 components");
                }
                return true;
            }
            return fail(child(path, field), "must be a float3/float4");
        };
        const auto scalar = [&](std::string_view field) {
            const Value* value = block.parameter(field);
            if (value == nullptr || value->isExpression()) {
                return true;
            }
            if (value->kind != Value::Kind::Number) {
                return fail(child(path, field), "must be a number or expression");
            }
            return true;
        };
        const auto integer = [&](std::string_view field) {
            const Value* value = block.parameter(field);
            if (value == nullptr || value->isExpression()) {
                return true;
            }
            if (value->kind != Value::Kind::Number || std::floor(value->number) != value->number) {
                return fail(child(path, field), "must be an integer or expression");
            }
            return true;
        };
        const auto string = [&](std::string_view field) {
            const Value* value = block.parameter(field);
            if (value == nullptr || value->isExpression()) {
                return true;
            }
            if (value->kind != Value::Kind::String) {
                return fail(child(path, field), "must be a string or expression");
            }
            return true;
        };
        const auto boolean = [&](std::string_view field) {
            const Value* value = block.parameter(field);
            if (value == nullptr || value->isExpression()) {
                return true;
            }
            if (value->kind != Value::Kind::Bool) {
                return fail(child(path, field), "must be a boolean or expression");
            }
            return true;
        };

        if (block.type == "set_position_sphere" || block.type == "set_position_box") {
            if (!vector3("center")) return false;
        }
        if (block.type == "set_position_box" && !vector3("size")) return false;
        if (block.type == "set_position_sphere") {
            if (!scalar("radius")) return false;
            if (!boolean("surface")) return false;
        }
        if (block.type == "set_velocity_random") {
            if (!scalar("minSpeed") || !scalar("maxSpeed")) return false;
            if (!vector3("direction")) return false;
            if (!scalar("coneAngle")) return false;
        }
        if (block.type == "set_velocity_direction") {
            if (!vector3("direction") || !scalar("speed")) return false;
        }
        if (block.type == "set_color" && !vector3or4("color")) return false;
        if (block.type == "set_alpha" && !scalar("alpha")) return false;
        if (block.type == "set_size" && !scalar("size")) return false;
        if (block.type == "set_lifetime" && !scalar("lifetime")) return false;
        if (block.type == "set_angle" && !scalar("angle")) return false;
        if (block.type == "set_scale" && !vector3("scale")) return false;
        if (block.type == "set_attribute" && !string("attribute")) return false;
        if (block.type == "set_random" && !string("attribute")) return false;
        if (block.type == "set_random" && (!scalar("min") || !scalar("max"))) return false;
        if (block.type == "gravity" && !vector3("gravity")) return false;
        if (block.type == "drag") {
            if (!scalar("drag") || !boolean("useSize")) return false;
        }
        if (block.type == "force" && !vector3("force")) return false;
        if (block.type == "force") {
            if (!scalar("drag")) return false;
            const Value* mode = block.parameter("mode");
            if (mode != nullptr && !mode->isExpression() &&
                (mode->kind != Value::Kind::String ||
                 (mode->string != "absolute" && mode->string != "relative"))) {
                return fail(child(path, "mode"), "must be 'absolute' or 'relative'");
            }
            const Value* space = block.parameter("space");
            if (space != nullptr && !space->isExpression() &&
                (space->kind != Value::Kind::String ||
                 (space->string != "world" && space->string != "local"))) {
                return fail(child(path, "space"), "must be 'world' or 'local'");
            }
        }
        if (block.type == "attractor") {
            if (!vector3("position") || !scalar("strength") || !scalar("falloff")) return false;
        }
        if (block.type == "turbulence") {
            if (!scalar("intensity") || !scalar("frequency") || !scalar("roughness") ||
                !scalar("lacunarity") || !integer("octaves") || !scalar("drag")) {
                return false;
            }
            if (!vector3("scroll")) return false;
            const Value* mode = block.parameter("mode");
            if (mode != nullptr && !mode->isExpression() &&
                (mode->kind != Value::Kind::String ||
                 (mode->string != "absolute" && mode->string != "relative"))) {
                return fail(child(path, "mode"), "must be 'absolute' or 'relative'");
            }
        }
        if (block.type == "reap") {
            const Value* condition = block.parameter("condition");
            if (condition != nullptr && condition->kind != Value::Kind::Expression) {
                return fail(child(path, "condition"), "must be an expression");
            }
        }
        if (block.type == "collision_plane") {
            if (!vector3("normal") || !scalar("distance")) return false;
        }
        if (block.type == "collision_sphere") {
            if (!vector3("center") || !scalar("radius")) return false;
        }
        if (block.type == "collision_plane" || block.type == "collision_sphere") {
            const Value* mode = block.parameter("mode");
            if (mode != nullptr && !mode->isExpression() &&
                (mode->kind != Value::Kind::String ||
                 (mode->string != "kill" && mode->string != "bounce" && mode->string != "stick"))) {
                return fail(child(path, "mode"), "must be 'kill', 'bounce', or 'stick'");
            }
            if (!scalar("restitution") || !scalar("friction")) return false;
        }
        if (block.type == "color_over_life") {
            const Value* gradient = block.parameter("gradient");
            if (gradient == nullptr || (!gradient->isExpression() && gradient->kind != Value::Kind::String)) {
                return fail(child(path, "gradient"), "must be a gradient property name or expression");
            }
        }
        if (block.type == "alpha_over_life" || block.type == "size_over_life") {
            const Value* curve = block.parameter("curve");
            if (curve == nullptr || (!curve->isExpression() && curve->kind != Value::Kind::String)) {
                return fail(child(path, "curve"), "must be a curve property name or expression");
            }
        }
        if (block.type == "trigger_event") {
            if (!string("event") || !scalar("count")) {
                return false;
            }
            const Value* condition = block.parameter("condition");
            if (condition != nullptr && !condition->isExpression() && condition->kind != Value::Kind::Bool) {
                return fail(child(path, "condition"), "must be a boolean or expression");
            }
        }
        if (block.type == "custom_hlsl") {
            const Value* code = block.parameter("code");
            if (code == nullptr || code->kind != Value::Kind::String) {
                return fail(child(path, "code"), "must be a string");
            }
            for (const char* field : {"reads", "writes"}) {
                const Value* list = block.parameter(field);
                if (list == nullptr) {
                    continue;
                }
                if (list->kind != Value::Kind::Array) {
                    return fail(child(path, field), "must be an array of attribute names");
                }
                for (std::size_t i = 0; i < list->array.size(); ++i) {
                    if (list->array[i].kind != Value::Kind::String) {
                        return fail(element(child(path, field), i), "must be a string");
                    }
                }
            }
        }
        return true;
    }

    bool parseOutput(const Json& value, const std::string& path, OutputDesc& out,
                     const std::vector<Property>& properties) {
        if (!value.is_object()) {
            return fail(path, "must be an object");
        }
        static constexpr const char* kFields[] = {
            "type", "lit", "blend", "texture", "softParticles", "softDistance", "castShadows",
            "generateMotionVector", "sortPriority", "metallic", "roughness", "sort", "sortOrder",
        };
        if (!checkKnownFields(value, path, kFields)) {
            return false;
        }
        std::string typeName = "quad";
        const auto typeIt = value.find("type");
        if (typeIt != value.end()) {
            if (!typeIt->is_string()) {
                return fail(child(path, "type"), "must be a string");
            }
            typeName = typeIt->get<std::string>();
        }
        if (typeName != "quad") {
            return fail(child(path, "type"), "V1 supports only 'quad' outputs");
        }
        out.type = OutputType::Quad;

        if (!readBool(value, "lit", child(path, "lit"), out.lit) ||
            !readBool(value, "softParticles", child(path, "softParticles"), out.softParticles) ||
            !readBool(value, "castShadows", child(path, "castShadows"), out.castShadows) ||
            !readBool(value, "generateMotionVector", child(path, "generateMotionVector"),
                      out.generateMotionVector)) {
            return false;
        }
        const auto sortPriority = value.find("sortPriority");
        if (sortPriority != value.end()) {
            const std::string fieldPath = child(path, "sortPriority");
            if (sortPriority->is_number_unsigned()) {
                const std::uint64_t raw = sortPriority->get<std::uint64_t>();
                if (raw > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
                    return fail(fieldPath, "must fit in int32");
                }
                out.sortPriority = static_cast<std::int32_t>(raw);
            } else if (sortPriority->is_number_integer()) {
                const std::int64_t raw = sortPriority->get<std::int64_t>();
                if (raw < std::numeric_limits<std::int32_t>::min() ||
                    raw > static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())) {
                    return fail(fieldPath, "must fit in int32");
                }
                out.sortPriority = static_cast<std::int32_t>(raw);
            } else {
                return fail(fieldPath, "must be an integer");
            }
        }
        const auto softDistance = value.find("softDistance");
        if (softDistance != value.end()) {
            const std::string fieldPath = child(path, "softDistance");
            if (!readFloat(*softDistance, fieldPath, out.softDistance)) {
                return false;
            }
            if (out.softDistance < 0.001f) {
                return fail(fieldPath, "must be at least 0.001");
            }
        }

        const auto readMaterialScalar = [&](const char* field, float& constant, std::string& property) {
            const auto it = value.find(field);
            if (it == value.end()) {
                return true;
            }
            const std::string fieldPath = child(path, field);
            if (it->is_number()) {
                if (!readFloat(*it, fieldPath, constant)) {
                    return false;
                }
                if (constant < 0.0f || constant > 1.0f) {
                    return fail(fieldPath, "must be between 0 and 1");
                }
                return true;
            }
            if (!it->is_string()) {
                return fail(fieldPath, "must be a number or float property name");
            }
            property = it->get<std::string>();
            if (property.empty()) {
                return fail(fieldPath, "must not be empty");
            }
            const Property* found = findProperty(properties, property);
            if (found == nullptr) {
                return fail(fieldPath, "unknown property '" + property + "'");
            }
            if (found->type != PropertyType::Float) {
                return fail(fieldPath, "property '" + property + "' must be a float");
            }
            return true;
        };
        if (!readMaterialScalar("metallic", out.metallic, out.metallicProperty) ||
            !readMaterialScalar("roughness", out.roughness, out.roughnessProperty)) {
            return false;
        }

        const auto blend = value.find("blend");
        if (blend != value.end()) {
            if (!blend->is_string()) {
                return fail(child(path, "blend"), "must be a string");
            }
            const std::string name = blend->get<std::string>();
            if (name == "alpha") {
                out.blend = BlendMode::Alpha;
            } else if (name == "additive") {
                out.blend = BlendMode::Additive;
            } else if (name == "premultiply") {
                out.blend = BlendMode::Premultiply;
            } else {
                return fail(child(path, "blend"), "unknown blend mode '" + name + "'");
            }
        }

        const auto texture = value.find("texture");
        if (texture != value.end()) {
            if (!texture->is_string()) {
                return fail(child(path, "texture"), "must be a property name");
            }
            out.texture = texture->get<std::string>();
        }

        const auto sort = value.find("sort");
        if (sort != value.end()) {
            if (!sort->is_string()) {
                return fail(child(path, "sort"), "must be a string");
            }
            SortDesc parsed;
            const std::string mode = sort->get<std::string>();
            if (mode == "none") {
                parsed.mode = SortMode::None;
            } else if (mode == "distance") {
                parsed.mode = SortMode::Distance;
            } else if (mode == "custom") {
                parsed.mode = SortMode::Custom;
            } else if (mode == "cameraDepth") {
                parsed.mode = SortMode::CameraDepth;
            } else if (mode == "youngest") {
                parsed.mode = SortMode::YoungestInFront;
            } else {
                return fail(child(path, "sort"), "unknown sort mode '" + mode + "'");
            }
            out.sort = parsed;
        }
        const auto sortOrder = value.find("sortOrder");
        if (sortOrder != value.end()) {
            if (!sortOrder->is_string()) {
                return fail(child(path, "sortOrder"), "must be a string");
            }
            const std::string name = sortOrder->get<std::string>();
            if (name == "ascending") {
                out.sortOrder = SortOrder::Ascending;
            } else if (name == "descending") {
                out.sortOrder = SortOrder::Descending;
            } else {
                return fail(child(path, "sortOrder"), "unknown sort order '" + name + "'");
            }
        }
        return true;
    }

    bool parseEvents(const Json& value, const std::string& path, EventsDesc& out) {
        if (!value.is_object()) {
            return fail(path, "must be an object");
        }
        static constexpr const char* kFields[] = {"receive", "emit"};
        if (!checkKnownFields(value, path, kFields)) {
            return false;
        }
        for (const char* field : kFields) {
            const auto list = value.find(field);
            if (list == value.end()) {
                continue;
            }
            if (!list->is_array()) {
                return fail(child(path, field), "must be an array");
            }
            std::vector<EventDesc>& target = field == std::string("receive") ? out.receive : out.emit;
            target.reserve(list->size());
            for (std::size_t i = 0; i < list->size(); ++i) {
                const auto& item = (*list)[i];
                const std::string itemPath = element(child(path, field), i);
                EventDesc event;
                if (item.is_string()) {
                    event.name = item.get<std::string>();
                } else if (item.is_object()) {
                    if (!readRequiredString(item, "name", child(itemPath, "name"), event.name)) {
                        return false;
                    }
                    Value attributes;
                    attributes.kind = Value::Kind::Object;
                    const auto nestedAttributes = item.find("attributes");
                    if (nestedAttributes != item.end()) {
                        if (!nestedAttributes->is_object()) {
                            return fail(child(itemPath, "attributes"), "must be an object");
                        }
                        for (const auto& [name, nested] : nestedAttributes->items()) {
                            std::optional<Value> parsed;
                            if (parseValue(nested, child(child(itemPath, "attributes"), name), 0, parsed) == nullptr) {
                                return false;
                            }
                            attributes.object.emplace_back(name, std::move(*parsed));
                        }
                    } else {
                        for (const auto& [name, nested] : item.items()) {
                            if (name == "name") {
                                continue;
                            }
                            std::optional<Value> parsed;
                            if (parseValue(nested, child(itemPath, name), 0, parsed) == nullptr) {
                                return false;
                            }
                            attributes.object.emplace_back(name, std::move(*parsed));
                        }
                    }
                    event.attributes = std::move(attributes);
                } else {
                    return fail(itemPath, "must be a string or object");
                }
                if (event.name.empty()) {
                    return fail(child(itemPath, "name"), "must not be empty");
                }
                target.push_back(std::move(event));
            }
        }
        return true;
    }

    bool parseSubgraph(const Json& value, const std::string& path, SubgraphDesc& out) {
        if (!value.is_object()) {
            return fail(path, "must be an object");
        }
        static constexpr const char* kFields[] = {"name", "inputs", "outputs", "init", "update"};
        if (!checkKnownFields(value, path, kFields)) {
            return false;
        }
        if (!readRequiredString(value, "name", child(path, "name"), out.name)) {
            return false;
        }
        if (out.name.empty()) {
            return fail(child(path, "name"), "must not be empty");
        }
        const auto inputs = value.find("inputs");
        if (inputs != value.end()) {
            if (!parseValueChecked(*inputs, child(path, "inputs"), out.inputs)) {
                return false;
            }
        }
        const auto outputs = value.find("outputs");
        if (outputs != value.end()) {
            if (!parseValueChecked(*outputs, child(path, "outputs"), out.outputs)) {
                return false;
            }
        }
        for (const char* field : {"init", "update"}) {
            const auto stage = value.find(field);
            if (stage == value.end()) {
                continue;
            }
            if (!stage->is_object()) {
                return fail(child(path, field), "must be an object");
            }
            static constexpr const char* kStageFields[] = {"blocks"};
            if (!checkKnownFields(*stage, child(path, field), kStageFields)) {
                return false;
            }
            const auto blocks = stage->find("blocks");
            if (blocks == stage->end()) {
                continue;
            }
            std::vector<Block>& target = field == std::string("init") ? out.initBlocks : out.updateBlocks;
            if (!parseBlocks(*blocks, child(child(path, field), "blocks"), field == std::string("update"), target)) {
                return false;
            }
        }
        return true;
    }

    bool parseSystem(const Json& value, const std::string& path, SystemDesc& out,
                     const std::vector<Property>& properties) {
        if (!value.is_object()) {
            return fail(path, "must be an object");
        }
        static constexpr const char* kFields[] = {
            "name", "capacity", "space", "bounds", "sort", "spawner", "init", "update", "events", "outputs",
        };
        if (!checkKnownFields(value, path, kFields)) {
            return false;
        }
        if (!readRequiredString(value, "name", child(path, "name"), out.name)) {
            return false;
        }
        if (out.name.empty()) {
            return fail(child(path, "name"), "must not be empty");
        }

        if (!readRequiredUint(value, "capacity", child(path, "capacity"), out.requestedCapacity)) {
            return false;
        }
        if (out.requestedCapacity < 1 || out.requestedCapacity > kMaxParticleCapacity) {
            return fail(child(path, "capacity"), "must be between 1 and 1048576");
        }
        out.capacity = alignAttributeCapacity(out.requestedCapacity);

        const auto space = value.find("space");
        if (space != value.end()) {
            if (!space->is_string()) {
                return fail(child(path, "space"), "must be a string");
            }
            const std::string name = space->get<std::string>();
            if (name == "world") {
                out.space = SystemSpace::World;
            } else if (name == "local") {
                out.space = SystemSpace::Local;
            } else {
                return fail(child(path, "space"), "unknown space '" + name + "'");
            }
        }

        const auto bounds = value.find("bounds");
        if (bounds != value.end()) {
            if (!bounds->is_object()) {
                return fail(child(path, "bounds"), "must be an object");
            }
            static constexpr const char* kBoundsFields[] = {"mode", "center", "extents", "padding"};
            if (!checkKnownFields(*bounds, child(path, "bounds"), kBoundsFields)) {
                return false;
            }
            bool automatic = false;
            const auto mode = bounds->find("mode");
            if (mode != bounds->end()) {
                if (!mode->is_string()) {
                    return fail(child(child(path, "bounds"), "mode"), "must be a string");
                }
                const std::string modeName = mode->get<std::string>();
                if (modeName == "manual") {
                    out.bounds.mode = BoundsMode::Manual;
                } else if (modeName == "automatic") {
                    out.bounds.mode = BoundsMode::Automatic;
                    automatic = true;
                } else {
                    return fail(child(child(path, "bounds"), "mode"), "unknown bounds mode '" + modeName + "'");
                }
            }
            const auto center = bounds->find("center");
            const auto extents = bounds->find("extents");
            const auto padding = bounds->find("padding");
            if (!automatic) {
                if (center == bounds->end()) {
                    return fail(child(child(path, "bounds"), "center"), "is required for manual bounds");
                }
                if (extents == bounds->end()) {
                    return fail(child(child(path, "bounds"), "extents"), "is required for manual bounds");
                }
            }
            if (center != bounds->end() &&
                !readVec3(*center, child(child(path, "bounds"), "center"), out.bounds.center)) {
                return false;
            }
            if (extents != bounds->end() &&
                !readVec3(*extents, child(child(path, "bounds"), "extents"), out.bounds.extents)) {
                return false;
            }
            if (padding != bounds->end() &&
                !readVec3(*padding, child(child(path, "bounds"), "padding"), out.bounds.padding)) {
                return false;
            }
            if (out.bounds.extents.x <= 0.0f || out.bounds.extents.y <= 0.0f || out.bounds.extents.z <= 0.0f) {
                return fail(child(child(path, "bounds"), "extents"), "components must be greater than zero");
            }
        }

        const auto sort = value.find("sort");
        if (sort != value.end()) {
            if (!sort->is_object()) {
                return fail(child(path, "sort"), "must be an object");
            }
            static constexpr const char* kSortFields[] = {"mode", "order"};
            if (!checkKnownFields(*sort, child(path, "sort"), kSortFields)) {
                return false;
            }
            const auto mode = sort->find("mode");
            if (mode != sort->end()) {
                if (!mode->is_string()) {
                    return fail(child(child(path, "sort"), "mode"), "must be a string");
                }
                const std::string name = mode->get<std::string>();
                if (name == "none") {
                    out.sort.mode = SortMode::None;
                } else if (name == "distance") {
                    out.sort.mode = SortMode::Distance;
                } else if (name == "custom") {
                    out.sort.mode = SortMode::Custom;
                } else if (name == "cameraDepth") {
                    out.sort.mode = SortMode::CameraDepth;
                } else if (name == "youngest") {
                    out.sort.mode = SortMode::YoungestInFront;
                } else {
                    return fail(child(child(path, "sort"), "mode"), "unknown sort mode '" + name + "'");
                }
            }
            const auto order = sort->find("order");
            if (order != sort->end()) {
                if (!order->is_string()) {
                    return fail(child(child(path, "sort"), "order"), "must be a string");
                }
                const std::string name = order->get<std::string>();
                if (name == "ascending") {
                    out.sort.order = SortOrder::Ascending;
                } else if (name == "descending") {
                    out.sort.order = SortOrder::Descending;
                } else {
                    return fail(child(child(path, "sort"), "order"), "unknown sort order '" + name + "'");
                }
            }
        }
        if (out.sort.mode != SortMode::None && out.capacity > kMaxSortedCapacity) {
            return fail(child(path, "capacity"),
                        "sorted systems are limited to 262144 particles in V1");
        }

        const auto spawner = value.find("spawner");
        if (spawner != value.end()) {
            if (!spawner->is_object()) {
                return fail(child(path, "spawner"), "must be an object");
            }
            static constexpr const char* kSpawnerFields[] = {"rate", "bursts"};
            if (!checkKnownFields(*spawner, child(path, "spawner"), kSpawnerFields)) {
                return false;
            }
            const auto rate = spawner->find("rate");
            if (rate != spawner->end()) {
                if (!readFloat(*rate, child(child(path, "spawner"), "rate"), out.spawner.rate)) {
                    return false;
                }
                if (out.spawner.rate < 0.0f) {
                    return fail(child(child(path, "spawner"), "rate"), "must be greater than or equal to zero");
                }
            }
            const auto bursts = spawner->find("bursts");
            if (bursts != spawner->end()) {
                if (!bursts->is_array()) {
                    return fail(child(child(path, "spawner"), "bursts"), "must be an array");
                }
                out.spawner.bursts.reserve(bursts->size());
                for (std::size_t i = 0; i < bursts->size(); ++i) {
                    const auto& item = (*bursts)[i];
                    const std::string burstPath = element(child(child(path, "spawner"), "bursts"), i);
                    if (!item.is_object()) {
                        return fail(burstPath, "must be an object");
                    }
                    static constexpr const char* kBurstFields[] = {"time", "count", "period", "repeat"};
                    if (!checkKnownFields(item, burstPath, kBurstFields)) {
                        return false;
                    }
                    Burst burst;
                    if (!readFloat(item.at("time"), child(burstPath, "time"), burst.time)) {
                        return false;
                    }
                    if (burst.time < 0.0f) {
                        return fail(child(burstPath, "time"), "must be greater than or equal to zero");
                    }
                    if (!readRequiredUint(item, "count", child(burstPath, "count"), burst.count)) {
                        return false;
                    }
                    const auto period = item.find("period");
                    if (period != item.end()) {
                        float periodValue = 0.0f;
                        if (!readFloat(*period, child(burstPath, "period"), periodValue) || periodValue <= 0.0f) {
                            return fail(child(burstPath, "period"), "must be greater than zero");
                        }
                        burst.period = periodValue;
                    }
                    const auto repeat = item.find("repeat");
                    if (repeat != item.end()) {
                        if (!burst.period.has_value()) {
                            return fail(child(burstPath, "repeat"), "requires 'period'");
                        }
                        std::uint32_t repeatValue = 0;
                        if (!readUint(*repeat, child(burstPath, "repeat"), repeatValue) || repeatValue < 1u) {
                            return fail(child(burstPath, "repeat"), "must be at least 1");
                        }
                        burst.repeat = repeatValue;
                    }
                    out.spawner.bursts.push_back(burst);
                }
            }
        }

        for (const char* field : {"init", "update"}) {
            const auto stage = value.find(field);
            if (stage == value.end()) {
                continue;
            }
            if (!stage->is_object()) {
                return fail(child(path, field), "must be an object");
            }
            static constexpr const char* kStageFields[] = {"blocks"};
            if (!checkKnownFields(*stage, child(path, field), kStageFields)) {
                return false;
            }
            const auto blocks = stage->find("blocks");
            if (blocks == stage->end()) {
                continue;
            }
            std::vector<Block>& target = field == std::string("init") ? out.initBlocks : out.updateBlocks;
            if (!parseBlocks(*blocks, child(child(path, field), "blocks"), field == std::string("update"), target)) {
                return false;
            }
        }

        const auto events = value.find("events");
        if (events != value.end() && !parseEvents(*events, child(path, "events"), out.events)) {
            return false;
        }

        const auto outputs = value.find("outputs");
        if (outputs == value.end() || !outputs->is_array() || outputs->empty()) {
            return fail(child(path, "outputs"), "must be a non-empty array");
        }
        out.outputs.reserve(outputs->size());
        for (std::size_t i = 0; i < outputs->size(); ++i) {
            const std::string outputPath = element(child(path, "outputs"), i);
            OutputDesc output;
            if (!parseOutput((*outputs)[i], outputPath, output, properties)) {
                return false;
            }
            if (out.sort.mode != SortMode::None && !output.sort.has_value()) {
                return fail(child(outputPath, "sort"), "is required when the system sort mode is enabled");
            }
            if (output.sortOrder.has_value() && out.sort.mode == SortMode::None) {
                return fail(child(outputPath, "sortOrder"),
                            "requires the system sort mode to be enabled");
            }
            if (!output.texture.empty()) {
                const Property* property = findProperty(properties, output.texture);
                if (property == nullptr) {
                    return fail(child(outputPath, "texture"),
                                "unknown property '" + output.texture + "'");
                }
                if (property->type != PropertyType::Texture) {
                    return fail(child(outputPath, "texture"),
                                "property '" + output.texture + "' is not a texture");
                }
            }
            out.outputs.push_back(std::move(output));
        }
        return true;
    }

    static const Property* findProperty(const std::vector<Property>& properties, std::string_view name) {
        const auto found = std::find_if(properties.begin(), properties.end(),
                                        [name](const Property& property) { return property.name == name; });
        return found == properties.end() ? nullptr : &*found;
    }

    static const AttributeDesc* findAttribute(const std::vector<AttributeDesc>& attributes, std::string_view name) {
        const auto found = std::find_if(attributes.begin(), attributes.end(),
                                        [name](const AttributeDesc& attribute) { return attribute.name == name; });
        return found == attributes.end() ? nullptr : &*found;
    }

    const Json& m_root;
    std::vector<ParseError>& m_errors;
    GraphParseMode m_mode;
};

void hashBytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
}

void hashU64(std::uint64_t& hash, std::uint64_t value) {
    hashBytes(hash, &value, sizeof(value));
}

void hashString(std::uint64_t& hash, std::string_view value) {
    hashU64(hash, value.size());
    hashBytes(hash, value.data(), value.size());
}

void hashFloat(std::uint64_t& hash, float value) {
    hashU64(hash, std::bit_cast<std::uint32_t>(value));
}

void hashValue(std::uint64_t& hash, const Value& value);

void hashExpression(std::uint64_t& hash, const Expression& expression) {
    hashString(hash, expression.op);
    hashU64(hash, expression.arguments.size());
    for (const auto& [name, argument] : expression.arguments) {
        hashString(hash, name);
        hashValue(hash, argument);
    }
}

void hashValue(std::uint64_t& hash, const Value& value) {
    hashU64(hash, static_cast<std::uint64_t>(value.kind));
    switch (value.kind) {
    case Value::Kind::Invalid:
        break;
    case Value::Kind::Number:
        hashU64(hash, std::bit_cast<std::uint64_t>(value.number));
        break;
    case Value::Kind::Bool:
        hashU64(hash, value.boolean ? 1u : 0u);
        break;
    case Value::Kind::String:
        hashString(hash, value.string);
        break;
    case Value::Kind::Array:
        hashU64(hash, value.array.size());
        for (const Value& item : value.array) {
            hashValue(hash, item);
        }
        break;
    case Value::Kind::Object:
        hashU64(hash, value.object.size());
        for (const auto& [name, member] : value.object) {
            hashString(hash, name);
            hashValue(hash, member);
        }
        break;
    case Value::Kind::Expression:
        if (value.expression != nullptr) {
            hashExpression(hash, *value.expression);
        }
        break;
    }
}

std::uint64_t hashGraph(const Graph& graph) {
    std::uint64_t hash = 1469598103934665603ull;
    hashU64(hash, graph.version);
    hashString(hash, graph.name);
    hashU64(hash, graph.properties.size());
    for (const Property& property : graph.properties) {
        hashString(hash, property.name);
        hashU64(hash, static_cast<std::uint64_t>(property.type));
        for (float value : property.defaultFloat) {
            hashFloat(hash, value);
        }
        for (std::uint32_t value : property.defaultUint) {
            hashU64(hash, value);
        }
        hashString(hash, property.defaultTexture);
        hashU64(hash, property.curve.size());
        for (const CurveKey& key : property.curve) {
            hashFloat(hash, key.time);
            hashFloat(hash, key.value);
        }
        hashU64(hash, property.gradient.size());
        for (const GradientKey& key : property.gradient) {
            hashFloat(hash, key.time);
            hashFloat(hash, key.color.x);
            hashFloat(hash, key.color.y);
            hashFloat(hash, key.color.z);
            hashFloat(hash, key.color.w);
        }
    }
    hashU64(hash, graph.attributes.size());
    for (const AttributeDesc& attribute : graph.attributes) {
        hashString(hash, attribute.name);
        hashU64(hash, static_cast<std::uint64_t>(attribute.type));
        for (float value : attribute.defaultFloat) {
            hashFloat(hash, value);
        }
        for (std::uint32_t value : attribute.defaultUint) {
            hashU64(hash, value);
        }
    }
    hashU64(hash, graph.subgraphs.size());
    for (const SubgraphDesc& subgraph : graph.subgraphs) {
        hashString(hash, subgraph.name);
        hashValue(hash, subgraph.inputs);
        hashValue(hash, subgraph.outputs);
        hashU64(hash, subgraph.initBlocks.size());
        for (const Block& block : subgraph.initBlocks) {
            hashString(hash, block.type);
            hashU64(hash, block.enabled ? 1u : 0u);
            hashValue(hash, block.parameters);
        }
        hashU64(hash, subgraph.updateBlocks.size());
        for (const Block& block : subgraph.updateBlocks) {
            hashString(hash, block.type);
            hashU64(hash, block.enabled ? 1u : 0u);
            hashValue(hash, block.parameters);
        }
    }
    hashU64(hash, graph.systems.size());
    for (const SystemDesc& system : graph.systems) {
        hashString(hash, system.name);
        hashU64(hash, system.requestedCapacity);
        hashU64(hash, system.capacity);
        hashU64(hash, static_cast<std::uint64_t>(system.space));
        hashU64(hash, static_cast<std::uint64_t>(system.bounds.mode));
        hashU64(hash, static_cast<std::uint64_t>(system.sort.mode));
        hashU64(hash, static_cast<std::uint64_t>(system.sort.order));
        hashFloat(hash, system.bounds.center.x);
        hashFloat(hash, system.bounds.center.y);
        hashFloat(hash, system.bounds.center.z);
        hashFloat(hash, system.bounds.extents.x);
        hashFloat(hash, system.bounds.extents.y);
        hashFloat(hash, system.bounds.extents.z);
        if (system.bounds.padding.x != 0.0f || system.bounds.padding.y != 0.0f ||
            system.bounds.padding.z != 0.0f) {
            hashU64(hash, 1u);
            hashFloat(hash, system.bounds.padding.x);
            hashFloat(hash, system.bounds.padding.y);
            hashFloat(hash, system.bounds.padding.z);
        }
        hashFloat(hash, system.spawner.rate);
        hashU64(hash, system.spawner.bursts.size());
        for (const Burst& burst : system.spawner.bursts) {
            hashFloat(hash, burst.time);
            hashU64(hash, burst.count);
            hashU64(hash, burst.period.has_value() ? 1u : 0u);
            hashFloat(hash, burst.period.value_or(0.0f));
            hashU64(hash, burst.repeat.value_or(0u));
        }
        hashU64(hash, system.initBlocks.size());
        for (const Block& block : system.initBlocks) {
            hashString(hash, block.type);
            hashU64(hash, block.enabled ? 1u : 0u);
            hashValue(hash, block.parameters);
        }
        hashU64(hash, system.updateBlocks.size());
        for (const Block& block : system.updateBlocks) {
            hashString(hash, block.type);
            hashU64(hash, block.enabled ? 1u : 0u);
            hashValue(hash, block.parameters);
        }
        hashU64(hash, system.outputs.size());
        for (const OutputDesc& output : system.outputs) {
            hashU64(hash, static_cast<std::uint64_t>(output.type));
            hashU64(hash, output.lit ? 1u : 0u);
            hashU64(hash, static_cast<std::uint64_t>(output.blend));
            hashString(hash, output.texture);
            hashU64(hash, output.softParticles ? 1u : 0u);
            hashU64(hash, output.castShadows ? 1u : 0u);
            hashU64(hash, output.generateMotionVector ? 1u : 0u);
            if (output.sortPriority != 0) {
                hashU64(hash, static_cast<std::uint64_t>(output.sortPriority));
            }
            hashFloat(hash, output.softDistance);
            hashFloat(hash, output.metallic);
            hashFloat(hash, output.roughness);
            hashString(hash, output.metallicProperty);
            hashString(hash, output.roughnessProperty);
            hashU64(hash, output.sort.has_value() ? 1u : 0u);
            if (output.sort.has_value()) {
                hashU64(hash, static_cast<std::uint64_t>(output.sort->mode));
                hashU64(hash, static_cast<std::uint64_t>(output.sort->order));
            }
            if (output.sortOrder.has_value()) {
                hashU64(hash, static_cast<std::uint64_t>(*output.sortOrder));
            }
        }
        hashU64(hash, system.events.receive.size());
        for (const EventDesc& event : system.events.receive) {
            hashString(hash, event.name);
            hashValue(hash, event.attributes);
        }
        hashU64(hash, system.events.emit.size());
        for (const EventDesc& event : system.events.emit) {
            hashString(hash, event.name);
            hashValue(hash, event.attributes);
        }
    }
    if (graph.cullingFlags != 0u) {
        hashU64(hash, 1u);
        hashU64(hash, graph.cullingFlags);
    }
    return hash;
}

} // namespace

std::string formatParseError(const ParseError& error) {
    std::string result;
    if (error.location == ErrorLocationKind::Json && !error.source.empty()) {
        result += error.source;
    }
    if (error.hasLocation()) {
        if (!result.empty()) {
            result += ":";
        }
        result += std::to_string(error.line) + ":" + std::to_string(error.column);
    }
    if (!error.path.empty()) {
        if (!result.empty()) {
            result += ": ";
        }
        result += error.path;
    }
    if (!error.message.empty()) {
        if (!result.empty()) {
            result += ": ";
        }
        result += error.message;
    }
    return result;
}

bool locateJsonPath(std::string_view json, std::string_view path, std::uint32_t& line, std::uint32_t& column) {
    const JsonLocationFinder finder(json);
    if (finder.locate(path, line, column)) {
        return true;
    }
    finder.rootLocation(line, column);
    return false;
}

ParseResult parseGraphJson(std::string_view json, std::string_view sourceName, GraphParseMode mode) {
    ParseResult result;
    Json root;
    try {
        root = Json::parse(json.begin(), json.end());
    } catch (const std::exception& exception) {
        ParseError error;
        error.path = "$";
        error.message = exception.what();
        error.source = std::string(sourceName);
        error.location = ErrorLocationKind::Json;
        jsonParseErrorLocation(error.message, error.line, error.column);
        result.errors.push_back(std::move(error));
        return result;
    }

    GraphParser parser(root, result.errors, mode);
    result.graph = parser.parse();
    if (!result.errors.empty()) {
        result.graph.reset();
        const JsonLocationFinder finder(json);
        for (ParseError& error : result.errors) {
            if (error.location == ErrorLocationKind::Hlsl) {
                if (error.source.empty()) {
                    error.source = std::string(sourceName);
                }
                continue;
            }
            error.source = std::string(sourceName);
            error.location = ErrorLocationKind::Json;
            if (!finder.locate(error.path, error.line, error.column)) {
                finder.rootLocation(error.line, error.column);
            }
        }
    }
    return result;
}

ParseResult loadGraphFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        ParseError error;
        error.message = "failed to open '" + path.string() + "'";
        error.source = path.string();
        return ParseResult{nullptr, {std::move(error)}};
    }
    const std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return parseGraphJson(contents, path.string());
}

std::uint64_t graphHash(const Graph& graph) noexcept {
    return hashGraph(graph);
}

} // namespace vfx
