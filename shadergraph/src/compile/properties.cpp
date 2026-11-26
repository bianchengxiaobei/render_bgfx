#include "compile_internal.h"

#include "sg/validate.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>

namespace sg::detail {
namespace {

bool isTextureLike(ValueType type) {
    return type == ValueType::Texture2D || type == ValueType::Texture2DArray ||
           type == ValueType::Texture3D || type == ValueType::Cubemap ||
           type == ValueType::Gradient;
}

bool isMatrix(ValueType type) {
    return type == ValueType::Matrix2 || type == ValueType::Matrix3 || type == ValueType::Matrix4;
}

std::string textureDeclaration(const PropertyInfo& property, int stage) {
    switch (property.type) {
    case ValueType::Texture2DArray:
        return "SAMPLER2DARRAY(s_sg_" + property.refName + ", " + std::to_string(stage) + ");";
    case ValueType::Texture3D:
        return "SAMPLER3D(s_sg_" + property.refName + ", " + std::to_string(stage) + ");";
    case ValueType::Cubemap:
        return "SAMPLERCUBE(s_sg_" + property.refName + ", " + std::to_string(stage) + ");";
    default:
        return "SAMPLER2D(s_sg_" + property.refName + ", " + std::to_string(stage) + ");";
    }
}

std::string propertyDeclaration(const PropertyInfo& property) {
    if (isTextureLike(property.type)) {
        std::string declaration = textureDeclaration(property, property.samplerStage);
        if (property.type != ValueType::Gradient) {
            declaration += "\nuniform vec4 u_sg_" + property.refName + "_TexelSize;";
        }
        if (property.useTilingAndOffset) {
            declaration += "\nuniform vec4 u_sg_" + property.refName + "_ST;";
        }
        if (property.isHDR) {
            declaration += "\nuniform vec4 u_sg_" + property.refName + "_HDR;";
        }
        return declaration;
    }
    if (isMatrix(property.type)) {
        return "uniform mat4 u_sg_" + property.refName + ";";
    }
    if (property.type == ValueType::SamplerState) {
        return {};
    }
    return "uniform vec4 u_sg_" + property.refName + ";";
}

std::string propertyExpression(ValueType type, const std::string& refName) {
    const std::string uniform = "u_sg_" + refName;
    switch (type) {
    case ValueType::Float:
        return uniform + ".x";
    case ValueType::Vector2:
        return uniform + ".xy";
    case ValueType::Vector3:
        return uniform + ".xyz";
    case ValueType::Vector4:
    case ValueType::Color:
        return uniform;
    case ValueType::Boolean:
        return "bool(" + uniform + ".x)";
    case ValueType::Integer:
        return "int(" + uniform + ".x)";
    case ValueType::Matrix2:
        return "mat2(" + uniform + "[0].xy, " + uniform + "[0].zw)";
    case ValueType::Matrix3:
        return "mat3(" + uniform + "[0].xyz, vec3(" + uniform + "[0].w, " + uniform +
               "[1].xy), vec3(" + uniform + "[1].zw, " + uniform + "[2].x))";
    case ValueType::Matrix4:
        return uniform;
    case ValueType::SamplerState:
        return "s_sg_" + refName;
    default:
        return {};
    }
}

std::string safeRefName(std::string value, NodeId nodeId) {
    std::string result;
    for (char character : value) {
        const bool alpha = (character >= 'a' && character <= 'z') ||
                           (character >= 'A' && character <= 'Z') || character == '_';
        const bool digit = character >= '0' && character <= '9';
        if (alpha || (!result.empty() && digit)) {
            result.push_back(character);
        }
    }
    if (result.empty() || !(result.front() >= 'a' && result.front() <= 'z') &&
                              !(result.front() >= 'A' && result.front() <= 'Z') &&
                              result.front() != '_') {
        result.insert(result.begin(), '_');
    }
    if (result.empty()) {
        result = "_ImplicitTexture" + std::to_string(nodeId);
    }
    return result;
}

void addProperty(PropertySet& result, PropertyInfo property, std::set<std::string>& refs,
                 std::vector<Diagnostic>& diagnostics) {
    const auto existing = std::find_if(result.properties.begin(), result.properties.end(),
                                       [&](const PropertyInfo& item) {
                                           return item.refName == property.refName;
                                       });
    if (existing != result.properties.end()) {
        if (existing->type != property.type) {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorNameConflict;
            diagnostic.message = "property reference name has conflicting types: " + property.refName;
            diagnostics.push_back(std::move(diagnostic));
            result.ok = false;
            return;
        }
        return;
    }
    if (!refs.insert(property.refName).second) {
        return;
    }
    result.properties.push_back(std::move(property));
}

std::optional<std::string> settingString(const NodeInstance& node, const char* key) {
    const auto found = node.settings.find(key);
    if (found == node.settings.end()) {
        return std::nullopt;
    }
    if (const std::string* value = found->second.asString()) {
        return *value;
    }
    return std::nullopt;
}

std::optional<bool> settingBool(const NodeInstance& node, const char* key) {
    const auto found = node.settings.find(key);
    if (found == node.settings.end()) {
        return std::nullopt;
    }
    if (const bool* value = found->second.asBool()) {
        return *value;
    }
    return std::nullopt;
}

std::optional<int> settingInt(const NodeInstance& node, const char* key) {
    const auto found = node.settings.find(key);
    if (found == node.settings.end()) {
        return std::nullopt;
    }
    if (const std::int64_t* value = found->second.asInteger()) {
        return static_cast<int>(*value);
    }
    if (const float* value = found->second.asFloat()) {
        return static_cast<int>(*value);
    }
    return std::nullopt;
}

std::string samplerAnisotropySetting(const NodeInstance& node) {
    if (const auto text = settingString(node, "anisotropic")) {
        const std::string normalized = normalizeSamplerAnisotropy(*text);
        if (!normalized.empty()) {
            return normalized;
        }
    }
    if (const auto index = settingInt(node, "anisotropic")) {
        switch (*index) {
        case 1:
            return "x2";
        case 2:
            return "x4";
        case 3:
            return "x8";
        case 4:
            return "x16";
        default:
            return "none";
        }
    }
    return "none";
}

std::string normalizeToken(std::string_view value) {
    std::string result;
    for (char character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
        if ((character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9')) {
            result.push_back(character);
        }
    }
    return result;
}

std::string nodeTypeKey(std::string_view type) {
    std::string result;
    for (char character : type) {
        if (character != ' ' && character != '-') {
            result.push_back(character);
        }
    }
    return result;
}

SlotId textureInputSlot(const std::string& type) {
    if (type == "NormalFromTexture" || type == "SplitTextureTransform") {
        return 0;
    }
    if (type == "SampleTexture2D" || type == "SampleTexture2DLOD" ||
        type == "SampleTexture2DArray" || type == "SampleTexture3D" ||
        type == "SampleCubemap" || type == "SampleReflectedCubemap" ||
        type == "TextureSize" || type == "GatherTexture2D" ||
        type == "CalculateLevelOfDetailTexture2D" || type == "ParallaxMapping" ||
        type == "Triplanar") {
        return 1;
    }
    if (type == "ParallaxOcclusionMapping") {
        return 2;
    }
    return kInvalidSlot;
}

std::optional<std::string> textureRefForValue(const Graph& graph, const NodeInstance& source,
                                              SlotId sourceSlot, int depth) {
    if (depth > 16) {
        return std::nullopt;
    }
    if (nodeTypeKey(source.type) == "SplitTextureTransform" && sourceSlot == 3) {
        const Edge* edge = graph.incomingEdge(source.id, 0);
        if (edge != nullptr) {
            const NodeInstance* input = graph.findNode(edge->fromNode);
            return input == nullptr
                       ? std::nullopt
                       : textureRefForValue(graph, *input, edge->fromSlot, depth + 1);
        }
        const auto ref = settingString(source, "texture");
        return ref && !ref->empty() ? ref : std::nullopt;
    }
    if (const auto ref = settingString(source, "property")) {
        return ref;
    }
    if (const auto ref = settingString(source, "texture")) {
        return ref;
    }
    return std::nullopt;
}

std::optional<std::string> textureSourceRef(const Graph& graph, const NodeInstance& node) {
    const std::string type = nodeTypeKey(node.type);
    const SlotId textureSlot = textureInputSlot(type);
    if (textureSlot != kInvalidSlot) {
        const Edge* edge = graph.incomingEdge(node.id, textureSlot);
        if (edge != nullptr) {
            const NodeInstance* source = graph.findNode(edge->fromNode);
            return source == nullptr
                       ? std::nullopt
                       : textureRefForValue(graph, *source, edge->fromSlot, 0);
        }
    }
    return settingString(node, "texture");
}

struct SamplerStateInfo {
    std::string filter = "linear";
    std::string wrap = "repeat";
    std::string anisotropic = "none";
};

std::optional<SamplerStateInfo> samplerStateForNode(const Graph& graph,
                                                    const NodeInstance& node) {
    const SlotInstance* samplerSlot = node.findSlot("Sampler");
    if (samplerSlot == nullptr) {
        samplerSlot = node.findSlot("HeightmapSampler");
    }
    if (samplerSlot == nullptr || !samplerSlot->isInput()) {
        return std::nullopt;
    }
    const Edge* edge = graph.incomingEdge(node.id, samplerSlot->spec.id);
    if (edge == nullptr) {
        return std::nullopt;
    }
    const NodeInstance* source = graph.findNode(edge->fromNode);
    if (source == nullptr) {
        return std::nullopt;
    }
    SamplerStateInfo info;
    const std::string sourceType = nodeTypeKey(source->type);
    if (sourceType == "SamplerState") {
        if (const auto filter = settingString(*source, "filter")) {
            const std::string normalized = normalizeToken(*filter);
            if (!normalized.empty()) {
                info.filter = normalized;
            }
        }
        if (const auto wrap = settingString(*source, "wrap")) {
            const std::string normalized = normalizeToken(*wrap);
            if (!normalized.empty()) {
                info.wrap = normalized;
            }
        }
        info.anisotropic = samplerAnisotropySetting(*source);
        return info;
    }
    if (sourceType == "Property") {
        const auto ref = settingString(*source, "property");
        if (!ref) {
            return std::nullopt;
        }
        const Property* property = graph.findProperty(*ref);
        if (property == nullptr || property->type != ValueType::SamplerState) {
            return std::nullopt;
        }
        const std::string filter = normalizeToken(property->samplerFilter);
        const std::string wrap = normalizeToken(property->samplerWrap);
        info.filter = filter.empty() ? "linear" : filter;
        info.wrap = wrap.empty() ? "repeat" : wrap;
        const std::string anisotropic = normalizeSamplerAnisotropy(property->samplerAnisotropic);
        info.anisotropic = anisotropic.empty() ? "none" : anisotropic;
        return info;
    }
    return std::nullopt;
}

} // namespace

std::optional<VirtualTextureSourceInfo> virtualTextureSource(const Graph& graph,
                                                             const NodeInstance& sampleNode) {
    if (nodeTypeKey(sampleNode.type) != "SampleVirtualTexture") {
        return std::nullopt;
    }
    const SlotInstance* vtSlot = sampleNode.findSlot("VT");
    if (vtSlot == nullptr || !vtSlot->isInput()) {
        return std::nullopt;
    }
    const Edge* edge = graph.incomingEdge(sampleNode.id, vtSlot->spec.id);
    if (edge == nullptr) {
        return std::nullopt;
    }
    const NodeInstance* source = graph.findNode(edge->fromNode);
    if (source == nullptr) {
        return std::nullopt;
    }
    VirtualTextureSourceInfo info;
    info.node = source->id;
    const std::string sourceType = nodeTypeKey(source->type);
    if (sourceType == "ProceduralVirtualTexture") {
        if (const auto name = settingString(*source, "vtName"); name && !name->empty()) {
            info.name = *name;
        }
        if (const auto layers = settingInt(*source, "layers")) {
            info.layers = std::clamp(*layers, 1, 4);
        }
    } else if (sourceType == "Property") {
        const auto ref = settingString(*source, "property");
        if (!ref) {
            return std::nullopt;
        }
        const Property* property = graph.findProperty(*ref);
        if (property == nullptr || property->type != ValueType::VirtualTexture) {
            return std::nullopt;
        }
        info.name = property->refName;
        info.layers = std::clamp(property->vtLayers, 1, 4);
    } else {
        return std::nullopt;
    }
    const Property* vtProperty = nullptr;
    if (sourceType == "Property") {
        vtProperty = graph.findProperty(info.name);
    }
    for (int layer = 0; layer < info.layers; ++layer) {
        info.layerRefNames[layer] = virtualTextureLayerRefName(info.name, layer);
        info.layerTextureTypes[layer] = "default";
        if (vtProperty != nullptr) {
            if (vtProperty->vtLayerRefNames.size() > static_cast<std::size_t>(layer) &&
                !vtProperty->vtLayerRefNames[layer].empty()) {
                info.layerRefNames[layer] = vtProperty->vtLayerRefNames[layer];
            }
            if (vtProperty->vtLayerTextureTypes.size() > static_cast<std::size_t>(layer)) {
                if (const auto normalized = virtualTextureLayerTextureTypeFromString(
                        vtProperty->vtLayerTextureTypes[layer])) {
                    info.layerTextureTypes[layer] = *normalized;
                }
            }
        }
    }
    if (const auto addressMode = settingString(sampleNode, "addressMode")) {
        const std::string normalized = normalizeToken(*addressMode);
        if (!normalized.empty()) {
            info.addressMode = normalized;
        }
    } else if (const auto addressModeIndex = settingInt(sampleNode, "addressMode")) {
        info.addressMode = *addressModeIndex == 1 ? "clamp" : "wrap";
    }
    if (const auto lodCalculation = settingString(sampleNode, "lodCalculation")) {
        const std::string normalized = normalizeToken(*lodCalculation);
        if (normalized == "lodlevel") {
            info.lodCalculation = "lod";
        } else if (normalized == "lodbias") {
            info.lodCalculation = "bias";
        } else if (!normalized.empty()) {
            info.lodCalculation = normalized;
        }
    } else if (const auto lodCalculationIndex = settingInt(sampleNode, "lodCalculation")) {
        switch (*lodCalculationIndex) {
        case 1:
            info.lodCalculation = "lod";
            break;
        case 2:
            info.lodCalculation = "bias";
            break;
        case 3:
            info.lodCalculation = "derivatives";
            break;
        default:
            info.lodCalculation = "automatic";
            break;
        }
    }
    if (const auto sampleQuality = settingString(sampleNode, "sampleQuality")) {
        const std::string normalized = normalizeToken(*sampleQuality);
        if (normalized == "low" || normalized == "high") {
            info.sampleQuality = normalized;
        }
    } else if (const auto sampleQualityIndex = settingInt(sampleNode, "sampleQuality")) {
        info.sampleQuality = *sampleQualityIndex == 0 ? "low" : "high";
    }
    if (const auto enableGlobalMipBias = settingBool(sampleNode, "enableGlobalMipBias")) {
        info.enableGlobalMipBias = *enableGlobalMipBias;
    }
    if (const auto noFeedback = settingBool(sampleNode, "noFeedback")) {
        info.noFeedback = *noFeedback;
    }
    return info;
}

std::string virtualTextureLayerRefName(std::string_view name, int layer) {
    std::string result = name.empty() ? "VirtualTexture" : std::string(name);
    result += "_Layer";
    result += std::to_string(layer);
    return safeRefName(std::move(result), kInvalidNode);
}

const PropertyInfo* PropertySet::find(std::string_view refName) const {
    for (const PropertyInfo& property : properties) {
        if (property.refName == refName) {
            return &property;
        }
    }
    return nullptr;
}

PropertySet collectProperties(const Graph& graph, const ActiveGraph& active, const TargetSpec& target) {
    PropertySet result;
    std::set<std::string> refs;
    std::map<std::string, SamplerStateInfo> samplerStates;
    for (const Property& property : graph.properties) {
        if (property.type == ValueType::VirtualTexture) {
            continue;
        }
        PropertyInfo info;
        info.name = property.name;
        info.refName = property.refName;
        info.type = property.type;
        info.precision = property.precision;
        info.defaultValue = property.defaultValue;
        const std::string filter = normalizeToken(property.samplerFilter);
        const std::string wrap = normalizeToken(property.samplerWrap);
        info.samplerFilter = filter.empty() ? "linear" : filter;
        info.samplerWrap = wrap.empty() ? "repeat" : wrap;
        const std::string anisotropic = normalizeSamplerAnisotropy(property.samplerAnisotropic);
        info.samplerAnisotropic = anisotropic.empty() ? "none" : anisotropic;
        info.isHDR = property.isHDR;
        info.useTilingAndOffset = property.useTilingAndOffset;
        info.defaultType = textureDefaultTypeCode(property.defaultType);
        info.tiling = property.tiling;
        info.offset = property.offset;
        addProperty(result, std::move(info), refs, result.diagnostics);
    }

    for (NodeId nodeId : active.order) {
        const NodeInstance* node = graph.findNode(nodeId);
        if (node == nullptr || node->unknown) {
            continue;
        }
        std::string type;
        for (char character : node->type) {
            if (character != ' ' && character != '-') {
                type.push_back(character);
            }
        }
        const std::optional<ValueType> expectedType = textureTypeForNode(type);
        if (expectedType) {
            const auto textureRef = textureSourceRef(graph, *node);
            if (!textureRef) {
                continue;
            }
            if (const auto samplerState = samplerStateForNode(graph, *node)) {
                const auto found = samplerStates.find(*textureRef);
                if (found == samplerStates.end()) {
                    samplerStates.emplace(*textureRef, *samplerState);
                } else if (found->second.filter != samplerState->filter ||
                           found->second.wrap != samplerState->wrap ||
                           found->second.anisotropic != samplerState->anisotropic) {
                    Diagnostic diagnostic;
                    diagnostic.severity = Severity::Error;
                    diagnostic.code = kErrorNameConflict;
                    diagnostic.message = "texture '" + *textureRef +
                                         "' is sampled with conflicting Sampler State settings";
                    result.diagnostics.push_back(std::move(diagnostic));
                    result.ok = false;
                }
            }
            if (const Property* property = graph.findProperty(*textureRef)) {
                if (property->type != *expectedType) {
                    Diagnostic diagnostic;
                    diagnostic.severity = Severity::Error;
                    diagnostic.code = kErrorNameConflict;
                    diagnostic.message = "texture node '" + node->type +
                                         "' expects " + toString(*expectedType) +
                                         " but property '" + *textureRef + "' is " +
                                         toString(property->type);
                    result.diagnostics.push_back(std::move(diagnostic));
                    result.ok = false;
                }
                continue;
            }
            PropertyInfo info;
            info.name = *textureRef;
            info.refName = safeRefName(*textureRef, node->id);
            info.type = *expectedType;
            info.implicit = true;
            if (const auto samplerState = samplerStates.find(*textureRef);
                samplerState != samplerStates.end()) {
                info.samplerFilter = samplerState->second.filter;
                info.samplerWrap = samplerState->second.wrap;
                info.samplerAnisotropic = samplerState->second.anisotropic;
            }
            addProperty(result, std::move(info), refs, result.diagnostics);
        }
        if (type == "SampleVirtualTexture") {
            const auto source = virtualTextureSource(graph, *node);
            if (!source) {
                continue;
            }
            for (int layer = 0; layer < source->layers; ++layer) {
                PropertyInfo info;
                info.name = source->layerRefNames[layer].empty()
                                ? virtualTextureLayerRefName(source->name, layer)
                                : source->layerRefNames[layer];
                info.refName = info.name;
                info.type = ValueType::Texture2D;
                info.implicit = true;
                info.samplerWrap = source->addressMode == "clamp" ? "clamp" : "repeat";
                addProperty(result, std::move(info), refs, result.diagnostics);
            }
        }
    }

    for (PropertyInfo& property : result.properties) {
        const auto found = samplerStates.find(property.refName);
        if (found != samplerStates.end()) {
            property.samplerFilter = found->second.filter;
            property.samplerWrap = found->second.wrap;
            property.samplerAnisotropic = found->second.anisotropic;
        }
    }

    std::sort(result.properties.begin(), result.properties.end(), [](const PropertyInfo& lhs,
                                                                      const PropertyInfo& rhs) {
        return lhs.refName < rhs.refName;
    });

    int textureStage = 0;
    int uniformCount = 0;
    int matrixCount = 0;
    for (PropertyInfo& property : result.properties) {
        if (isTextureLike(property.type)) {
            if (textureStage >= target.textureBudget) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorBudgetExceeded;
                diagnostic.message = "graph exceeds the texture sampler budget";
                result.diagnostics.push_back(std::move(diagnostic));
                result.ok = false;
            }
            property.samplerStage = textureStage++;
        } else if (isMatrix(property.type)) {
            if (++matrixCount > target.matrixBudget) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorBudgetExceeded;
                diagnostic.message = "graph exceeds the matrix uniform budget";
                result.diagnostics.push_back(std::move(diagnostic));
                result.ok = false;
            }
        } else if (property.type != ValueType::SamplerState) {
            if (++uniformCount > target.uniformBudget) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorBudgetExceeded;
                diagnostic.message = "graph exceeds the scalar/vector uniform budget";
                result.diagnostics.push_back(std::move(diagnostic));
                result.ok = false;
            }
        }
        property.declaration = propertyDeclaration(property);
        property.expression = propertyExpression(property.type, property.refName);
    }
    if (result.ok || result.diagnostics.empty()) {
        result.ok = !hasError(result.diagnostics);
    }
    return result;
}

} // namespace sg::detail
