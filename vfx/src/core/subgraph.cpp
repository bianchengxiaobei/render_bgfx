#include "subgraph.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vfx::detail {
namespace {

constexpr std::size_t kMaxSubgraphDepth = 16u;

std::string childPath(std::string_view base, std::string_view name) {
    if (base.empty()) {
        return std::string(name);
    }
    return std::string(base) + "." + std::string(name);
}

std::string elementPath(std::string_view base, std::size_t index) {
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

bool expressionTypeFromString(std::string_view name, PropertyType& type) {
    if (name == "float") {
        type = PropertyType::Float;
    } else if (name == "int") {
        type = PropertyType::Int;
    } else if (name == "bool") {
        type = PropertyType::Bool;
    } else if (name == "vector2" || name == "float2") {
        type = PropertyType::Vector2;
    } else if (name == "vector3" || name == "float3") {
        type = PropertyType::Vector3;
    } else if (name == "vector4" || name == "float4") {
        type = PropertyType::Vector4;
    } else if (name == "color") {
        type = PropertyType::Color;
    } else {
        return false;
    }
    return true;
}

bool isExpressionPropertyType(PropertyType type) {
    return type == PropertyType::Float || type == PropertyType::Int || type == PropertyType::Bool ||
           type == PropertyType::Vector2 || type == PropertyType::Vector3 ||
           type == PropertyType::Vector4 || type == PropertyType::Color;
}

struct InputSpec {
    std::string name;
    PropertyType type = PropertyType::Float;
    bool hasDefault = false;
    Value defaultValue;
};

struct OutputSpec {
    std::string name;
    PropertyType type = PropertyType::Float;
    Value value;
};

struct Definition {
    std::size_t index = 0;
    std::string path;
    const SubgraphDesc* desc = nullptr;
    std::vector<InputSpec> inputs;
    std::vector<OutputSpec> outputs;
};

class Flattener {
  public:
    Flattener(Graph& graph, std::vector<ParseError>& errors) : m_graph(graph), m_errors(errors) {}

    bool run() {
        if (!parseDefinitions()) {
            return false;
        }
        if (!validateDependencies()) {
            return false;
        }
        const ValueBindings bindings;
        std::vector<std::string> stack;
        for (std::size_t systemIndex = 0; systemIndex < m_graph.systems.size(); ++systemIndex) {
            SystemDesc& system = m_graph.systems[systemIndex];
            const std::string systemPath = "systems[" + std::to_string(systemIndex) + "]";
            std::vector<Block> expandedInit;
            if (!expandBlocks(system.initBlocks, false, systemPath + ".init.blocks", bindings, stack,
                              expandedInit)) {
                return false;
            }
            system.initBlocks = std::move(expandedInit);
            std::vector<Block> expandedUpdate;
            if (!expandBlocks(system.updateBlocks, true, systemPath + ".update.blocks", bindings, stack,
                              expandedUpdate)) {
                return false;
            }
            system.updateBlocks = std::move(expandedUpdate);
            const std::string eventsPath = childPath(systemPath, "events");
            for (std::size_t i = 0; i < system.events.receive.size(); ++i) {
                const std::string path =
                    elementPath(childPath(eventsPath, "receive"), i) + ".attributes";
                if (!expandValue(system.events.receive[i].attributes, path, bindings, stack,
                                 system.events.receive[i].attributes)) {
                    return false;
                }
            }
            for (std::size_t i = 0; i < system.events.emit.size(); ++i) {
                const std::string path = elementPath(childPath(eventsPath, "emit"), i) + ".attributes";
                if (!expandValue(system.events.emit[i].attributes, path, bindings, stack,
                                 system.events.emit[i].attributes)) {
                    return false;
                }
            }
        }
        m_graph.subgraphs.clear();
        return true;
    }

  private:
    using ValueBindings = std::vector<std::pair<std::string, Value>>;

    bool fail(const std::string& path, std::string message) {
        if (m_errors.empty()) {
            m_errors.push_back(ParseError{path, std::move(message)});
        }
        return false;
    }

    const Definition* findDefinition(std::string_view name) const {
        for (const Definition& definition : m_definitions) {
            if (definition.desc->name == name) {
                return &definition;
            }
        }
        return nullptr;
    }

    const Value* findBinding(const ValueBindings& bindings, std::string_view name) const {
        for (const auto& [bindingName, value] : bindings) {
            if (bindingName == name) {
                return &value;
            }
        }
        return nullptr;
    }

    bool parseDefinitions() {
        m_definitions.reserve(m_graph.subgraphs.size());
        for (std::size_t i = 0; i < m_graph.subgraphs.size(); ++i) {
            Definition definition;
            definition.index = i;
            definition.path = "subgraphs[" + std::to_string(i) + "]";
            definition.desc = &m_graph.subgraphs[i];
            if (!parseDefinition(definition)) {
                return false;
            }
            m_definitions.push_back(std::move(definition));
        }
        return true;
    }

    bool parseDefinition(Definition& definition) {
        const SubgraphDesc& desc = *definition.desc;
        if (desc.inputs.kind != Value::Kind::Invalid) {
            if (desc.inputs.kind != Value::Kind::Array) {
                return fail(childPath(definition.path, "inputs"), "must be an array");
            }
            for (std::size_t i = 0; i < desc.inputs.array.size(); ++i) {
                if (!parseInput(desc.inputs.array[i], definition, i)) {
                    return false;
                }
            }
        }
        if (desc.outputs.kind != Value::Kind::Invalid) {
            if (desc.outputs.kind != Value::Kind::Array) {
                return fail(childPath(definition.path, "outputs"), "must be an array");
            }
            for (std::size_t i = 0; i < desc.outputs.array.size(); ++i) {
                if (!parseOutput(desc.outputs.array[i], definition, i)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool parseInput(const Value& value, Definition& definition, std::size_t index) {
        const std::string path = elementPath(childPath(definition.path, "inputs"), index);
        if (!value.isObject()) {
            return fail(path, "must be an object");
        }
        InputSpec input;
        for (const auto& [field, member] : value.object) {
            (void)member;
            if (field != "name" && field != "type" && field != "default") {
                return fail(childPath(path, field), "unknown input field");
            }
        }
        const Value* name = value.find("name");
        if (name == nullptr || name->kind != Value::Kind::String) {
            return fail(childPath(path, "name"), "must be a string");
        }
        if (!isIdentifier(name->string)) {
            return fail(childPath(path, "name"), "must be a valid identifier");
        }
        if (std::any_of(definition.inputs.begin(), definition.inputs.end(),
                        [&](const InputSpec& existing) { return existing.name == name->string; })) {
            return fail(childPath(path, "name"), "duplicate input '" + name->string + "'");
        }
        input.name = name->string;
        const Value* type = value.find("type");
        if (type == nullptr || type->kind != Value::Kind::String) {
            return fail(childPath(path, "type"), "must be a string");
        }
        if (!expressionTypeFromString(type->string, input.type)) {
            return fail(childPath(path, "type"), "unknown subgraph input type '" + type->string + "'");
        }
        if (!isExpressionPropertyType(input.type)) {
            return fail(childPath(path, "type"),
                        "texture/curve/gradient inputs are not supported in pure-function subgraphs");
        }
        if (const Value* defaultValue = value.find("default"); defaultValue != nullptr) {
            input.hasDefault = true;
            input.defaultValue = *defaultValue;
        }
        definition.inputs.push_back(std::move(input));
        return true;
    }

    bool parseOutput(const Value& value, Definition& definition, std::size_t index) {
        const std::string path = elementPath(childPath(definition.path, "outputs"), index);
        if (!value.isObject()) {
            return fail(path, "must be an object");
        }
        OutputSpec output;
        for (const auto& [field, member] : value.object) {
            (void)member;
            if (field != "name" && field != "type" && field != "value") {
                return fail(childPath(path, field), "unknown output field");
            }
        }
        const Value* name = value.find("name");
        if (name == nullptr || name->kind != Value::Kind::String) {
            return fail(childPath(path, "name"), "must be a string");
        }
        if (!isIdentifier(name->string)) {
            return fail(childPath(path, "name"), "must be a valid identifier");
        }
        if (std::any_of(definition.outputs.begin(), definition.outputs.end(),
                        [&](const OutputSpec& existing) { return existing.name == name->string; })) {
            return fail(childPath(path, "name"), "duplicate output '" + name->string + "'");
        }
        output.name = name->string;
        const Value* type = value.find("type");
        if (type == nullptr || type->kind != Value::Kind::String) {
            return fail(childPath(path, "type"), "must be a string");
        }
        if (!expressionTypeFromString(type->string, output.type)) {
            return fail(childPath(path, "type"), "unknown subgraph output type '" + type->string + "'");
        }
        if (!isExpressionPropertyType(output.type)) {
            return fail(childPath(path, "type"),
                        "texture/curve/gradient outputs are not supported in pure-function subgraphs");
        }
        const Value* outputValue = value.find("value");
        if (outputValue == nullptr) {
            return fail(childPath(path, "value"), "is required for pure-function outputs");
        }
        output.value = *outputValue;
        definition.outputs.push_back(std::move(output));
        return true;
    }

    struct Reference {
        std::string name;
        std::string path;
    };

    bool collectBlockReferences(const std::vector<Block>& blocks, const std::string& basePath,
                                std::vector<Reference>& references) {
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            const Block& block = blocks[i];
            const std::string path = elementPath(basePath, i);
            if (block.type == "subgraph") {
                const Value* name = block.parameter("name");
                if (name == nullptr || name->kind != Value::Kind::String) {
                    return fail(childPath(path, "name"), "must name a subgraph");
                }
                references.push_back(Reference{name->string, childPath(path, "name")});
                if (const Value* args = block.parameter("args"); args != nullptr &&
                    !collectValueReferences(*args, childPath(path, "args"), references)) {
                    return false;
                }
            } else if (block.parameters.kind == Value::Kind::Object) {
                for (const auto& [name, value] : block.parameters.object) {
                    if (!collectValueReferences(value, childPath(path, name), references)) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    bool collectValueReferences(const Value& value, const std::string& path,
                                std::vector<Reference>& references) {
        switch (value.kind) {
        case Value::Kind::Expression:
            if (value.expression == nullptr) {
                return true;
            }
            if (value.expression->op == "subgraph") {
                std::string name;
                for (const auto& [argumentName, argument] : value.expression->arguments) {
                    if (argumentName == "name") {
                        if (argument.kind != Value::Kind::String) {
                            return fail(childPath(path, "name"), "must name a subgraph");
                        }
                        name = argument.string;
                    }
                }
                if (name.empty()) {
                    return fail(childPath(path, "name"), "must name a subgraph");
                }
                references.push_back(Reference{name, childPath(path, "name")});
            }
            for (const auto& [argumentName, argument] : value.expression->arguments) {
                if (!collectValueReferences(argument, childPath(path, argumentName), references)) {
                    return false;
                }
            }
            return true;
        case Value::Kind::Array:
            for (std::size_t i = 0; i < value.array.size(); ++i) {
                if (!collectValueReferences(value.array[i], elementPath(path, i), references)) {
                    return false;
                }
            }
            return true;
        case Value::Kind::Object:
            for (const auto& [name, member] : value.object) {
                if (!collectValueReferences(member, childPath(path, name), references)) {
                    return false;
                }
            }
            return true;
        case Value::Kind::Invalid:
        case Value::Kind::Number:
        case Value::Kind::Bool:
        case Value::Kind::String:
            return true;
        }
        return true;
    }

    bool validateDependencies() {
        std::vector<std::vector<Reference>> references(m_definitions.size());
        for (std::size_t i = 0; i < m_definitions.size(); ++i) {
            const Definition& definition = m_definitions[i];
            if (!collectBlockReferences(definition.desc->initBlocks,
                                        childPath(definition.path, "init.blocks"),
                                        references[i]) ||
                !collectBlockReferences(definition.desc->updateBlocks,
                                        childPath(definition.path, "update.blocks"),
                                        references[i])) {
                return false;
            }
            for (std::size_t j = 0; j < definition.outputs.size(); ++j) {
                if (!collectValueReferences(definition.outputs[j].value,
                                            childPath(elementPath(childPath(definition.path, "outputs"), j),
                                                      "value"),
                                            references[i])) {
                    return false;
                }
            }
        }

        std::vector<std::size_t> state(m_definitions.size(), 0u);
        std::vector<std::size_t> depth(m_definitions.size(), 0u);
        std::vector<std::size_t> active;
        for (std::size_t i = 0; i < m_definitions.size(); ++i) {
            if (!visitDependency(i, references, state, depth, active)) {
                return false;
            }
        }
        return true;
    }

    bool visitDependency(std::size_t index, const std::vector<std::vector<Reference>>& references,
                         std::vector<std::size_t>& state, std::vector<std::size_t>& depth,
                         std::vector<std::size_t>& active) {
        if (state[index] == 2u) {
            return true;
        }
        if (state[index] == 1u) {
            std::string cycle;
            const auto start = std::find(active.begin(), active.end(), index);
            for (auto it = start; it != active.end(); ++it) {
                if (!cycle.empty()) {
                    cycle += " -> ";
                }
                cycle += m_definitions[*it].desc->name;
            }
            cycle += " -> " + m_definitions[index].desc->name;
            return fail(m_definitions[index].path, "subgraph dependency cycle: " + cycle);
        }
        state[index] = 1u;
        active.push_back(index);
        std::size_t maxDepth = 0u;
        for (const Reference& reference : references[index]) {
            const auto found = std::find_if(m_definitions.begin(), m_definitions.end(),
                                            [&](const Definition& definition) {
                                                return definition.desc->name == reference.name;
                                            });
            if (found == m_definitions.end()) {
                return fail(reference.path, "unknown subgraph '" + reference.name + "'");
            }
            const std::size_t child = static_cast<std::size_t>(found - m_definitions.begin());
            if (!visitDependency(child, references, state, depth, active)) {
                return false;
            }
            maxDepth = std::max(maxDepth, depth[child]);
        }
        active.pop_back();
        state[index] = 2u;
        depth[index] = maxDepth + 1u;
        if (depth[index] > kMaxSubgraphDepth) {
            return fail(m_definitions[index].path, "subgraph nesting depth exceeds 16");
        }
        return true;
    }

    bool enterSubgraph(const Definition& definition, const std::string& path,
                       std::vector<std::string>& stack) {
        if (std::find(stack.begin(), stack.end(), definition.desc->name) != stack.end()) {
            std::string cycle;
            const auto start = std::find(stack.begin(), stack.end(), definition.desc->name);
            for (auto it = start; it != stack.end(); ++it) {
                if (!cycle.empty()) {
                    cycle += " -> ";
                }
                cycle += *it;
            }
            cycle += " -> " + definition.desc->name;
            return fail(path, "subgraph dependency cycle: " + cycle);
        }
        if (stack.size() >= kMaxSubgraphDepth) {
            return fail(path, "subgraph nesting depth exceeds 16");
        }
        return true;
    }

    bool bindArguments(const Definition& definition, const Value* args, const std::string& path,
                       const ValueBindings& outer, std::vector<std::string>& stack,
                       ValueBindings& bindings) {
        if (args != nullptr && !args->isObject()) {
            return fail(path, "must be an object");
        }
        std::vector<bool> bound(definition.inputs.size(), false);
        if (args != nullptr) {
            for (const auto& [name, value] : args->object) {
                const auto found = std::find_if(definition.inputs.begin(), definition.inputs.end(),
                                                [&](const InputSpec& input) { return input.name == name; });
                if (found == definition.inputs.end()) {
                    return fail(childPath(path, name), "unknown subgraph argument '" + name + "'");
                }
                const std::size_t index = static_cast<std::size_t>(found - definition.inputs.begin());
                Value expanded;
                if (!expandValue(value, childPath(path, name), outer, stack, expanded)) {
                    return false;
                }
                bindings.emplace_back(name, std::move(expanded));
                bound[index] = true;
            }
        }
        for (std::size_t i = 0; i < definition.inputs.size(); ++i) {
            if (bound[i]) {
                continue;
            }
            const InputSpec& input = definition.inputs[i];
            if (!input.hasDefault) {
                return fail(path, "missing subgraph argument '" + input.name + "'");
            }
            Value expanded;
            const std::string defaultPath =
                childPath(elementPath(childPath(definition.path, "inputs"), i), "default");
            if (!expandValue(input.defaultValue, defaultPath, outer, stack, expanded)) {
                return false;
            }
            bindings.emplace_back(input.name, std::move(expanded));
        }
        return true;
    }

    bool expandBlocks(const std::vector<Block>& source, bool update, const std::string& basePath,
                      const ValueBindings& bindings, std::vector<std::string>& stack,
                      std::vector<Block>& output) {
        for (std::size_t i = 0; i < source.size(); ++i) {
            const Block& block = source[i];
            const std::string path = elementPath(basePath, i);
            if (block.type == "subgraph") {
                if (!expandBlockInvocation(block, update, path, bindings, stack, output)) {
                    return false;
                }
                continue;
            }
            Block expanded = block;
            if (block.parameters.kind == Value::Kind::Object) {
                expanded.parameters.object.clear();
                for (const auto& [name, value] : block.parameters.object) {
                    Value result;
                    if (!expandValue(value, childPath(path, name), bindings, stack, result)) {
                        return false;
                    }
                    expanded.parameters.object.emplace_back(name, std::move(result));
                }
            }
            output.push_back(std::move(expanded));
        }
        return true;
    }

    bool expandBlockInvocation(const Block& block, bool update, const std::string& path,
                               const ValueBindings& outer, std::vector<std::string>& stack,
                               std::vector<Block>& output) {
        const Value* name = block.parameter("name");
        if (name == nullptr || name->kind != Value::Kind::String) {
            return fail(childPath(path, "name"), "must name a subgraph");
        }
        if (block.parameter("output") != nullptr) {
            return fail(childPath(path, "output"), "output is only valid for expression subgraphs");
        }
        const Definition* definition = findDefinition(name->string);
        if (definition == nullptr) {
            return fail(childPath(path, "name"), "unknown subgraph '" + name->string + "'");
        }
        ValueBindings bindings;
        if (!bindArguments(*definition, block.parameter("args"), childPath(path, "args"), outer, stack,
                           bindings)) {
            return false;
        }
        if (!enterSubgraph(*definition, path, stack)) {
            return false;
        }
        stack.push_back(definition->desc->name);
        const std::vector<Block>& source =
            update ? definition->desc->updateBlocks : definition->desc->initBlocks;
        const std::string sourcePath =
            childPath(definition->path, update ? "update.blocks" : "init.blocks");
        const bool ok = expandBlocks(source, update, sourcePath, bindings, stack, output);
        stack.pop_back();
        return ok;
    }

    bool expandExpressionInvocation(const Expression& expression, const std::string& path,
                                    const ValueBindings& outer, std::vector<std::string>& stack,
                                    Value& output) {
        for (const auto& [name, value] : expression.arguments) {
            (void)value;
            if (name != "name" && name != "args" && name != "output") {
                return fail(childPath(path, name), "unknown subgraph expression argument");
            }
        }
        std::string name;
        const Value* args = nullptr;
        std::string outputName;
        for (const auto& [argumentName, argument] : expression.arguments) {
            if (argumentName == "name") {
                if (argument.kind != Value::Kind::String) {
                    return fail(childPath(path, "name"), "must name a subgraph");
                }
                name = argument.string;
            } else if (argumentName == "args") {
                args = &argument;
            } else if (argumentName == "output") {
                if (argument.kind != Value::Kind::String) {
                    return fail(childPath(path, "output"), "must name an output");
                }
                outputName = argument.string;
            }
        }
        if (name.empty()) {
            return fail(childPath(path, "name"), "must name a subgraph");
        }
        const Definition* definition = findDefinition(name);
        if (definition == nullptr) {
            return fail(childPath(path, "name"), "unknown subgraph '" + name + "'");
        }
        ValueBindings bindings;
        if (!bindArguments(*definition, args, childPath(path, "args"), outer, stack, bindings)) {
            return false;
        }
        const OutputSpec* selected = nullptr;
        if (!outputName.empty()) {
            const auto found = std::find_if(definition->outputs.begin(), definition->outputs.end(),
                                            [&](const OutputSpec& candidate) {
                                                return candidate.name == outputName;
                                            });
            if (found == definition->outputs.end()) {
                return fail(childPath(path, "output"), "unknown subgraph output '" + outputName + "'");
            }
            selected = &*found;
        } else if (definition->outputs.size() == 1u) {
            selected = &definition->outputs.front();
        } else if (definition->outputs.empty()) {
            return fail(path, "subgraph has no outputs");
        } else {
            return fail(path, "subgraph has multiple outputs; specify 'output'");
        }
        if (!enterSubgraph(*definition, path, stack)) {
            return false;
        }
        stack.push_back(definition->desc->name);
        const std::size_t outputIndex =
            static_cast<std::size_t>(selected - definition->outputs.data());
        const std::string valuePath =
            childPath(elementPath(childPath(definition->path, "outputs"), outputIndex), "value");
        const bool ok = expandValue(selected->value, valuePath, bindings, stack, output);
        stack.pop_back();
        return ok;
    }

    bool expandValue(const Value& value, const std::string& path, const ValueBindings& bindings,
                     std::vector<std::string>& stack, Value& output) {
        switch (value.kind) {
        case Value::Kind::Expression: {
            if (value.expression == nullptr) {
                output = value;
                return true;
            }
            const Expression& expression = *value.expression;
            if (expression.op == "input" || expression.op == "arg") {
                if (expression.arguments.size() != 1u ||
                    expression.arguments.front().first != "name") {
                    return fail(path, "input expects a single 'name' argument");
                }
                const Value& nameValue = expression.arguments.front().second;
                if (nameValue.kind != Value::Kind::String) {
                    return fail(childPath(path, "name"), "must be string");
                }
                const Value* bound = findBinding(bindings, nameValue.string);
                if (bound == nullptr) {
                    return fail(childPath(path, "name"),
                                "subgraph input '" + nameValue.string + "' is not bound");
                }
                output = *bound;
                return true;
            }
            if (expression.op == "subgraph") {
                return expandExpressionInvocation(expression, path, bindings, stack, output);
            }
            if (expression.op == "property") {
                const Value* nameValue = nullptr;
                for (const auto& [argumentName, argument] : expression.arguments) {
                    if (argumentName == "name") {
                        nameValue = &argument;
                    }
                }
                if (nameValue != nullptr && nameValue->kind == Value::Kind::String) {
                    if (const Value* bound = findBinding(bindings, nameValue->string);
                        bound != nullptr) {
                        output = *bound;
                        return true;
                    }
                }
            }
            Value expanded;
            expanded.kind = Value::Kind::Expression;
            auto result = std::make_shared<Expression>();
            result->op = expression.op;
            for (const auto& [argumentName, argument] : expression.arguments) {
                Value member;
                if (!expandValue(argument, childPath(path, argumentName), bindings, stack, member)) {
                    return false;
                }
                result->arguments.emplace_back(argumentName, std::move(member));
            }
            expanded.expression = std::move(result);
            output = std::move(expanded);
            return true;
        }
        case Value::Kind::Array: {
            Value expanded;
            expanded.kind = Value::Kind::Array;
            expanded.array.reserve(value.array.size());
            for (std::size_t i = 0; i < value.array.size(); ++i) {
                Value item;
                if (!expandValue(value.array[i], elementPath(path, i), bindings, stack, item)) {
                    return false;
                }
                expanded.array.push_back(std::move(item));
            }
            output = std::move(expanded);
            return true;
        }
        case Value::Kind::Object: {
            Value expanded;
            expanded.kind = Value::Kind::Object;
            expanded.object.reserve(value.object.size());
            for (const auto& [name, member] : value.object) {
                Value item;
                if (!expandValue(member, childPath(path, name), bindings, stack, item)) {
                    return false;
                }
                expanded.object.emplace_back(name, std::move(item));
            }
            output = std::move(expanded);
            return true;
        }
        case Value::Kind::Invalid:
        case Value::Kind::Number:
        case Value::Kind::Bool:
        case Value::Kind::String:
            output = value;
            return true;
        }
        return true;
    }

    Graph& m_graph;
    std::vector<ParseError>& m_errors;
    std::vector<Definition> m_definitions;
};

} // namespace

bool flattenSubgraphs(Graph& graph, std::vector<ParseError>& errors) {
    Flattener flattener(graph, errors);
    return flattener.run();
}

} // namespace vfx::detail
