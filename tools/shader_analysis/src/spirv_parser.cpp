#include "hbg/analysis/spirv_parser.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace hbg::analysis {
namespace {

namespace fs = std::filesystem;

std::string trimCopy(std::string_view text) {
    while (!text.empty() &&
           (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
            text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
            text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

bool startsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::optional<int> parseId(std::string_view token) {
    if (token.size() < 2 || token.front() != '%') {
        return std::nullopt;
    }
    int value = 0;
    const auto parsed =
        std::from_chars(token.data() + 1, token.data() + token.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::int64_t> parseInteger(std::string_view token) {
    if (token.empty()) {
        return std::nullopt;
    }
    std::string text(token);
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.erase(0, 2);
    }
    std::int64_t value = 0;
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::int64_t> lastInteger(const std::vector<std::string>& tokens) {
    for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
        if (const auto value = parseInteger(*it); value.has_value()) {
            return value;
        }
    }
    return std::nullopt;
}

std::vector<std::string> splitTokens(std::string_view text) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        if (i >= text.size()) {
            break;
        }
        if (text[i] == '"') {
            size_t end = i + 1;
            while (end < text.size() && text[end] != '"') {
                ++end;
            }
            tokens.emplace_back(text.substr(i + 1, end - i - 1));
            i = end < text.size() ? end + 1 : end;
            continue;
        }
        size_t end = i;
        while (end < text.size() &&
               !std::isspace(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        tokens.emplace_back(text.substr(i, end - i));
        i = end;
    }
    return tokens;
}

struct RawInstruction {
    int index = 0;
    std::string text;
    std::string opcode;
    std::optional<int> resultId;
    std::vector<int> ids;
    std::vector<std::string> tokens;
    bool inFunction = false;
    bool functionBoundary = false;
    bool debugInstruction = false;
};

struct ParsedLine {
    std::optional<int> resultId;
    std::string opcode;
    std::vector<std::string> tokens;
};

ParsedLine parseLine(std::string_view text) {
    ParsedLine result;
    std::string line = trimCopy(text);
    if (line.empty()) {
        return result;
    }
    const size_t equals = line.find('=');
    std::string right = line;
    if (equals != std::string::npos) {
        const std::string left = trimCopy(line.substr(0, equals));
        if (const auto id = parseId(left); id.has_value()) {
            result.resultId = id;
        }
        right = trimCopy(line.substr(equals + 1));
    }
    if (right.empty()) {
        return result;
    }
    const size_t space = right.find_first_of(" \t");
    if (space == std::string::npos) {
        result.opcode = right;
        return result;
    }
    result.opcode = right.substr(0, space);
    result.tokens = splitTokens(right.substr(space + 1));
    return result;
}

bool isTypeOpcode(std::string_view opcode) {
    return startsWith(opcode, "OpType");
}

struct DecorationInfo {
    std::optional<int> binding;
    std::optional<int> descriptorSet;
    std::optional<int> arrayStride;
};

struct ResourceVariable {
    int id = 0;
    int pointerType = 0;
    std::string storageClass;
    Resource resource;
};

struct Module {
    std::vector<RawInstruction> instructions;
    std::map<int, std::string> names;
    std::map<int, DecorationInfo> decorations;
    std::map<int, std::vector<int>> memberOffsets;
    std::map<int, int> arrayStrides;
    std::map<int, std::string> typeKinds;
    std::map<int, int> pointerPointee;
    std::map<int, std::vector<int>> structMembers;
    std::map<int, int> arrayElements;
    std::map<int, int> typeDwords;
    std::map<int, std::int64_t> constants;
    std::set<int> typeIds;
    std::set<int> labelIds;
    std::optional<int> debugInfoSet;
    std::set<int> ssaIds;
    std::set<int> resourceIds;
    std::vector<ResourceVariable> variables;
};

int typeDwordsFor(const Module& module, int typeId) {
    if (typeId <= 0) {
        return 4;
    }
    const auto it = module.typeDwords.find(typeId);
    if (it == module.typeDwords.end() || it->second <= 0) {
        return 4;
    }
    return it->second;
}

std::string fallbackName(int id) {
    return "id_" + std::to_string(id);
}

std::optional<std::string> environmentValue(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    size_t size = 0;
    if (_dupenv_s(&value, &size, name) == 0 && value != nullptr) {
        std::string result(value);
        free(value);
        return result;
    }
    return std::nullopt;
#else
    if (const char* value = std::getenv(name); value != nullptr) {
        return std::string(value);
    }
    return std::nullopt;
#endif
}

std::string classifyUniformConstant(const Module& module, int typeId,
                                    std::set<int>& seen) {
    if (typeId <= 0 || !seen.insert(typeId).second) {
        return "resource";
    }
    const auto kind = module.typeKinds.find(typeId);
    if (kind == module.typeKinds.end()) {
        return "resource";
    }
    if (kind->second == "image" || kind->second == "sampled_image") {
        return "image";
    }
    if (kind->second == "sampler") {
        return "sampler";
    }
    if (kind->second == "array" || kind->second == "runtime_array") {
        const auto element = module.arrayElements.find(typeId);
        if (element != module.arrayElements.end()) {
            return classifyUniformConstant(module, element->second, seen);
        }
    }
    return "resource";
}

Resource buildResource(const Module& module, const ResourceVariable& variable) {
    Resource resource = variable.resource;
    if (resource.name.empty()) {
        const auto name = module.names.find(variable.id);
        if (name != module.names.end()) {
            resource.name = name->second;
        }
    }
    if (resource.name.empty()) {
        resource.name = fallbackName(variable.id);
    }
    return resource;
}

std::string storageKind(const Module& module, std::string_view storageClass,
                        int pointeeType) {
    if (storageClass == "UniformConstant") {
        std::set<int> seen;
        return classifyUniformConstant(module, pointeeType, seen);
    }
    if (storageClass == "Uniform") {
        return "uniform_buffer";
    }
    if (storageClass == "StorageBuffer") {
        return "storage_buffer";
    }
    if (storageClass == "PushConstant") {
        return "push_constant";
    }
    return {};
}

void parseGlobalInstruction(Module& module, const RawInstruction& instruction,
                            std::string_view text) {
    const ParsedLine parsed = parseLine(text);
    const std::string& opcode = parsed.opcode;
    const std::vector<std::string>& tokens = parsed.tokens;
    if (opcode == "OpExtInstImport" && parsed.resultId.has_value() &&
        !tokens.empty() &&
        tokens[0].find("NonSemantic.Shader.DebugInfo") != std::string::npos) {
        module.debugInfoSet = *parsed.resultId;
        return;
    }
    if (opcode == "OpName" && tokens.size() >= 2) {
        if (const auto id = parseId(tokens[0]); id.has_value()) {
            module.names[*id] = tokens[1];
        }
        return;
    }
    if (opcode == "OpDecorate" && tokens.size() >= 2) {
        const auto id = parseId(tokens[0]);
        if (!id.has_value()) {
            return;
        }
        DecorationInfo& decoration = module.decorations[*id];
        if (tokens[1] == "Binding" && tokens.size() >= 3) {
            if (const auto value = parseInteger(tokens[2]); value.has_value()) {
                decoration.binding = static_cast<int>(*value);
            }
        } else if (tokens[1] == "DescriptorSet" && tokens.size() >= 3) {
            if (const auto value = parseInteger(tokens[2]); value.has_value()) {
                decoration.descriptorSet = static_cast<int>(*value);
            }
        } else if (tokens[1] == "ArrayStride" && tokens.size() >= 3) {
            if (const auto value = parseInteger(tokens[2]); value.has_value()) {
                decoration.arrayStride = static_cast<int>(*value);
                module.arrayStrides[*id] = static_cast<int>(*value);
            }
        }
        return;
    }
    if (opcode == "OpMemberDecorate" && tokens.size() >= 4) {
        const auto id = parseId(tokens[0]);
        const auto member = parseInteger(tokens[1]);
        const auto value = parseInteger(tokens[3]);
        if (id.has_value() && member.has_value() && tokens[2] == "Offset" &&
            value.has_value()) {
            auto& offsets = module.memberOffsets[*id];
            if (offsets.size() <= static_cast<size_t>(*member)) {
                offsets.resize(static_cast<size_t>(*member) + 1, 0);
            }
            offsets[static_cast<size_t>(*member)] = static_cast<int>(*value);
        }
        return;
    }
    if (isTypeOpcode(opcode) && parsed.resultId.has_value()) {
        const int id = *parsed.resultId;
        module.typeIds.insert(id);
        const auto ids = [&]() {
            std::vector<int> values;
            for (const std::string& token : tokens) {
                if (const auto value = parseId(token); value.has_value()) {
                    values.push_back(*value);
                }
            }
            return values;
        }();
        if (opcode == "OpTypePointer" && !ids.empty()) {
            module.typeKinds[id] = "pointer";
            module.pointerPointee[id] = ids[0];
        } else if (opcode == "OpTypeStruct") {
            module.typeKinds[id] = "struct";
            module.structMembers[id] = ids;
        } else if (opcode == "OpTypeArray" && !ids.empty()) {
            module.typeKinds[id] = "array";
            module.arrayElements[id] = ids[0];
            auto count = lastInteger(tokens);
            if (!count.has_value() && ids.size() >= 2) {
                if (const auto value = module.constants.find(ids.back());
                    value != module.constants.end() && value->second > 0) {
                    count = value->second;
                }
            }
            if (count.has_value() && *count > 0) {
                module.typeDwords[id] =
                    typeDwordsFor(module, ids[0]) * static_cast<int>(*count);
            }
        } else if (opcode == "OpTypeRuntimeArray" && !ids.empty()) {
            module.typeKinds[id] = "runtime_array";
            module.arrayElements[id] = ids[0];
        } else if (opcode == "OpTypeVector" && !ids.empty()) {
            module.typeKinds[id] = "vector";
            const auto count = lastInteger(tokens);
            if (count.has_value() && *count > 0) {
                module.typeDwords[id] =
                    typeDwordsFor(module, ids[0]) * static_cast<int>(*count);
            }
        } else if (opcode == "OpTypeMatrix" && !ids.empty()) {
            module.typeKinds[id] = "matrix";
            const auto count = lastInteger(tokens);
            if (count.has_value() && *count > 0) {
                module.typeDwords[id] =
                    typeDwordsFor(module, ids[0]) * static_cast<int>(*count);
            }
        } else if (opcode == "OpTypeFloat" || opcode == "OpTypeInt") {
            module.typeKinds[id] = "scalar";
            const auto width = lastInteger(tokens);
            module.typeDwords[id] =
                width.has_value() && *width > 32 ? 2 : 1;
        } else if (opcode == "OpTypeImage") {
            module.typeKinds[id] = "image";
        } else if (opcode == "OpTypeSampler") {
            module.typeKinds[id] = "sampler";
        } else if (opcode == "OpTypeSampledImage") {
            module.typeKinds[id] = "sampled_image";
        }
        return;
    }
    if ((opcode == "OpConstant" || opcode == "OpSpecConstant") &&
        parsed.resultId.has_value()) {
        if (const auto value = lastInteger(tokens); value.has_value()) {
            module.constants[*parsed.resultId] = *value;
        }
        return;
    }
    if (opcode == "OpConstantNull" && parsed.resultId.has_value()) {
        module.constants[*parsed.resultId] = 0;
        return;
    }
    if ((opcode == "OpConstantTrue" || opcode == "OpConstantFalse") &&
        parsed.resultId.has_value()) {
        module.constants[*parsed.resultId] = opcode == "OpConstantTrue" ? 1 : 0;
        return;
    }
    if (opcode == "OpVariable" && parsed.resultId.has_value() && tokens.size() >= 2) {
        const auto pointerType = parseId(tokens[0]);
        const auto storageClass = tokens[1];
        if (!pointerType.has_value()) {
            return;
        }
        const auto pointee = module.pointerPointee.find(*pointerType);
        const std::string kind =
            storageKind(module, storageClass,
                        pointee == module.pointerPointee.end() ? 0 : pointee->second);
        if (kind.empty()) {
            return;
        }
        ResourceVariable variable;
        variable.id = *parsed.resultId;
        variable.pointerType = *pointerType;
        variable.storageClass = storageClass;
        variable.resource.kind = kind;
        variable.resource.binding =
            module.decorations[variable.id].binding.value_or(-1);
        variable.resource.name = module.names.count(variable.id)
                                     ? module.names[variable.id]
                                     : fallbackName(variable.id);
        module.resourceIds.insert(variable.id);
        module.variables.push_back(std::move(variable));
        return;
    }
    if (opcode == "OpLabel" && parsed.resultId.has_value()) {
        module.labelIds.insert(*parsed.resultId);
    }
    (void)instruction;
}

void buildModule(const std::string& disassembly, Module& module) {
    std::istringstream stream(disassembly);
    std::string line;
    int index = 0;
    while (std::getline(stream, line)) {
        const std::string trimmed = trimCopy(line);
        if (trimmed.empty()) {
            continue;
        }
        RawInstruction instruction;
        instruction.index = index++;
        instruction.text = trimmed;
        const ParsedLine parsed = parseLine(trimmed);
        instruction.opcode = parsed.opcode;
        instruction.resultId = parsed.resultId;
        instruction.tokens = parsed.tokens;
        for (const std::string& token : parsed.tokens) {
            if (const auto id = parseId(token); id.has_value()) {
                instruction.ids.push_back(*id);
            }
        }
        module.instructions.push_back(std::move(instruction));
    }

    bool inFunction = false;
    for (RawInstruction& instruction : module.instructions) {
        if (instruction.opcode == "OpFunction") {
            inFunction = true;
            instruction.functionBoundary = true;
        } else if (instruction.opcode == "OpFunctionEnd") {
            instruction.functionBoundary = true;
        }
        instruction.inFunction = inFunction && instruction.opcode != "OpFunctionEnd";
        if (instruction.opcode == "OpFunctionEnd") {
            inFunction = false;
        }
        parseGlobalInstruction(module, instruction, instruction.text);
        if (instruction.opcode == "OpExtInst" && module.debugInfoSet.has_value() &&
            std::find(instruction.ids.begin(), instruction.ids.end(),
                      *module.debugInfoSet) != instruction.ids.end()) {
            for (const std::string& token : instruction.tokens) {
                if (startsWith(token, "Debug")) {
                    instruction.debugInstruction = true;
                    break;
                }
            }
        }
        if (instruction.inFunction && !instruction.functionBoundary) {
            if (instruction.resultId.has_value() && !instruction.debugInstruction &&
                instruction.opcode != "OpLabel" &&
                instruction.opcode != "OpFunction" &&
                instruction.opcode != "OpFunctionEnd") {
                module.ssaIds.insert(*instruction.resultId);
            }
        }
    }
}

std::optional<int> resultTypeFor(const Module& module, const RawInstruction& instruction) {
    for (const int id : instruction.ids) {
        if (id != instruction.resultId.value_or(-1) &&
            module.typeIds.count(id) != 0) {
            return id;
        }
    }
    return std::nullopt;
}

std::vector<int> valueIdsFor(const Module& module, const RawInstruction& instruction) {
    std::vector<int> result;
    const auto resultType = resultTypeFor(module, instruction);
    bool skippedType = false;
    for (const int id : instruction.ids) {
        if (instruction.resultId.has_value() && id == *instruction.resultId) {
            continue;
        }
        if (!skippedType && resultType.has_value() && id == *resultType) {
            skippedType = true;
            continue;
        }
        result.push_back(id);
    }
    return result;
}

std::string branchKind(std::string_view opcode) {
    if (opcode == "OpBranch") {
        return "branch";
    }
    if (opcode == "OpBranchConditional") {
        return "branchConditional";
    }
    if (opcode == "OpSwitch") {
        return "switch";
    }
    if (opcode == "OpSelectionMerge") {
        return "selectionMerge";
    }
    if (opcode == "OpLoopMerge") {
        return "loopMerge";
    }
    if (opcode == "OpLabel") {
        return "label";
    }
    if (opcode == "OpReturn" || opcode == "OpReturnValue") {
        return "return";
    }
    if (opcode == "OpKill" || opcode == "OpTerminateInvocation") {
        return "kill";
    }
    if (opcode == "OpUnreachable") {
        return "unreachable";
    }
    return "none";
}

bool isBranchMetricOpcode(std::string_view opcode) {
    return opcode == "OpBranch" || opcode == "OpBranchConditional" ||
           opcode == "OpSwitch" || opcode == "OpReturn" ||
           opcode == "OpReturnValue" || opcode == "OpKill" ||
           opcode == "OpTerminateInvocation" || opcode == "OpUnreachable";
}

std::string resourceKey(const Resource& resource) {
    return resource.kind + "|" + std::to_string(resource.binding) + "|" + resource.name;
}

struct InstructionAnalysis {
    std::vector<Instruction> instructions;
    std::vector<LivenessInterval> liveness;
    std::vector<CbufferRead> cbufferReads;
    std::map<std::string, std::int64_t> opcodeHistogram;
    std::int64_t instructionCount = 0;
    std::int64_t maxLiveSsaValues = 0;
    std::int64_t accessChainCount = 0;
    std::int64_t loadCount = 0;
    std::int64_t storeCount = 0;
    std::int64_t branchCount = 0;
    std::int64_t loopCount = 0;
    std::int64_t sourceLineCount = 0;
    std::int64_t cbufferSlotCount = 0;
    std::int64_t cbufferDwordsRead = 0;
    std::int64_t dynamicCBufferReadCount = 0;
};

void buildResourceMapping(const Module& module, const SpirvReflection& reflection,
                          std::vector<Resource>& resources,
                          std::map<int, Resource>& variableResources) {
    std::vector<Resource> disassemblyResources;
    for (const ResourceVariable& variable : module.variables) {
        disassemblyResources.push_back(buildResource(module, variable));
    }

    if (reflection.available) {
        resources = reflection.resources;
        bool hasPushConstant = false;
        for (const Resource& resource : resources) {
            if (resource.kind == "push_constant") {
                hasPushConstant = true;
            }
        }
        if (!hasPushConstant) {
            for (const Resource& resource : disassemblyResources) {
                if (resource.kind == "push_constant") {
                    resources.push_back(resource);
                }
            }
        }
    } else {
        resources = disassemblyResources;
    }

    std::set<std::string> seen;
    std::vector<Resource> unique;
    for (const Resource& resource : resources) {
        if (seen.insert(resourceKey(resource)).second) {
            unique.push_back(resource);
        }
    }
    resources = std::move(unique);
    std::sort(resources.begin(), resources.end(),
              [](const Resource& left, const Resource& right) {
                  return std::tie(left.binding, left.kind, left.name) <
                         std::tie(right.binding, right.kind, right.name);
              });

    for (const ResourceVariable& variable : module.variables) {
        const Resource candidate = buildResource(module, variable);
        const Resource* matched = nullptr;
        for (const Resource& resource : resources) {
            if (resource.kind == candidate.kind &&
                resource.binding == candidate.binding) {
                matched = &resource;
                break;
            }
            if (resource.kind == candidate.kind && resource.name == candidate.name) {
                matched = &resource;
            }
        }
        if (matched != nullptr) {
            variableResources[variable.id] = *matched;
        }
    }
}

int offsetForChain(const Module& module, int rootPointee,
                   const std::vector<int>& indices, bool& dynamic) {
    int offset = 0;
    int currentType = rootPointee;
    for (const int indexId : indices) {
        const auto constant = module.constants.find(indexId);
        const auto kind = module.typeKinds.find(currentType);
        if (kind == module.typeKinds.end()) {
            break;
        }
        if (kind->second == "struct") {
            if (constant == module.constants.end()) {
                dynamic = true;
                break;
            }
            const auto members = module.structMembers.find(currentType);
            const auto offsets = module.memberOffsets.find(currentType);
            if (members == module.structMembers.end() || constant->second < 0 ||
                static_cast<size_t>(constant->second) >= members->second.size()) {
                break;
            }
            const size_t member = static_cast<size_t>(constant->second);
            if (offsets != module.memberOffsets.end() && member < offsets->second.size()) {
                offset += offsets->second[member];
            }
            currentType = members->second[member];
        } else if (kind->second == "array") {
            if (constant == module.constants.end()) {
                dynamic = true;
                break;
            }
            const auto stride = module.arrayStrides.find(currentType);
            if (stride == module.arrayStrides.end()) {
                break;
            }
            offset += static_cast<int>(constant->second) * stride->second;
            const auto element = module.arrayElements.find(currentType);
            if (element == module.arrayElements.end()) {
                break;
            }
            currentType = element->second;
        } else {
            break;
        }
    }
    return offset;
}

void analyzeInstructions(const Module& module,
                         const std::map<int, Resource>& variableResources,
                         int rootPointee, InstructionAnalysis& analysis) {
    std::map<int, int> chainRoot;
    std::map<int, std::vector<int>> chainIndices;
    std::map<int, int> chainPointee;
    std::map<int, int> labelIndices;
    std::map<int, int> definition;
    std::map<int, int> firstUse;
    std::map<int, int> lastUse;
    std::set<int> labels = module.labelIds;

    for (const RawInstruction& raw : module.instructions) {
        if (raw.opcode == "OpLabel" && raw.resultId.has_value()) {
            labelIndices[*raw.resultId] = raw.index;
        }
    }

    for (const ResourceVariable& variable : module.variables) {
        const int pointee = module.pointerPointee.count(variable.pointerType)
                                ? module.pointerPointee.at(variable.pointerType)
                                : 0;
        chainRoot[variable.id] = variable.id;
        chainPointee[variable.id] = pointee;
    }

    int pendingSourceLine = -1;
    for (const RawInstruction& raw : module.instructions) {
        Instruction instruction;
        instruction.index = raw.index;
        instruction.opcode = raw.opcode;
        instruction.text = raw.text;
        instruction.branch.kind = branchKind(raw.opcode);
        bool isDebugLine = false;
        bool isDebugNoLine = false;
        if (raw.debugInstruction) {
            for (const std::string& token : raw.tokens) {
                if (token == "DebugLine") {
                    isDebugLine = true;
                } else if (token == "DebugNoLine") {
                    isDebugNoLine = true;
                }
            }
        }
        if (raw.opcode == "OpLine") {
            if (raw.tokens.size() >= 2) {
                if (const auto value = parseInteger(raw.tokens[1]);
                    value.has_value() && *value >= 0) {
                    pendingSourceLine = static_cast<int>(*value);
                }
            }
            ++analysis.sourceLineCount;
        } else if (raw.opcode == "OpNoLine") {
            pendingSourceLine = -1;
        } else if (isDebugLine) {
            size_t nameIndex = 0;
            for (size_t i = 0; i < raw.tokens.size(); ++i) {
                if (raw.tokens[i] == "DebugLine") {
                    nameIndex = i;
                    break;
                }
            }
            if (nameIndex + 2 < raw.tokens.size()) {
                if (const auto id = parseId(raw.tokens[nameIndex + 2]);
                    id.has_value()) {
                    const auto line = module.constants.find(*id);
                    if (line != module.constants.end() && line->second >= 0) {
                        pendingSourceLine = static_cast<int>(line->second);
                    }
                }
            }
            ++analysis.sourceLineCount;
        } else if (isDebugNoLine) {
            pendingSourceLine = -1;
        } else if (!raw.debugInstruction && pendingSourceLine >= 0 &&
                   raw.opcode != "OpFunctionEnd") {
            instruction.sourceLine = pendingSourceLine;
            pendingSourceLine = -1;
        }
        if (raw.debugInstruction) {
            analysis.instructions.push_back(std::move(instruction));
            continue;
        }
        if (raw.inFunction && raw.opcode != "OpFunction" &&
            raw.opcode != "OpFunctionEnd") {
            ++analysis.instructionCount;
            ++analysis.opcodeHistogram[raw.opcode];
            if (raw.opcode == "OpAccessChain" || raw.opcode == "OpInBoundsAccessChain") {
                ++analysis.accessChainCount;
            } else if (raw.opcode == "OpLoad") {
                ++analysis.loadCount;
            } else if (raw.opcode == "OpStore") {
                ++analysis.storeCount;
            }
            if (isBranchMetricOpcode(raw.opcode)) {
                ++analysis.branchCount;
            }
            if (raw.opcode == "OpLoopMerge") {
                ++analysis.loopCount;
            }
        }
        if (raw.opcode == "OpLabel" && raw.resultId.has_value()) {
            labelIndices[*raw.resultId] = raw.index;
        }

        if (!raw.inFunction) {
            analysis.instructions.push_back(std::move(instruction));
            continue;
        }

        const auto resultType = resultTypeFor(module, raw);
        const std::vector<int> values = valueIdsFor(module, raw);
        if (raw.opcode == "OpAccessChain" || raw.opcode == "OpInBoundsAccessChain") {
            if (raw.resultId.has_value() && !values.empty()) {
                const int base = values[0];
                if (chainRoot.count(base)) {
                    chainRoot[*raw.resultId] = chainRoot[base];
                    std::vector<int> indices = chainIndices[base];
                    indices.insert(indices.end(), values.begin() + 1, values.end());
                    chainIndices[*raw.resultId] = std::move(indices);
                    chainPointee[*raw.resultId] = chainPointee[base];
                }
            }
        }

        if (raw.resultId.has_value() && module.ssaIds.count(*raw.resultId)) {
            RegisterRef ref{"ssa", *raw.resultId, ""};
            instruction.writes.push_back(ref);
            definition.emplace(*raw.resultId, raw.index);
        }

        std::set<int> seenReads;
        for (const int id : values) {
            if (labels.count(id) || (raw.resultId.has_value() && id == *raw.resultId)) {
                continue;
            }
            if (module.ssaIds.count(id)) {
                if (seenReads.insert(id).second) {
                    instruction.reads.push_back({"ssa", id, ""});
                }
                firstUse.emplace(id, raw.index);
                lastUse[id] = raw.index;
            } else if (module.resourceIds.count(id)) {
                if (seenReads.insert(id).second) {
                    instruction.reads.push_back({"resource", id, ""});
                }
                firstUse.emplace(id, raw.index);
                lastUse[id] = raw.index;
                const auto resource = variableResources.find(id);
                if (resource != variableResources.end()) {
                    instruction.resourceRefs.push_back(
                        {resource->second.kind, resource->second.name,
                         resource->second.binding});
                }
            }
        }

        if (raw.opcode == "OpLoad" && !values.empty()) {
            const int pointer = values[0];
            const auto root = chainRoot.find(pointer);
            if (root != chainRoot.end()) {
                const auto resource = variableResources.find(root->second);
                if (resource != variableResources.end() &&
                    resource->second.kind == "uniform_buffer") {
                    bool dynamic = false;
                    const int pointee = chainPointee.count(pointer)
                                            ? chainPointee.at(pointer)
                                            : rootPointee;
                    const std::vector<int> indices = chainIndices[pointer];
                    const int offset = offsetForChain(module, pointee, indices, dynamic);
                    const int dwords = resultType.has_value()
                                           ? typeDwordsFor(module, *resultType)
                                           : 4;
                    CbufferRead read;
                    read.instruction = raw.index;
                    read.slot = resource->second.binding >= 0
                                    ? resource->second.binding
                                    : 0;
                    read.offset = offset;
                    read.dwords = dwords;
                    read.dynamic = dynamic;
                    instruction.cbufferRead = read;
                    analysis.cbufferReads.push_back(read);
                    analysis.cbufferDwordsRead += dwords;
                    if (dynamic) {
                        ++analysis.dynamicCBufferReadCount;
                    }
                }
            }
        }

        if (raw.opcode == "OpSelectionMerge" || raw.opcode == "OpLoopMerge") {
            if (!values.empty()) {
                const auto target = labelIndices.find(values[0]);
                if (target != labelIndices.end()) {
                    instruction.branch.targetIndex = target->second;
                }
            }
        } else if (raw.opcode == "OpBranch") {
            if (!values.empty()) {
                const auto target = labelIndices.find(values[0]);
                if (target != labelIndices.end()) {
                    instruction.branch.targetIndex = target->second;
                }
            }
        } else if (raw.opcode == "OpBranchConditional") {
            if (values.size() >= 2) {
                const auto target = labelIndices.find(values[1]);
                if (target != labelIndices.end()) {
                    instruction.branch.targetIndex = target->second;
                }
            }
        } else if (raw.opcode == "OpSwitch") {
            if (values.size() >= 2) {
                const auto target = labelIndices.find(values[1]);
                if (target != labelIndices.end()) {
                    instruction.branch.targetIndex = target->second;
                }
            }
        }

        if (raw.opcode == "OpFunctionEnd") {
            pendingSourceLine = -1;
        }
        analysis.instructions.push_back(std::move(instruction));
    }

    std::vector<int> deltas(module.instructions.size() + 1, 0);
    for (const auto& [id, start] : definition) {
        const auto last = lastUse.find(id);
        const int end = last == lastUse.end() ? start : std::max(start, last->second);
        analysis.liveness.push_back({"ssa", id, start, end});
        if (start >= 0 && static_cast<size_t>(start) < deltas.size()) {
            ++deltas[start];
        }
        if (end + 1 >= 0 && static_cast<size_t>(end + 1) < deltas.size()) {
            --deltas[end + 1];
        }
    }
    for (const int id : module.resourceIds) {
        const auto first = firstUse.find(id);
        const auto last = lastUse.find(id);
        if (first == firstUse.end() || last == lastUse.end()) {
            continue;
        }
        analysis.liveness.push_back({"resource", id, first->second, last->second});
    }
    std::sort(analysis.liveness.begin(), analysis.liveness.end(),
              [](const LivenessInterval& left, const LivenessInterval& right) {
                  return std::tie(left.registerClass, left.index, left.start) <
                         std::tie(right.registerClass, right.index, right.start);
              });

    int pressure = 0;
    for (size_t i = 0; i < analysis.instructions.size(); ++i) {
        pressure += deltas[i];
        analysis.instructions[i].pressure.ssa = pressure;
        analysis.maxLiveSsaValues =
            std::max<std::int64_t>(analysis.maxLiveSsaValues, pressure);
    }

    std::set<int> slots;
    for (const CbufferRead& read : analysis.cbufferReads) {
        slots.insert(read.slot);
    }
    analysis.cbufferSlotCount = static_cast<std::int64_t>(slots.size());

    std::vector<int> depthDeltas(module.instructions.size() + 1, 0);
    for (const RawInstruction& raw : module.instructions) {
        if (raw.opcode != "OpSelectionMerge" && raw.opcode != "OpLoopMerge") {
            continue;
        }
        const std::vector<int> values = valueIdsFor(module, raw);
        if (values.empty()) {
            continue;
        }
        const auto target = labelIndices.find(values[0]);
        if (target == labelIndices.end()) {
            continue;
        }
        const int start = raw.index + 1;
        const int end = target->second;
        if (start < end && start >= 0 && static_cast<size_t>(end) < depthDeltas.size()) {
            ++depthDeltas[start];
            --depthDeltas[end];
        }
    }
    int depth = 0;
    for (size_t i = 0; i < analysis.instructions.size(); ++i) {
        depth += depthDeltas[i];
        analysis.instructions[i].branch.indent = depth;
    }
}

void mergeReflectionMetrics(const SpirvReflection& reflection, Metrics& metrics) {
    if (reflection.available) {
        metrics.uniformBufferBytes = reflection.uniformBufferBytes.value_or(0);
        metrics.pushConstantBytes = reflection.pushConstantBytes.value_or(0);
    }
}

} // namespace

SpirvReflection parseSpirvReflectionJson(std::string_view text) {
    SpirvReflection reflection;
    try {
        const nlohmann::json document = nlohmann::json::parse(text.begin(), text.end());
        if (!document.is_object()) {
            return reflection;
        }
        reflection.available = true;
        std::int64_t uniformBytes = 0;
        std::int64_t pushBytes = 0;
        const auto append = [&](const nlohmann::json& array, const char* kind,
                                bool uniformBlock, bool pushConstant) {
            if (!array.is_array()) {
                return;
            }
            for (const nlohmann::json& value : array) {
                if (!value.is_object()) {
                    continue;
                }
                Resource resource;
                resource.kind = kind;
                resource.name = value.value("name", std::string{});
                if (resource.name.empty()) {
                    resource.name = value.value("type", std::string{});
                }
                resource.binding = value.value("binding", -1);
                if (value.contains("block_size") && value["block_size"].is_number_integer()) {
                    const std::int64_t size = value["block_size"].get<std::int64_t>();
                    if (size > 0) {
                        resource.size = static_cast<int>(size);
                    }
                    if (uniformBlock) {
                        uniformBytes += size;
                    }
                    if (pushConstant) {
                        pushBytes += size;
                    }
                }
                reflection.resources.push_back(std::move(resource));
            }
        };
        append(document.value("separate_images", nlohmann::json::array()), "image",
               false, false);
        append(document.value("storage_images", nlohmann::json::array()), "image",
               false, false);
        append(document.value("separate_samplers", nlohmann::json::array()), "sampler",
               false, false);
        append(document.value("ubos", nlohmann::json::array()), "uniform_buffer", true,
               false);
        append(document.value("ssbos", nlohmann::json::array()), "storage_buffer",
               false, false);
        append(document.value("push_constants", nlohmann::json::array()),
               "push_constant", false, true);
        reflection.uniformBufferBytes = uniformBytes;
        reflection.pushConstantBytes = pushBytes;
    } catch (const std::exception&) {
        reflection.available = false;
        reflection.resources.clear();
    }
    return reflection;
}

SpirvAnalysisResult analyzeSpirvDisassembly(std::string_view disassembly,
                                            const SpirvReflection& reflection,
                                            const SpirvAnalysisOptions& options,
                                            std::string source, std::string stage,
                                            std::string variantId) {
    Module module;
    buildModule(std::string(disassembly), module);

    std::vector<Resource> resources;
    std::map<int, Resource> variableResources;
    buildResourceMapping(module, reflection, resources, variableResources);

    int rootPointee = 0;
    for (const ResourceVariable& variable : module.variables) {
        if (variable.resource.kind == "uniform_buffer") {
            const auto pointee = module.pointerPointee.find(variable.pointerType);
            if (pointee != module.pointerPointee.end()) {
                rootPointee = pointee->second;
                break;
            }
        }
    }

    InstructionAnalysis analysis;
    analyzeInstructions(module, variableResources, rootPointee, analysis);

    DetailReport detail;
    detail.source = std::move(source);
    detail.stage = std::move(stage);
    detail.backend = "spirv";
    detail.variantId = std::move(variantId);
    detail.instructions = std::move(analysis.instructions);
    detail.liveness = std::move(analysis.liveness);
    detail.resources = std::move(resources);
    detail.cbufferReads = std::move(analysis.cbufferReads);

    Metrics metrics;
    metrics.instructionCount = analysis.instructionCount;
    metrics.opcodeHistogram = std::move(analysis.opcodeHistogram);
    metrics.ssaValueCount = static_cast<std::int64_t>(module.ssaIds.size());
    metrics.maxLiveSsaValues = analysis.maxLiveSsaValues;
    metrics.accessChainCount = analysis.accessChainCount;
    metrics.loadCount = analysis.loadCount;
    metrics.storeCount = analysis.storeCount;
    metrics.branchCount = analysis.branchCount;
    metrics.loopCount = analysis.loopCount;
    metrics.sourceLineCount = analysis.sourceLineCount;
    metrics.debugInfo = options.debugInfo || analysis.sourceLineCount > 0;
    metrics.cbufferSlotCount = analysis.cbufferSlotCount;
    metrics.cbufferDwordsRead = analysis.cbufferDwordsRead;
    metrics.dynamicCBufferReadCount = analysis.dynamicCBufferReadCount;

    std::int64_t imageCount = 0;
    std::int64_t samplerCount = 0;
    std::int64_t uniformBufferCount = 0;
    std::int64_t storageBufferCount = 0;
    std::int64_t pushConstantCount = 0;
    for (const Resource& resource : detail.resources) {
        if (resource.kind == "image") {
            ++imageCount;
        } else if (resource.kind == "sampler") {
            ++samplerCount;
        } else if (resource.kind == "uniform_buffer") {
            ++uniformBufferCount;
        } else if (resource.kind == "storage_buffer") {
            ++storageBufferCount;
        } else if (resource.kind == "push_constant") {
            ++pushConstantCount;
        }
    }
    metrics.imageCount = imageCount;
    metrics.samplerCount = samplerCount;
    metrics.uniformBufferCount = uniformBufferCount;
    metrics.storageBufferCount = storageBufferCount;
    metrics.pushConstantCount = pushConstantCount;
    mergeReflectionMetrics(reflection, metrics);

    return {std::move(detail), std::move(metrics)};
}

fs::path findSpirvTool(const fs::path& configured, std::string_view name) {
    const std::string base(name);
    std::vector<std::string> candidates;
    const auto addCandidate = [&](const fs::path& path) {
        if (path.empty()) {
            return;
        }
        candidates.push_back(path.string());
#if defined(_WIN32)
        if (!path.has_extension()) {
            candidates.push_back((path.string() + ".exe"));
        }
#endif
    };
    if (!configured.empty()) {
        addCandidate(configured);
        for (const std::string& candidate : candidates) {
            std::error_code error;
            if (fs::is_regular_file(candidate, error) && !error) {
                return fs::path(candidate);
            }
        }
        return {};
    }
#if defined(_WIN32)
    if (const auto sdk = environmentValue("VULKAN_SDK");
        sdk.has_value() && !sdk->empty()) {
        addCandidate(fs::path(*sdk) / "Bin" / (base + ".exe"));
    }
    if (const auto path = environmentValue("PATH"); path.has_value()) {
        std::stringstream stream(*path);
        std::string item;
        while (std::getline(stream, item, ';')) {
            if (!item.empty()) {
                addCandidate(fs::path(item) / (base + ".exe"));
            }
        }
    }
    std::error_code directoryError;
    for (fs::directory_iterator it("C:\\VulkanSDK", directoryError), end;
         !directoryError && it != end; it.increment(directoryError)) {
        if (it->is_directory(directoryError) && !directoryError) {
            addCandidate(it->path() / "Bin" / (base + ".exe"));
        }
        directoryError.clear();
    }
#else
    if (const auto sdk = environmentValue("VULKAN_SDK");
        sdk.has_value() && !sdk->empty()) {
        addCandidate(fs::path(*sdk) / "bin" / base);
    }
    if (const auto path = environmentValue("PATH"); path.has_value()) {
        std::stringstream stream(*path);
        std::string item;
        while (std::getline(stream, item, ':')) {
            if (!item.empty()) {
                addCandidate(fs::path(item) / base);
            }
        }
    }
#endif
    for (const std::string& candidate : candidates) {
        std::error_code candidateError;
        if (fs::is_regular_file(candidate, candidateError) && !candidateError) {
            return fs::path(candidate);
        }
    }
    return {};
}

} // namespace hbg::analysis
