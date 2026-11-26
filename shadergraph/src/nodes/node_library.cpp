#include "sg/node_library.h"

#include "nodes/custom_function.h"
#include "nodes/switch_cases.h"
#include "nodes/swizzle_mask.h"

#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sg {
namespace {

Value f(float value) { return Value::makeFloat(value); }
Value b(bool value) { return Value::makeBool(value); }
Value v2(float x, float y) { return Value::makeVector2({x, y}); }
Value v3(float x, float y, float z) { return Value::makeVector3({x, y, z}); }
Value v4(float x, float y, float z, float w) { return Value::makeVector4({x, y, z, w}); }

SlotSpec input(SlotId id, std::string name, ValueType type,
               std::optional<Value> value = std::nullopt, bool literal = true) {
    SlotSpec slot;
    slot.id = id;
    slot.name = std::move(name);
    slot.displayName = slot.name;
    slot.direction = SlotDirection::Input;
    slot.type = type;
    slot.defaultValue = std::move(value);
    slot.supportsLiteral = literal;
    return slot;
}

SlotSpec output(SlotId id, std::string name, ValueType type) {
    SlotSpec slot;
    slot.id = id;
    slot.name = std::move(name);
    slot.displayName = slot.name;
    slot.direction = SlotDirection::Output;
    slot.type = type;
    return slot;
}

SlotSpec dynamic(SlotId id, std::string name, ValueType type = ValueType::DynamicVector,
                 std::optional<Value> value = std::nullopt) {
    return input(id, std::move(name), type, std::move(value));
}

NodeDef node(std::string type, std::string title, std::string category,
             std::vector<SlotSpec> slots, std::string body) {
    NodeDef definition;
    definition.type = std::move(type);
    definition.title = std::move(title);
    definition.category = std::move(category);
    definition.slots = std::move(slots);
    definition.emitBody = std::move(body);
    return definition;
}

void add(NodeRegistry& registry, NodeDef definition) {
    Diagnostic diagnostic;
    if (!registry.add(std::move(definition), &diagnostic)) {
        throw std::runtime_error(diagnostic.message);
    }
}

void addMathBinary(NodeRegistry& registry, const char* type, const char* title, const char* op,
                   float defaultValue = 0.0f) {
    add(registry, node(type, title, "Math/Basic",
                       {dynamic(0, "A", ValueType::DynamicVector, f(defaultValue)),
                        dynamic(1, "B", ValueType::DynamicVector, f(defaultValue)),
                        output(2, "Out", ValueType::DynamicVector)},
                       std::string("$2 = $0 ") + op + " $1;"));
}

void addMathUnary(NodeRegistry& registry, const char* type, const char* title, const char* expression) {
    add(registry, node(type, title, "Math/Advanced",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        output(1, "Out", ValueType::DynamicVector)},
                       std::string("$1 = ") + expression + ";"));
}

void addSearchable(NodeRegistry& registry, NodeDef definition,
                   std::initializer_list<const char*> synonyms) {
    definition.synonyms.assign(synonyms.begin(), synonyms.end());
    add(registry, std::move(definition));
}

void addTargetSpecific(NodeRegistry& registry, NodeDef definition,
                       std::initializer_list<const char*> targets) {
    definition.allowedTargets.assign(targets.begin(), targets.end());
    definition.isTargetSpecific = true;
    add(registry, std::move(definition));
}

void addTrigUnary(NodeRegistry& registry, const char* type, const char* title,
                  const char* function, float defaultValue,
                  std::initializer_list<const char*> synonyms = {}) {
    addSearchable(
        registry,
        node(type, title, "Math/Trigonometry",
             {dynamic(0, "In", ValueType::DynamicVector, f(defaultValue)),
              output(1, "Out", ValueType::DynamicVector)},
             std::string("$1 = ") + function + "($0);"),
        synonyms);
}

void addTrigBinary(NodeRegistry& registry, const char* type, const char* title,
                   const char* function, std::initializer_list<const char*> synonyms = {}) {
    addSearchable(
        registry,
        node(type, title, "Math/Trigonometry",
             {dynamic(0, "A", ValueType::DynamicVector, f(0.0f)),
              dynamic(1, "B", ValueType::DynamicVector, f(0.0f)),
              output(2, "Out", ValueType::DynamicVector)},
             std::string("$2 = ") + function + "($0, $1);"),
        synonyms);
}

void addWave(NodeRegistry& registry, const char* type, const char* title, const char* body,
             std::initializer_list<const char*> synonyms = {}) {
    addSearchable(
        registry,
        node(type, title, "Math/Wave",
             {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
              output(1, "Out", ValueType::DynamicVector)},
             body),
        synonyms);
}

void addDerivative(NodeRegistry& registry, const char* type, const char* title, const char* body) {
    addSearchable(
        registry,
        node(type, title, "Math/Derivative",
             {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
              output(1, "Out", ValueType::DynamicVector)},
             body),
        {"derivative"});
}


bool updateVectorComponentDefaults(Graph&, NodeInstance& node) {
    const Value* value = node.setting("value");
    if (value == nullptr) {
        return false;
    }

    Vec4 components{0.0f, 0.0f, 0.0f, 0.0f};
    if (const Vec2* vector2 = value->asVector2()) {
        components = {vector2->x, vector2->y, 0.0f, 0.0f};
    } else if (const Vec3* vector3 = value->asVector3()) {
        components = {vector3->x, vector3->y, vector3->z, 0.0f};
    } else if (const Vec4* vector4 = value->asVector4()) {
        components = *vector4;
    } else if (const float* scalar = value->asFloat()) {
        components = {*scalar, 0.0f, 0.0f, 0.0f};
    } else {
        return false;
    }

    bool changed = false;
    for (SlotInstance& slot : node.slots) {
        if (slot.isOutput() || slot.spec.id < 1 || slot.spec.id > 4 ||
            slot.hasExplicitValue()) {
            continue;
        }
        float component = 0.0f;
        switch (slot.spec.id) {
        case 1:
            component = components.x;
            break;
        case 2:
            component = components.y;
            break;
        case 3:
            component = components.z;
            break;
        case 4:
            component = components.w;
            break;
        default:
            break;
        }
        Value next = f(component);
        if (!slot.state.value || !(*slot.state.value == next)) {
            slot.state.value = std::move(next);
            changed = true;
        }
    }
    return changed;
}

bool updateMatrixSplitSlots(Graph&, NodeInstance& node) {
    const SlotInstance* input = node.findSlot(0);
    const int dimension = input == nullptr ? 2 : matrixDimension(input->concreteType);
    const ConcreteValueType outputType =
        dimension == 2 ? ConcreteValueType::Vector2
                       : (dimension == 3 ? ConcreteValueType::Vector3
                                         : ConcreteValueType::Vector4);
    bool changed = false;
    for (SlotInstance& slot : node.slots) {
        if (slot.isOutput() && slot.isDynamicVector() && slot.concreteType != outputType) {
            slot.concreteType = outputType;
            slot.state.concreteType = outputType;
            changed = true;
        }
    }
    return changed;
}

bool updateDynamicSlots(Graph& graph, NodeInstance& node,
                        const std::vector<SlotSpec>& desired) {
    bool changed = node.slots.size() != desired.size();
    if (!changed) {
        for (std::size_t index = 0; index < desired.size(); ++index) {
            const SlotSpec& current = node.slots[index].spec;
            const SlotSpec& next = desired[index];
            if (current.id != next.id || current.name != next.name ||
                current.displayName != next.displayName || current.direction != next.direction ||
                current.type != next.type || current.hidden != next.hidden ||
                current.defaultValue != next.defaultValue) {
                changed = true;
                break;
            }
        }
    }
    if (!changed) {
        return false;
    }

    std::vector<SlotInstance> slots;
    slots.reserve(desired.size());
    for (const SlotSpec& spec : desired) {
        const SlotInstance* existing = node.findSlot(spec.id);
        SlotInstance slot;
        if (existing != nullptr && existing->spec.type == spec.type) {
            slot = *existing;
            slot.spec = spec;
        } else {
            slot = makeSlotInstance(spec);
        }
        slot.connected = spec.direction == SlotDirection::Input &&
                         graph.incomingEdge(node.id, spec.id) != nullptr;
        slot.hasError = false;
        slots.push_back(std::move(slot));
    }
    node.slots = std::move(slots);
    return true;
}

std::string keywordReference(NodeInstance& node) {
    for (const char* key : {"keyword", "keywordRef", "refName"}) {
        const Value* value = node.setting(key);
        if (value == nullptr) {
            continue;
        }
        if (const std::string* text = value->asString(); text != nullptr && !text->empty()) {
            const std::string reference = *text;
            if (std::string(key) != "keyword") {
                node.settings.erase("keywordRef");
                node.settings.erase("refName");
                node.settings.insert_or_assign(
                    "keyword", Value::makeString(ValueType::String, reference));
            }
            return reference;
        }
    }
    return {};
}

std::vector<SlotSpec> keywordSlots(const Keyword* keyword) {
    std::vector<SlotSpec> result;
    SlotSpec out = output(0, "Out", ValueType::DynamicVector);
    if (keyword != nullptr) {
        out.displayName = keyword->name;
    }
    result.push_back(std::move(out));
    if (keyword != nullptr && keyword->type == KeywordType::Enum) {
        for (const KeywordEntry& entry : keyword->entries) {
            if (entry.id <= 0) {
                continue;
            }
            const std::string name =
                entry.displayName.empty() ? entry.referenceName : entry.displayName;
            result.push_back(dynamic(entry.id, name, ValueType::DynamicVector, f(0.0f)));
        }
        return result;
    }
    result.push_back(dynamic(1, "On", ValueType::DynamicVector, f(0.0f)));
    result.push_back(dynamic(2, "Off", ValueType::DynamicVector, f(0.0f)));
    return result;
}

bool updateKeywordSlots(Graph& graph, NodeInstance& node) {
    const std::string ref = keywordReference(node);
    const Keyword* keyword = graph.findKeyword(ref);
    return updateDynamicSlots(graph, node, keywordSlots(keyword));
}

std::string dropdownReference(NodeInstance& node) {
    for (const char* key : {"dropdown", "dropdownRef", "refName"}) {
        const Value* value = node.setting(key);
        if (value == nullptr) {
            continue;
        }
        if (const std::string* text = value->asString(); text != nullptr && !text->empty()) {
            const std::string reference = *text;
            if (std::string(key) != "dropdown") {
                node.settings.erase("dropdownRef");
                node.settings.erase("refName");
                node.settings.insert_or_assign(
                    "dropdown", Value::makeString(ValueType::String, reference));
            }
            return reference;
        }
    }
    return {};
}

std::vector<SlotSpec> dropdownSlots(const Dropdown* dropdown) {
    std::vector<SlotSpec> result;
    SlotSpec out = output(0, "Out", ValueType::DynamicVector);
    if (dropdown != nullptr && !dropdown->name.empty()) {
        out.displayName = dropdown->name;
    }
    result.push_back(std::move(out));
    if (dropdown != nullptr) {
        for (const DropdownEntry& entry : dropdown->entries) {
            if (entry.id <= 0) {
                continue;
            }
            std::string name = entry.displayName;
            if (name.empty()) {
                name = "Entry " + std::to_string(entry.id);
            }
            result.push_back(dynamic(entry.id, std::move(name), ValueType::DynamicVector, f(0.0f)));
        }
    }
    return result;
}

bool updateDropdownSlots(Graph& graph, NodeInstance& node) {
    const std::string ref = dropdownReference(node);
    const Dropdown* dropdown = graph.findDropdown(ref);
    return updateDynamicSlots(graph, node, dropdownSlots(dropdown));
}

bool updateSwizzleSlots(Graph& graph, NodeInstance& node) {
    std::string encoded = "xyzw";
    if (const Value* value = node.setting("order")) {
        if (const std::string* text = value->asString(); text != nullptr && !text->empty()) {
            encoded = *text;
        }
    }
    SwizzleMask mask = parseSwizzleMask(encoded);
    if (!mask.valid) {
        mask = parseSwizzleMask("xyzw");
    }

    std::vector<SlotSpec> desired;
    desired.push_back(input(0, "In", swizzleVectorType(mask.inputCount)));
    desired.push_back(output(1, "Out", swizzleVectorType(mask.outputCount)));
    return updateDynamicSlots(graph, node, desired);
}

bool updateSwitchSlots(Graph& graph, NodeInstance& node) {
    EffectiveSwitchCases effective = effectiveSwitchCases(graph, node);
    if (effective.cases.size() > 26) {
        effective.cases.resize(26);
    }

    std::vector<SlotSpec> desired;
    desired.reserve(effective.cases.size() + 3);
    desired.push_back(input(0, "Predicate", ValueType::Float, f(0.0f)));
    desired.push_back(dynamic(1, "Fallback", ValueType::DynamicVector, f(0.0f)));
    desired.push_back(output(2, "Out", ValueType::DynamicVector));
    for (std::size_t index = 0; index < effective.cases.size(); ++index) {
        const std::string label =
            effective.fromProperty
                ? (effective.cases[index].name.empty()
                       ? "Case " + std::to_string(index + 1)
                       : effective.cases[index].name)
                : (index < 26 ? std::string(1, static_cast<char>('A' + index))
                              : "Case " + std::to_string(index + 1));
        desired.push_back(dynamic(static_cast<SlotId>(3 + index), label,
                                  ValueType::DynamicVector, f(0.0f)));
    }
    return updateDynamicSlots(graph, node, desired);
}

std::string customFunctionText(const NodeInstance& node, const char* name) {
    if (const Value* value = node.setting(name)) {
        if (const std::string* text = value->asString()) {
            return *text;
        }
    }
    return {};
}

bool updateCustomFunctionSlots(Graph& graph, NodeInstance& node) {
    constexpr std::size_t kMaxSlots = 16;
    std::vector<CustomFunctionSlotSpec> inputs =
        parseCustomFunctionSlots(customFunctionText(node, "inputSlots"));
    std::vector<CustomFunctionSlotSpec> outputs =
        parseCustomFunctionSlots(customFunctionText(node, "outputSlots"));
    if (inputs.size() > kMaxSlots) {
        inputs.resize(kMaxSlots);
    }
    if (outputs.size() > kMaxSlots) {
        outputs.resize(kMaxSlots);
    }

    std::vector<SlotSpec> desired;
    desired.reserve(inputs.size() + outputs.size());
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        desired.push_back(input(static_cast<SlotId>(index), inputs[index].name,
                                inputs[index].type));
    }
    for (std::size_t index = 0; index < outputs.size(); ++index) {
        desired.push_back(output(static_cast<SlotId>(100 + index), outputs[index].name,
                                 outputs[index].type));
    }
    return updateDynamicSlots(graph, node, desired);
}

void addBlock(NodeRegistry& registry, const char* type, const char* title, const char* field,
              ValueType valueType) {
    NodeDef definition = node(type, title, "Master",
                              {input(0, "Input", valueType, std::nullopt, false)}, {});
    definition.blockNode = true;
    definition.blockField = field;
    definition.allowInSubGraph = false;
    definition.hasPreview = false;
    add(registry, std::move(definition));
}

} // namespace

void registerBuiltinNodes(NodeRegistry& registry) {
    addBlock(registry, "BlockPosition", "Position", "positionOS", ValueType::Vector3);
    addBlock(registry, "BlockNormal", "Normal", "normal", ValueType::Vector3);
    addBlock(registry, "BlockTangent", "Tangent", "tangent", ValueType::Vector3);
    addBlock(registry, "BlockBaseColor", "Base Color", "baseColor", ValueType::Color);
    addBlock(registry, "BlockMetallic", "Metallic", "metallic", ValueType::Float);
    addBlock(registry, "BlockSmoothness", "Smoothness", "smoothness", ValueType::Float);
    addBlock(registry, "BlockNormalTangent", "Normal (Tangent)", "normalTangent", ValueType::Vector3);
    addBlock(registry, "BlockNormalObject", "Normal (Object)", "normalObject", ValueType::Vector3);
    addBlock(registry, "BlockNormalWorld", "Normal (World)", "normalWorld", ValueType::Vector3);
    addBlock(registry, "BlockOcclusion", "Occlusion", "occlusion", ValueType::Float);
    addBlock(registry, "BlockEmission", "Emission", "emission", ValueType::Color);
    addBlock(registry, "BlockAlpha", "Alpha", "alpha", ValueType::Float);
    addBlock(registry, "BlockAlphaClipThreshold", "Alpha Clip Threshold", "alphaClipThreshold",
             ValueType::Float);

    addBlock(registry, "BlockLitBaseColor", "Base Color", "baseColor", ValueType::Color);
    addBlock(registry, "BlockLitMetallic", "Metallic", "metallic", ValueType::Float);
    addBlock(registry, "BlockLitSmoothness", "Smoothness", "smoothness", ValueType::Float);
    addBlock(registry, "BlockLitNormalTangent", "Normal (Tangent)", "normalTangent", ValueType::Vector3);
    addBlock(registry, "BlockLitNormalObject", "Normal (Object)", "normalObject", ValueType::Vector3);
    addBlock(registry, "BlockLitNormalWorld", "Normal (World)", "normalWorld", ValueType::Vector3);
    addBlock(registry, "BlockLitOcclusion", "Occlusion", "occlusion", ValueType::Float);
    addBlock(registry, "BlockLitEmission", "Emission", "emission", ValueType::Color);
    addBlock(registry, "BlockLitAlpha", "Alpha", "alpha", ValueType::Float);
    addBlock(registry, "BlockLitAlphaClipThreshold", "Alpha Clip Threshold", "alphaClipThreshold",
             ValueType::Float);
    addBlock(registry, "BlockUnlitBaseColor", "Base Color", "baseColor", ValueType::Color);
    addBlock(registry, "BlockUnlitAlpha", "Alpha", "alpha", ValueType::Float);
    addBlock(registry, "BlockUnlitAlphaClipThreshold", "Alpha Clip Threshold", "alphaClipThreshold",
             ValueType::Float);

    {
        NodeDef definition = node("Property", "Property", "Input",
                                  {output(0, "Out", ValueType::DynamicVector)}, {});
        definition.settings = {"property"};
        definition.allowInSubGraph = true;
        add(registry, std::move(definition));
    }

    {
        NodeDef definition = node("Boolean", "Boolean", "Input/Basic",
                                  {output(0, "Out", ValueType::Boolean)}, "$0 = false;");
        definition.settings = {"value"};
        definition.synonyms = {"switch", "true", "false", "on", "off"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Color", "Color", "Input/Basic",
                                  {output(0, "Out", ValueType::Vector4)},
                                  "$0 = vec4(0.0, 0.0, 0.0, 0.0);");
        definition.settings = {"value"};
        definition.synonyms = {"rgba"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Float", "Float", "Input/Basic",
                                  {output(0, "Out", ValueType::Float)}, "$0 = 0.0;");
        definition.settings = {"value"};
        definition.synonyms = {"Vector 1", "1", "v1", "vec1", "scalar"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Integer", "Integer", "Input/Basic",
                                  {output(0, "Out", ValueType::Float)}, "$0 = 0.0;");
        definition.settings = {"value"};
        definition.synonyms = {"whole number"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Slider", "Slider", "Input/Basic",
                                  {output(0, "Out", ValueType::Float)}, "$0 = 0.0;");
        definition.settings = {"value"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Vector 2", "Vector 2", "Input/Basic",
                                  {input(1, "X", ValueType::Float, f(0.0f)),
                                   input(2, "Y", ValueType::Float, f(0.0f)),
                                   output(0, "Out", ValueType::Vector2)},
                                  "$0 = vec2($1, $2);");
        definition.settings = {"value"};
        definition.synonyms = {"2", "v2", "vec2", "float2"};
        definition.updateSlots = updateVectorComponentDefaults;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Vector 3", "Vector 3", "Input/Basic",
                                  {input(1, "X", ValueType::Float, f(0.0f)),
                                   input(2, "Y", ValueType::Float, f(0.0f)),
                                   input(3, "Z", ValueType::Float, f(0.0f)),
                                   output(0, "Out", ValueType::Vector3)},
                                  "$0 = vec3($1, $2, $3);");
        definition.settings = {"value"};
        definition.synonyms = {"3", "v3", "vec3", "float3"};
        definition.updateSlots = updateVectorComponentDefaults;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Vector 4", "Vector 4", "Input/Basic",
                                  {input(1, "X", ValueType::Float, f(0.0f)),
                                   input(2, "Y", ValueType::Float, f(0.0f)),
                                   input(3, "Z", ValueType::Float, f(0.0f)),
                                   input(4, "W", ValueType::Float, f(0.0f)),
                                   output(0, "Out", ValueType::Vector4)},
                                  "$0 = vec4($1, $2, $3, $4);");
        definition.settings = {"value"};
        definition.synonyms = {"4", "v4", "vec4", "float4"};
        definition.updateSlots = updateVectorComponentDefaults;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Constant", "Constant", "Input/Basic",
                                  {output(0, "Out", ValueType::Float)},
                                  "$0 = 3.1415926;");
        definition.settings = {"constant"};
        definition.synonyms = {"pi", "tau", "phi"};
        add(registry, std::move(definition));
    }
    add(registry, node("Time", "Time", "Input/Basic",
                       {output(0, "Time", ValueType::Float), output(1, "Sine Time", ValueType::Float),
                        output(2, "Cosine Time", ValueType::Float),
                        output(3, "Delta Time", ValueType::Float),
                        output(4, "Smooth Delta", ValueType::Float)},
                       "$0 = gi.time; $1 = sin(gi.time); $2 = cos(gi.time); $3 = gi.deltaTime; $4 = gi.deltaTime;"));

    {
        NodeDef definition = node("UV", "UV", "Input/Geometry",
                                  {output(0, "Out", ValueType::Vector2)}, "$0 = gi.uv0;");
        definition.settings = {"channel"};
        definition.synonyms = {"texcoords", "coords", "coordinates"};
        add(registry, std::move(definition));
    }
    addSearchable(
        registry,
        node("Tiling And Offset", "Tiling And Offset", "UV",
             {input(0, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
              input(1, "Tiling", ValueType::Vector2, v2(1.0f, 1.0f)),
              input(2, "Offset", ValueType::Vector2, v2(0.0f, 0.0f)),
              output(3, "Out", ValueType::Vector2)},
             ""),
        {"pan", "scale"});
    {
        NodeDef definition = node(
            "Rotate", "Rotate", "UV",
            {input(0, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
             input(1, "Center", ValueType::Vector2, v2(0.5f, 0.5f)),
             input(2, "Rotation", ValueType::Float, f(0.0f)),
             output(3, "Out", ValueType::Vector2)},
            "");
        definition.settings = {"unit"};
        add(registry, std::move(definition));
    }
    add(registry, node("Polar Coordinates", "Polar Coordinates", "UV",
                       {input(0, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
                        input(1, "Center", ValueType::Vector2, v2(0.5f, 0.5f)),
                        input(2, "Radial Scale", ValueType::Float, f(1.0f)),
                        input(3, "Length Scale", ValueType::Float, f(1.0f)),
                        output(4, "Out", ValueType::Vector2)},
                       ""));
    add(registry, node("Radial Shear", "Radial Shear", "UV",
                       {input(0, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
                        input(1, "Center", ValueType::Vector2, v2(0.5f, 0.5f)),
                        input(2, "Strength", ValueType::Vector2, v2(10.0f, 10.0f)),
                        input(3, "Offset", ValueType::Vector2, v2(0.0f, 0.0f)),
                        output(4, "Out", ValueType::Vector2)},
                       ""));
    add(registry, node("Spherize", "Spherize", "UV",
                       {input(0, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
                        input(1, "Center", ValueType::Vector2, v2(0.5f, 0.5f)),
                        input(2, "Strength", ValueType::Vector2, v2(10.0f, 10.0f)),
                        input(3, "Offset", ValueType::Vector2, v2(0.0f, 0.0f)),
                        output(4, "Out", ValueType::Vector2)},
                       ""));
    add(registry, node("Twirl", "Twirl", "UV",
                       {input(0, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
                        input(1, "Center", ValueType::Vector2, v2(0.5f, 0.5f)),
                        input(2, "Strength", ValueType::Float, f(10.0f)),
                        input(3, "Offset", ValueType::Vector2, v2(0.0f, 0.0f)),
                        output(4, "Out", ValueType::Vector2)},
                       ""));
    {
        NodeDef definition = node(
            "Flipbook", "Flipbook", "UV",
            {input(0, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
             input(1, "Width", ValueType::Float, f(1.0f)),
             input(2, "Height", ValueType::Float, f(1.0f)),
             input(3, "Tile", ValueType::Float, f(0.0f)),
             output(4, "Out", ValueType::Vector2)},
            "");
        definition.synonyms = {"atlas", "animation"};
        definition.settings = {"invertX", "invertY"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node(
            "Parallax Mapping", "Parallax Mapping", "UV",
            {output(0, "ParallaxUVs", ValueType::Vector2),
             input(1, "Heightmap", ValueType::Texture2D, std::nullopt, false),
             input(2, "HeightmapSampler", ValueType::SamplerState, std::nullopt, false),
             input(3, "Amplitude", ValueType::Float, f(1.0f)),
             input(4, "UVs", ValueType::Vector2, v2(0.0f, 0.0f))},
            "");
        definition.settings = {"texture", "channel"};
        definition.synonyms = {"offset mapping"};
        definition.hasPreview = false;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node(
            "Parallax Occlusion Mapping", "Parallax Occlusion Mapping", "UV",
            {output(0, "PixelDepthOffset", ValueType::Float),
             output(1, "ParallaxUVs", ValueType::Vector2),
             input(2, "Heightmap", ValueType::Texture2D, std::nullopt, false),
             input(3, "HeightmapSampler", ValueType::SamplerState, std::nullopt, false),
             input(4, "Amplitude", ValueType::Float, f(1.0f)),
             input(5, "Steps", ValueType::Float, f(5.0f)),
             input(6, "UVs", ValueType::Vector2, v2(0.0f, 0.0f)),
             input(7, "LOD", ValueType::Float, f(0.0f)),
             input(8, "LODThreshold", ValueType::Float, f(0.0f)),
             input(10, "Tiling", ValueType::Vector2, v2(1.0f, 1.0f)),
             input(11, "Offset", ValueType::Vector2, v2(0.0f, 0.0f)),
             input(12, "PrimitiveSize", ValueType::Vector2, v2(1.0f, 1.0f))},
            "");
        definition.settings = {"texture", "channel"};
        definition.synonyms = {"pom"};
        definition.hasPreview = false;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node(
            "Triplanar", "Triplanar", "UV",
            {output(0, "Out", ValueType::Vector4),
             input(1, "Texture", ValueType::Texture2D, std::nullopt, false),
             input(2, "Sampler", ValueType::SamplerState, std::nullopt, false),
             input(3, "Position", ValueType::Vector3, v3(0.0f, 0.0f, 0.0f)),
             input(4, "Normal", ValueType::Vector3, v3(0.0f, 0.0f, 1.0f)),
             input(5, "Tile", ValueType::Float, f(1.0f)),
             input(6, "Blend", ValueType::Float, f(1.0f))},
            "");
        definition.settings = {"texture", "textureType", "inputSpace", "normalOutputSpace"};
        definition.synonyms = {"project"};
        add(registry, std::move(definition));
    }
    add(registry, node("Checkerboard", "Checkerboard", "Procedural",
                       {input(0, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
                        input(1, "Color A", ValueType::Vector3, v3(0.2f, 0.2f, 0.2f)),
                        input(2, "Color B", ValueType::Vector3, v3(0.7f, 0.7f, 0.7f)),
                        input(3, "Frequency", ValueType::Vector2, v2(1.0f, 1.0f)),
                        output(4, "Out", ValueType::Vector3)},
                       ""));
    {
        NodeDef definition = node(
            "Simple Noise", "Simple Noise", "Procedural/Noise",
            {input(0, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
             input(1, "Scale", ValueType::Float, f(500.0f)),
             output(2, "Out", ValueType::Float)},
            "");
        definition.settings = {"hashType"};
        addSearchable(registry, std::move(definition), {"value noise"});
    }
    {
        NodeDef definition = node(
            "Gradient Noise", "Gradient Noise", "Procedural/Noise",
            {input(0, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
             input(1, "Scale", ValueType::Float, f(10.0f)),
             output(2, "Out", ValueType::Float)},
            "");
        definition.settings = {"hashType"};
        addSearchable(registry, std::move(definition), {"perlin noise"});
    }
    {
        NodeDef definition = node(
            "Voronoi", "Voronoi", "Procedural/Noise",
            {input(0, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
             input(1, "AngleOffset", ValueType::Float, f(2.0f)),
             input(2, "CellDensity", ValueType::Float, f(5.0f)),
             output(3, "Out", ValueType::Float),
             output(4, "Cells", ValueType::Float)},
            "");
        definition.settings = {"hashType"};
        addSearchable(registry, std::move(definition), {"worley noise"});
    }
    addSearchable(
        registry,
        node("Ellipse", "Ellipse", "Procedural/Shape",
             {input(0, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
              input(2, "Width", ValueType::Float, f(0.5f)),
              input(3, "Height", ValueType::Float, f(0.5f)),
              output(4, "Out", ValueType::Float)},
             ""),
        {"circle"});
    addSearchable(
        registry,
        node("Polygon", "Polygon", "Procedural/Shape",
             {input(0, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
              input(1, "Sides", ValueType::Float, f(6.0f)),
              input(2, "Width", ValueType::Float, f(0.5f)),
              input(3, "Height", ValueType::Float, f(0.5f)),
              output(4, "Out", ValueType::Float)},
             ""),
        {"shape"});
    {
        NodeDef definition = node(
            "Rectangle", "Rectangle", "Procedural/Shape",
            {input(0, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
             input(1, "Width", ValueType::Float, f(0.5f)),
             input(2, "Height", ValueType::Float, f(0.5f)),
             output(3, "Out", ValueType::Float)},
            "");
        definition.synonyms = {"square"};
        definition.settings = {"clampType"};
        add(registry, std::move(definition));
    }
    addSearchable(
        registry,
        node("Rounded Polygon", "Rounded Polygon", "Procedural/Shape",
             {input(0, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
              input(1, "Width", ValueType::Float, f(0.5f)),
              input(2, "Height", ValueType::Float, f(0.5f)),
              input(3, "Sides", ValueType::Float, f(5.0f)),
              input(4, "Roundness", ValueType::Float, f(0.3f)),
              output(5, "Out", ValueType::Float)},
             ""),
        {"shape"});
    addSearchable(
        registry,
        node("Rounded Rectangle", "Rounded Rectangle", "Procedural/Shape",
             {input(0, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
              input(1, "Width", ValueType::Float, f(0.5f)),
              input(2, "Height", ValueType::Float, f(0.5f)),
              input(3, "Radius", ValueType::Float, f(0.1f)),
              output(4, "Out", ValueType::Float)},
             ""),
        {"square"});
    add(registry, node("Contrast", "Contrast", "Artistic/Adjustment",
                       {input(0, "In", ValueType::Vector3, v3(0.0f, 0.0f, 0.0f)),
                        input(1, "Contrast", ValueType::Float, f(1.0f)),
                        output(2, "Out", ValueType::Vector3)},
                       ""));
    add(registry, node("Saturation", "Saturation", "Artistic/Adjustment",
                       {input(0, "In", ValueType::Vector3, v3(0.0f, 0.0f, 0.0f)),
                        input(1, "Saturation", ValueType::Float, f(1.0f)),
                        output(2, "Out", ValueType::Vector3)},
                       ""));
    {
        NodeDef definition = node(
            "Hue", "Hue", "Artistic/Adjustment",
            {input(0, "In", ValueType::Vector3, v3(0.0f, 0.0f, 0.0f)),
             input(1, "Offset", ValueType::Float, f(0.0f)),
             output(2, "Out", ValueType::Vector3)},
            "");
        definition.settings = {"range"};
        add(registry, std::move(definition));
    }
    add(registry, node("White Balance", "White Balance", "Artistic/Adjustment",
                       {input(0, "In", ValueType::Vector3, v3(0.0f, 0.0f, 0.0f)),
                        input(1, "Temperature", ValueType::Float, f(0.0f)),
                        input(2, "Tint", ValueType::Float, f(0.0f)),
                        output(3, "Out", ValueType::Vector3)},
                       ""));
    add(registry, node("Replace Color", "Replace Color", "Artistic/Adjustment",
                       {input(0, "In", ValueType::Vector3, v3(0.0f, 0.0f, 0.0f)),
                        input(1, "From", ValueType::Vector3, v3(0.0f, 0.0f, 0.0f)),
                        input(2, "To", ValueType::Vector3, v3(0.0f, 0.0f, 0.0f)),
                        input(3, "Range", ValueType::Float, f(0.0f)),
                        output(4, "Out", ValueType::Vector3),
                        input(5, "Fuzziness", ValueType::Float, f(0.0f))},
                       ""));
    {
        NodeDef definition = node(
            "Invert Colors", "Invert Colors", "Artistic/Adjustment",
            {dynamic(0, "In", ValueType::DynamicVector, v4(0.0f, 0.0f, 0.0f, 0.0f)),
             output(1, "Out", ValueType::DynamicVector)},
            "");
        definition.settings = {"redChannel", "greenChannel", "blueChannel", "alphaChannel"};
        definition.synonyms = {"negative"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node(
            "Channel Mixer", "Channel Mixer", "Artistic/Adjustment",
            {input(0, "In", ValueType::Vector3, v3(0.0f, 0.0f, 0.0f)),
             output(1, "Out", ValueType::Vector3)},
            "");
        definition.settings = {"outRed", "outGreen", "outBlue"};
        definition.synonyms = {"matrix"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node(
            "Blend", "Blend", "Artistic/Blend",
            {dynamic(0, "Base", ValueType::DynamicVector, v3(0.0f, 0.0f, 0.0f)),
             dynamic(1, "Blend", ValueType::DynamicVector, v3(0.0f, 0.0f, 0.0f)),
             output(2, "Out", ValueType::DynamicVector),
             input(3, "Opacity", ValueType::Float, f(1.0f))},
            "");
        definition.settings = {"mode"};
        definition.synonyms = {"burn", "darken", "difference", "dodge", "divide", "exclusion",
                               "hard light", "hard mix", "lighten", "linear burn", "linear dodge",
                               "linear light", "multiply", "negate", "overlay", "pin light",
                               "screen", "soft light", "subtract", "vivid light", "overwrite"};
        add(registry, std::move(definition));
    }
    addSearchable(
        registry,
        node("Dither", "Dither", "Artistic/Filter",
             {dynamic(0, "In", ValueType::DynamicVector, v3(0.0f, 0.0f, 0.0f)),
              input(1, "Screen Position", ValueType::Vector2, v2(0.0f, 0.0f)),
              output(2, "Out", ValueType::DynamicVector)},
             ""),
        {"blue noise", "half tone"});
    {
        NodeDef definition = node(
            "Fade Transition", "Fade Transition", "Artistic/Filter",
            {output(0, "Fade", ValueType::Float),
             input(1, "NoiseValue", ValueType::Float, f(0.5f)),
             input(2, "FadeValue", ValueType::Float, f(0.5f)),
             input(3, "FadeContrast", ValueType::Float, f(1.0f))},
            "");
        definition.synonyms = {"fade"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node(
            "Channel Mask", "Channel Mask", "Artistic/Mask",
            {dynamic(0, "In", ValueType::DynamicVector, v3(0.0f, 0.0f, 0.0f)),
             output(1, "Out", ValueType::DynamicVector)},
            "");
        definition.settings = {"channelMask"};
        definition.synonyms = {"component mask"};
        add(registry, std::move(definition));
    }
    add(registry, node("Color Mask", "Color Mask", "Artistic/Mask",
                       {input(0, "In", ValueType::Vector3, v3(0.0f, 0.0f, 0.0f)),
                        input(1, "Mask Color", ValueType::Vector3, v3(0.0f, 0.0f, 0.0f)),
                        input(2, "Range", ValueType::Float, f(0.0f)),
                        output(3, "Out", ValueType::Float),
                        input(4, "Fuzziness", ValueType::Float, f(0.0f))},
                       ""));
    {
        NodeDef definition = node(
            "Colorspace Conversion", "Colorspace Conversion", "Artistic/Utility",
            {input(0, "In", ValueType::Vector3, v3(0.0f, 0.0f, 0.0f)),
             output(1, "Out", ValueType::Vector3)},
            "");
        definition.settings = {"from", "to"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node(
            "Normal Blend", "Normal Blend", "Artistic/Normal",
            {input(0, "A", ValueType::Vector3, v3(0.0f, 0.0f, 1.0f)),
             input(1, "B", ValueType::Vector3, v3(0.0f, 0.0f, 1.0f)),
             output(2, "Out", ValueType::Vector3)},
            "");
        definition.settings = {"mode"};
        addSearchable(registry, std::move(definition), {"blend normals"});
    }
    {
        NodeDef definition = node(
            "Normal Strength", "Normal Strength", "Artistic/Normal",
            {input(0, "In", ValueType::Vector3, v3(0.0f, 0.0f, 1.0f)),
             input(1, "Strength", ValueType::Float, f(1.0f)),
             output(2, "Out", ValueType::Vector3)},
            "");
        definition.synonyms = {"intensity"};
        add(registry, std::move(definition));
    }
    addSearchable(
        registry,
        node("Normal Reconstruct Z", "Normal Reconstruct Z", "Artistic/Normal",
             {input(0, "In", ValueType::Vector2, v2(0.0f, 0.0f)),
              output(2, "Out", ValueType::Vector3)},
             ""),
        {"derive z"});
    {
        NodeDef definition = node(
            "Normal From Height", "Normal From Height", "Artistic/Normal",
            {input(0, "In", ValueType::Float, f(0.0f)),
             input(2, "Strength", ValueType::Float, f(0.01f)),
             output(1, "Out", ValueType::Vector3)},
            "");
        definition.settings = {"outputSpace"};
        definition.synonyms = {"convert to normal", "bump map"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node(
            "Normal From Texture", "Normal From Texture", "Artistic/Normal",
            {input(0, "Texture", ValueType::Texture2D, std::nullopt, false),
             input(1, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
             input(2, "Sampler", ValueType::SamplerState, std::nullopt, false),
             input(3, "Offset", ValueType::Float, f(0.5f)),
             input(4, "Strength", ValueType::Float, f(8.0f)),
             output(5, "Out", ValueType::Vector3)},
            "");
        definition.settings = {"texture"};
        definition.synonyms = {"convert to normal", "bump map"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node(
            "Normal Unpack", "Normal Unpack", "Artistic/Normal",
            {input(0, "In", ValueType::Vector4, v4(0.0f, 0.0f, 0.0f, 0.0f)),
             output(1, "Out", ValueType::Vector3)},
            "");
        definition.settings = {"space"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Position", "Position", "Input/Geometry",
                                  {output(0, "Out", ValueType::Vector3)}, "$0 = gi.positionWS;");
        definition.settings = {"space"};
        definition.synonyms = {"location"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Normal Vector", "Normal Vector", "Input/Geometry",
                                  {output(0, "Out", ValueType::Vector3)},
                                  "$0 = normalize(gi.normalWS);");
        definition.settings = {"space"};
        definition.synonyms = {"surface direction"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Tangent Vector", "Tangent Vector", "Input/Geometry",
                                  {output(0, "Out", ValueType::Vector3)},
                                  "$0 = normalize(gi.tangentWS);");
        definition.settings = {"space"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Bitangent Vector", "Bitangent Vector", "Input/Geometry",
                                  {output(0, "Out", ValueType::Vector3)},
                                  "$0 = normalize(gi.bitangentWS);");
        definition.settings = {"space"};
        definition.synonyms = {"binormal"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("View Direction", "View Direction", "Input/Geometry",
                                  {output(0, "Out", ValueType::Vector3)},
                                  "$0 = normalize(gi.viewDirWS);");
        definition.settings = {"space"};
        definition.synonyms = {"eye direction"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("View Vector", "View Vector", "Input/Geometry",
                                  {output(0, "Out", ValueType::Vector3)}, "$0 = gi.viewDirWS;");
        definition.settings = {"space"};
        definition.synonyms = {"eye vector"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Screen Position", "Screen Position", "Input/Geometry",
                                  {output(0, "Out", ValueType::Vector4)},
                                  "$0 = vec4(gi.screenPos.xy, 0.0, 0.0);");
        definition.settings = {"mode"};
        add(registry, std::move(definition));
    }
    add(registry, node("Vertex Color", "Vertex Color", "Input/Geometry",
                       {output(0, "Out", ValueType::Vector4)}, "$0 = gi.vertexColor;"));
    add(registry, node("Vertex ID", "Vertex ID", "Input/Geometry",
                       {output(0, "Out", ValueType::Float)}, "$0 = gi.vertexID;"));
    add(registry, node("Instance ID", "Instance ID", "Input/Geometry",
                       {output(0, "Out", ValueType::Float)}, "$0 = gi.instanceID;"));

    {
        NodeDef definition = node("Camera", "Camera", "Input/Scene",
                                  {output(0, "Position", ValueType::Vector3),
                                   output(1, "Direction", ValueType::Vector3),
                                   output(2, "Orthographic", ValueType::Float),
                                   output(3, "Near Plane", ValueType::Float),
                                   output(4, "Far Plane", ValueType::Float),
                                   output(5, "Z Buffer Sign", ValueType::Float),
                                   output(6, "Width", ValueType::Float),
                                   output(7, "Height", ValueType::Float)},
                                  "");
        definition.synonyms = {"position", "direction", "orthographic", "near plane",
                               "far plane", "width", "height"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Eye Index", "Eye Index", "Input/Scene",
                                  {output(0, "Out", ValueType::Float)}, "");
        definition.synonyms = {"stereo", "3d"};
        definition.hasPreview = false;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Object", "Object", "Input/Scene",
                                  {output(0, "Position", ValueType::Vector3),
                                   output(1, "Scale", ValueType::Vector3),
                                   output(2, "World Bounds Min", ValueType::Vector3),
                                   output(3, "World Bounds Max", ValueType::Vector3),
                                   output(4, "Bounds Size", ValueType::Vector3)},
                                  "");
        definition.synonyms = {"position", "scale", "bounds", "size"};
        add(registry, std::move(definition));
    }
    add(registry, node("Screen", "Screen", "Input/Scene",
                       {output(0, "Width", ValueType::Float),
                        output(1, "Height", ValueType::Float)},
                       ""));
    {
        NodeDef definition = node("Scene Color", "Scene Color", "Input/Scene",
                                  {input(0, "UV", ValueType::Vector4, v4(0.0f, 0.0f, 0.0f, 0.0f)),
                                   output(1, "Out", ValueType::Vector3)},
                                  "");
        definition.synonyms = {"screen buffer"};
        definition.hasPreview = false;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Scene Depth", "Scene Depth", "Input/Scene",
                                  {input(0, "UV", ValueType::Vector4, v4(0.0f, 0.0f, 0.0f, 0.0f)),
                                   output(1, "Out", ValueType::Float)},
                                  "");
        definition.settings = {"mode"};
        definition.synonyms = {"zbuffer", "zdepth"};
        definition.hasPreview = false;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Scene Depth Difference", "Scene Depth Difference",
                                  "Input/Scene", {output(0, "Out", ValueType::Float)}, "");
        definition.settings = {"mode"};
        definition.synonyms = {"zbuffer", "zdepth", "difference"};
        definition.hasPreview = false;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Fog", "Fog", "Input/Scene",
                                  {output(0, "Color", ValueType::Vector4),
                                   output(1, "Density", ValueType::Float),
                                   input(2, "Position", ValueType::Vector3,
                                         v3(0.0f, 0.0f, 0.0f))},
                                  "");
        definition.hasPreview = false;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Ambient", "Ambient", "Input/Lighting",
                                  {output(0, "Color/Sky", ValueType::Vector3),
                                   output(1, "Equator", ValueType::Vector3),
                                   output(2, "Ground", ValueType::Vector3)},
                                  "");
        definition.synonyms = {"scene color"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Baked GI", "Baked GI", "Input/Lighting",
                                  {input(0, "NormalWS", ValueType::Vector3,
                                         v3(0.0f, 0.0f, 1.0f)),
                                   input(2, "PositionWS", ValueType::Vector3,
                                         v3(0.0f, 0.0f, 0.0f)),
                                   input(3, "StaticUV", ValueType::Vector2,
                                         v2(0.0f, 0.0f)),
                                   input(4, "DynamicUV", ValueType::Vector2,
                                         v2(0.0f, 0.0f)),
                                   output(1, "Out", ValueType::Vector3)},
                                  "");
        definition.settings = {"applyScaling"};
        definition.synonyms = {"global illumination"};
        definition.hasPreview = false;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Main Light Direction", "Main Light Direction",
                                  "Input/Lighting",
                                  {output(0, "Direction", ValueType::Vector3)}, "");
        definition.synonyms = {"sun"};
        definition.hasPreview = false;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Reflection Probe", "Reflection Probe",
                                  "Input/Lighting",
                                  {input(0, "ViewDir", ValueType::Vector3,
                                         v3(0.0f, 0.0f, 0.0f)),
                                   input(1, "Normal", ValueType::Vector3,
                                         v3(0.0f, 0.0f, 1.0f)),
                                   input(2, "LOD", ValueType::Float, f(0.0f)),
                                   output(3, "Out", ValueType::Vector3)},
                                  "");
        definition.synonyms = {"light probe", "cube map", "environment"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Gradient", "Gradient", "Input/Gradient",
                                  {output(0, "Out", ValueType::Gradient)}, "");
        definition.settings = {"gradient", "mode"};
        definition.synonyms = {"ramp"};
        definition.hasPreview = false;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Sample Gradient", "Sample Gradient", "Input/Gradient",
                                  {input(0, "Gradient", ValueType::Gradient, std::nullopt, false),
                                   input(1, "Time", ValueType::Float, f(0.0f)),
                                   output(2, "Out", ValueType::Vector4)},
                                  "");
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Blackbody", "Blackbody", "Input/Gradient",
                                  {input(0, "Temperature", ValueType::Float, f(512.0f)),
                                   output(1, "Out", ValueType::Vector3)},
                                  "");
        add(registry, std::move(definition));
    }

    {
        NodeDef definition = node("Sample Texture 2D", "Sample Texture 2D", "Input/Texture",
                                  {output(0, "RGBA", ValueType::Vector4),
                                   input(1, "Texture", ValueType::Texture2D, std::nullopt, false),
                                   input(2, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
                                   input(3, "Sampler", ValueType::SamplerState, std::nullopt, false),
                                   output(4, "R", ValueType::Float),
                                   output(5, "G", ValueType::Float),
                                   output(6, "B", ValueType::Float),
                                   output(7, "A", ValueType::Float)},
                                  "");
        definition.settings = {"texture", "textureType", "normalMapSpace",
                               "mipSamplingMode", "enableGlobalMipBias"};
        definition.synonyms = {"tex2d"};
        for (SlotSpec slot : {input(8, "Bias", ValueType::Float, f(0.0f)),
                              input(9, "LOD", ValueType::Float, f(0.0f)),
                              input(10, "DDX", ValueType::Vector2, v2(0.0f, 0.0f)),
                              input(11, "DDY", ValueType::Vector2, v2(0.0f, 0.0f))}) {
            slot.hidden = true;
            definition.slots.push_back(std::move(slot));
        }
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Sample Texture 2D LOD", "Sample Texture 2D LOD",
                                  "Input/Texture",
                                  {output(0, "RGBA", ValueType::Vector4),
                                   input(1, "Texture", ValueType::Texture2D, std::nullopt, false),
                                   input(2, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
                                   input(3, "Sampler", ValueType::SamplerState, std::nullopt, false),
                                   input(4, "LOD", ValueType::Float, f(0.0f)),
                                   output(5, "R", ValueType::Float),
                                   output(6, "G", ValueType::Float),
                                   output(7, "B", ValueType::Float),
                                   output(8, "A", ValueType::Float)},
                                  "");
        definition.settings = {"texture", "textureType", "normalMapSpace"};
        definition.synonyms = {"tex2dlod"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Sample Texture 2D Array", "Sample Texture 2D Array",
                                  "Input/Texture",
                                  {output(0, "RGBA", ValueType::Vector4),
                                   input(1, "Texture Array", ValueType::Texture2DArray,
                                         std::nullopt, false),
                                   input(2, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
                                   input(3, "Sampler", ValueType::SamplerState, std::nullopt,
                                         false),
                                   output(4, "R", ValueType::Float),
                                   output(5, "G", ValueType::Float),
                                   output(6, "B", ValueType::Float),
                                   output(7, "A", ValueType::Float),
                                   input(8, "Index", ValueType::Float, f(0.0f))},
                                  "");
        definition.settings = {"texture", "mipSamplingMode", "enableGlobalMipBias"};
        definition.synonyms = {"stack", "pile", "tex2darray"};
        for (SlotSpec slot : {input(9, "Bias", ValueType::Float, f(0.0f)),
                              input(10, "LOD", ValueType::Float, f(0.0f)),
                              input(11, "DDX", ValueType::Vector2, v2(0.0f, 0.0f)),
                              input(12, "DDY", ValueType::Vector2, v2(0.0f, 0.0f))}) {
            slot.hidden = true;
            definition.slots.push_back(std::move(slot));
        }
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Sample Texture 3D", "Sample Texture 3D", "Input/Texture",
                                  {output(0, "RGBA", ValueType::Vector4),
                                   input(1, "Texture", ValueType::Texture3D, std::nullopt,
                                         false),
                                   input(2, "UV", ValueType::Vector3, v3(0.0f, 0.0f, 0.0f)),
                                   input(3, "Sampler", ValueType::SamplerState, std::nullopt,
                                         false),
                                   output(5, "R", ValueType::Float),
                                   output(6, "G", ValueType::Float),
                                   output(7, "B", ValueType::Float),
                                   output(8, "A", ValueType::Float)},
                                  "");
        definition.settings = {"texture", "mipSamplingMode"};
        definition.synonyms = {"volume", "tex3d"};
        {
            SlotSpec lod = input(4, "LOD", ValueType::Float, f(0.0f));
            lod.hidden = true;
            definition.slots.push_back(std::move(lod));
        }
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Sample Cubemap", "Sample Cubemap", "Input/Texture",
                                  {output(0, "Out", ValueType::Vector4),
                                   input(1, "Cube", ValueType::Cubemap, std::nullopt, false),
                                   input(2, "Dir", ValueType::Vector3, v3(0.0f, 0.0f, 1.0f)),
                                   input(3, "Sampler", ValueType::SamplerState, std::nullopt,
                                         false),
                                   input(4, "LOD", ValueType::Float, f(0.0f))},
                                  "");
        definition.settings = {"texture"};
        definition.synonyms = {"texcube"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition =
            node("Sample Reflected Cubemap", "Sample Reflected Cubemap", "Input/Texture",
                 {output(0, "Out", ValueType::Vector4),
                  input(1, "Cube", ValueType::Cubemap, std::nullopt, false),
                  input(2, "ViewDir", ValueType::Vector3, v3(0.0f, 0.0f, 1.0f)),
                  input(3, "Normal", ValueType::Vector3, v3(0.0f, 0.0f, 1.0f)),
                  input(4, "LOD", ValueType::Float, f(0.0f)),
                  input(5, "Sampler", ValueType::SamplerState, std::nullopt, false)},
                 "");
        definition.settings = {"texture"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Texture 2D Asset", "Texture 2D Asset", "Input/Texture",
                                  {output(0, "Out", ValueType::Texture2D)}, "");
        definition.settings = {"texture"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Texture 2D Array Asset", "Texture 2D Array Asset",
                                  "Input/Texture",
                                  {output(0, "Out", ValueType::Texture2DArray)}, "");
        definition.settings = {"texture"};
        definition.synonyms = {"stack", "pile"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Texture 3D Asset", "Texture 3D Asset", "Input/Texture",
                                  {output(0, "Out", ValueType::Texture3D)}, "");
        definition.settings = {"texture"};
        definition.synonyms = {"volume"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Cubemap Asset", "Cubemap Asset", "Input/Texture",
                                  {output(0, "Out", ValueType::Cubemap)}, "");
        definition.settings = {"texture"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Sampler State", "Sampler State", "Input/Texture",
                                  {output(0, "Out", ValueType::SamplerState)}, "");
        definition.settings = {"filter", "wrap", "anisotropic"};
        definition.hasPreview = false;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Texture Size", "Texture Size", "Input/Texture",
                                  {output(0, "Width", ValueType::Float),
                                   input(1, "Texture", ValueType::Texture2D, std::nullopt,
                                         false),
                                   output(2, "Height", ValueType::Float),
                                   output(3, "Texel Width", ValueType::Float),
                                   output(4, "Texel Height", ValueType::Float)},
                                  "");
        definition.settings = {"texture"};
        definition.synonyms = {"texel size"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Gather Texture 2D", "Gather Texture 2D", "Input/Texture",
                                  {output(0, "RGBA", ValueType::Vector4),
                                   input(1, "Texture", ValueType::Texture2D, std::nullopt,
                                         false),
                                   input(2, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
                                   input(3, "Sampler", ValueType::SamplerState, std::nullopt,
                                         false),
                                   input(4, "Offset", ValueType::Vector2, v2(0.0f, 0.0f)),
                                   output(5, "R", ValueType::Float),
                                   output(6, "G", ValueType::Float),
                                   output(7, "B", ValueType::Float),
                                   output(8, "A", ValueType::Float)},
                                  "");
        definition.settings = {"texture"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Calculate Level Of Detail Texture 2D",
                                  "Calculate Level Of Detail Texture 2D", "Input/Texture",
                                  {output(0, "LOD", ValueType::Float),
                                   input(1, "Texture", ValueType::Texture2D, std::nullopt,
                                         false),
                                   input(2, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
                                   input(3, "Sampler", ValueType::SamplerState, std::nullopt,
                                         false)},
                                  "");
        definition.settings = {"texture", "clamp"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition =
            node("Split Texture Transform", "Split Texture Transform", "Input/Texture",
                 {input(0, "Texture", ValueType::Texture2D, std::nullopt, false),
                  output(1, "Tiling", ValueType::Vector2),
                  output(2, "Offset", ValueType::Vector2),
                  output(3, "Texture Only", ValueType::Texture2D)},
                 "");
        definition.settings = {"texture"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Procedural Virtual Texture", "Procedural Virtual Texture",
                                  "Input/Texture",
                                  {output(0, "Out", ValueType::VirtualTexture)}, "");
        definition.settings = {"vtName", "layers", "shaderDeclaration"};
        definition.hasPreview = false;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition =
            node("Sample Virtual Texture", "Sample Virtual Texture", "Input/Texture",
                 {input(0, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
                  input(1, "VT", ValueType::VirtualTexture, std::nullopt, false),
                  input(2, "Lod", ValueType::Float, f(0.0f)),
                  input(3, "Bias", ValueType::Float, f(0.0f)),
                  input(4, "Dx", ValueType::Vector2, v2(1.0f, 1.0f)),
                  input(5, "Dy", ValueType::Vector2, v2(1.0f, 1.0f)),
                  output(11, "Out", ValueType::Vector4),
                  output(12, "Out2", ValueType::Vector4),
                  output(13, "Out3", ValueType::Vector4),
                  output(14, "Out4", ValueType::Vector4)},
                 "");
        definition.settings = {"addressMode", "lodCalculation", "sampleQuality",
                               "enableGlobalMipBias", "noFeedback"};
        definition.synonyms = {"buffer"};
        definition.hasPreview = false;
        add(registry, std::move(definition));
    }

    {
        NodeDef definition = node("Matrix 2x2", "Matrix 2x2", "Input/Matrix",
                                  {output(0, "Out", ValueType::Matrix2)}, "");
        definition.settings = {"row0", "row1"};
        definition.hasPreview = false;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Matrix 3x3", "Matrix 3x3", "Input/Matrix",
                                  {output(0, "Out", ValueType::Matrix3)}, "");
        definition.settings = {"row0", "row1", "row2"};
        definition.hasPreview = false;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Matrix 4x4", "Matrix 4x4", "Input/Matrix",
                                  {output(0, "Out", ValueType::Matrix4)}, "");
        definition.settings = {"row0", "row1", "row2", "row3"};
        definition.hasPreview = false;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition =
            node("Transformation Matrix", "Transformation Matrix", "Input/Matrix",
                 {output(0, "Out", ValueType::Matrix4)}, "");
        definition.settings = {"matrixType"};
        definition.hasPreview = false;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition =
            node("Dielectric Specular", "Dielectric Specular", "Input/PBR",
                 {output(0, "Out", ValueType::Float)}, "");
        definition.settings = {"material", "range", "indexOfRefraction"};
        definition.synonyms = {"reflectance"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition =
            node("Metal Reflectance", "Metal Reflectance", "Input/PBR",
                 {output(0, "Out", ValueType::Vector3)}, "");
        definition.settings = {"material"};
        add(registry, std::move(definition));
    }

    add(registry, node("Split", "Split", "Channel",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        output(1, "R", ValueType::Float), output(2, "G", ValueType::Float),
                        output(3, "B", ValueType::Float), output(4, "A", ValueType::Float)},
                       ""));
    add(registry, node("Combine", "Combine", "Channel",
                       {input(0, "R", ValueType::Float, f(0.0f)), input(1, "G", ValueType::Float, f(0.0f)),
                        input(2, "B", ValueType::Float, f(0.0f)), input(3, "A", ValueType::Float, f(0.0f)),
                        output(4, "Out", ValueType::Vector4)},
                       "$4 = vec4($0, $1, $2, $3);"));
    add(registry, node("Append", "Append", "Channel",
                       {dynamic(0, "A", ValueType::DynamicVector, f(0.0f)),
                        dynamic(1, "B", ValueType::DynamicVector, f(0.0f)),
                        output(2, "Out", ValueType::DynamicVector)},
                       ""));
    {
        NodeDef definition = node("Swizzle", "Swizzle", "Channel",
                                  {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                                   output(1, "Out", ValueType::DynamicVector)},
                                  "");
        definition.settings = {"order"};
        definition.synonyms = {"swap", "reorder", "component mask"};
        definition.updateSlots = updateSwizzleSlots;
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Flip", "Flip", "Channel",
                                  {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                                   output(1, "Out", ValueType::DynamicVector)},
                                  "");
        definition.settings = {"redChannel", "greenChannel", "blueChannel", "alphaChannel"};
        add(registry, std::move(definition));
    }

    addMathBinary(registry, "Add", "Add", "+");
    addMathBinary(registry, "Subtract", "Subtract", "-");
    addMathBinary(registry, "Multiply", "Multiply", "*", 1.0f);
    addMathBinary(registry, "Divide", "Divide", "/", 1.0f);
    add(registry, node("Power", "Power", "Math/Basic",
                       {dynamic(0, "A", ValueType::DynamicVector, f(1.0f)),
                        dynamic(1, "B", ValueType::DynamicVector, f(1.0f)),
                        output(2, "Out", ValueType::DynamicVector)},
                       "$2 = pow($0, $1);"));
    add(registry, node("Modulo", "Modulo", "Math/Advanced",
                       {dynamic(0, "A", ValueType::DynamicVector, f(1.0f)),
                        dynamic(1, "B", ValueType::DynamicVector, f(1.0f)),
                        output(2, "Out", ValueType::DynamicVector)},
                       "$2 = mod($0, $1);"));
    add(registry, node("Square Root", "Square Root", "Math/Basic",
                       {dynamic(0, "In", ValueType::DynamicVector, f(1.0f)),
                        output(1, "Out", ValueType::DynamicVector)},
                       "$1 = sqrt($0);"));
    addMathUnary(registry, "Absolute", "Absolute", "abs($0)");
    addMathUnary(registry, "Exponential", "Exponential", "exp($0)");
    addMathUnary(registry, "Negate", "Negate", "-($0)");
    addMathUnary(registry, "Normalize", "Normalize", "normalize($0)");
    addMathUnary(registry, "Reciprocal", "Reciprocal", "1.0 / ($0)");
    addMathUnary(registry, "Reciprocal Square Root", "Reciprocal Square Root", "rsqrt($0)");
    add(registry, node("Length", "Length", "Math/Advanced",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        output(1, "Out", ValueType::Float)},
                       "$1 = length($0);"));
    add(registry, node("Log", "Log", "Math/Advanced",
                       {dynamic(0, "In", ValueType::DynamicVector, f(1.0f)),
                        output(1, "Out", ValueType::DynamicVector)},
                       "$1 = log($0);"));
    add(registry, node("Posterize", "Posterize", "Math/Advanced",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        input(1, "Steps", ValueType::Float, f(2.0f)),
                        output(2, "Out", ValueType::DynamicVector)},
                       "$2 = floor($0 * $1) / $1;"));

    add(registry, node("Lerp", "Lerp", "Math/Interpolation",
                       {dynamic(0, "A", ValueType::DynamicVector, f(0.0f)),
                        dynamic(1, "B", ValueType::DynamicVector, f(1.0f)),
                        dynamic(2, "T", ValueType::DynamicVector, f(0.5f)),
                        output(3, "Out", ValueType::DynamicVector)},
                       "$3 = lerp($0, $1, $2);"));
    add(registry, node("Inverse Lerp", "Inverse Lerp", "Math/Interpolation",
                       {dynamic(0, "A", ValueType::DynamicVector, f(0.0f)),
                        dynamic(1, "B", ValueType::DynamicVector, f(1.0f)),
                        dynamic(2, "T", ValueType::DynamicVector, f(0.5f)),
                        output(3, "Out", ValueType::DynamicVector)},
                       "$3 = ($2 - $0) / ($1 - $0);"));
    add(registry, node("Smoothstep", "Smoothstep", "Math/Interpolation",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        input(1, "Edge1", ValueType::Float, f(0.0f)),
                        input(2, "Edge2", ValueType::Float, f(1.0f)),
                        output(3, "Out", ValueType::DynamicVector)},
                       "$3 = smoothstep($1, $2, $0);"));

    add(registry, node("Clamp", "Clamp", "Math/Range",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        dynamic(1, "Min", ValueType::DynamicVector, f(0.0f)),
                        dynamic(2, "Max", ValueType::DynamicVector, f(1.0f)),
                        output(3, "Out", ValueType::DynamicVector)},
                       "$3 = clamp($0, $1, $2);"));
    add(registry, node("Fraction", "Fraction", "Math/Range",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        output(1, "Out", ValueType::DynamicVector)},
                       "$1 = frac($0);"));
    add(registry, node("Maximum", "Maximum", "Math/Range",
                       {dynamic(0, "A", ValueType::DynamicVector, f(0.0f)),
                        dynamic(1, "B", ValueType::DynamicVector, f(0.0f)),
                        output(2, "Out", ValueType::DynamicVector)},
                       "$2 = max($0, $1);"));
    add(registry, node("Minimum", "Minimum", "Math/Range",
                       {dynamic(0, "A", ValueType::DynamicVector, f(0.0f)),
                        dynamic(1, "B", ValueType::DynamicVector, f(0.0f)),
                        output(2, "Out", ValueType::DynamicVector)},
                       "$2 = min($0, $1);"));
    add(registry, node("One Minus", "One Minus", "Math/Range",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        output(1, "Out", ValueType::DynamicVector)},
                       "$1 = 1.0 - $0;"));
    add(registry, node("Remap", "Remap", "Math/Range",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        input(1, "In Min Max", ValueType::Vector2, v2(0.0f, 1.0f)),
                        input(2, "Out Min Max", ValueType::Vector2, v2(0.0f, 1.0f)),
                        output(3, "Out", ValueType::DynamicVector)},
                       "$3 = $2.x + ($0 - $1.x) * ($2.y - $2.x) / ($1.y - $1.x);"));
    add(registry, node("Saturate", "Saturate", "Math/Range",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        output(1, "Out", ValueType::DynamicVector)},
                       "$1 = saturate($0);"));
    add(registry, node("Random Range", "Random Range", "Math/Range",
                       {input(0, "Seed", ValueType::Float, f(0.0f)),
                        input(1, "Min", ValueType::Float, f(0.0f)),
                        input(2, "Max", ValueType::Float, f(1.0f)),
                        output(3, "Out", ValueType::Float)},
                       "$3 = lerp($1, $2, frac(sin($0 * 12.9898) * 43758.5453));"));

    add(registry, node("Ceiling", "Ceiling", "Math/Round",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        output(1, "Out", ValueType::DynamicVector)},
                       "$1 = ceil($0);"));
    add(registry, node("Floor", "Floor", "Math/Round",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        output(1, "Out", ValueType::DynamicVector)},
                       "$1 = floor($0);"));
    add(registry, node("Round", "Round", "Math/Round",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        output(1, "Out", ValueType::DynamicVector)},
                       "$1 = round($0);"));
    add(registry, node("Sign", "Sign", "Math/Round",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        output(1, "Out", ValueType::DynamicVector)},
                       "$1 = sign($0);"));
    add(registry, node("Step", "Step", "Math/Round",
                       {dynamic(0, "Edge", ValueType::DynamicVector, f(0.0f)),
                        dynamic(1, "In", ValueType::DynamicVector, f(0.0f)),
                        output(2, "Out", ValueType::DynamicVector)},
                       "$2 = step($0, $1);"));
    add(registry, node("Truncate", "Truncate", "Math/Round",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        output(1, "Out", ValueType::DynamicVector)},
                       "$1 = trunc($0);"));

    addTrigUnary(registry, "Arccosine", "Arccosine", "acos", 1.0f, {"acos"});
    addTrigUnary(registry, "Arcsine", "Arcsine", "asin", 0.0f, {"asin"});
    addTrigUnary(registry, "Arctangent", "Arctangent", "atan", 0.0f, {"atan"});
    addTrigBinary(registry, "Arctangent2", "Arctangent2", "atan2", {"atan2"});
    addTrigUnary(registry, "Cosine", "Cosine", "cos", 0.0f);
    addTrigUnary(registry, "Degrees To Radians", "Degrees To Radians", "radians", 0.0f,
                 {"degtorad", "radians", "convert"});
    addTrigUnary(registry, "Hyperbolic Cosine", "Hyperbolic Cosine", "cosh", 0.0f, {"cosh"});
    addTrigUnary(registry, "Hyperbolic Sine", "Hyperbolic Sine", "sinh", 0.0f, {"sinh"});
    addTrigUnary(registry, "Hyperbolic Tangent", "Hyperbolic Tangent", "tanh", 0.0f, {"tanh"});
    addTrigUnary(registry, "Radians To Degrees", "Radians To Degrees", "degrees", 0.0f,
                 {"radtodeg", "degrees", "convert"});
    addTrigUnary(registry, "Sine", "Sine", "sin", 0.0f);
    addTrigUnary(registry, "Tangent", "Tangent", "tan", 0.0f);

    addWave(registry, "Sawtooth Wave", "Sawtooth Wave",
            "$1 = 2.0 * ($0 - floor(0.5 + $0));", {"triangle wave"});
    addWave(registry, "Triangle Wave", "Triangle Wave",
            "$1 = 2.0 * abs(2.0 * ($0 - floor(0.5 + $0))) - 1.0;", {"sawtooth wave"});
    addWave(registry, "Square Wave", "Square Wave", "$1 = 1.0 - 2.0 * round(frac($0));");
    {
        NodeDef definition = node("Noise Sine Wave", "Noise Sine Wave", "Math/Wave",
                                  {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                                   input(1, "MinMax", ValueType::Vector2, v2(-0.5f, 0.5f)),
                                   output(2, "Out", ValueType::DynamicVector)},
                                  "$2 = sin($0) + ($1.x + ($1.y - $1.x) * "
                                  "frac(sin((sin($0) - sin($0 + 1.0)) * 91.2228) * "
                                  "43758.5453));");
        definition.typeVersion = 1;
        add(registry, std::move(definition));
    }

    addDerivative(registry, "DDX", "DDX", "$1 = ddx($0);");
    addDerivative(registry, "DDXY", "DDXY", "$1 = abs(ddx($0)) + abs(ddy($0));");
    addDerivative(registry, "DDY", "DDY", "$1 = ddy($0);");

    {
        NodeDef definition = node("Dot Product", "Dot Product", "Math/Vector",
                                  {dynamic(0, "A", ValueType::DynamicVector, f(0.0f)),
                                   dynamic(1, "B", ValueType::DynamicVector, f(0.0f)),
                                   output(2, "Out", ValueType::Float)},
                                  "");
        definition.synonyms = {"scalar product"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Cross Product", "Cross Product", "Math/Vector",
                                  {input(0, "A", ValueType::Vector3, v3(0.0f, 0.0f, 0.0f)),
                                   input(1, "B", ValueType::Vector3, v3(0.0f, 1.0f, 0.0f)),
                                   output(2, "Out", ValueType::Vector3)},
                                  "$2 = cross($0, $1);");
        definition.synonyms = {"perpendicular"};
        add(registry, std::move(definition));
    }
    add(registry, node("Distance", "Distance", "Math/Vector",
                       {dynamic(0, "A", ValueType::DynamicVector, f(0.0f)),
                        dynamic(1, "B", ValueType::DynamicVector, f(0.0f)),
                        output(2, "Out", ValueType::Float)},
                       "$2 = distance($0, $1);"));
    add(registry, node("Fresnel Effect", "Fresnel Effect", "Math/Vector",
                       {input(0, "Normal", ValueType::Vector3, v3(0.0f, 0.0f, 1.0f)),
                        input(1, "ViewDir", ValueType::Vector3, v3(0.0f, 0.0f, 1.0f)),
                        input(2, "Power", ValueType::Float, f(1.0f)),
                        output(3, "Out", ValueType::Float)},
                       "$3 = pow(1.0 - saturate(dot(normalize($0), $1)), $2);"));
    {
        NodeDef definition = node("Projection", "Projection", "Math/Vector",
                                  {dynamic(0, "A", ValueType::DynamicVector, f(0.0f)),
                                   dynamic(1, "B", ValueType::DynamicVector, f(0.0f)),
                                   output(2, "Out", ValueType::DynamicVector)},
                                  "");
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Reflection", "Reflection", "Math/Vector",
                                  {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                                   dynamic(1, "Normal", ValueType::DynamicVector, f(0.0f)),
                                   output(2, "Out", ValueType::DynamicVector)},
                                  "");
        definition.synonyms = {"mirror"};
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node("Rejection", "Rejection", "Math/Vector",
                                  {dynamic(0, "A", ValueType::DynamicVector, f(0.0f)),
                                   dynamic(1, "B", ValueType::DynamicVector, f(0.0f)),
                                   output(2, "Out", ValueType::DynamicVector)},
                                  "");
        add(registry, std::move(definition));
    }
    {
        NodeDef definition = node(
            "Rotate About Axis", "Rotate About Axis", "Math/Vector",
            {input(0, "In", ValueType::Vector3, v3(0.0f, 0.0f, 0.0f)),
             input(1, "Axis", ValueType::Vector3, v3(0.0f, 1.0f, 0.0f)),
             input(2, "Rotation", ValueType::Float, f(0.0f)),
             output(3, "Out", ValueType::Vector3)},
            "");
        definition.synonyms = {"pivot"};
        definition.settings = {"unit"};
        add(registry, std::move(definition));
    }
    add(registry, node("Sphere Mask", "Sphere Mask", "Math/Vector",
                       {dynamic(0, "Coords", ValueType::DynamicVector, v3(0.5f, 0.5f, 0.5f)),
                        dynamic(1, "Center", ValueType::DynamicVector, v3(0.5f, 0.5f, 0.5f)),
                        input(2, "Radius", ValueType::Float, f(0.1f)),
                        input(3, "Hardness", ValueType::Float, f(0.8f)),
                        output(4, "Out", ValueType::Float)},
                       "$4 = 1.0 - saturate((distance($0, $1) - $2) / max(1.0 - $3, 1.0e-5));"));
    {
        NodeDef definition = node(
            "Refract", "Refract", "Math/Vector",
            {input(0, "Incident", ValueType::Vector3, v3(0.0f, 0.0f, 1.0f)),
             input(1, "Normal", ValueType::Vector3, v3(0.0f, 0.0f, -1.0f)),
             input(2, "IORSource", ValueType::Float, f(1.0f)),
             input(3, "IORMedium", ValueType::Float, f(1.5f)),
             output(4, "Refracted", ValueType::Vector3),
             output(5, "Intensity", ValueType::Float)},
            "");
        definition.synonyms = {"warp", "bend", "distort"};
        definition.settings = {"mode"};
        add(registry, std::move(definition));
    }

    {
        NodeDef definition = node(
            "Transform", "Transform", "Math/Vector",
            {input(0, "In", ValueType::Vector3, v3(0.0f, 0.0f, 0.0f)),
             output(1, "Out", ValueType::Vector3)},
            "");
        definition.synonyms = {"world", "tangent", "object", "view", "screen", "convert"};
        definition.settings = {"from", "to", "type", "normalize"};
        add(registry, std::move(definition));
    }

    {
        NodeDef definition = node(
            "Matrix Construction", "Matrix Construction", "Math/Matrix",
            {input(0, "M0", ValueType::Vector4, v4(0.0f, 0.0f, 0.0f, 0.0f)),
             input(1, "M1", ValueType::Vector4, v4(0.0f, 0.0f, 0.0f, 0.0f)),
             input(2, "M2", ValueType::Vector4, v4(0.0f, 0.0f, 0.0f, 0.0f)),
             input(3, "M3", ValueType::Vector4, v4(0.0f, 0.0f, 0.0f, 0.0f)),
             output(4, "4x4", ValueType::Matrix4),
             output(5, "3x3", ValueType::Matrix3),
             output(6, "2x2", ValueType::Matrix2)},
            "");
        definition.settings = {"axis"};
        definition.hasPreview = false;
        add(registry, std::move(definition));
    }
    add(registry, node("Matrix Determinant", "Matrix Determinant", "Math/Matrix",
                       {dynamic(0, "In", ValueType::DynamicMatrix, Value::makeMatrix2(Mat2::identity())),
                        output(1, "Out", ValueType::Float)},
                       "$1 = determinant($0);"));
    {
        NodeDef definition = node(
            "Matrix Split", "Matrix Split", "Math/Matrix",
            {dynamic(0, "In", ValueType::DynamicMatrix, Value::makeMatrix2(Mat2::identity())),
             output(1, "M0", ValueType::DynamicVector),
             output(2, "M1", ValueType::DynamicVector),
             output(3, "M2", ValueType::DynamicVector),
             output(4, "M3", ValueType::DynamicVector)},
            "");
        definition.settings = {"axis"};
        definition.updateSlots = updateMatrixSplitSlots;
        definition.hasPreview = false;
        add(registry, std::move(definition));
    }
    add(registry, node("Matrix Transpose", "Matrix Transpose", "Math/Matrix",
                       {dynamic(0, "In", ValueType::DynamicMatrix, Value::makeMatrix2(Mat2::identity())),
                        output(1, "Out", ValueType::DynamicMatrix)},
                       "$1 = transpose($0);"));

    add(registry, node("All", "All", "Utility/Logic",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        output(1, "Out", ValueType::Boolean)},
                       "$1 = all($0);"));
    add(registry, node("Any", "Any", "Utility/Logic",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        output(1, "Out", ValueType::Boolean)},
                       "$1 = any($0);"));
    add(registry, node("And", "And", "Utility/Logic",
                       {input(0, "A", ValueType::Boolean, b(false)), input(1, "B", ValueType::Boolean, b(false)),
                        output(2, "Out", ValueType::Boolean)},
                       "$2 = ($0 && $1);"));
    add(registry, node("Or", "Or", "Utility/Logic",
                       {input(0, "A", ValueType::Boolean, b(false)), input(1, "B", ValueType::Boolean, b(false)),
                        output(2, "Out", ValueType::Boolean)},
                       "$2 = ($0 || $1);"));
    add(registry, node("Nand", "Nand", "Utility/Logic",
                       {input(0, "A", ValueType::Boolean, b(false)), input(1, "B", ValueType::Boolean, b(false)),
                        output(2, "Out", ValueType::Boolean)},
                       "$2 = !($0 && $1);"));
    add(registry, node("Not", "Not", "Utility/Logic",
                       {input(0, "In", ValueType::Boolean, b(false)),
                        output(1, "Out", ValueType::Boolean)},
                       "$1 = !($0);"));
    add(registry, node("Branch", "Branch", "Utility/Logic",
                       {input(0, "Predicate", ValueType::Boolean, b(false)),
                        dynamic(1, "True", ValueType::DynamicVector, f(0.0f)),
                        dynamic(2, "False", ValueType::DynamicVector, f(0.0f)),
                        output(3, "Out", ValueType::DynamicVector)},
                       "$3 = $0 ? $1 : $2;"));
    add(registry, node("Branch On Input Connection", "Branch On Input Connection", "Utility/Logic",
                       {input(0, "Input", ValueType::PropertyConnectionState, std::nullopt, false),
                        dynamic(1, "Connected", ValueType::DynamicVector, f(1.0f)),
                        dynamic(2, "NotConnected", ValueType::DynamicVector, f(0.0f)),
                        output(3, "Out", ValueType::DynamicVector)},
                       "$3 = $0 ? $1 : $2;"));
    {
        NodeDef definition = node("Comparison", "Comparison", "Utility/Logic",
                                  {dynamic(0, "A", ValueType::DynamicVector, f(0.0f)),
                                   dynamic(1, "B", ValueType::DynamicVector, f(0.0f)),
                                   output(2, "Out", ValueType::Boolean)},
                                  "$2 = ($0 == $1);");
        definition.settings = {"comparison"};
        definition.synonyms = {"equal", "greater than", "less than"};
        add(registry, std::move(definition));
    }
    add(registry, node("Is Front Face", "Is Front Face", "Utility/Logic",
                       {output(0, "Out", ValueType::Boolean)}, "$0 = gi.faceSign > 0.0;"));
    add(registry, node("Is Infinite", "Is Infinite", "Utility/Logic",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        output(1, "Out", ValueType::Boolean)},
                       "$1 = any(isinf($0));"));
    add(registry, node("Is NaN", "Is NaN", "Utility/Logic",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        output(1, "Out", ValueType::Boolean)},
                       "$1 = any(isnan($0));"));

    {
        std::vector<SlotSpec> slots;
        slots.push_back(input(0, "Predicate", ValueType::Float, f(0.0f)));
        slots.push_back(dynamic(1, "Fallback", ValueType::DynamicVector, f(0.0f)));
        slots.push_back(output(2, "Out", ValueType::DynamicVector));
        for (SlotId id = 3; id <= 28; ++id) {
            SlotSpec slot = dynamic(id, "Case " + std::to_string(id - 2),
                                    ValueType::DynamicVector, f(0.0f));
            slot.hidden = true;
            slots.push_back(std::move(slot));
        }
        NodeDef definition =
            node("Switch", "Switch", "Utility/Logic", std::move(slots), "");
        definition.settings = {"cases", "floorPredicate"};
        definition.synonyms = {"case", "select"};
        definition.updateSlots = updateSwitchSlots;
        add(registry, std::move(definition));
    }

    {
        std::vector<SlotSpec> slots;
        slots.push_back(output(0, "Out", ValueType::DynamicVector));
        slots.push_back(dynamic(1, "On", ValueType::DynamicVector, f(0.0f)));
        slots.push_back(dynamic(2, "Off", ValueType::DynamicVector, f(0.0f)));
        for (SlotId id = 3; id <= 8; ++id) {
            SlotSpec slot = dynamic(id, "Entry " + std::to_string(id),
                                    ValueType::DynamicVector, f(0.0f));
            slot.hidden = true;
            slots.push_back(std::move(slot));
        }
        NodeDef definition = node("Keyword", "Keyword", "Utility", std::move(slots), "");
        definition.settings = {"keyword"};
        definition.updateSlots = updateKeywordSlots;
        add(registry, std::move(definition));
    }
    {
        std::vector<SlotSpec> slots;
        slots.push_back(output(0, "Out", ValueType::DynamicVector));
        for (SlotId id = 1; id <= 16; ++id) {
            SlotSpec slot = dynamic(id, "Entry " + std::to_string(id),
                                    ValueType::DynamicVector, f(0.0f));
            slot.hidden = true;
            slots.push_back(std::move(slot));
        }
        NodeDef definition = node("Dropdown", "Dropdown", "Utility", std::move(slots), "");
        definition.settings = {"dropdown"};
        definition.synonyms = {"dropdown", "enum"};
        definition.canPropagateFloatLiteral = true;
        definition.updateSlots = updateDropdownSlots;
        add(registry, std::move(definition));
    }
    add(registry, node("Preview", "Preview", "Utility",
                       {dynamic(0, "In", ValueType::DynamicVector, f(0.0f)),
                        output(1, "Out", ValueType::DynamicVector)},
                       "$1 = $0;"));
    const auto addSubGraphNode = [&registry](const char* type) {
        std::vector<SlotSpec> slots;
        slots.push_back(dynamic(0, "In", ValueType::DynamicVector, std::nullopt));
        for (SlotId id = 2; id <= 4; ++id) {
            SlotSpec slot = dynamic(id, "In " + std::to_string(id),
                                    ValueType::DynamicVector, std::nullopt);
            slot.hidden = true;
            slots.push_back(std::move(slot));
        }
        slots.push_back(output(1, "Out", ValueType::DynamicVector));
        for (SlotId id = 5; id <= 7; ++id) {
            SlotSpec slot;
            slot.id = id;
            slot.name = "Out " + std::to_string(id - 3);
            slot.displayName = slot.name;
            slot.direction = SlotDirection::Output;
            slot.type = ValueType::DynamicVector;
            slot.hidden = true;
            slots.push_back(std::move(slot));
        }
        NodeDef definition = node(type, "Sub-graph", "Utility", std::move(slots), "$1 = $0;");
        definition.settings = {"subgraph"};
        definition.synonyms = {"nested", "subgraph"};
        add(registry, std::move(definition));
    };
    addSubGraphNode("Sub-graph");
    addSubGraphNode("SubGraph");
    {
        std::vector<SlotSpec> slots;
        for (SlotId id = 0; id < 16; ++id) {
            SlotSpec slot = input(id, "Input " + std::to_string(id + 1),
                                  ValueType::DynamicVector, std::nullopt);
            slot.hidden = true;
            slots.push_back(std::move(slot));
        }
        for (SlotId id = 100; id < 116; ++id) {
            SlotSpec slot = output(id, "Output " + std::to_string(id - 99),
                                   ValueType::DynamicVector);
            slot.hidden = true;
            slots.push_back(std::move(slot));
        }
        NodeDef definition =
            node("Custom Function", "Custom Function", "Utility", std::move(slots), "");
        definition.settings = {"sourceType", "functionName", "functionSource", "functionBody",
                               "inputSlots", "outputSlots", "functionSourceUsePragmas"};
        definition.synonyms = {"code", "hlsl", "custom"};
        definition.updateSlots = updateCustomFunctionSlots;
        add(registry, std::move(definition));
    }

    {
        NodeDef definition = node("Element Layout UV", "Element Layout UV", "Input/UI",
                                  {output(0, "Layout UV", ValueType::Vector2)},
                                  "$0 = gi.uv0;");
        definition.hasPreview = false;
        addTargetSpecific(registry, std::move(definition), {"preview", "ui"});
    }
    {
        NodeDef definition = node("Element Opacity", "Element Opacity", "Input/UI",
                                  {output(0, "Opacity", ValueType::Float)},
                                  "$0 = 1.0;");
        definition.synonyms = {"alpha", "fade"};
        addTargetSpecific(registry, std::move(definition), {"preview", "ui"});
    }
    {
        NodeDef definition = node(
            "Element Texture Size", "Element Texture Size", "Input/UI",
            {output(0, "Width", ValueType::Float), output(1, "Height", ValueType::Float),
             output(2, "Texel Width", ValueType::Float),
             output(3, "Texel Height", ValueType::Float)},
            "$0 = 1.0; $1 = 1.0; $2 = 1.0; $3 = 1.0;");
        definition.hasPreview = false;
        addTargetSpecific(registry, std::move(definition), {"preview", "ui"});
    }
    {
        NodeDef definition = node("Element Texture UV", "Element Texture UV", "Input/UI",
                                  {output(0, "Texture UV", ValueType::Vector2)},
                                  "$0 = gi.uv0;");
        definition.hasPreview = false;
        addTargetSpecific(registry, std::move(definition), {"preview", "ui"});
    }
    {
        NodeDef definition = node(
            "Element Tint", "Element Tint", "Input/UI",
            {output(0, "Tint", ValueType::Vector4)},
            "$0 = vec4(1.0, 1.0, 1.0, 1.0);");
        definition.synonyms = {"color", "vertex color", "dynamic color"};
        addTargetSpecific(registry, std::move(definition), {"preview", "ui"});
    }
    {
        NodeDef definition = node(
            "SDF Text Parameters", "SDF Text Parameters", "Input/UI",
            {output(0, "Outline Color", ValueType::Vector4),
             output(1, "Shadow Color", ValueType::Vector4),
             output(2, "Sharpness", ValueType::Float),
             output(3, "Dilate", ValueType::Float),
             output(4, "Outline Dilate", ValueType::Float),
             output(5, "Shadow Offset", ValueType::Vector2),
             output(6, "Shadow Softness", ValueType::Float)},
            "$0 = vec4(0.0, 0.0, 0.0, 0.0); $1 = vec4(0.0, 0.0, 0.0, 0.0); "
            "$2 = 0.0; $3 = 0.0; $4 = 0.0; $5 = vec2(0.0, 0.0); $6 = 0.0;");
        definition.synonyms = {"element", "ui", "text", "font", "outline", "underlay",
                               "shadow", "bold", "dilate"};
        definition.hasPreview = false;
        addTargetSpecific(registry, std::move(definition), {"preview", "ui"});
    }

    {
        NodeDef definition = node(
            "Default Solid", "Default Solid", "UI",
            {output(0, "Solid", ValueType::Vector4)},
            "$0 = vec4(1.0, 1.0, 1.0, 1.0);");
        addTargetSpecific(registry, std::move(definition), {"preview", "ui"});
    }
    {
        NodeDef definition = node(
            "Default Texture", "Default Texture", "UI",
            {input(0, "UV", ValueType::Vector2, v2(0.0f, 0.0f)),
             input(1, "Tint", ValueType::Vector4, v4(1.0f, 1.0f, 1.0f, 1.0f)),
             output(2, "Texture", ValueType::Vector4)},
            "$2 = $1;");
        addTargetSpecific(registry, std::move(definition), {"preview", "ui"});
    }
    {
        NodeDef definition = node(
            "Default Gradient", "Default Gradient", "UI",
            {output(0, "Gradient", ValueType::Vector4)},
            "$0 = vec4(1.0, 1.0, 1.0, 1.0);");
        addTargetSpecific(registry, std::move(definition), {"preview", "ui"});
    }
    {
        NodeDef definition = node(
            "Default SDF Text", "Default SDF Text", "UI",
            {input(0, "Tint", ValueType::Vector4, v4(1.0f, 1.0f, 1.0f, 1.0f)),
             input(2, "Outline Color", ValueType::Vector4, v4(0.0f, 0.0f, 0.0f, 0.0f)),
             input(3, "Shadow Color", ValueType::Vector4, v4(0.0f, 0.0f, 0.0f, 0.0f)),
             input(4, "Sharpness", ValueType::Float, f(0.0f)),
             input(5, "Dilate", ValueType::Float, f(0.0f)),
             input(6, "Outline Dilate", ValueType::Float, f(0.0f)),
             input(7, "Shadow Offset", ValueType::Vector2, v2(0.0f, 0.0f)),
             input(8, "Shadow Softness", ValueType::Float, f(0.0f)),
             output(1, "SDF Text", ValueType::Vector4)},
            "$1 = $0;");
        addTargetSpecific(registry, std::move(definition), {"preview", "ui"});
    }
    {
        NodeDef definition = node(
            "Default Bitmap Text", "Default Bitmap Text", "UI",
            {input(0, "Tint", ValueType::Vector4, v4(1.0f, 1.0f, 1.0f, 1.0f)),
             output(1, "Bitmap Text", ValueType::Vector4)},
            "$1 = $0;");
        addTargetSpecific(registry, std::move(definition), {"preview", "ui"});
    }
    {
        NodeDef definition = node(
            "Is Forced Gamma", "Is Forced Gamma", "UI",
            {output(0, "Out", ValueType::Boolean)}, "$0 = false;");
        addTargetSpecific(registry, std::move(definition), {"preview", "ui"});
    }
    {
        NodeDef definition = node(
            "Render Type", "Render Type", "UI",
            {output(0, "Solid", ValueType::Boolean), output(1, "Texture", ValueType::Boolean),
             output(2, "SDF Text", ValueType::Boolean),
             output(3, "Bitmap Text", ValueType::Boolean),
             output(4, "Gradient", ValueType::Boolean)},
            "$0 = true; $1 = false; $2 = false; $3 = false; $4 = false;");
        addTargetSpecific(registry, std::move(definition), {"preview", "ui"});
    }
    {
        NodeDef definition = node(
            "Render Type Branch", "Render Type Branch", "UI",
            {input(0, "Solid", ValueType::Vector4, v4(1.0f, 1.0f, 1.0f, 1.0f)),
             input(1, "Texture", ValueType::Vector4, v4(1.0f, 1.0f, 1.0f, 1.0f)),
             input(2, "SDF Text", ValueType::Vector4, v4(1.0f, 1.0f, 1.0f, 1.0f)),
             input(3, "Bitmap Text", ValueType::Vector4, v4(1.0f, 1.0f, 1.0f, 1.0f)),
             input(4, "Gradient", ValueType::Vector4, v4(1.0f, 1.0f, 1.0f, 1.0f)),
             output(5, "Color", ValueType::Vector3), output(6, "Alpha", ValueType::Float)},
            "$5 = $0.rgb; $6 = $0.a;");
        definition.synonyms = {"toggle", "uber"};
        addTargetSpecific(registry, std::move(definition), {"preview", "ui"});
    }
    {
        NodeDef definition = node(
            "Sample Element Texture", "Sample Element Texture", "UI",
            {input(0, "UV 0", ValueType::Vector2, v2(0.0f, 0.0f)),
             input(1, "UV 1", ValueType::Vector2, v2(0.0f, 0.0f)),
             input(2, "UV 2", ValueType::Vector2, v2(0.0f, 0.0f)),
             input(3, "UV 3", ValueType::Vector2, v2(0.0f, 0.0f)),
             input(5, "Sampler", ValueType::SamplerState, std::nullopt, false),
             output(10, "Color 0", ValueType::Vector4),
             output(11, "Color 1", ValueType::Vector4),
             output(12, "Color 2", ValueType::Vector4),
             output(13, "Color 3", ValueType::Vector4)},
            "$10 = vec4(1.0, 1.0, 1.0, 1.0); $11 = vec4(1.0, 1.0, 1.0, 1.0); "
            "$12 = vec4(1.0, 1.0, 1.0, 1.0); $13 = vec4(1.0, 1.0, 1.0, 1.0);");
        definition.settings = {"sampleCount", "mipSamplingMode"};
        definition.hasPreview = false;
        addTargetSpecific(registry, std::move(definition), {"preview", "ui"});
    }

    {
        NodeDef definition = node(
            "Terrain Properties", "Terrain Properties", "Terrain",
            {output(0, "Max Local Height", ValueType::Float),
             output(1, "Basemap Distance", ValueType::Float),
             output(2, "Layers Count", ValueType::Float)},
            "$0 = 600.0; $1 = 1.0; $2 = 4.0;");
        definition.hasPreview = false;
        addTargetSpecific(registry, std::move(definition), {"preview", "terrain"});
    }
    {
        NodeDef definition = node(
            "Terrain Texture", "Terrain Texture", "Terrain",
            {input(0, "Index", ValueType::Float, f(0.0f)),
             output(1, "Texture", ValueType::Texture2D),
             output(2, "Available", ValueType::Boolean),
             output(3, "Normal Scale", ValueType::Float),
             output(4, "Metallic Default", ValueType::Float),
             output(5, "Smoothness Default", ValueType::Float),
             output(6, "Color Tint", ValueType::Vector3),
             output(7, "Opacity As Density", ValueType::Float),
             output(8, "Channel Remapping Offset", ValueType::Vector4),
             output(9, "Channel Remapping Scale", ValueType::Vector4)},
            "$2 = true; $3 = 1.0; $4 = 0.0; $5 = 0.5; "
            "$6 = vec3(1.0, 1.0, 1.0); $7 = 1.0; $8 = vec4(0.0, 0.0, 0.0, 0.0); $9 = vec4(1.0, 1.0, 1.0, 1.0);");
        definition.settings = {"textureType"};
        addTargetSpecific(registry, std::move(definition), {"preview", "terrain"});
    }
    {
        NodeDef definition = node(
            "Compute Deformation", "Compute Deformation", "Input/Mesh Deformation",
            {output(0, "Deformed Position", ValueType::Vector3),
             output(1, "Deformed Normal", ValueType::Vector3),
             output(2, "Deformed Tangent", ValueType::Vector3)},
            "$0 = gi.positionOS; $1 = gi.normalOS; $2 = gi.tangentOS;");
        definition.synonyms = {"deform", "dots"};
        addTargetSpecific(registry, std::move(definition),
                          {"preview", "mesh-deformation"});
    }
    {
        NodeDef definition = node(
            "Linear Blend Skinning", "Linear Blend Skinning", "Input/Mesh Deformation",
            {input(0, "Vertex Position", ValueType::Vector3, v3(0.0f, 0.0f, 0.0f)),
             input(1, "Vertex Normal", ValueType::Vector3, v3(0.0f, 0.0f, 1.0f)),
             input(2, "Vertex Tangent", ValueType::Vector3, v3(1.0f, 0.0f, 0.0f)),
             output(3, "Skinned Position", ValueType::Vector3),
             output(4, "Skinned Normal", ValueType::Vector3),
             output(5, "Skinned Tangent", ValueType::Vector3)},
            "$3 = $0; $4 = $1; $5 = $2;");
        definition.synonyms = {"skinning", "skin"};
        addTargetSpecific(registry, std::move(definition),
                          {"preview", "mesh-deformation"});
    }
    {
        NodeDef definition = node(
            "Instance", "Instance", "Custom Interpolators",
            {output(0, "Out", ValueType::Vector4)},
            "$0 = vec4(0.0, 0.0, 0.0, 0.0);");
        definition.settings = {"customBlockNodeName", "type"};
        definition.allowInSubGraph = false;
        addTargetSpecific(registry, std::move(definition), {"preview"});
    }

    std::vector<std::pair<std::string, NodeDef>> aliases;
    for (const auto& [type, definition] : registry.definitions()) {
        std::string compact;
        for (char character : type) {
            if (character != ' ' && character != '-') {
                compact.push_back(character);
            }
        }
        if (compact != type && registry.find(compact) == nullptr) {
            aliases.emplace_back(compact, definition);
        }
    }
    for (auto& [type, definition] : aliases) {
        definition.type = type;
        add(registry, std::move(definition));
    }
}

NodeRegistry makeBuiltinNodeRegistry() {
    NodeRegistry registry;
    registerBuiltinNodes(registry);
    return registry;
}

} // namespace sg
