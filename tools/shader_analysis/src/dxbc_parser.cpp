#include "hbg/analysis/dxbc_parser.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hbg::analysis {
namespace {

bool isSpace(char character) {
    return character == '\0' ||
           std::isspace(static_cast<unsigned char>(character)) != 0;
}

bool isDigit(char character) {
    return std::isdigit(static_cast<unsigned char>(character)) != 0;
}

bool isAlpha(char character) {
    return std::isalpha(static_cast<unsigned char>(character)) != 0;
}

std::string trim(std::string_view text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && isSpace(text[begin])) {
        ++begin;
    }
    while (end > begin && isSpace(text[end - 1])) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

bool startsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

bool allDigits(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](char character) {
        return isDigit(character);
    });
}

int parseInt(std::string_view text, int fallback = 0) {
    int value = fallback;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc{} ? value : fallback;
}

bool isShaderModelLine(std::string_view line) {
    if (line.find_first_of(" \t") != std::string_view::npos) {
        return false;
    }
    constexpr std::string_view prefixes[] = {
        "ps_", "vs_", "gs_", "hs_", "ds_", "cs_", "ms_", "ts_", "lib_", "fx_"};
    for (const std::string_view prefix : prefixes) {
        if (startsWith(line, prefix)) {
            return true;
        }
    }
    return false;
}

bool isRegisterClass(char character) {
    switch (character) {
    case 'r':
    case 'v':
    case 'o':
    case 'x':
    case 't':
    case 's':
    case 'u':
    case 'g':
    case 'a':
    case 'p':
        return true;
    default:
        return false;
    }
}

bool isMaskCharacter(char character) {
    return character == 'x' || character == 'y' ||
           character == 'z' || character == 'w';
}

int componentIndex(char character) {
    switch (character) {
    case 'x':
        return 0;
    case 'y':
        return 1;
    case 'z':
        return 2;
    case 'w':
        return 3;
    default:
        return 0;
    }
}

std::string parseMask(std::string_view text, size_t position) {
    if (position >= text.size() || text[position] != '.') {
        return "xyzw";
    }
    size_t cursor = position + 1;
    std::string mask;
    while (cursor < text.size() && isMaskCharacter(text[cursor])) {
        mask.push_back(text[cursor]);
        ++cursor;
    }
    return mask.empty() ? "xyzw" : mask;
}

size_t maskEnd(std::string_view text, size_t position) {
    if (position >= text.size() || text[position] != '.') {
        return position;
    }
    size_t cursor = position + 1;
    while (cursor < text.size() && isMaskCharacter(text[cursor])) {
        ++cursor;
    }
    return cursor;
}

std::vector<std::string> splitOperands(std::string_view text) {
    std::vector<std::string> result;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char character = text[i];
        if (character == '(' || character == '[' || character == '{') {
            ++depth;
        } else if (character == ')' || character == ']' || character == '}') {
            depth = std::max(0, depth - 1);
        } else if (character == ',' && depth == 0) {
            result.push_back(trim(text.substr(start, i - start)));
            start = i + 1;
        }
    }
    result.push_back(trim(text.substr(start)));
    return result;
}

std::string opcodeBase(std::string_view opcode) {
    const size_t parenthesis = opcode.find('(');
    if (parenthesis != std::string_view::npos) {
        return std::string(opcode.substr(0, parenthesis));
    }
    return std::string(opcode);
}

bool opcodeHasDestination(std::string_view opcode) {
    if (opcode == "nop" || opcode == "ret" || opcode == "retc" ||
        opcode == "discard" || opcode == "break" || opcode == "continue" ||
        opcode == "if" || opcode == "else" || opcode == "endif" ||
        opcode == "loop" || opcode == "endloop" || opcode == "switch" ||
        opcode == "case" || opcode == "default" || opcode == "endswitch" ||
        opcode == "call" || opcode == "callc" || opcode == "sync" ||
        opcode == "emit" || opcode == "cut" || opcode == "emitthencut") {
        return false;
    }
    if (startsWith(opcode, "if_") || startsWith(opcode, "breakc") ||
        startsWith(opcode, "retc") || startsWith(opcode, "callc") ||
        startsWith(opcode, "continuec") || startsWith(opcode, "discard_") ||
        startsWith(opcode, "store") || startsWith(opcode, "emit") ||
        startsWith(opcode, "atomic")) {
        return false;
    }
    return true;
}

std::string branchKindFor(std::string_view opcode) {
    if (opcode == "if" || startsWith(opcode, "if_")) {
        return "if";
    }
    if (opcode == "else") {
        return "else";
    }
    if (opcode == "endif") {
        return "endif";
    }
    if (opcode == "loop") {
        return "loop";
    }
    if (opcode == "endloop") {
        return "endloop";
    }
    if (opcode == "switch") {
        return "switch";
    }
    if (opcode == "case") {
        return "case";
    }
    if (opcode == "default") {
        return "default";
    }
    if (opcode == "endswitch") {
        return "endswitch";
    }
    if (startsWith(opcode, "break")) {
        return "break";
    }
    if (startsWith(opcode, "continue")) {
        return "continue";
    }
    if (startsWith(opcode, "call")) {
        return "call";
    }
    if (startsWith(opcode, "ret")) {
        return "ret";
    }
    if (startsWith(opcode, "discard")) {
        return "discard";
    }
    return "none";
}

struct RegisterStart {
    size_t position = std::string_view::npos;
    char registerClass = '\0';
    bool cbuffer = false;
};

RegisterStart findRegisterStart(std::string_view text, size_t from = 0) {
    for (size_t i = from; i < text.size(); ++i) {
        if (i > 0 && (std::isalnum(static_cast<unsigned char>(text[i - 1])) != 0 ||
                      text[i - 1] == '_')) {
            continue;
        }
        if (text[i] == 'c' && i + 2 < text.size() &&
            (text[i + 1] == 'b' || text[i + 1] == 'B') &&
            isDigit(text[i + 2])) {
            return RegisterStart{i, 'c', true};
        }
        if (isRegisterClass(text[i]) && i + 1 < text.size() &&
            isDigit(text[i + 1])) {
            return RegisterStart{i, text[i], false};
        }
    }
    return {};
}

std::vector<RegisterRef> parseIndexReads(std::string_view expression) {
    std::vector<RegisterRef> result;
    size_t cursor = 0;
    while (cursor < expression.size()) {
        const RegisterStart start = findRegisterStart(expression, cursor);
        if (start.position == std::string_view::npos) {
            break;
        }
        if (start.cbuffer) {
            cursor = start.position + 3;
            continue;
        }
        size_t end = start.position + 1;
        while (end < expression.size() && isDigit(expression[end])) {
            ++end;
        }
        const std::string mask = parseMask(expression, end);
        result.push_back(RegisterRef{std::string(1, start.registerClass),
                                     parseInt(expression.substr(
                                         start.position + 1, end - start.position - 1)),
                                     mask});
        cursor = std::max(end, maskEnd(expression, end));
    }
    return result;
}

int staticBaseIndex(std::string_view expression) {
    const std::string text = trim(expression);
    if (allDigits(text)) {
        return parseInt(text);
    }
    for (size_t i = text.size(); i > 0; --i) {
        if (text[i - 1] != '+') {
            continue;
        }
        const std::string suffix = trim(std::string_view(text).substr(i));
        if (allDigits(suffix)) {
            return parseInt(suffix);
        }
    }
    size_t digits = 0;
    while (digits < text.size() && isDigit(text[digits])) {
        ++digits;
    }
    if (digits > 0) {
        return parseInt(std::string_view(text).substr(0, digits));
    }
    return 0;
}

struct ParsedOperand {
    enum class Kind {
        None,
        Register,
        Resource,
        Cbuffer,
        Immediate,
    };

    Kind kind = Kind::None;
    RegisterRef reg;
    ResourceRef resource;
    CbufferRead cbuffer;
    std::vector<RegisterRef> indexReads;
};

class DxbcParser {
public:
    DxbcProgram parse(std::string_view text) {
        if (startsWith(text, "\xEF\xBB\xBF")) {
            text.remove_prefix(3);
        }
        size_t start = 0;
        while (start <= text.size()) {
            const size_t newline = text.find('\n', start);
            const size_t end =
                newline == std::string_view::npos ? text.size() : newline;
            std::string line = trim(text.substr(start, end - start));
            if (!line.empty()) {
                parseLine(line);
            }
            if (newline == std::string_view::npos) {
                break;
            }
            start = newline + 1;
        }
        finalize();
        return std::move(program_);
    }

private:
    void parseLine(const std::string& rawLine) {
        std::string line = rawLine;
        const size_t comment = line.find("//");
        if (comment != std::string::npos) {
            line = trim(line.substr(0, comment));
        }
        if (line.empty()) {
            return;
        }
        if (startsWith(line, "label_") && line.back() == ':') {
            labels_[line.substr(0, line.size() - 1)] =
                static_cast<int>(program_.instructions.size());
            return;
        }
        if (startsWith(line, "dcl_")) {
            parseDeclaration(line);
            return;
        }
        if (isShaderModelLine(line)) {
            return;
        }

        std::string predicate;
        if (line.front() == '(') {
            const size_t close = line.find(')');
            if (close != std::string::npos && close > 1) {
                predicate = trim(std::string_view(line).substr(1, close - 1));
                line = trim(std::string_view(line).substr(close + 1));
            }
        }
        if (line.empty()) {
            return;
        }
        if (line.back() == ':') {
            line.pop_back();
            line = trim(line);
        }

        const size_t space = line.find_first_of(" \t");
        const std::string opcodeText =
            space == std::string::npos ? line : line.substr(0, space);
        const std::string rest =
            space == std::string::npos ? std::string{} : trim(line.substr(space + 1));
        const std::string opcode = opcodeBase(opcodeText);
        if (opcode.empty()) {
            return;
        }

        Instruction instruction;
        instruction.index = static_cast<int>(program_.instructions.size());
        instruction.opcode = opcode;
        instruction.text = line;
        applyBranchInfo(instruction, opcode);

        const std::vector<std::string> operands = splitOperands(rest);
        const bool hasDestination = opcodeHasDestination(opcode);
        size_t writeIndex = static_cast<size_t>(-1);
        size_t sourceStart = 0;
        if (hasDestination && !operands.empty()) {
            if (operands[0] == "null" || operands[0] == "_") {
                if (operands.size() > 1) {
                    writeIndex = 1;
                    sourceStart = 2;
                } else {
                    sourceStart = 1;
                }
            } else {
                writeIndex = 0;
                sourceStart = 1;
            }
        }

        for (size_t i = 0; i < operands.size(); ++i) {
            const ParsedOperand parsed = parseOperand(operands[i]);
            appendIndexReads(instruction, parsed);
            if (i == writeIndex && parsed.kind == ParsedOperand::Kind::Register) {
                appendRegister(instruction.writes, parsed.reg);
                continue;
            }
            if (i < sourceStart) {
                continue;
            }
            switch (parsed.kind) {
            case ParsedOperand::Kind::Register:
                appendRegister(instruction.reads, parsed.reg);
                break;
            case ParsedOperand::Kind::Resource:
                appendResource(instruction.resourceRefs, parsed.resource);
                break;
            case ParsedOperand::Kind::Cbuffer: {
                CbufferRead read = parsed.cbuffer;
                read.instruction = instruction.index;
                if (!instruction.cbufferRead.has_value()) {
                    instruction.cbufferRead = read;
                }
                program_.cbufferReads.push_back(read);
                break;
            }
            case ParsedOperand::Kind::None:
            case ParsedOperand::Kind::Immediate:
                break;
            }
        }

        if (!predicate.empty()) {
            const ParsedOperand parsed = parseOperand(predicate);
            if (parsed.kind == ParsedOperand::Kind::Register) {
                appendRegister(instruction.reads, parsed.reg);
            }
        }

        updateStats(instruction);
        program_.instructions.push_back(std::move(instruction));
    }

    void parseDeclaration(const std::string& line) {
        const std::string header =
            line.substr(0, line.find_first_of(" \t"));
        if (startsWith(header, "dcl_temps")) {
            const size_t space = line.find_first_of(" \t");
            if (space != std::string::npos) {
                program_.stats.declaredTemps = parseInt(trim(line.substr(space + 1)));
            }
            return;
        }
        if (startsWith(header, "dcl_constantbuffer")) {
            const size_t cb = line.find("CB");
            const size_t cbLower = line.find("cb");
            const size_t position = cb != std::string::npos ? cb : cbLower;
            if (position != std::string::npos && position + 2 < line.size()) {
                size_t cursor = position + 2;
                while (cursor < line.size() && isDigit(line[cursor])) {
                    ++cursor;
                }
                if (cursor > position + 2) {
                    cbufferSlots_.insert(parseInt(std::string_view(line).substr(
                        position + 2, cursor - position - 2)));
                }
            }
            return;
        }
        if (startsWith(header, "dcl_sampler")) {
            const std::optional<int> binding = findBinding(line, 's');
            if (binding.has_value()) {
                addResource(Resource{"sampler", "s" + std::to_string(*binding),
                                     *binding, std::nullopt});
            }
            return;
        }
        if (startsWith(header, "dcl_resource_")) {
            const std::string type = header.substr(std::string("dcl_resource_").size());
            std::string kind = type;
            if (type == "structured") {
                kind = "structured_buffer";
            } else if (type == "raw") {
                kind = "raw_buffer";
            }
            const std::optional<int> binding = findBinding(line, 't');
            if (binding.has_value()) {
                addResource(Resource{kind, "t" + std::to_string(*binding), *binding,
                                     std::nullopt});
            }
            return;
        }
        if (startsWith(header, "dcl_uav_")) {
            const std::optional<int> binding = findBinding(line, 'u');
            if (binding.has_value()) {
                addResource(Resource{"uav", "u" + std::to_string(*binding), *binding,
                                     std::nullopt});
            }
            return;
        }
        if (startsWith(header, "dcl_input")) {
            const std::optional<int> binding = findBinding(line, 'v');
            if (binding.has_value()) {
                inputs_.insert(*binding);
            }
            return;
        }
        if (startsWith(header, "dcl_output")) {
            const std::optional<int> binding = findBinding(line, 'o');
            if (binding.has_value()) {
                outputs_.insert(*binding);
            }
        }
    }

    static std::optional<int> findBinding(std::string_view line, char prefix) {
        for (size_t i = 0; i + 1 < line.size(); ++i) {
            if (line[i] != prefix || !isDigit(line[i + 1])) {
                continue;
            }
            if (i > 0 && (std::isalnum(static_cast<unsigned char>(line[i - 1])) != 0 ||
                          line[i - 1] == '_')) {
                continue;
            }
            size_t cursor = i + 1;
            while (cursor < line.size() && isDigit(line[cursor])) {
                ++cursor;
            }
            return parseInt(line.substr(i + 1, cursor - i - 1));
        }
        return std::nullopt;
    }

    void addResource(Resource resource) {
        const auto existing = resources_.find(resource.name);
        if (existing == resources_.end()) {
            resources_.emplace(resource.name, std::move(resource));
        }
    }

    ResourceRef resourceRefFor(const std::string& name) const {
        const auto existing = resources_.find(name);
        const std::string kind =
            existing == resources_.end() ? std::string{} : existing->second.kind;
        const int binding = existing == resources_.end() ? 0 : existing->second.binding;
        if (!kind.empty()) {
            return ResourceRef{kind, name, binding};
        }
        if (!name.empty() && name.front() == 's') {
            return ResourceRef{"sampler", name, binding};
        }
        if (!name.empty() && name.front() == 'u') {
            return ResourceRef{"uav", name, binding};
        }
        if (!name.empty() && name.front() == 'g') {
            return ResourceRef{"global", name, binding};
        }
        return ResourceRef{"texture", name, binding};
    }

    ParsedOperand parseOperand(std::string_view rawText) const {
        const std::string text = trim(rawText);
        ParsedOperand result;
        if (text.empty() || text == "null" || text == "_") {
            return result;
        }
        const RegisterStart start = findRegisterStart(text);
        if (start.position == std::string_view::npos) {
            result.kind = ParsedOperand::Kind::Immediate;
            return result;
        }

        size_t cursor = start.position + 1;
        if (start.cbuffer) {
            ++cursor;
        }
        const size_t digitsBegin = cursor;
        while (cursor < text.size() && isDigit(text[cursor])) {
            ++cursor;
        }
        if (cursor == digitsBegin) {
            result.kind = ParsedOperand::Kind::Immediate;
            return result;
        }
        const int index =
            parseInt(std::string_view(text).substr(digitsBegin, cursor - digitsBegin));
        const std::string name =
            std::string(1, start.registerClass) +
            std::string(text.begin() + static_cast<std::ptrdiff_t>(digitsBegin),
                        text.begin() + static_cast<std::ptrdiff_t>(cursor));
        if (start.cbuffer) {
            const std::string bracket =
                readBracket(text, cursor, &cursor);
            const std::string mask = parseMask(text, cursor);
            const bool dynamic = !parseIndexReads(bracket).empty();
            const int base = staticBaseIndex(bracket);
            const int firstComponent =
                mask.empty() ? 0 : componentIndex(mask.front());
            result.kind = ParsedOperand::Kind::Cbuffer;
            result.cbuffer = CbufferRead{0, index, base * 16 + firstComponent * 4,
                                         static_cast<int>(mask.size()), dynamic};
            result.indexReads = parseIndexReads(bracket);
            return result;
        }
        if (start.registerClass == 'x' && cursor < text.size() &&
            text[cursor] == '[') {
            const std::string bracket =
                readBracket(text, cursor, &cursor);
            result.indexReads = parseIndexReads(bracket);
        }
        const std::string mask = parseMask(text, cursor);
        if (start.registerClass == 't' || start.registerClass == 's' ||
            start.registerClass == 'u' || start.registerClass == 'g') {
            result.kind = ParsedOperand::Kind::Resource;
            result.resource = resourceRefFor(name);
            return result;
        }
        result.kind = ParsedOperand::Kind::Register;
        result.reg = RegisterRef{std::string(1, start.registerClass), index, mask};
        return result;
    }

    static std::string readBracket(std::string_view text, size_t position,
                                   size_t* endPosition) {
        if (position >= text.size() || text[position] != '[') {
            if (endPosition != nullptr) {
                *endPosition = position;
            }
            return {};
        }
        const size_t close = text.find(']', position + 1);
        if (close == std::string_view::npos) {
            if (endPosition != nullptr) {
                *endPosition = text.size();
            }
            return trim(text.substr(position + 1));
        }
        if (endPosition != nullptr) {
            *endPosition = close + 1;
        }
        return trim(text.substr(position + 1, close - position - 1));
    }

    void applyBranchInfo(Instruction& instruction, std::string_view opcode) {
        const std::string kind = branchKindFor(opcode);
        instruction.branch.kind = kind;
        if (kind == "if" || kind == "loop" || kind == "switch") {
            instruction.branch.indent = indent_;
            ++indent_;
            blocks_.push_back(kind);
            return;
        }
        if (kind == "else" || kind == "case" || kind == "default") {
            instruction.branch.indent = std::max(0, indent_ - 1);
            return;
        }
        if (kind == "endif" || kind == "endloop" || kind == "endswitch") {
            if (!blocks_.empty()) {
                blocks_.pop_back();
            }
            indent_ = std::max(0, indent_ - 1);
            instruction.branch.indent = indent_;
            return;
        }
        instruction.branch.indent = indent_;
    }

    static void appendRegister(std::vector<RegisterRef>& refs,
                               const RegisterRef& ref) {
        const auto existing = std::find_if(
            refs.begin(), refs.end(), [&](const RegisterRef& value) {
                return value.registerClass == ref.registerClass &&
                       value.index == ref.index && value.mask == ref.mask;
            });
        if (existing == refs.end()) {
            refs.push_back(ref);
        }
    }

    static void appendResource(std::vector<ResourceRef>& refs,
                               const ResourceRef& ref) {
        const auto existing = std::find_if(
            refs.begin(), refs.end(), [&](const ResourceRef& value) {
                return value.kind == ref.kind && value.name == ref.name &&
                       value.index == ref.index;
            });
        if (existing == refs.end()) {
            refs.push_back(ref);
        }
    }

    static void appendIndexReads(Instruction& instruction,
                                 const ParsedOperand& parsed) {
        for (const RegisterRef& ref : parsed.indexReads) {
            appendRegister(instruction.reads, ref);
        }
    }

    void updateStats(const Instruction& instruction) {
        ++program_.stats.opcodeHistogram[instruction.opcode];
        if (startsWith(instruction.opcode, "sample")) {
            ++program_.stats.sampleCount;
        }
        if (startsWith(instruction.opcode, "ld")) {
            ++program_.stats.loadCount;
        }
        if (startsWith(instruction.opcode, "store")) {
            ++program_.stats.storeCount;
        }
        if (branchKindFor(instruction.opcode) != "none") {
            ++program_.stats.branchCount;
        }
        if (instruction.opcode == "loop") {
            ++program_.stats.loopCount;
        }
        for (const RegisterRef& write : instruction.writes) {
            if (write.registerClass == "r") {
                program_.stats.maxTempIndex =
                    std::max(program_.stats.maxTempIndex, write.index);
            }
        }
        for (const RegisterRef& read : instruction.reads) {
            if (read.registerClass == "r") {
                program_.stats.maxTempIndex =
                    std::max(program_.stats.maxTempIndex, read.index);
            }
        }
    }

    void finalize() {
        for (const Instruction& instruction : program_.instructions) {
            for (const RegisterRef& read : instruction.reads) {
                if (read.registerClass == "v") {
                    inputs_.insert(read.index);
                } else if (read.registerClass == "o") {
                    outputs_.insert(read.index);
                }
            }
            for (const RegisterRef& write : instruction.writes) {
                if (write.registerClass == "o") {
                    outputs_.insert(write.index);
                } else if (write.registerClass == "v") {
                    inputs_.insert(write.index);
                }
            }
        }

        program_.stats.inputCount = static_cast<int>(inputs_.size());
        program_.stats.outputCount = static_cast<int>(outputs_.size());
        program_.stats.cbufferSlotCount = static_cast<int>(cbufferSlots_.size());

        for (auto& [name, resource] : resources_) {
            (void)name;
            if (startsWith(resource.kind, "texture")) {
                ++program_.stats.textureCount;
            }
            if (resource.kind == "sampler") {
                ++program_.stats.samplerCount;
            }
            if (resource.kind == "uav") {
                ++program_.stats.uavCount;
            }
            if (resource.kind == "structured_buffer") {
                ++program_.stats.structuredBufferCount;
            }
            program_.resources.push_back(resource);
        }
        std::sort(program_.resources.begin(), program_.resources.end(),
                  [](const Resource& left, const Resource& right) {
                      if (left.binding != right.binding) {
                          return left.binding < right.binding;
                      }
                      if (left.kind != right.kind) {
                          return left.kind < right.kind;
                      }
                      return left.name < right.name;
                  });

        for (Instruction& instruction : program_.instructions) {
            const size_t label = instruction.text.find("label_");
            if (label == std::string::npos) {
                continue;
            }
            size_t cursor = label + 6;
            while (cursor < instruction.text.size() &&
                   isDigit(instruction.text[cursor])) {
                ++cursor;
            }
            if (cursor == label + 6) {
                continue;
            }
            const std::string name =
                instruction.text.substr(label, cursor - label);
            const auto target = labels_.find(name);
            if (target != labels_.end()) {
                instruction.branch.targetIndex = target->second;
            }
        }
    }

    DxbcProgram program_;
    std::map<std::string, Resource> resources_;
    std::set<int> inputs_;
    std::set<int> outputs_;
    std::set<int> cbufferSlots_;
    std::map<std::string, int> labels_;
    std::vector<std::string> blocks_;
    int indent_ = 0;
};

} // namespace

DxbcProgram parseDxbcDisassembly(std::string_view text) {
    DxbcParser parser;
    return parser.parse(text);
}

void applyLiveness(DxbcProgram& program, const LivenessAnalysis& analysis) {
    program.liveness = analysis.intervals;
    program.tempRegisterPeak = analysis.peak;
    for (size_t i = 0; i < program.instructions.size(); ++i) {
        program.instructions[i].pressure.temp =
            i < analysis.pressure.size() ? analysis.pressure[i] : 0;
    }
}

DetailReport buildDxbcDetail(const DxbcProgram& program, std::string source,
                             std::string stage, std::string variantId) {
    DetailReport detail;
    detail.source = std::move(source);
    detail.stage = std::move(stage);
    detail.backend = "dxbc";
    detail.variantId = std::move(variantId);
    detail.instructions = program.instructions;
    detail.liveness = program.liveness;
    detail.resources = program.resources;
    detail.cbufferReads = program.cbufferReads;
    return detail;
}

Metrics buildDxbcMetrics(const DxbcProgram& program) {
    Metrics metrics;
    metrics.instructionCount =
        static_cast<std::int64_t>(program.instructions.size());
    metrics.opcodeHistogram = program.stats.opcodeHistogram;
    if (program.stats.declaredTemps > 0) {
        metrics.tempRegisterMax = program.stats.declaredTemps;
    } else if (program.stats.maxTempIndex >= 0) {
        metrics.tempRegisterMax = program.stats.maxTempIndex + 1;
    } else {
        metrics.tempRegisterMax = 0;
    }
    metrics.tempRegisterPeak = program.tempRegisterPeak;
    metrics.inputCount = program.stats.inputCount;
    metrics.outputCount = program.stats.outputCount;
    metrics.cbufferSlotCount = program.stats.cbufferSlotCount;
    if (*metrics.cbufferSlotCount == 0 && !program.cbufferReads.empty()) {
        std::set<int> slots;
        for (const CbufferRead& read : program.cbufferReads) {
            slots.insert(read.slot);
        }
        metrics.cbufferSlotCount = static_cast<std::int64_t>(slots.size());
    }
    std::int64_t cbufferDwords = 0;
    std::int64_t dynamicReads = 0;
    for (const CbufferRead& read : program.cbufferReads) {
        cbufferDwords += read.dwords;
        if (read.dynamic) {
            ++dynamicReads;
        }
    }
    metrics.cbufferDwordsRead = cbufferDwords;
    metrics.dynamicCBufferReadCount = dynamicReads;
    metrics.textureCount = program.stats.textureCount;
    metrics.samplerCount = program.stats.samplerCount;
    metrics.uavCount = program.stats.uavCount;
    metrics.structuredBufferCount = program.stats.structuredBufferCount;
    metrics.sampleCount = program.stats.sampleCount;
    metrics.loadCount = program.stats.loadCount;
    metrics.storeCount = program.stats.storeCount;
    metrics.branchCount = program.stats.branchCount;
    metrics.loopCount = program.stats.loopCount;
    metrics.debugInfo = false;
    return metrics;
}

} // namespace hbg::analysis
