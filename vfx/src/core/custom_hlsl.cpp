#include "custom_hlsl.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vfx {
namespace {

constexpr std::size_t kMaxCustomHlslBytes = 32u * 1024u;

bool isIdentifierStart(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isIdentifierChar(char c) noexcept {
    return isIdentifierStart(c) || (c >= '0' && c <= '9');
}

bool isIdentifier(std::string_view text) noexcept {
    if (text.empty() || !isIdentifierStart(text.front())) {
        return false;
    }
    return std::all_of(text.begin() + 1, text.end(), isIdentifierChar);
}

bool startsWith(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::string child(std::string_view base, std::string_view name) {
    if (base.empty()) {
        return std::string(name);
    }
    return std::string(base) + "." + std::string(name);
}

std::string element(std::string_view base, std::size_t index) {
    return std::string(base) + "[" + std::to_string(index) + "]";
}

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' ||
                           text[begin] == '\n')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' ||
                           text[end - 1] == '\n')) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::pair<std::size_t, std::size_t> lineColumn(std::string_view code, std::size_t offset) noexcept {
    std::size_t line = 1;
    std::size_t column = 1;
    const std::size_t limit = std::min(offset, code.size());
    for (std::size_t i = 0; i < limit; ++i) {
        if (code[i] == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
    return {line, column};
}

const AttributeDesc* findAttribute(const Graph& graph, std::string_view name) noexcept {
    if (const AttributeDesc* builtIn = findBuiltInAttribute(name)) {
        return builtIn;
    }
    const auto found = std::find_if(graph.attributes.begin(), graph.attributes.end(),
                                    [name](const AttributeDesc& attribute) {
                                        return attribute.name == name;
                                    });
    return found == graph.attributes.end() ? nullptr : &*found;
}

bool findMatching(std::string_view code, std::size_t open, char opening, char closing,
                  std::size_t& result) {
    if (open >= code.size() || code[open] != opening) {
        return false;
    }
    std::size_t depth = 0;
    for (std::size_t i = open; i < code.size(); ++i) {
        const char c = code[i];
        if (c == '"' || c == '\'') {
            const char quote = c;
            ++i;
            while (i < code.size()) {
                if (code[i] == '\\') {
                    i += 2;
                    continue;
                }
                if (code[i] == quote) {
                    break;
                }
                ++i;
            }
            continue;
        }
        if (c == '/' && i + 1 < code.size() && code[i + 1] == '/') {
            const std::size_t end = code.find('\n', i + 2);
            i = end == std::string_view::npos ? code.size() : end;
            continue;
        }
        if (c == '/' && i + 1 < code.size() && code[i + 1] == '*') {
            const std::size_t end = code.find("*/", i + 2);
            i = end == std::string_view::npos ? code.size() : end + 1;
            continue;
        }
        if (c == opening) {
            ++depth;
        } else if (c == closing) {
            if (depth == 0) {
                return false;
            }
            --depth;
            if (depth == 0) {
                result = i;
                return true;
            }
        }
    }
    return false;
}

std::size_t skipTrivia(std::string_view code, std::size_t offset) {
    while (offset < code.size()) {
        if (code[offset] == ' ' || code[offset] == '\t' || code[offset] == '\r' || code[offset] == '\n') {
            ++offset;
            continue;
        }
        if (code[offset] == '/' && offset + 1 < code.size()) {
            if (code[offset + 1] == '/') {
                const std::size_t end = code.find('\n', offset + 2);
                offset = end == std::string_view::npos ? code.size() : end + 1;
                continue;
            }
            if (code[offset + 1] == '*') {
                const std::size_t end = code.find("*/", offset + 2);
                offset = end == std::string_view::npos ? code.size() : end + 2;
                continue;
            }
        }
        break;
    }
    return offset;
}

std::vector<std::string> splitArguments(std::string_view text) {
    std::vector<std::string> arguments;
    std::size_t depth = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"' || c == '\'') {
            const char quote = c;
            ++i;
            while (i < text.size()) {
                if (text[i] == '\\') {
                    i += 2;
                    continue;
                }
                if (text[i] == quote) {
                    break;
                }
                ++i;
            }
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            const std::size_t end = text.find('\n', i + 2);
            i = end == std::string_view::npos ? text.size() : end;
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            const std::size_t end = text.find("*/", i + 2);
            i = end == std::string_view::npos ? text.size() : end + 1;
            continue;
        }
        if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            if (depth != 0) {
                --depth;
            }
        } else if (c == ',' && depth == 0) {
            arguments.push_back(trim(text.substr(start, i - start)));
            start = i + 1;
        }
    }
    arguments.push_back(trim(text.substr(start)));
    return arguments;
}

bool isAssignmentAfter(std::string_view code, std::size_t offset) noexcept {
    while (offset < code.size() && (code[offset] == ' ' || code[offset] == '\t' ||
                                    code[offset] == '\r' || code[offset] == '\n')) {
        ++offset;
    }
    if (offset + 1 < code.size()) {
        const std::string_view pair = code.substr(offset, 2);
        if (pair == "++" || pair == "--") {
            return true;
        }
        if (pair == "==" || pair == "!=" || pair == "<=" || pair == ">=" || pair == "&&" ||
            pair == "||") {
            return false;
        }
    }
    if (offset >= code.size()) {
        return false;
    }
    if (code[offset] == '=') {
        return true;
    }
    return offset + 1 < code.size() && code[offset + 1] == '=' &&
           (code[offset] == '+' || code[offset] == '-' || code[offset] == '*' || code[offset] == '/' ||
            code[offset] == '%' || code[offset] == '&' || code[offset] == '|' || code[offset] == '^');
}

bool isAttributeWriteContext(std::string_view code, std::size_t start, std::size_t close) {
    std::size_t before = start;
    while (before > 0 && (code[before - 1] == ' ' || code[before - 1] == '\t' ||
                          code[before - 1] == '\r' || code[before - 1] == '\n' ||
                          code[before - 1] == '(')) {
        --before;
    }
    if (before >= 2) {
        const std::string_view prefix = code.substr(before - 2, 2);
        if (prefix == "++" || prefix == "--") {
            return true;
        }
    }

    std::size_t after = close + 1;
    while (after < code.size() && (code[after] == ' ' || code[after] == '\t' ||
                                   code[after] == '\r' || code[after] == '\n' || code[after] == ')')) {
        ++after;
    }
    after = skipTrivia(code, after);
    for (;;) {
        if (after < code.size() && code[after] == '.') {
            ++after;
            while (after < code.size() && isIdentifierChar(code[after])) {
                ++after;
            }
            after = skipTrivia(code, after);
            continue;
        }
        if (after < code.size() && code[after] == '[') {
            std::size_t end = 0;
            if (!findMatching(code, after, '[', ']', end)) {
                return false;
            }
            after = skipTrivia(code, end + 1);
            continue;
        }
        break;
    }
    return isAssignmentAfter(code, after);
}

class CodeValidator {
  public:
    CodeValidator(const Graph& graph, const CustomHlslBlock& block, std::string_view path)
        : m_graph(graph), m_path(path),
          m_reads(block.reads.begin(), block.reads.end()),
          m_writes(block.writes.begin(), block.writes.end()), m_code(block.code) {}

    bool validate() {
        const std::string_view code = m_code;
        for (std::size_t i = 0; i < code.size();) {
            const char c = code[i];
            if (c == '"' || c == '\'') {
                if (!skipString(code, i)) {
                    return false;
                }
                continue;
            }
            if (c == '/' && i + 1 < code.size() && code[i + 1] == '/') {
                const std::size_t end = code.find('\n', i + 2);
                i = end == std::string_view::npos ? code.size() : end + 1;
                continue;
            }
            if (c == '/' && i + 1 < code.size() && code[i + 1] == '*') {
                const std::size_t end = code.find("*/", i + 2);
                if (end == std::string_view::npos) {
                    return fail(i, "unterminated block comment");
                }
                i = end + 2;
                continue;
            }
            if (c == '#') {
                return fail(i, "preprocessor directives are not allowed");
            }
            if (isIdentifierStart(c)) {
                const std::size_t start = i;
                while (i < code.size() && isIdentifierChar(code[i])) {
                    ++i;
                }
                if (!checkIdentifier(code, start, i)) {
                    return false;
                }
                continue;
            }
            ++i;
        }
        return true;
    }

    const CustomHlslError& error() const noexcept { return m_error; }

  private:
    bool skipString(std::string_view code, std::size_t& offset) {
        const char quote = code[offset];
        const std::size_t start = offset;
        ++offset;
        while (offset < code.size()) {
            if (code[offset] == '\\') {
                offset += 2;
                continue;
            }
            if (code[offset] == quote) {
                ++offset;
                return true;
            }
            if (code[offset] == '\n') {
                return fail(start, "unterminated string literal");
            }
            ++offset;
        }
        return fail(start, "unterminated string literal");
    }

    bool checkIdentifier(std::string_view code, std::size_t start, std::size_t end) {
        const std::string_view name = code.substr(start, end - start);
        if (startsWith(name, "a_")) {
            return fail(start, "direct attribute access is not allowed; use VFX_ATTR/VFX_SET_ATTR");
        }
        if (startsWith(name, "s_vfx") || startsWith(name, "u_vfx") || startsWith(name, "gl_")) {
            return fail(start, "generated shader symbols are not accessible");
        }
        if (startsWith(name, "VFX_ATTRIBUTE_") || startsWith(name, "VFX_ATTRIBUTES_")) {
            return fail(start, "generated attribute metadata is not accessible");
        }
        if (startsWith(name, "Load_") || startsWith(name, "Store_") || name == "attributes" ||
            name == "VfxAttributes") {
            return fail(start, "raw attribute loads/stores are not allowed");
        }
        if (startsWith(name, "vfxEvent")) {
            return fail(start, "event buffer internals are not accessible");
        }
        if (name == "VFX_ATTR") {
            return checkReadMacro(code, start);
        }
        if (name == "VFX_SET_ATTR") {
            return checkWriteMacro(code, start);
        }
        if (name == "VFX_PROP") {
            return checkPropertyMacro(code, start);
        }
        if (name == "VFX_LIFETIME_RATIO") {
            if (m_reads.count("age") == 0u || m_reads.count("lifetime") == 0u) {
                return fail(start, "VFX_LIFETIME_RATIO requires reads age and lifetime");
            }
            return true;
        }
        if (isBannedIdentifier(name)) {
            return fail(start, "'" + std::string(name) + "' is not allowed in custom HLSL");
        }
        return true;
    }

    bool isBannedIdentifier(std::string_view name) noexcept {
        if (name == "main" || name == "return" || name == "discard" || name == "barrier" ||
            name == "uniform" || name == "buffer" || name == "cbuffer" || name == "tbuffer" ||
            name == "register" || name == "packoffset" || name == "globallycoherent" ||
            name == "groupshared" || name == "shared" || name == "static" || name == "volatile" ||
            name == "include" || name == "define" || name == "Buffer") {
            return true;
        }
        if (startsWith(name, "Texture") || startsWith(name, "Sampler") ||
            startsWith(name, "StructuredBuffer") || startsWith(name, "ByteAddressBuffer") ||
            startsWith(name, "RWTexture") || startsWith(name, "RWBuffer") ||
            startsWith(name, "RWStructuredBuffer") || startsWith(name, "RWByteAddressBuffer") ||
            startsWith(name, "AppendStructuredBuffer") || startsWith(name, "ConsumeStructuredBuffer")) {
            return true;
        }
        return name == "GroupMemoryBarrier" || name == "DeviceMemoryBarrier" ||
               name == "MemoryBarrier" || name == "AllMemoryBarrier" ||
               name == "AllMemoryBarrierWithGroupSync" || name == "DeviceMemoryBarrierWithGroupSync" ||
               name == "GroupMemoryBarrierWithGroupSync";
    }

    bool checkReadMacro(std::string_view code, std::size_t start) {
        const std::size_t open = skipTrivia(code, start + 8);
        if (open >= code.size() || code[open] != '(') {
            return fail(start, "VFX_ATTR requires an attribute name");
        }
        std::size_t close = 0;
        if (!findMatching(code, open, '(', ')', close)) {
            return fail(start, "unbalanced VFX_ATTR invocation");
        }
        const std::vector<std::string> arguments = splitArguments(code.substr(open + 1, close - open - 1));
        if (arguments.size() != 1 || arguments[0].empty()) {
            return fail(start, "VFX_ATTR requires exactly one attribute name");
        }
        if (!isIdentifier(arguments[0])) {
            return fail(start, "VFX_ATTR requires a valid attribute name");
        }
        if (findAttribute(m_graph, arguments[0]) == nullptr) {
            return fail(start, "unknown attribute '" + arguments[0] + "'");
        }
        if (m_reads.count(arguments[0]) == 0u) {
            return fail(start, "attribute '" + arguments[0] + "' must be declared in reads");
        }
        if (isAttributeWriteContext(code, start, close)) {
            return fail(start, "VFX_ATTR is read-only; use VFX_SET_ATTR to write attributes");
        }
        return true;
    }

    bool checkWriteMacro(std::string_view code, std::size_t start) {
        const std::size_t open = skipTrivia(code, start + 12);
        if (open >= code.size() || code[open] != '(') {
            return fail(start, "VFX_SET_ATTR requires an attribute name and a value");
        }
        std::size_t close = 0;
        if (!findMatching(code, open, '(', ')', close)) {
            return fail(start, "unbalanced VFX_SET_ATTR invocation");
        }
        std::vector<std::string> arguments = splitArguments(code.substr(open + 1, close - open - 1));
        if (arguments.size() != 2 || arguments[0].empty() || arguments[1].empty()) {
            return fail(start, "VFX_SET_ATTR requires exactly an attribute name and a value");
        }
        if (!isIdentifier(arguments[0])) {
            return fail(start, "VFX_SET_ATTR requires a valid attribute name");
        }
        if (findAttribute(m_graph, arguments[0]) == nullptr) {
            return fail(start, "unknown attribute '" + arguments[0] + "'");
        }
        if (m_writes.count(arguments[0]) == 0u) {
            return fail(start, "attribute '" + arguments[0] + "' must be declared in writes");
        }
        return true;
    }

    bool checkPropertyMacro(std::string_view code, std::size_t start) {
        const std::size_t open = skipTrivia(code, start + 8);
        if (open >= code.size() || code[open] != '(') {
            return fail(start, "VFX_PROP requires a property name");
        }
        std::size_t close = 0;
        if (!findMatching(code, open, '(', ')', close)) {
            return fail(start, "unbalanced VFX_PROP invocation");
        }
        const std::vector<std::string> arguments = splitArguments(code.substr(open + 1, close - open - 1));
        if (arguments.size() != 1 || arguments[0].empty() || !isIdentifier(arguments[0])) {
            return fail(start, "VFX_PROP requires exactly one property name");
        }
        const auto found = std::find_if(m_graph.properties.begin(), m_graph.properties.end(),
                                        [&](const Property& property) {
                                            return property.name == arguments[0];
                                        });
        if (found == m_graph.properties.end()) {
            return fail(start, "unknown property '" + arguments[0] + "'");
        }
        return true;
    }

    bool fail(std::size_t offset, const std::string& message) {
        const auto [line, column] = lineColumn(m_code, offset);
        m_error.path = m_path + ".code";
        m_error.message = message;
        m_error.line = static_cast<std::uint32_t>(line);
        m_error.column = static_cast<std::uint32_t>(column);
        return false;
    }

    const Graph& m_graph;
    std::string m_path;
    std::unordered_set<std::string> m_reads;
    std::unordered_set<std::string> m_writes;
    std::string m_code;
    CustomHlslError m_error;
};

} // namespace

bool readCustomHlslBlock(const Block& block, const Graph& graph, std::string_view path,
                         CustomHlslBlock& out, CustomHlslError& error) {
    out = {};
    error = {};
    const auto fail = [&](std::string errorPath, std::string message) {
        error.path = std::move(errorPath);
        error.message = std::move(message);
        return false;
    };
    if (block.type != "custom_hlsl") {
        return fail(std::string(path) + ".type", "expected 'custom_hlsl'");
    }
    if (block.parameters.kind != Value::Kind::Object) {
        return fail(std::string(path), "must be an object");
    }
    const Value* code = block.parameter("code");
    if (code == nullptr || code->kind != Value::Kind::String) {
        return fail(child(path, "code"), "must be a string");
    }
    if (code->string.empty()) {
        return fail(child(path, "code"), "must not be empty");
    }
    if (code->string.size() > kMaxCustomHlslBytes) {
        return fail(child(path, "code"), "exceeds 32768 bytes");
    }
    std::string normalized;
    normalized.reserve(code->string.size());
    for (std::size_t i = 0; i < code->string.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(code->string[i]);
        if (c == '\r') {
            if (i + 1 < code->string.size() && code->string[i + 1] == '\n') {
                continue;
            }
            normalized.push_back('\n');
            continue;
        }
        if (c < 0x20u && c != '\n' && c != '\t') {
            return fail(child(path, "code"), "contains an unsupported control character");
        }
        normalized.push_back(static_cast<char>(c));
    }
    out.code = normalized;

    const auto readList = [&](const char* field, std::vector<std::string>& list) {
        const Value* value = block.parameter(field);
        if (value == nullptr) {
            return true;
        }
        if (value->kind != Value::Kind::Array) {
            return fail(child(path, field), "must be an array of attribute names");
        }
        std::unordered_set<std::string> seen;
        for (std::size_t i = 0; i < value->array.size(); ++i) {
            const Value& item = value->array[i];
            const std::string itemPath = element(child(path, field), i);
            if (item.kind != Value::Kind::String || !isIdentifier(item.string)) {
                return fail(itemPath, "must be a valid attribute name");
            }
            if (findAttribute(graph, item.string) == nullptr) {
                return fail(itemPath, "unknown attribute '" + item.string + "'");
            }
            if (!seen.insert(item.string).second) {
                return fail(itemPath, "duplicate attribute '" + item.string + "'");
            }
            list.push_back(item.string);
        }
        return true;
    };
    if (!readList("reads", out.reads) || !readList("writes", out.writes)) {
        return false;
    }

    CodeValidator validator(graph, out, path);
    if (!validator.validate()) {
        error = validator.error();
        return false;
    }
    return true;
}

std::string formatCustomHlslError(const CustomHlslError& error) {
    std::string result = error.path;
    if (error.line != 0) {
        result += ":" + std::to_string(error.line) + ":" + std::to_string(error.column);
    }
    if (!error.message.empty()) {
        result += ": ";
        result += error.message;
    }
    return result;
}

} // namespace vfx
