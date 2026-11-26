#include "compile_internal.h"

#include "nodes/switch_cases.h"
#include "nodes/swizzle_mask.h"
#include "sg/validate.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>

namespace sg::detail {
namespace {

std::string formatFloat(float value) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(9) << value;
    std::string text = stream.str();
    if (text.find('.') == std::string::npos && text.find('e') == std::string::npos &&
        text.find('E') == std::string::npos) {
        text += ".0";
    }
    return text;
}

void valueComponents(const Value& value, float* values, int count) {
    for (int index = 0; index < count; ++index) {
        values[index] = 0.0f;
    }
    if (const float* scalar = value.asFloat()) {
        for (int index = 0; index < count; ++index) {
            values[index] = *scalar;
        }
        return;
    }
    if (const bool* boolean = value.asBool()) {
        for (int index = 0; index < count; ++index) {
            values[index] = *boolean ? 1.0f : 0.0f;
        }
        return;
    }
    if (const std::int64_t* integer = value.asInteger()) {
        for (int index = 0; index < count; ++index) {
            values[index] = static_cast<float>(*integer);
        }
        return;
    }
    if (const Vec2* vector = value.asVector2()) {
        const float source[2] = {vector->x, vector->y};
        for (int index = 0; index < count && index < 2; ++index) {
            values[index] = source[index];
        }
        return;
    }
    if (const Vec3* vector = value.asVector3()) {
        const float source[3] = {vector->x, vector->y, vector->z};
        for (int index = 0; index < count && index < 3; ++index) {
            values[index] = source[index];
        }
        return;
    }
    if (const Vec4* vector = value.asVector4()) {
        const float source[4] = {vector->x, vector->y, vector->z, vector->w};
        for (int index = 0; index < count && index < 4; ++index) {
            values[index] = source[index];
        }
    }
}

std::string makeVectorLiteral(int count, const float* values) {
    std::ostringstream stream;
    stream << "vec" << count << "(";
    for (int index = 0; index < count; ++index) {
        if (index != 0) {
            stream << ", ";
        }
        stream << formatFloat(values[index]);
    }
    stream << ")";
    return stream.str();
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

std::string varName(NodeId node, SlotId slot) {
    return "n" + std::to_string(node) + "_s" + std::to_string(slot);
}

std::string replaceTokens(std::string body, const std::map<SlotId, std::string>& values) {
    std::vector<SlotId> ids;
    for (const auto& [id, unused] : values) {
        (void)unused;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end(), [](SlotId lhs, SlotId rhs) { return lhs > rhs; });
    for (SlotId id : ids) {
        const std::string token = "$" + std::to_string(id);
        const std::string& replacement = values.at(id);
        std::string::size_type position = 0;
        while ((position = body.find(token, position)) != std::string::npos) {
            body.replace(position, token.size(), replacement);
            position += replacement.size();
        }
    }
    return body;
}

std::string comparisonOperator(std::string_view value) {
    if (value == "not equal" || value == "notEqual" || value == "!=") {
        return "!=";
    }
    if (value == "less" || value == "<") {
        return "<";
    }
    if (value == "less or equal" || value == "lessOrEqual" || value == "<=") {
        return "<=";
    }
    if (value == "greater" || value == ">") {
        return ">";
    }
    if (value == "greater or equal" || value == "greaterOrEqual" || value == ">=") {
        return ">=";
    }
    return "==";
}

class Emitter {
  public:
    Emitter(const Graph& graph, const NodeRegistry& registry, const ActiveGraph& active,
            const TargetSpec& target, const PropertySet& properties, const KeywordSet& keywords,
            const VariantInfo& variant)
        : m_graph(graph),
          m_registry(registry),
          m_active(active),
          m_target(target),
          m_properties(properties),
          m_keywords(keywords),
          m_variant(variant) {}

    EmittedGraph run() {
        EmittedGraph result;
        const bool sceneColor = usesSceneColor();
        const bool sceneDepth = usesSceneDepth();
        const bool fog = usesFog();
        const bool modelInverse = usesModelInverse();
        if (sceneDepth) {
            result.requiredFeatureMask |= kGraphFeatureSceneDepth;
        }
        if (sceneColor) {
            result.requiredFeatureMask |= kGraphFeatureSceneColor;
        }
        if (fog) {
            result.requiredFeatureMask |= kGraphFeatureFog;
        }
        int nextStage = 0;
        for (const PropertyInfo& property : m_properties.properties) {
            if (property.samplerStage >= 0) {
                nextStage = std::max(nextStage, property.samplerStage + 1);
            }
        }
        const int sceneColorStage = sceneColor ? nextStage++ : -1;
        const int sceneDepthStage = sceneDepth ? nextStage++ : -1;
        if (nextStage > m_target.textureBudget) {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorTargetUnsupported;
            diagnostic.message =
                "Scene Color/Depth require free graph texture stages; increase the target "
                "texture budget or remove graph texture properties";
            result.diagnostics.push_back(std::move(diagnostic));
            result.ok = false;
            return result;
        }
        std::ostringstream source;
        const bool vertexDisplacement = hasVertexDisplacement(m_graph, m_registry, m_active);
        if (vertexDisplacement) {
            bool vertexStageError = false;
            for (NodeId id : m_active.order) {
                const NodeInstance* node = m_graph.findNode(id);
                if (node == nullptr || nodeTypeKey(node->type) != "SampleVirtualTexture") {
                    continue;
                }
                const auto vtSource = virtualTextureSource(m_graph, *node);
                if (!vtSource) {
                    continue;
                }
                if (vtSource->noFeedback && vtSource->lodCalculation == "lod") {
                    continue;
                }
                if (!reachesPositionBlock(id)) {
                    continue;
                }
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorTargetUnsupported;
                diagnostic.message =
                    "Sample Virtual Texture output reaches vertex displacement but is "
                    "fragment-only; enable No Feedback and Lod Level";
                diagnostic.node = id;
                result.diagnostics.push_back(std::move(diagnostic));
                vertexStageError = true;
            }
            if (vertexStageError) {
                result.ok = false;
                return result;
            }
            source << "#define HBG_SG_VERTEX_DISPLACEMENT 1\n";
        }
        source << "#include \"common/common.sc\"\n";
        if (modelInverse) {
            source << "uniform mat4 u_sg_modelInverse;\n";
        }
        if (usesEnvironment()) {
            source << "#include \"lighting/environment.sc\"\n";
        }
        if (sceneColor) {
            source << "SAMPLER2D(s_sceneColor, " << sceneColorStage << ");\n";
        }
        if (sceneDepth) {
            source << "SAMPLER2D(s_sceneDepth, " << sceneDepthStage << ");\n";
        }
        source << "\n";
        source << "struct GraphSurfaceInputs\n{\n";
        source << "    vec2 uv0;\n    vec2 uv1;\n";
        source << "    vec3 positionOS;\n    vec3 positionWS;\n";
        source << "    vec3 normalOS;\n    vec3 normalWS;\n";
        source << "    vec3 tangentOS;\n    vec3 tangentWS;\n";
        source << "    vec3 bitangentOS;\n    vec3 bitangentWS;\n";
        source << "    vec3 viewDirWS;\n    vec4 screenPos;\n";
        source << "    vec4 vertexColor;\n    float time;\n    float deltaTime;\n    float faceSign;\n";
        source << "    float vertexID;\n    float instanceID;\n";
        source << "};\n\n";
        source << "struct GraphSurface\n{\n";
        source << "    vec3 positionOS;\n    vec3 baseColor;\n    float alpha;\n    float metallic;\n";
        source << "    float smoothness;\n    vec3 normalWS;\n    float occlusion;\n";
        source << "    vec3 emission;\n    float alphaClipThreshold;\n};\n\n";

        for (const PropertyInfo& property : m_properties.properties) {
            if (!property.declaration.empty()) {
                source << property.declaration << "\n";
            }
        }
        if (usesGlobalMipBias()) {
            source << "uniform vec4 u_sg_globalMipBias;\n";
        }
        source << "\n";

        std::ostringstream declarations;
        std::ostringstream body;
        for (NodeId id : m_active.order) {
            emitNode(id, declarations, body);
        }

        for (NodeId id : m_active.order) {
            const NodeInstance* node = m_graph.findNode(id);
            if (node == nullptr) {
                continue;
            }
            const NodeDef* definition = m_registry.find(node->type);
            if (definition == nullptr || !definition->blockNode) {
                continue;
            }
            emitBlock(*node, *definition, body);
        }

        const std::string functionSource = m_functions.render();
        source << functionSource;
        source << "GraphSurface hbgEvaluateGraphSurface(GraphSurfaceInputs gi)\n{\n";
        source << "    GraphSurface surface;\n";
        source << "    surface.positionOS = gi.positionOS;\n";
        source << "    surface.baseColor = " << defaultField("baseColor", ConcreteValueType::Vector3) << ";\n";
        source << "    surface.alpha = " << defaultField("alpha", ConcreteValueType::Float) << ";\n";
        source << "    surface.metallic = " << defaultField("metallic", ConcreteValueType::Float) << ";\n";
        source << "    surface.smoothness = " << defaultField("smoothness", ConcreteValueType::Float) << ";\n";
        source << "    surface.normalWS = normalize(gi.normalWS);\n";
        source << "    surface.occlusion = " << defaultField("occlusion", ConcreteValueType::Float) << ";\n";
        source << "    surface.emission = " << defaultField("emission", ConcreteValueType::Vector3) << ";\n";
        source << "    surface.alphaClipThreshold = "
               << defaultField("alphaClipThreshold", ConcreteValueType::Float) << ";\n";
        source << declarations.str();
        source << body.str();
        source << "    return surface;\n}\n";

        result.ok = !hasError(m_diagnostics);
        result.source = source.str();
        result.functionSource = functionSource;
        result.diagnostics = std::move(m_diagnostics);
        result.requirements = {"uv0", "worldPosition", "normal", "tangent", "bitangent"};
        return result;
    }

  private:
    std::string defaultField(const char* name, ConcreteValueType type) const {
        const auto found = m_target.defaults.find(name);
        if (found == m_target.defaults.end()) {
            return literal(Value(ValueType::Float, 0.0f), type, ConcretePrecision::Single);
        }
        return literal(found->second, type, ConcretePrecision::Single);
    }

    std::string settingString(const NodeInstance& node, const char* key) const {
        const auto found = node.settings.find(key);
        if (found == node.settings.end()) {
            return {};
        }
        if (const std::string* value = found->second.asString()) {
            return *value;
        }
        return {};
    }

    bool settingBool(const NodeInstance& node, const char* key, bool fallback) const {
        const auto found = node.settings.find(key);
        if (found == node.settings.end()) {
            return fallback;
        }
        if (const bool* value = found->second.asBool()) {
            return *value;
        }
        return fallback;
    }

    Vec3 settingVector3(const NodeInstance& node, const char* key, Vec3 fallback) const {
        const auto found = node.settings.find(key);
        if (found == node.settings.end()) {
            return fallback;
        }
        if (const Vec3* value = found->second.asVector3()) {
            return *value;
        }
        return fallback;
    }

    float settingFloat(const NodeInstance& node, const char* key, float fallback) const {
        const auto found = node.settings.find(key);
        if (found == node.settings.end()) {
            return fallback;
        }
        if (const float* value = found->second.asFloat()) {
            return *value;
        }
        if (const std::int64_t* value = found->second.asInteger()) {
            return static_cast<float>(*value);
        }
        return fallback;
    }

    Vec4 settingVector4(const NodeInstance& node, const char* key, Vec4 fallback) const {
        const auto found = node.settings.find(key);
        if (found == node.settings.end()) {
            return fallback;
        }
        if (const Vec4* value = found->second.asVector4()) {
            return *value;
        }
        if (const Vec3* value = found->second.asVector3()) {
            return Vec4{value->x, value->y, value->z, 0.0f};
        }
        if (const Vec2* value = found->second.asVector2()) {
            return Vec4{value->x, value->y, 0.0f, 0.0f};
        }
        return fallback;
    }

    int settingInteger(const NodeInstance& node, const char* key, int fallback) const {
        const auto found = node.settings.find(key);
        if (found == node.settings.end()) {
            return fallback;
        }
        if (const std::int64_t* value = found->second.asInteger()) {
            return static_cast<int>(*value);
        }
        if (const float* value = found->second.asFloat()) {
            return static_cast<int>(*value);
        }
        if (const std::string* value = found->second.asString()) {
            char* end = nullptr;
            const long parsed = std::strtol(value->c_str(), &end, 10);
            if (end != value->c_str() && *end == '\0') {
                return static_cast<int>(parsed);
            }
        }
        return fallback;
    }

    std::string normalizedSetting(const NodeInstance& node, const char* key,
                                  const char* fallback) const {
        std::string result;
        const std::string text = settingString(node, key);
        for (char character : text) {
            if (character >= 'A' && character <= 'Z') {
                character = static_cast<char>(character - 'A' + 'a');
            }
            if ((character >= 'a' && character <= 'z') ||
                (character >= '0' && character <= '9')) {
                result.push_back(character);
            }
        }
        return result.empty() ? std::string(fallback) : result;
    }

    int channelMaskSetting(const NodeInstance& node) const {
        const auto found = node.settings.find("channelMask");
        if (found == node.settings.end()) {
            return -1;
        }
        if (const std::int64_t* value = found->second.asInteger()) {
            return static_cast<int>(*value);
        }
        if (const float* value = found->second.asFloat()) {
            return static_cast<int>(*value);
        }
        const std::string* text = found->second.asString();
        if (text == nullptr) {
            return -1;
        }
        bool numeric = !text->empty();
        for (char character : *text) {
            if ((character < '0' || character > '9') && character != '-' && character != '+') {
                numeric = false;
                break;
            }
        }
        if (numeric) {
            return static_cast<int>(std::strtol(text->c_str(), nullptr, 10));
        }
        int mask = 0;
        for (char character : *text) {
            if (character == 'r' || character == 'R') {
                mask |= 1;
            } else if (character == 'g' || character == 'G') {
                mask |= 2;
            } else if (character == 'b' || character == 'B') {
                mask |= 4;
            } else if (character == 'a' || character == 'A') {
                mask |= 8;
            }
        }
        return mask;
    }

    void provideFadeTransition() {
        m_functions.provide(
            "hbgSGFadeTransition",
            "float hbgSGFadeTransition(float noise, float fadeValue, float fadeContrast)\n"
            "{\n"
            "    return saturate(fadeValue * (fadeContrast + 1.0) + (noise - 1.0) * fadeContrast);\n"
            "}\n");
    }

    bool usesEnvironment() const {
        for (NodeId id : m_active.order) {
            const NodeInstance* node = m_graph.findNode(id);
            if (node != nullptr && nodeTypeKey(node->type) == "ReflectionProbe") {
                return true;
            }
        }
        return false;
    }

    bool usesSceneColor() const {
        for (NodeId id : m_active.order) {
            const NodeInstance* node = m_graph.findNode(id);
            if (node != nullptr && nodeTypeKey(node->type) == "SceneColor") {
                return true;
            }
        }
        return false;
    }

    bool usesSceneDepth() const {
        for (NodeId id : m_active.order) {
            const NodeInstance* node = m_graph.findNode(id);
            if (node == nullptr) {
                continue;
            }
            const std::string type = nodeTypeKey(node->type);
            if (type == "SceneDepth" || type == "SceneDepthDifference") {
                return true;
            }
        }
        return false;
    }

    bool usesFog() const {
        for (NodeId id : m_active.order) {
            const NodeInstance* node = m_graph.findNode(id);
            if (node != nullptr && nodeTypeKey(node->type) == "Fog") {
                return true;
            }
        }
        return false;
    }

    bool usesModelInverse() const {
        for (NodeId id : m_active.order) {
            const NodeInstance* node = m_graph.findNode(id);
            if (node == nullptr) {
                continue;
            }
            const std::string type = nodeTypeKey(node->type);
            if (type == "TransformationMatrix") {
                const std::string kind = normalizedSetting(*node, "matrixType", "model");
                if (kind == "inversemodel" || kind == "worldtoobject") {
                    return true;
                }
                if (settingString(*node, "matrixType").empty() &&
                    node->setting("matrixType") != nullptr &&
                    settingInteger(*node, "matrixType", -1) == 1) {
                    return true;
                }
            } else if (type == "Transform") {
                const std::string from = coordinateSpace(*node, "from", "object");
                const std::string to = coordinateSpace(*node, "to", "world");
                if ((from == "world" || from == "view" || from == "tangent") &&
                    to == "object") {
                    return true;
                }
                if (normalizedSetting(*node, "type", "position") == "normal" &&
                    from == "object" && to != "object") {
                    return true;
                }
            } else if (type == "ViewDirection" || type == "ViewVector") {
                if (coordinateSpace(*node, "space", "world") == "object") {
                    return true;
                }
            }
        }
        return false;
    }

    void provideSceneColor() {
        m_functions.provide(
            "hbgSGSampleSceneColor",
            "vec3 hbgSGSampleSceneColor(vec2 uv)\n"
            "{\n"
            "    return max(texture2D(s_sceneColor, uv).rgb, vec3(0.0, 0.0, 0.0));\n"
            "}\n");
    }

    void provideSceneDepth() {
        m_functions.provide(
            "hbgSGLinearEyeDepth",
            "float hbgSGLinearEyeDepth(float rawDepth, vec2 uv)\n"
            "{\n"
            "    vec4 worldPosition = mul(u_global_invViewProj, vec4(uv * 2.0 - 1.0, rawDepth, 1.0));\n"
            "    worldPosition.xyz /= max(worldPosition.w, 1.0e-6);\n"
            "    vec4 viewPosition = mul(u_global_view, vec4(worldPosition.xyz, 1.0));\n"
            "    return max(-viewPosition.z, 0.0);\n"
            "}\n");
        m_functions.provide(
            "hbgSGCameraNearFar",
            "vec2 hbgSGCameraNearFar()\n"
            "{\n"
            "    float nearPlane = max(-u_global_frustumPlanes[4].w - "
            "dot(u_global_frustumPlanes[4].xyz, u_global_cameraPos.xyz), 0.0);\n"
            "    float farPlane = max(u_global_frustumPlanes[5].w - "
            "dot(u_global_frustumPlanes[5].xyz, u_global_cameraPos.xyz), nearPlane + 1.0e-5);\n"
            "    return vec2(nearPlane, farPlane);\n"
            "}\n");
        m_functions.provide(
            "hbgSGLinear01Depth",
            "float hbgSGLinear01Depth(float rawDepth, vec2 uv)\n"
            "{\n"
            "    const vec2 nearFar = hbgSGCameraNearFar();\n"
            "    const float eye = hbgSGLinearEyeDepth(rawDepth, uv);\n"
            "    return clamp((eye - nearFar.x) / max(nearFar.y - nearFar.x, 1.0e-5), 0.0, 1.0);\n"
            "}\n");
        m_functions.provide(
            "hbgSGSampleSceneDepth",
            "float hbgSGSampleSceneDepth(vec2 uv)\n"
            "{\n"
            "    return texture2D(s_sceneDepth, uv).r;\n"
            "}\n");
    }

    void provideFog() {
        m_functions.provide(
            "hbgSGFog",
            "void hbgSGFog(vec3 positionOS, out vec4 color, out float density)\n"
            "{\n"
            "    const vec3 positionWS = mul(u_model[0], vec4(positionOS, 1.0)).xyz;\n"
            "    const float extinction = max(u_fog_params.z, 0.0);\n"
            "    density = extinction * exp(-(positionWS.y - u_fog_params.x) * "
            "max(u_fog_params.y, 1.0e-6)) * step(0.5, u_fog_params2.z);\n"
            "    const vec3 ambient = max(u_pass_ambientColor.rgb * u_pass_ambientColor.w, "
            "vec3(0.0, 0.0, 0.0));\n"
            "    color = vec4(max(u_fog_albedo.rgb, vec3(0.0, 0.0, 0.0)) * ambient, 1.0);\n"
            "}\n");
    }

    void provideAmbientSH() {
        m_functions.provide(
            "hbgSGSampleSH9",
            "vec3 hbgSGSampleSH9(vec3 normal)\n"
            "{\n"
            "    const vec4 vA = vec4(normal, 1.0);\n"
            "    vec3 result;\n"
            "    result.r = dot(u_environment_shAr, vA);\n"
            "    result.g = dot(u_environment_shAg, vA);\n"
            "    result.b = dot(u_environment_shAb, vA);\n"
            "    const vec4 vB = normal.xyzz * normal.yzzx;\n"
            "    vec3 quadratic;\n"
            "    quadratic.r = dot(u_environment_shBr, vB);\n"
            "    quadratic.g = dot(u_environment_shBg, vB);\n"
            "    quadratic.b = dot(u_environment_shBb, vB);\n"
            "    const float vC = normal.x * normal.x - normal.y * normal.y;\n"
            "    result += quadratic + u_environment_shCr.xyz * vC;\n"
            "    return max(result, vec3(0.0, 0.0, 0.0));\n"
            "}\n");
        m_functions.provide(
            "hbgSGSampleAmbient",
            "vec3 hbgSGSampleAmbient(vec3 normal)\n"
            "{\n"
            "    const vec3 ambient = u_pass_ambientColor.rgb * u_pass_ambientColor.w;\n"
            "    if (u_environment_skyParams.z < 0.5) {\n"
            "        return ambient;\n"
            "    }\n"
            "    return ambient + hbgSGSampleSH9(normal) * u_environment_skyParams.x;\n"
            "}\n");
    }

    void provideUnpackNormal() {
        m_functions.provide(
            "hbgSGUnpackNormal",
            "vec3 hbgSGUnpackNormal(vec4 packedNormal)\n"
            "{\n"
            "    float packedA = packedNormal.w * packedNormal.x;\n"
            "    vec2 xy = vec2(packedA, packedNormal.y) * 2.0 - 1.0;\n"
            "    return vec3(xy.x, xy.y,\n"
            "                sqrt(max(1.0e-16, 1.0 - saturate(dot(xy, xy)))));\n"
            "}\n");
    }

    void provideDisplacementObjectScale() {
        m_functions.provide(
            "hbgSGDisplacementObjectScale",
            "vec3 hbgSGDisplacementObjectScale()\n"
            "{\n"
            "    return vec3(1.0 / max(length(u_model[0][0].xyz), 1.192092896e-07),\n"
            "                1.0 / max(length(u_model[0][1].xyz), 1.192092896e-07),\n"
            "                1.0 / max(length(u_model[0][2].xyz), 1.192092896e-07));\n"
            "}\n");
    }

    bool hasExplicitInput(const NodeInstance& node, SlotId slot) const {
        const SlotInstance* instance = node.findSlot(slot);
        return m_graph.incomingEdge(node.id, slot) != nullptr ||
               (instance != nullptr && instance->hasExplicitValue());
    }

    int channelIndexSetting(const NodeInstance& node, int fallback) const {
        if (const Value* value = node.setting("channel")) {
            if (const std::int64_t* integer = value->asInteger()) {
                return std::clamp(static_cast<int>(*integer), 0, 3);
            }
            if (const float* scalar = value->asFloat()) {
                return std::clamp(static_cast<int>(*scalar), 0, 3);
            }
        }
        const char* fallbackName = fallback == 1 ? "green"
                                  : fallback == 2 ? "blue"
                                  : fallback == 3 ? "alpha"
                                                  : "red";
        const std::string value = normalizedSetting(node, "channel", fallbackName);
        if (value == "green") {
            return 1;
        }
        if (value == "blue") {
            return 2;
        }
        if (value == "alpha") {
            return 3;
        }
        if (value == "red") {
            return 0;
        }
        return fallback;
    }

    std::string coordinateSpace(const NodeInstance& node, const char* key,
                                const char* fallback) const {
        std::string value = normalizedSetting(node, key, fallback);
        if (value == "absoluteworld") {
            return "world";
        }
        if (value != "object" && value != "view" && value != "world" && value != "tangent") {
            return fallback;
        }
        return value;
    }

    std::string positionInput(const NodeInstance& node,
                              const std::map<SlotId, std::string>& inputs,
                              const std::string& space) const {
        if (hasExplicitInput(node, 3)) {
            return inputs.at(3);
        }
        if (space == "object") {
            return "gi.positionOS";
        }
        if (space == "view") {
            return "mul(u_global_view, vec4(gi.positionWS, 1.0)).xyz";
        }
        return "gi.positionWS";
    }

    std::string normalInput(const NodeInstance& node,
                            const std::map<SlotId, std::string>& inputs,
                            const std::string& space) const {
        if (hasExplicitInput(node, 4)) {
            return inputs.at(4);
        }
        if (space == "object") {
            return "normalize(gi.normalOS)";
        }
        if (space == "view") {
            return "normalize(mul(u_global_view, vec4(gi.normalWS, 0.0)).xyz)";
        }
        if (space == "tangent") {
            return "normalize(vec3(dot(gi.normalWS, gi.tangentWS),\n"
                   "                    dot(gi.normalWS, gi.bitangentWS),\n"
                   "                    dot(gi.normalWS, gi.normalWS)))";
        }
        return "normalize(gi.normalWS)";
    }

    std::string noiseHashType(const NodeInstance& node) const {
        const std::string value = settingString(node, "hashType");
        if (value == "legacySine" || value == "legacyMod") {
            return value;
        }
        return "deterministic";
    }

    std::string hash21Name(const std::string& hashType) const {
        if (hashType == "legacySine") {
            return "hbgSGHashLegacySine21";
        }
        if (hashType == "legacyMod") {
            return "hbgSGHashLegacyMod21";
        }
        return "hbgSGHashTchou21";
    }

    std::string hash22Name(const std::string& hashType) const {
        return hashType == "legacySine" ? "hbgSGHashLegacySine22" : "hbgSGHashTchou22";
    }

    void provideNoiseHash21(const std::string& hashType) {
        if (hashType == "legacySine") {
            m_functions.provide(
                "hbgSGHashLegacySine21",
                "float hbgSGHashLegacySine21(vec2 i)\n"
                "{\n"
                "    float angle = dot(i, vec2(12.9898, 78.233));\n"
                "    return frac(sin(angle) * 43758.5453);\n"
                "}\n");
            return;
        }
        if (hashType == "legacyMod") {
            m_functions.provide(
                "hbgSGHashLegacyMod21",
                "float hbgSGHashLegacyMod21(vec2 i)\n"
                "{\n"
                "    vec2 v = vec2(fmod(i.x, 289.0), fmod(i.y, 289.0));\n"
                "    float x = fmod((34.0 * v.x + 1.0) * v.x, 289.0) + v.y;\n"
                "    x = fmod((34.0 * x + 1.0) * x, 289.0);\n"
                "    return frac(x / 41.0) * 2.0 - 1.0;\n"
                "}\n");
            return;
        }
        m_functions.provide(
            "hbgSGHashTchou21",
            "float hbgSGHashTchou21(vec2 i)\n"
            "{\n"
            "    uint vx = uint(int(round(i.x)));\n"
            "    uint vy = uint(int(round(i.y)));\n"
            "    vy ^= 1103515245u;\n"
            "    vx += vy;\n"
            "    vx *= vy;\n"
            "    vx ^= vx >> 5u;\n"
            "    vx *= 668265261u;\n"
            "    return float(vx >> 8u) * (1.0 / 16777215.0);\n"
            "}\n");
    }

    void provideNoiseHash22(const std::string& hashType) {
        if (hashType == "legacySine") {
            m_functions.provide(
                "hbgSGHashLegacySine22",
                "vec2 hbgSGHashLegacySine22(vec2 i)\n"
                "{\n"
                "    vec2 angles = vec2(i.x * 15.27 + i.y * 99.41,\n"
                "                       i.x * 47.63 + i.y * 89.98);\n"
                "    return frac(sin(angles));\n"
                "}\n");
            return;
        }
        m_functions.provide(
            "hbgSGHashTchou22",
            "vec2 hbgSGHashTchou22(vec2 i)\n"
            "{\n"
            "    uint vx = uint(int(round(i.x)));\n"
            "    uint vy = uint(int(round(i.y)));\n"
            "    vy ^= 1103515245u;\n"
            "    vx += vy;\n"
            "    vx *= vy;\n"
            "    vx ^= vx >> 5u;\n"
            "    vx *= 668265261u;\n"
            "    vy ^= (vx << 3u);\n"
            "    return vec2(float(vx >> 8u), float(vy >> 8u)) * (1.0 / 16777215.0);\n"
            "}\n");
    }

    void provideSimpleNoise(const std::string& suffix, const std::string& hashName) {
        const std::string valueName = "hbgSGSimpleNoiseValue" + suffix;
        const std::string noiseName = "hbgSGSimpleNoise" + suffix;
        m_functions.provide(
            valueName,
            "float " + valueName + "(vec2 uv)\n"
            "{\n"
            "    vec2 i = floor(uv);\n"
            "    vec2 f = frac(uv);\n"
            "    f = f * f * (3.0 - 2.0 * f);\n"
            "    float r0 = " + hashName + "(i);\n"
            "    float r1 = " + hashName + "(i + vec2(1.0, 0.0));\n"
            "    float r2 = " + hashName + "(i + vec2(0.0, 1.0));\n"
            "    float r3 = " + hashName + "(i + vec2(1.0, 1.0));\n"
            "    float bottom = lerp(r0, r1, f.x);\n"
            "    float top = lerp(r2, r3, f.x);\n"
            "    return lerp(bottom, top, f.y);\n"
            "}\n");
        m_functions.provide(
            noiseName,
            "float " + noiseName + "(vec2 uv, float scale)\n"
            "{\n"
            "    float value = 0.0;\n"
            "    value += " + valueName + "(uv * (scale / 1.0)) * 0.125;\n"
            "    value += " + valueName + "(uv * (scale / 2.0)) * 0.25;\n"
            "    value += " + valueName + "(uv * (scale / 4.0)) * 0.5;\n"
            "    return value;\n"
            "}\n");
    }

    void provideGradientNoise(const std::string& suffix, const std::string& hashName) {
        const std::string dirName = "hbgSGGradientNoiseDir" + suffix;
        const std::string noiseName = "hbgSGGradientNoise" + suffix;
        m_functions.provide(
            dirName,
            "vec2 " + dirName + "(vec2 p)\n"
            "{\n"
            "    float x = " + hashName + "(p);\n"
            "    return normalize(vec2(x - floor(x + 0.5), abs(x) - 0.5));\n"
            "}\n");
        m_functions.provide(
            noiseName,
            "float " + noiseName + "(vec2 uv, float scale)\n"
            "{\n"
            "    vec2 p = uv * scale;\n"
            "    vec2 ip = floor(p);\n"
            "    vec2 fp = frac(p);\n"
            "    float d00 = dot(" + dirName + "(ip), fp);\n"
            "    float d01 = dot(" + dirName + "(ip + vec2(0.0, 1.0)), fp - vec2(0.0, 1.0));\n"
            "    float d10 = dot(" + dirName + "(ip + vec2(1.0, 0.0)), fp - vec2(1.0, 0.0));\n"
            "    float d11 = dot(" + dirName + "(ip + vec2(1.0, 1.0)), fp - vec2(1.0, 1.0));\n"
            "    fp = fp * fp * fp * (fp * (fp * 6.0 - 15.0) + 10.0);\n"
            "    return lerp(lerp(d00, d01, fp.y), lerp(d10, d11, fp.y), fp.x) + 0.5;\n"
            "}\n");
    }

    void provideGradientHelpers() {
        m_functions.provide(
            "hbgSGGradient",
            "struct HbgSGGradient\n"
            "{\n"
            "    int type;\n"
            "    int colorsLength;\n"
            "    int alphasLength;\n"
            "    vec4 colors[8];\n"
            "    vec2 alphas[8];\n"
            "};\n"
            "HbgSGGradient hbgSGNewGradient(int type, int colorsLength, int alphasLength,\n"
            "    vec4 colors0, vec4 colors1, vec4 colors2, vec4 colors3,\n"
            "    vec4 colors4, vec4 colors5, vec4 colors6, vec4 colors7,\n"
            "    vec2 alphas0, vec2 alphas1, vec2 alphas2, vec2 alphas3,\n"
            "    vec2 alphas4, vec2 alphas5, vec2 alphas6, vec2 alphas7)\n"
            "{\n"
            "    HbgSGGradient output;\n"
            "    output.type = type;\n"
            "    output.colorsLength = colorsLength;\n"
            "    output.alphasLength = alphasLength;\n"
            "    output.colors[0] = colors0;\n"
            "    output.colors[1] = colors1;\n"
            "    output.colors[2] = colors2;\n"
            "    output.colors[3] = colors3;\n"
            "    output.colors[4] = colors4;\n"
            "    output.colors[5] = colors5;\n"
            "    output.colors[6] = colors6;\n"
            "    output.colors[7] = colors7;\n"
            "    output.alphas[0] = alphas0;\n"
            "    output.alphas[1] = alphas1;\n"
            "    output.alphas[2] = alphas2;\n"
            "    output.alphas[3] = alphas3;\n"
            "    output.alphas[4] = alphas4;\n"
            "    output.alphas[5] = alphas5;\n"
            "    output.alphas[6] = alphas6;\n"
            "    output.alphas[7] = alphas7;\n"
            "    return output;\n"
            "}\n"
            "vec3 hbgSGLinearToOklab(vec3 rgb)\n"
            "{\n"
            "    float l = 0.4122214708 * rgb.r + 0.5363325363 * rgb.g + 0.0514459929 * rgb.b;\n"
            "    float m = 0.2119034982 * rgb.r + 0.6806995451 * rgb.g + 0.1073969566 * rgb.b;\n"
            "    float s = 0.0883024619 * rgb.r + 0.2817188376 * rgb.g + 0.6299787005 * rgb.b;\n"
            "    float l_ = pow(l, 0.333333);\n"
            "    float m_ = pow(m, 0.333333);\n"
            "    float s_ = pow(s, 0.333333);\n"
            "    return vec3(0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_,\n"
            "                1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_,\n"
            "                0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_);\n"
            "}\n"
            "vec3 hbgSGOklabToLinear(vec3 lab)\n"
            "{\n"
            "    float l_ = lab.r + 0.3963377774 * lab.g + 0.2158037573 * lab.b;\n"
            "    float m_ = lab.r - 0.1055613458 * lab.g - 0.0638541728 * lab.b;\n"
            "    float s_ = lab.r - 0.0894841775 * lab.g - 1.2914855480 * lab.b;\n"
            "    float l = l_ * l_ * l_;\n"
            "    float m = m_ * m_ * m_;\n"
            "    float s = s_ * s_ * s_;\n"
            "    return vec3(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,\n"
            "                -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,\n"
            "                -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s);\n"
            "}\n"
            "vec4 hbgSGSampleGradient(HbgSGGradient gradient, float time)\n"
            "{\n"
            "    float perceptual = gradient.type == 2 ? 1.0 : 0.0;\n"
            "    float fixed = float(gradient.type % 2);\n"
            "    vec3 color = lerp(gradient.colors[0].rgb, hbgSGLinearToOklab(gradient.colors[0].rgb), perceptual);\n"
            "    for (int c = 1; c < 8; ++c)\n"
            "    {\n"
            "        float colorPos = saturate((time - gradient.colors[c - 1].w) / (gradient.colors[c].w - gradient.colors[c - 1].w)) * step(float(c), float(gradient.colorsLength - 1));\n"
            "        vec3 color2 = lerp(gradient.colors[c].rgb, hbgSGLinearToOklab(gradient.colors[c].rgb), perceptual);\n"
            "        color = lerp(color, color2, lerp(colorPos, step(0.01, colorPos), fixed));\n"
            "    }\n"
            "    color = lerp(color, hbgSGOklabToLinear(color), perceptual);\n"
            "    float alpha = gradient.alphas[0].x;\n"
            "    for (int a = 1; a < 8; ++a)\n"
            "    {\n"
            "        float alphaPos = saturate((time - gradient.alphas[a - 1].y) / (gradient.alphas[a].y - gradient.alphas[a - 1].y)) * step(float(a), float(gradient.alphasLength - 1));\n"
            "        alpha = lerp(alpha, gradient.alphas[a].x, lerp(alphaPos, step(0.01, alphaPos), fixed));\n"
            "    }\n"
            "    return vec4(color, alpha);\n"
            "}\n");
    }

    void provideTextureSamplingHelpers() {
        m_functions.provide(
            "hbgSGSampleTexture2DBias",
            "vec4 hbgSGSampleTexture2DBias(sampler2D textureSampler, vec2 uv, float bias)\n"
            "{\n"
            "#if BGFX_SHADER_TYPE_FRAGMENT\n"
            "    return texture2DBias(textureSampler, uv, bias);\n"
            "#else\n"
            "    return texture2DLod(textureSampler, uv, max(bias, 0.0));\n"
            "#endif\n"
            "}\n");
        m_functions.provide(
            "hbgSGSampleTexture2DGrad",
            "vec4 hbgSGSampleTexture2DGrad(sampler2D textureSampler, vec2 uv, vec2 dx, vec2 dy)\n"
            "{\n"
            "#if BGFX_SHADER_TYPE_FRAGMENT\n"
            "    return texture2DGrad(textureSampler, uv, dx, dy);\n"
            "#else\n"
            "    return texture2DLod(textureSampler, uv, 0.0);\n"
            "#endif\n"
            "}\n");
        m_functions.provide(
            "hbgSGSampleTexture2DArrayBias",
            "vec4 hbgSGSampleTexture2DArrayBias(sampler2DArray textureSampler, vec3 uvw, float bias)\n"
            "{\n"
            "#if BGFX_SHADER_TYPE_FRAGMENT\n"
            "    float lod = max(0.0, log2(max(length(ddx(uvw.xy)), length(ddy(uvw.xy))))) + bias;\n"
            "    return texture2DArrayLod(textureSampler, uvw, lod);\n"
            "#else\n"
            "    return texture2DArrayLod(textureSampler, uvw, max(bias, 0.0));\n"
            "#endif\n"
            "}\n");
        m_functions.provide(
            "hbgSGSampleTexture2DArrayGrad",
            "vec4 hbgSGSampleTexture2DArrayGrad(sampler2DArray textureSampler, vec3 uvw, vec2 dx, vec2 dy)\n"
            "{\n"
            "#if BGFX_SHADER_TYPE_FRAGMENT\n"
            "    float lod = max(0.0, log2(max(length(dx), length(dy))));\n"
            "    return texture2DArrayLod(textureSampler, uvw, lod);\n"
            "#else\n"
            "    return texture2DArrayLod(textureSampler, uvw, 0.0);\n"
            "#endif\n"
            "}\n");
    }

    void provideVirtualTextureSamplingHelpers() {
        m_functions.provide(
            "hbgSGVirtualTextureBiasLod",
            "float hbgSGVirtualTextureBiasLod(vec2 uv, float bias)\n"
            "{\n"
            "#if BGFX_SHADER_TYPE_FRAGMENT\n"
            "    float lod = max(0.0, log2(max(length(ddx(uv)), length(ddy(uv)))));\n"
            "    return max(0.0, lod + bias);\n"
            "#else\n"
            "    return max(bias, 0.0);\n"
            "#endif\n"
            "}\n");
        m_functions.provide(
            "hbgSGVirtualTextureDerivativeLod",
            "float hbgSGVirtualTextureDerivativeLod(vec2 dx, vec2 dy)\n"
            "{\n"
            "#if BGFX_SHADER_TYPE_FRAGMENT\n"
            "    return max(0.0, log2(max(length(dx), length(dy))));\n"
            "#else\n"
            "    return 0.0;\n"
            "#endif\n"
            "}\n");
    }

    void provideHDRDecodeHelper() {
        m_functions.provide(
            "hbgSGDecodeHDRSample",
            "vec4 hbgSGDecodeHDRSample(vec4 encoded, vec4 decodeInstructions)\n"
            "{\n"
            "    float alpha = decodeInstructions.w * (encoded.a - 1.0) + 1.0;\n"
            "#if defined(UNITY_COLORSPACE_GAMMA)\n"
            "    encoded.rgb = (decodeInstructions.x * alpha) * encoded.rgb;\n"
            "#else\n"
            "    encoded.rgb = (decodeInstructions.x * pow(abs(alpha), decodeInstructions.y)) * encoded.rgb;\n"
            "#endif\n"
            "    encoded.a = 1.0;\n"
            "    return encoded;\n"
            "}\n");
    }

    void provideScreenTransform() {
        m_functions.provide(
            "hbgSGWorldFromScreen",
            "vec3 hbgSGWorldFromScreen(vec3 screen)\n"
            "{\n"
            "    vec4 clip = vec4(screen.xy * 2.0 - 1.0, screen.z, 1.0);\n"
            "    vec4 world = mul(u_global_invViewProj, clip);\n"
            "    return world.xyz / world.w;\n"
            "}\n");
        m_functions.provide(
            "hbgSGWorldDirectionFromScreen",
            "vec3 hbgSGWorldDirectionFromScreen(vec3 screen)\n"
            "{\n"
            "    vec4 clip = vec4(screen.xy * 2.0 - 1.0, screen.z, 0.0);\n"
            "    return mul(u_global_invViewProj, clip).xyz;\n"
            "}\n");
        m_functions.provide(
            "hbgSGWorldToScreen",
            "vec3 hbgSGWorldToScreen(vec3 world)\n"
            "{\n"
            "    vec4 clip = mul(u_global_viewProj, vec4(world, 1.0));\n"
            "    vec3 ndc = clip.xyz / clip.w;\n"
            "    return vec3(ndc.xy * 0.5 + 0.5, ndc.z);\n"
            "}\n");
        m_functions.provide(
            "hbgSGWorldDirectionToScreen",
            "vec3 hbgSGWorldDirectionToScreen(vec3 direction)\n"
            "{\n"
            "    vec3 hcs = mul(u_global_viewProj, vec4(direction, 0.0)).xyz;\n"
            "    return vec3(hcs.xy * 0.5 + 0.5, hcs.z);\n"
            "}\n");
    }

    void provideVoronoi(const std::string& suffix, const std::string& hashName) {
        const std::string randomName = "hbgSGVoronoiRandom" + suffix;
        const std::string noiseName = "hbgSGVoronoi" + suffix;
        m_functions.provide(
            randomName,
            "vec2 " + randomName + "(vec2 uv, float offset)\n"
            "{\n"
            "    vec2 h = " + hashName + "(uv);\n"
            "    return vec2(sin(h.y * offset), cos(h.x * offset)) * 0.5 + 0.5;\n"
            "}\n");
        m_functions.provide(
            noiseName,
            "vec2 " + noiseName + "(vec2 uv, float angleOffset, float cellDensity)\n"
            "{\n"
            "    vec2 g = floor(uv * cellDensity);\n"
            "    vec2 f = frac(uv * cellDensity);\n"
            "    vec3 res = vec3(8.0, 0.0, 0.0);\n"
            "    for (int y = -1; y <= 1; ++y)\n"
            "    {\n"
            "        for (int x = -1; x <= 1; ++x)\n"
            "        {\n"
            "            vec2 lattice = vec2(float(x), float(y));\n"
            "            vec2 offset = " + randomName + "(lattice + g, angleOffset);\n"
            "            float d = distance(lattice + offset, f);\n"
            "            if (d < res.x)\n"
            "            {\n"
            "                res = vec3(d, offset.x, offset.y);\n"
            "            }\n"
            "        }\n"
            "    }\n"
            "    return res.xy;\n"
            "}\n");
    }

    void provideNormalHelpers() {
        m_functions.provide(
            "hbgSGSafeNormalize",
            "vec3 hbgSGSafeNormalize(vec3 v)\n"
            "{\n"
            "    float dp3 = max(1.17549435e-38, dot(v, v));\n"
            "    return v * rsqrt(dp3);\n"
            "}\n");
        m_functions.provide(
            "hbgSGTransformWorldToTangent",
            "vec3 hbgSGTransformWorldToTangent(vec3 normalWS, mat3 tangentToWorld)\n"
            "{\n"
            "    vec3 row0 = tangentToWorld[0];\n"
            "    vec3 row1 = tangentToWorld[1];\n"
            "    vec3 row2 = tangentToWorld[2];\n"
            "    vec3 col0 = cross(row1, row2);\n"
            "    vec3 col1 = cross(row2, row0);\n"
            "    vec3 col2 = cross(row0, row1);\n"
            "    float determinant = dot(row0, col0);\n"
            "    mat3 matTBN_I_T = mat3(col0, col1, col2);\n"
            "    vec3 result = mul(matTBN_I_T, normalWS);\n"
            "    float sgn = determinant < 0.0 ? -1.0 : 1.0;\n"
            "    return hbgSGSafeNormalize(sgn * result);\n"
            "}\n");
    }

    std::string uvInput(const NodeInstance& node,
                        const std::map<SlotId, std::string>& inputs,
                        SlotId uvSlot = 0) const {
        const SlotInstance* slot = node.findSlot(uvSlot);
        const bool explicitInput = m_graph.incomingEdge(node.id, uvSlot) != nullptr ||
                                   (slot != nullptr && slot->hasExplicitValue());
        return explicitInput ? inputs.at(uvSlot) : "gi.uv0";
    }

    struct TextureTransform {
        bool enabled = false;
        Vec2 tiling{1.0f, 1.0f};
        Vec2 offset{0.0f, 0.0f};
        std::string scaleTranslate;
    };

    TextureTransform transformFromProperty(const PropertyInfo& property) const {
        TextureTransform transform;
        if (property.useTilingAndOffset) {
            transform.enabled = true;
            transform.tiling = property.tiling;
            transform.offset = property.offset;
            transform.scaleTranslate = "u_sg_" + property.refName + "_ST";
        }
        return transform;
    }

    const PropertyInfo* texturePropertyForValue(const NodeInstance& source, SlotId sourceSlot,
                                                ValueType expected, int depth) const {
        if (depth > 16) {
            return nullptr;
        }
        const std::string type = nodeTypeKey(source.type);
        if (type == "SplitTextureTransform" && sourceSlot == 3) {
            const Edge* edge = m_graph.incomingEdge(source.id, 0);
            if (edge != nullptr) {
                const NodeInstance* input = m_graph.findNode(edge->fromNode);
                return input == nullptr
                           ? nullptr
                           : texturePropertyForValue(*input, edge->fromSlot, expected,
                                                     depth + 1);
            }
            const std::string ref = settingString(source, "texture");
            if (!ref.empty()) {
                const PropertyInfo* property = m_properties.find(ref);
                return property != nullptr && property->type == expected ? property : nullptr;
            }
            return nullptr;
        }
        std::string ref = settingString(source, "property");
        if (ref.empty()) {
            ref = settingString(source, "texture");
        }
        if (!ref.empty()) {
            const PropertyInfo* property = m_properties.find(ref);
            return property != nullptr && property->type == expected ? property : nullptr;
        }
        return nullptr;
    }

    TextureTransform textureTransformForValue(const NodeInstance& source, SlotId sourceSlot,
                                              int depth) const {
        if (depth > 16) {
            return {};
        }
        const std::string type = nodeTypeKey(source.type);
        if (type == "SplitTextureTransform" && sourceSlot == 3) {
            return {};
        }
        std::string ref = settingString(source, "property");
        if (ref.empty()) {
            ref = settingString(source, "texture");
        }
        if (!ref.empty()) {
            const PropertyInfo* property = m_properties.find(ref);
            return property == nullptr ? TextureTransform{} : transformFromProperty(*property);
        }
        return {};
    }

    TextureTransform textureTransformForInput(const NodeInstance& node,
                                              SlotId textureSlot) const {
        const Edge* edge = m_graph.incomingEdge(node.id, textureSlot);
        if (edge != nullptr) {
            const NodeInstance* source = m_graph.findNode(edge->fromNode);
            return source == nullptr
                       ? TextureTransform{}
                       : textureTransformForValue(*source, edge->fromSlot, 0);
        }
        std::string ref = settingString(node, "texture");
        if (ref.empty()) {
            ref = settingString(node, "textureRef");
        }
        if (!ref.empty()) {
            const PropertyInfo* property = m_properties.find(ref);
            return property == nullptr ? TextureTransform{} : transformFromProperty(*property);
        }
        return {};
    }

    std::string applyTextureTransform(const std::string& uv,
                                      const TextureTransform& transform) const {
        if (!transform.enabled) {
            return uv;
        }
        if (!transform.scaleTranslate.empty()) {
            return "(" + uv + " * " + transform.scaleTranslate + ".xy + " +
                   transform.scaleTranslate + ".zw)";
        }
        return "(" + uv + " * vec2(" + formatFloat(transform.tiling.x) + ", " +
               formatFloat(transform.tiling.y) + ") + vec2(" +
               formatFloat(transform.offset.x) + ", " + formatFloat(transform.offset.y) + "))";
    }

    const PropertyInfo* findTexture(const NodeInstance& node,
                                    ValueType expected = ValueType::Texture2D,
                                    SlotId textureSlot = kInvalidSlot) const {
        if (textureSlot != kInvalidSlot) {
            const Edge* edge = m_graph.incomingEdge(node.id, textureSlot);
            if (edge != nullptr) {
                const NodeInstance* source = m_graph.findNode(edge->fromNode);
                return source == nullptr
                           ? nullptr
                           : texturePropertyForValue(*source, edge->fromSlot, expected, 0);
            }
        }
        std::string ref = settingString(node, "texture");
        if (ref.empty()) {
            ref = settingString(node, "textureRef");
        }
        if (!ref.empty()) {
            const PropertyInfo* property = m_properties.find(ref);
            return property != nullptr && property->type == expected ? property : nullptr;
        }
        for (const PropertyInfo& property : m_properties.properties) {
            if (property.type == expected) {
                return &property;
            }
        }
        return nullptr;
    }

    std::string outputExpression(const NodeInstance& source, SlotId slot) const {
        const auto found = m_outputs.find(source.id);
        if (found == m_outputs.end()) {
            return {};
        }
        const auto variable = found->second.find(slot);
        return variable == found->second.end() ? std::string() : variable->second;
    }

    ConcreteValueType targetType(const SlotInstance& slot) const {
        return slot.concreteType;
    }

    std::string inputExpression(const NodeInstance& node, const SlotInstance& slot,
                                ConcreteValueType target) const {
        const Edge* edge = m_graph.incomingEdge(node.id, slot.spec.id);
        if (edge != nullptr) {
            const NodeInstance* source = m_graph.findNode(edge->fromNode);
            const SlotInstance* output = source == nullptr ? nullptr : source->findSlot(edge->fromSlot);
            if (source != nullptr && output != nullptr) {
                const std::string expression = outputExpression(*source, edge->fromSlot);
                if (!expression.empty()) {
                    return adapt(expression, output->concreteType, target, source->concretePrecision);
                }
            }
        }
        if (slot.state.value) {
            return literal(*slot.state.value, target, node.concretePrecision);
        }
        if (slot.spec.defaultValue) {
            return literal(*slot.spec.defaultValue, target, node.concretePrecision);
        }
        return literal(Value(ValueType::Float, 0.0f), target, node.concretePrecision);
    }

    std::map<SlotId, std::string> inputExpressions(const NodeInstance& node) const {
        std::map<SlotId, std::string> result;
        for (const SlotInstance& slot : node.slots) {
            if (!slot.isInput()) {
                continue;
            }
            result[slot.spec.id] = inputExpression(node, slot, targetType(slot));
        }
        return result;
    }

    std::vector<GradientKey> gradientKeysFromValue(const Value* value,
                                                   bool sampleDefault) const {
        std::vector<GradientKey> keys;
        if (value != nullptr) {
            if (const Gradient* gradient = value->asGradient()) {
                keys = gradient->keys;
            }
        }
        if (keys.empty()) {
            const Vec4 color = sampleDefault ? Vec4(1.0f, 1.0f, 1.0f, 1.0f)
                                             : Vec4(0.0f, 0.0f, 0.0f, 1.0f);
            keys.push_back({0.0f, Vec4(1.0f, 1.0f, 1.0f, 1.0f)});
            keys.push_back({1.0f, color});
        }
        std::stable_sort(keys.begin(), keys.end(),
                         [](const GradientKey& lhs, const GradientKey& rhs) {
                             return lhs.time < rhs.time;
                         });
        if (keys.size() > 8) {
            keys.resize(8);
        }
        return keys;
    }

    std::vector<GradientKey> gradientKeys(const NodeInstance& node, bool sampleDefault) const {
        return gradientKeysFromValue(node.setting("gradient"), sampleDefault);
    }

    int gradientMode(const NodeInstance& node) const {
        const Value* value = node.setting("mode");
        if (value != nullptr) {
            if (const std::int64_t* integer = value->asInteger()) {
                return std::clamp(static_cast<int>(*integer), 0, 2);
            }
            if (const float* number = value->asFloat()) {
                return std::clamp(static_cast<int>(*number), 0, 2);
            }
        }
        const std::string mode = normalizedSetting(node, "mode", "");
        if (mode == "fixed") {
            return 1;
        }
        if (mode == "perceptual" || mode == "oklab") {
            return 2;
        }
        return 0;
    }

    std::string gradientCall(const std::vector<GradientKey>& keys, int mode) const {
        std::ostringstream stream;
        stream << "hbgSGNewGradient(" << mode << ", " << keys.size() << ", " << keys.size();
        for (int index = 0; index < 8; ++index) {
            stream << ", ";
            if (index < static_cast<int>(keys.size())) {
                const Vec4& color = keys[static_cast<std::size_t>(index)].color;
                stream << "vec4(" << formatFloat(color.x) << ", " << formatFloat(color.y) << ", "
                       << formatFloat(color.z) << ", "
                       << formatFloat(keys[static_cast<std::size_t>(index)].time) << ")";
            } else {
                stream << "vec4(0.0, 0.0, 0.0, 0.0)";
            }
        }
        for (int index = 0; index < 8; ++index) {
            stream << ", ";
            if (index < static_cast<int>(keys.size())) {
                const GradientKey& key = keys[static_cast<std::size_t>(index)];
                stream << "vec2(" << formatFloat(key.color.w) << ", " << formatFloat(key.time)
                       << ")";
            } else {
                stream << "vec2(0.0, 0.0)";
            }
        }
        stream << ")";
        return stream.str();
    }

    void emitGradientNode(const NodeInstance& node, std::ostringstream& body) {
        provideGradientHelpers();
        const auto output = m_outputs.find(node.id);
        if (output == m_outputs.end() || output->second.empty()) {
            return;
        }
        body << "    " << output->second.begin()->second << " = "
             << gradientCall(gradientKeys(node, false), gradientMode(node)) << ";\n";
    }

    void emitSampleGradient(const NodeInstance& node, const std::map<SlotId, std::string>& inputs,
                            std::ostringstream& body) {
        provideGradientHelpers();
        const auto output = m_outputs.find(node.id);
        if (output == m_outputs.end() || output->second.empty()) {
            return;
        }
        std::string gradient;
        if (const Edge* edge = m_graph.incomingEdge(node.id, 0)) {
            const NodeInstance* source = m_graph.findNode(edge->fromNode);
            const SlotInstance* sourceSlot =
                source == nullptr ? nullptr : source->findSlot(edge->fromSlot);
            if (sourceSlot != nullptr &&
                sourceSlot->concreteType == ConcreteValueType::Gradient) {
                gradient = outputExpression(*source, edge->fromSlot);
            }
        }
        if (gradient.empty()) {
            gradient = gradientCall(gradientKeys(node, true), 0);
        }
        body << "    " << output->second.begin()->second << " = hbgSGSampleGradient(" << gradient
             << ", " << inputs.at(1) << ");\n";
    }

    void emitBlackbody(const NodeInstance& node, const std::map<SlotId, std::string>& inputs,
                       std::ostringstream& body) {
        const auto output = m_outputs.find(node.id);
        if (output == m_outputs.end() || output->second.empty()) {
            return;
        }
        const std::string value = output->second.begin()->second;
        const std::string temperature = "hbg_sg_blackbody_" + std::to_string(node.id);
        body << "    float " << temperature << " = " << inputs.at(0) << ";\n";
        body << "    " << value << " = vec3(255.0, 255.0, 255.0);\n";
        body << "    " << value << ".x = 56100000.0 * pow(" << temperature
             << ", (-3.0 / 2.0)) + 148.0;\n";
        body << "    " << value << ".y = 100.04 * log(" << temperature << ") - 623.6;\n";
        body << "    if (" << temperature << " > 6500.0) " << value << ".y = 35200000.0 * pow("
             << temperature << ", (-3.0 / 2.0)) + 184.0;\n";
        body << "    " << value << ".z = 194.18 * log(" << temperature << ") - 1448.6;\n";
        body << "    " << value << " = clamp(" << value
             << ", vec3(0.0, 0.0, 0.0), vec3(255.0, 255.0, 255.0)) / 255.0;\n";
        body << "    if (" << temperature << " < 1000.0) " << value << " *= " << temperature
             << " / 1000.0;\n";
    }

    void emitSwitchNode(const NodeInstance& node, const std::map<SlotId, std::string>& inputs,
                        std::ostringstream& body) {
        const auto outputs = m_outputs.find(node.id);
        if (outputs == m_outputs.end()) {
            return;
        }
        const auto resultVariable = outputs->second.find(2);
        if (resultVariable == outputs->second.end()) {
            return;
        }
        const SlotInstance* outputSlot = node.findSlot(2);
        if (outputSlot == nullptr) {
            return;
        }
        const ConcreteValueType valueType = outputSlot->concreteType;
        const std::string valueTypeName = typeName(valueType, node.concretePrecision);
        const std::string predicateTypeName =
            typeName(ConcreteValueType::Float, node.concretePrecision);
        const std::string zero = literal(Value::makeFloat(0.0f), valueType,
                                         node.concretePrecision);

        EffectiveSwitchCases effective = effectiveSwitchCases(m_graph, node);
        std::vector<SwitchCase>& cases = effective.cases;
        if (cases.size() > 26) {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorInvalidValue;
            diagnostic.message = "Switch node supports at most 26 cases";
            diagnostic.node = node.id;
            m_diagnostics.push_back(std::move(diagnostic));
            cases.resize(26);
        }

        const std::string functionName = "hbgSG_SwitchNode_" + std::to_string(node.id);
        const std::string predicate =
            inputs.contains(0) ? inputs.at(0) : std::string("0.0");
        const std::string fallback = inputs.contains(1) ? inputs.at(1) : zero;
        const bool floorPredicate = settingBool(node, "floorPredicate", true);
        const std::string predicateExpression =
            floorPredicate ? "floor(predicate)" : "predicate";
        const auto caseParameter = [](std::size_t index) {
            return "case_" + std::string(1, static_cast<char>('A' + index));
        };

        std::ostringstream function;
        function << "void " << functionName << "(" << predicateTypeName << " predicate, "
                 << valueTypeName << " fallback";
        for (std::size_t index = 0; index < cases.size(); ++index) {
            function << ", " << valueTypeName << " " << caseParameter(index);
        }
        function << ", out " << valueTypeName << " result)\n{\n";
        function << "    result = fallback;\n";
        for (std::size_t index = 0; index < cases.size(); ++index) {
            function << "    " << (index == 0 ? "if" : "else if") << " ("
                     << predicateExpression << " " << switchComparisonOperator(cases[index].comparison)
                     << " " << formatFloat(cases[index].threshold) << ")\n";
            function << "    {\n";
            function << "        result = " << caseParameter(index) << ";\n";
            function << "    }\n";
        }
        function << "}\n";
        m_functions.provide(functionName, function.str());

        body << "    " << functionName << "(" << predicate << ", " << fallback;
        for (std::size_t index = 0; index < cases.size(); ++index) {
            const SlotId slot = static_cast<SlotId>(3 + index);
            body << ", " << (inputs.contains(slot) ? inputs.at(slot) : zero);
        }
        body << ", " << resultVariable->second << ");\n";
    }

    void emitCustomFunctionNode(const NodeInstance& node,
                                const std::map<SlotId, std::string>& inputs,
                                std::ostringstream& body) {
        const auto outputs = m_outputs.find(node.id);
        if (outputs == m_outputs.end() || outputs->second.empty()) {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorMissingInput;
            diagnostic.message = "Custom Function must have at least one output";
            diagnostic.node = node.id;
            m_diagnostics.push_back(std::move(diagnostic));
            return;
        }

        const std::string functionName = settingString(node, "functionName");
        if (functionName.empty() || functionName == "Enter function name here..." ||
            !isIdentifier(functionName)) {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorInvalidValue;
            diagnostic.message = "Custom Function has an invalid function name";
            diagnostic.node = node.id;
            m_diagnostics.push_back(std::move(diagnostic));
            return;
        }

        std::string sourceType = settingString(node, "sourceType");
        std::transform(sourceType.begin(), sourceType.end(), sourceType.begin(),
                       [](unsigned char character) {
                           return static_cast<char>(std::tolower(character));
                       });
        const std::string functionBody = settingString(node, "functionBody");
        const std::string functionSource = settingString(node, "functionSource");
        const bool fileSource =
            sourceType == "file" ||
            (sourceType.empty() && functionBody.empty() && !functionSource.empty());
        if (!sourceType.empty() && sourceType != "string" && sourceType != "file") {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorInvalidValue;
            diagnostic.message = "Custom Function sourceType must be string or file";
            diagnostic.node = node.id;
            m_diagnostics.push_back(std::move(diagnostic));
            return;
        }

        std::vector<const SlotInstance*> inputSlots;
        std::vector<const SlotInstance*> outputSlots;
        for (const SlotInstance& slot : node.slots) {
            if (slot.isInput()) {
                inputSlots.push_back(&slot);
            } else {
                outputSlots.push_back(&slot);
            }
        }

        std::set<std::string> parameterNames;
        bool validNames = true;
        for (const SlotInstance* slot : inputSlots) {
            if (!isIdentifier(slot->spec.name) || !parameterNames.insert(slot->spec.name).second) {
                validNames = false;
            }
        }
        for (const SlotInstance* slot : outputSlots) {
            if (!isIdentifier(slot->spec.name) || !parameterNames.insert(slot->spec.name).second) {
                validNames = false;
            }
        }
        if (!validNames) {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorInvalidValue;
            diagnostic.message = "Custom Function slot names must be unique identifiers";
            diagnostic.node = node.id;
            m_diagnostics.push_back(std::move(diagnostic));
            return;
        }

        if (fileSource) {
            std::string source = functionSource;
            std::transform(source.begin(), source.end(), source.begin(),
                           [](unsigned char character) {
                               return static_cast<char>(std::tolower(character));
                           });
            const bool validExtension =
                source.ends_with(".hlsl") || source.ends_with(".cginc") || source.ends_with(".cg");
            if (functionSource.empty() || !validExtension) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorTemplate;
                diagnostic.message =
                    "Custom Function source file must be an .hlsl, .cginc, or .cg file";
                diagnostic.node = node.id;
                m_diagnostics.push_back(std::move(diagnostic));
                return;
            }
            std::string error;
            if (!m_functions.provide("custom_function_include:" + functionSource,
                                     "#include \"" + functionSource + "\"\n", &error)) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorTemplate;
                diagnostic.message = error;
                diagnostic.node = node.id;
                m_diagnostics.push_back(std::move(diagnostic));
                return;
            }
        } else {
            if (functionBody.empty() || functionBody == "Enter function body here...") {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorTemplate;
                diagnostic.message = "Custom Function has an empty function body";
                diagnostic.node = node.id;
                m_diagnostics.push_back(std::move(diagnostic));
                return;
            }

            std::ostringstream function;
            function << "void " << functionName << "(";
            bool first = true;
            for (const SlotInstance* slot : inputSlots) {
                if (!first) {
                    function << ", ";
                }
                first = false;
                function << typeName(slot->concreteType, node.concretePrecision) << " "
                         << slot->spec.name;
            }
            for (const SlotInstance* slot : outputSlots) {
                if (!first) {
                    function << ", ";
                }
                first = false;
                function << "out " << typeName(slot->concreteType, node.concretePrecision) << " "
                         << slot->spec.name;
            }
            function << ")\n{\n" << functionBody;
            if (functionBody.back() != '\n') {
                function << "\n";
            }
            function << "}\n";

            std::string error;
            if (!m_functions.provide(functionName, function.str(), &error)) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorTemplate;
                diagnostic.message = error;
                diagnostic.node = node.id;
                m_diagnostics.push_back(std::move(diagnostic));
                return;
            }
        }

        body << "    " << functionName << "(";
        bool first = true;
        for (const SlotInstance* slot : inputSlots) {
            if (!first) {
                body << ", ";
            }
            first = false;
            const auto found = inputs.find(slot->spec.id);
            body << (found == inputs.end() ? std::string("0.0") : found->second);
        }
        for (const SlotInstance* slot : outputSlots) {
            if (!first) {
                body << ", ";
            }
            first = false;
            const auto found = outputs->second.find(slot->spec.id);
            if (found == outputs->second.end()) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorMissingInput;
                diagnostic.message = "Custom Function output is not emitted";
                diagnostic.node = node.id;
                diagnostic.slot = slot->spec.id;
                m_diagnostics.push_back(std::move(diagnostic));
                return;
            }
            body << found->second;
        }
        body << ");\n";
    }

    void emitDropdownNode(const NodeInstance& node, const std::map<SlotId, std::string>& inputs,
                          std::ostringstream& body) {
        const auto outputs = m_outputs.find(node.id);
        if (outputs == m_outputs.end() || outputs->second.empty()) {
            return;
        }
        const std::string output = outputs->second.begin()->second;
        const SlotInstance* outputSlot = node.findSlot(0);
        const ConcreteValueType outputType =
            outputSlot == nullptr ? ConcreteValueType::Float : outputSlot->concreteType;
        const std::string zero =
            literal(Value::makeFloat(0.0f), outputType, node.concretePrecision);

        std::string ref;
        for (const char* key : {"dropdown", "dropdownRef", "refName"}) {
            const Value* value = node.setting(key);
            if (value == nullptr) {
                continue;
            }
            if (const std::string* text = value->asString(); text != nullptr && !text->empty()) {
                ref = *text;
                break;
            }
        }
        const Dropdown* dropdown = m_graph.findDropdown(ref);
        if (dropdown == nullptr) {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorMissingInput;
            diagnostic.message = "Dropdown node does not reference a graph dropdown";
            diagnostic.node = node.id;
            m_diagnostics.push_back(std::move(diagnostic));
            body << "    " << output << " = " << zero << ";\n";
            return;
        }
        if (dropdown->entries.empty()) {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorInvalidValue;
            diagnostic.message = "dropdown has no entries";
            diagnostic.node = node.id;
            m_diagnostics.push_back(std::move(diagnostic));
            body << "    " << output << " = " << zero << ";\n";
            return;
        }

        int selection = dropdown->value;
        selection = std::clamp(selection, 0, static_cast<int>(dropdown->entries.size()) - 1);
        const SlotId selectedSlot = dropdown->entries[static_cast<std::size_t>(selection)].id;
        const auto found = inputs.find(selectedSlot);
        body << "    " << output << " = " << (found == inputs.end() ? zero : found->second)
             << ";\n";
    }

    void emitKeywordNode(const NodeInstance& node, const std::map<SlotId, std::string>& inputs,
                         std::ostringstream& body) {
        const auto outputs = m_outputs.find(node.id);
        if (outputs == m_outputs.end() || outputs->second.empty()) {
            return;
        }
        const std::string output = outputs->second.begin()->second;
        const SlotInstance* outputSlot = node.findSlot(0);
        const ConcreteValueType outputType =
            outputSlot == nullptr ? ConcreteValueType::Float : outputSlot->concreteType;
        const std::string zero =
            literal(Value::makeFloat(0.0f), outputType, node.concretePrecision);
        const auto input = [&](SlotId id) {
            const auto found = inputs.find(id);
            return found == inputs.end() ? zero : found->second;
        };

        const std::string ref = settingString(node, "keyword");
        const Keyword* keyword = m_graph.findKeyword(ref);
        if (keyword == nullptr) {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorMissingInput;
            diagnostic.message = "Keyword node does not reference a graph keyword";
            diagnostic.node = node.id;
            m_diagnostics.push_back(std::move(diagnostic));
            body << "    " << output << " = " << zero << ";\n";
            return;
        }

        if (keyword->type == KeywordType::Boolean) {
            body << "#ifdef " << defineName(keyword->refName) << "\n    " << output << " = "
                 << input(1) << ";\n#else\n    " << output << " = " << input(2) << ";\n#endif\n";
            return;
        }

        if (keyword->entries.empty()) {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorMissingInput;
            diagnostic.message = "enum keyword has no entries";
            diagnostic.node = node.id;
            m_diagnostics.push_back(std::move(diagnostic));
            body << "    " << output << " = " << zero << ";\n";
            return;
        }

        for (std::size_t index = 0; index < keyword->entries.size(); ++index) {
            const KeywordEntry& entry = keyword->entries[index];
            if (index == 0) {
                body << "#if defined(" << defineName(keyword->refName) << "_"
                     << entry.referenceName << ")\n";
            } else {
                body << "#elif defined(" << defineName(keyword->refName) << "_"
                     << entry.referenceName << ")\n";
            }
            body << "    " << output << " = " << input(entry.id) << ";\n";
        }
        body << "#else\n    " << output << " = "
             << input(keyword->entries.back().id) << ";\n#endif\n";
    }

    void emitNode(NodeId id, std::ostringstream& declarations, std::ostringstream& body) {
        const NodeInstance* node = m_graph.findNode(id);
        if (node == nullptr || node->unknown) {
            return;
        }
        const NodeDef* definition = m_registry.find(node->type);
        if (definition == nullptr) {
            return;
        }
        const std::string type = nodeTypeKey(node->type);
        if (!definition->blockNode) {
            std::map<SlotId, std::string> outputs;
            for (const SlotInstance& slot : node->slots) {
                if (!slot.isOutput()) {
                    continue;
                }
                const ConcreteValueType valueType = slot.concreteType;
                if (valueType == ConcreteValueType::Gradient) {
                    if (node->type != "Gradient" && node->type != "Property") {
                        continue;
                    }
                    const std::string name = varName(node->id, slot.spec.id);
                    declarations << "    HbgSGGradient " << name << ";\n";
                    outputs[slot.spec.id] = name;
                    continue;
                }
                if (valueType == ConcreteValueType::Texture2D ||
                    valueType == ConcreteValueType::Texture2DArray ||
                    valueType == ConcreteValueType::Texture3D ||
                    valueType == ConcreteValueType::Cubemap ||
                    valueType == ConcreteValueType::VirtualTexture ||
                    valueType == ConcreteValueType::SamplerState) {
                    continue;
                }
                const std::string name = varName(node->id, slot.spec.id);
                declarations << "    " << typeName(valueType, node->concretePrecision) << " " << name
                             << ";\n";
                outputs[slot.spec.id] = name;
            }
            m_outputs[node->id] = std::move(outputs);
        }

        if (node->type == "Property") {
            const std::string ref = settingString(*node, "property");
            const PropertyInfo* property = m_properties.find(ref);
            const auto outputs = m_outputs.find(node->id);
            if (property != nullptr && outputs != m_outputs.end() && !outputs->second.empty()) {
                if (property->type == ValueType::Gradient) {
                    provideGradientHelpers();
                    body << "    " << outputs->second.begin()->second << " = "
                         << gradientCall(gradientKeysFromValue(&property->defaultValue, false), 0)
                         << ";\n";
                } else {
                    body << "    " << outputs->second.begin()->second << " = "
                         << property->expression << ";\n";
                }
            }
            return;
        }

        const auto inputs = inputExpressions(*node);
        if (type == "Gradient") {
            emitGradientNode(*node, body);
            return;
        }
        if (type == "SampleGradient") {
            emitSampleGradient(*node, inputs, body);
            return;
        }
        if (type == "Blackbody") {
            emitBlackbody(*node, inputs, body);
            return;
        }
        if (type == "Constant") {
            const std::string constant = normalizedSetting(*node, "constant", "pi");
            float value = 3.1415926f;
            if (constant == "tau") {
                value = 6.28318530f;
            } else if (constant == "phi") {
                value = 1.618034f;
            } else if (constant == "e") {
                value = 2.718282f;
            } else if (constant == "sqrt2") {
                value = 1.414214f;
            }
            const auto output = m_outputs.find(node->id);
            if (output != m_outputs.end() && !output->second.empty()) {
                const ConcreteValueType valueType = targetType(node->slots.back());
                body << "    " << output->second.begin()->second << " = "
                     << literal(Value(ValueType::Float, value), valueType,
                                node->concretePrecision)
                     << ";\n";
            }
            return;
        }
        if (type == "Boolean" || type == "Float" || type == "Integer" ||
            type == "Slider" || type == "Color") {
            const Value* value = node->setting("value");
            const auto output = m_outputs.find(node->id);
            if (output != m_outputs.end() && !output->second.empty()) {
                const ConcreteValueType valueType = targetType(node->slots.back());
                body << "    " << output->second.begin()->second << " = "
                     << (value == nullptr ? literal(Value(ValueType::Float, 0.0f), valueType,
                                                    node->concretePrecision)
                                          : literal(*value, valueType, node->concretePrecision))
                     << ";\n";
            }
            return;
        }
        if (type == "UV") {
            const int channel = settingInteger(*node, "channel", 0);
            const auto output = m_outputs[node->id];
            body << "    " << output.begin()->second << " = "
                 << (channel == 1 ? "gi.uv1" : "gi.uv0") << ";\n";
            return;
        }
        if (type == "TilingAndOffset" || type == "Rotate" || type == "PolarCoordinates" ||
            type == "RadialShear" || type == "Spherize" || type == "Twirl" ||
            type == "Flipbook") {
            const std::string suffix = std::to_string(node->id);
            const std::string uv = uvInput(*node, inputs);
            const std::string output = m_outputs[node->id].begin()->second;
            if (type == "TilingAndOffset") {
                body << "    " << output << " = " << uv << " * " << inputs.at(1) << " + "
                     << inputs.at(2) << ";\n";
                return;
            }
            if (type == "Rotate") {
                std::string angle = inputs.at(2);
                if (settingString(*node, "unit") == "degrees") {
                    angle = "radians(" + angle + ")";
                }
                body << "    vec2 hbg_uv_delta_" << suffix << " = " << uv << " - " << inputs.at(1)
                     << ";\n";
                body << "    float hbg_uv_s_" << suffix << ", hbg_uv_c_" << suffix << ";\n";
                body << "    sincos(" << angle << ", hbg_uv_s_" << suffix << ", hbg_uv_c_"
                     << suffix << ");\n";
                body << "    " << output << " = vec2(hbg_uv_c_" << suffix << " * hbg_uv_delta_"
                     << suffix << ".x + hbg_uv_s_" << suffix << " * hbg_uv_delta_" << suffix
                     << ".y, hbg_uv_c_" << suffix << " * hbg_uv_delta_" << suffix
                     << ".y - hbg_uv_s_" << suffix << " * hbg_uv_delta_" << suffix << ".x) + "
                     << inputs.at(1) << ";\n";
                return;
            }
            if (type == "PolarCoordinates") {
                body << "    vec2 hbg_uv_delta_" << suffix << " = " << uv << " - " << inputs.at(1)
                     << ";\n";
                body << "    " << output << " = vec2(length(hbg_uv_delta_" << suffix
                     << ") * 2.0 * " << inputs.at(2) << ", atan2(hbg_uv_delta_" << suffix
                     << ".x, hbg_uv_delta_" << suffix << ".y) * (1.0 / 6.28) * " << inputs.at(3)
                     << ");\n";
                return;
            }
            if (type == "RadialShear") {
                body << "    vec2 hbg_uv_delta_" << suffix << " = " << uv << " - " << inputs.at(1)
                     << ";\n";
                body << "    float hbg_uv_d2_" << suffix << " = dot(hbg_uv_delta_" << suffix
                     << ", hbg_uv_delta_" << suffix << ");\n";
                body << "    " << output << " = " << uv << " + vec2(hbg_uv_delta_" << suffix
                     << ".y, -hbg_uv_delta_" << suffix << ".x) * (hbg_uv_d2_" << suffix << " * "
                     << inputs.at(2) << ") + " << inputs.at(3) << ";\n";
                return;
            }
            if (type == "Spherize") {
                body << "    vec2 hbg_uv_delta_" << suffix << " = " << uv << " - " << inputs.at(1)
                     << ";\n";
                body << "    float hbg_uv_d2_" << suffix << " = dot(hbg_uv_delta_" << suffix
                     << ", hbg_uv_delta_" << suffix << ");\n";
                body << "    float hbg_uv_d4_" << suffix << " = hbg_uv_d2_" << suffix << " * "
                     << "hbg_uv_d2_" << suffix << ";\n";
                body << "    " << output << " = " << uv << " + hbg_uv_delta_" << suffix << " * (hbg_uv_d4_"
                     << suffix << " * " << inputs.at(2) << ") + " << inputs.at(3) << ";\n";
                return;
            }
            if (type == "Twirl") {
                body << "    vec2 hbg_uv_delta_" << suffix << " = " << uv << " - " << inputs.at(1)
                     << ";\n";
                body << "    float hbg_uv_angle_" << suffix << " = " << inputs.at(2)
                     << " * length(hbg_uv_delta_" << suffix << ");\n";
                body << "    float hbg_uv_s_" << suffix << ", hbg_uv_c_" << suffix << ";\n";
                body << "    sincos(hbg_uv_angle_" << suffix << ", hbg_uv_s_" << suffix
                     << ", hbg_uv_c_" << suffix << ");\n";
                body << "    " << output << " = vec2(hbg_uv_c_" << suffix << " * hbg_uv_delta_"
                     << suffix << ".x - hbg_uv_s_" << suffix << " * hbg_uv_delta_" << suffix
                     << ".y + " << inputs.at(1) << ".x + " << inputs.at(3) << ".x, hbg_uv_s_"
                     << suffix << " * hbg_uv_delta_" << suffix << ".x + hbg_uv_c_" << suffix
                     << " * hbg_uv_delta_" << suffix << ".y + " << inputs.at(1) << ".y + "
                     << inputs.at(3) << ".y);\n";
                return;
            }
            const std::string width = "hbg_fb_width_" + suffix;
            const std::string height = "hbg_fb_height_" + suffix;
            const std::string tile = "hbg_fb_tile_" + suffix;
            const std::string count = "hbg_fb_count_" + suffix;
            const std::string base = "hbg_fb_base_" + suffix;
            body << "    float " << width << " = " << inputs.at(1) << ";\n";
            body << "    float " << height << " = " << inputs.at(2) << ";\n";
            body << "    float " << tile << " = floor(fmod(" << inputs.at(3) << " + 0.00001, "
                 << width << " * " << height << "));\n";
            body << "    vec2 " << count << " = vec2(1.0, 1.0) / vec2(" << width << ", "
                 << height << ");\n";
            body << "    float " << base << " = floor((" << tile << " + 0.5) * " << count
                 << ".x);\n";
            const bool invertX = settingBool(*node, "invertX", false);
            const bool invertY = settingBool(*node, "invertY", true);
            const std::string tileX = invertX
                                          ? width + " - (" + tile + " - " + width + " * " + base +
                                                " + 1.0)"
                                          : tile + " - " + width + " * " + base;
            const std::string tileY = invertY ? height + " - (" + base + " + 1.0)" : base;
            body << "    " << output << " = (" << uv << " + vec2(" << tileX << ", " << tileY
                 << ")) * " << count << ";\n";
            return;
        }
        if (type == "ParallaxMapping" || type == "ParallaxOcclusionMapping" ||
            type == "Triplanar") {
            const SlotId textureSlot = type == "ParallaxOcclusionMapping" ? 2 : 1;
            const PropertyInfo* property =
                findTexture(*node, ValueType::Texture2D, textureSlot);
            if (property == nullptr) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorMissingInput;
                diagnostic.message = "texture UV node has no texture property";
                diagnostic.node = node->id;
                m_diagnostics.push_back(std::move(diagnostic));
                return;
            }
            const std::string suffix = std::to_string(node->id);
            const std::string sampler = "s_sg_" + property->refName;
            if (type == "ParallaxMapping") {
                const std::string uv = applyTextureTransform(
                    hasExplicitInput(*node, 4) ? inputs.at(4) : "gi.uv0",
                    textureTransformForInput(*node, 1));
                const int channel = channelIndexSetting(*node, 1);
                const char component = "rgba"[channel];
                const std::string scale = "hbg_parallax_scale_" + suffix;
                const std::string height = "hbg_parallax_height_" + suffix;
                const std::string view = "hbg_parallax_view_" + suffix;
                const std::string output = m_outputs[node->id].at(0);
                body << "    float " << scale << " = " << inputs.at(3) << " * 0.01;\n";
                body << "    vec3 " << view
                     << " = normalize(vec3(dot(gi.viewDirWS, gi.tangentWS), dot(gi.viewDirWS, "
                        "gi.bitangentWS), dot(gi.viewDirWS, gi.normalWS)));\n";
                body << "    float " << height << " = texture2D(" << sampler << ", " << uv
                     << ")." << component << ";\n";
                body << "    " << height << " = " << height << " * " << scale << " - "
                     << scale << " * 0.5;\n";
                body << "    " << view << ".z += 0.42;\n";
                body << "    " << output << " = " << uv << " + " << height << " * ("
                     << view << ".xy / " << view << ".z);\n";
                return;
            }
            if (type == "ParallaxOcclusionMapping") {
                provideDisplacementObjectScale();
                const std::string uv = hasExplicitInput(*node, 6) ? inputs.at(6) : "gi.uv0";
                const int channel = channelIndexSetting(*node, 0);
                const char component = "rgba"[channel];
                const std::string view = "hbg_pom_view_" + suffix;
                const std::string ndotv = "hbg_pom_ndotv_" + suffix;
                const std::string maxHeight = "hbg_pom_max_height_" + suffix;
                const std::string uvSpaceScale = "hbg_pom_uv_space_scale_" + suffix;
                const std::string viewUV = "hbg_pom_view_uv_" + suffix;
                const std::string baseUV = "hbg_pom_base_uv_" + suffix;
                const std::string numSteps = "hbg_pom_num_steps_" + suffix;
                const std::string stepSize = "hbg_pom_step_size_" + suffix;
                const std::string maxOffset = "hbg_pom_max_offset_" + suffix;
                const std::string offsetPerStep = "hbg_pom_offset_per_step_" + suffix;
                const std::string current = "hbg_pom_current_" + suffix;
                const std::string prevHeight = "hbg_pom_prev_height_" + suffix;
                const std::string currHeight = "hbg_pom_curr_height_" + suffix;
                const std::string rayHeight = "hbg_pom_ray_height_" + suffix;
                const std::string pt0 = "hbg_pom_pt0_" + suffix;
                const std::string pt1 = "hbg_pom_pt1_" + suffix;
                const std::string delta0 = "hbg_pom_delta0_" + suffix;
                const std::string delta1 = "hbg_pom_delta1_" + suffix;
                const std::string offset = "hbg_pom_offset_" + suffix;
                const std::string lod = inputs.at(7);
                const std::string lodThreshold = inputs.at(8);
                const std::string tiling = inputs.at(10);
                const std::string uvOffset = inputs.at(11);
                const std::string primitiveSize = inputs.at(12);
                const std::string ambient = "hbg_pom_intersection_height_" + suffix;
                const std::string delta = "hbg_pom_delta_" + suffix;
                const std::string pixelDepth = m_outputs[node->id].at(0);
                const std::string parallaxUV = m_outputs[node->id].at(1);
                const std::string transformedBaseUV = applyTextureTransform(
                    "(" + uv + " * " + tiling + " + " + uvOffset + ")",
                    textureTransformForInput(*node, 2));
                body << "    vec3 " << view
                     << " = normalize(vec3(dot(gi.viewDirWS, gi.tangentWS), dot(gi.viewDirWS, "
                        "gi.bitangentWS), dot(gi.viewDirWS, gi.normalWS))) * "
                        "hbgSGDisplacementObjectScale().xzy;\n";
                body << "    float " << ndotv << " = " << view << ".z;\n";
                body << "    float " << maxHeight << " = " << inputs.at(4) << " * 0.01;\n";
                body << "    " << maxHeight << " *= 2.0 / (abs(" << tiling << ".x) + abs("
                     << tiling << ".y));\n";
                body << "    vec2 " << uvSpaceScale << " = " << maxHeight << " * " << tiling
                     << " / " << primitiveSize << ";\n";
                body << "    vec3 " << viewUV << " = normalize(vec3(" << view << ".xy * "
                     << uvSpaceScale << ", " << view << ".z));\n";
                body << "    vec2 " << baseUV << " = " << transformedBaseUV << ";\n";
                body << "    int " << numSteps << " = int(max(min(" << inputs.at(5)
                     << ", 256.0), 1.0));\n";
                body << "    float " << stepSize << " = 1.0 / float(" << numSteps << ");\n";
                body << "    vec2 " << maxOffset << " = " << viewUV << ".xy / -"
                     << viewUV << ".z;\n";
                body << "    vec2 " << offsetPerStep << " = " << stepSize << " * " << maxOffset
                     << ";\n";
                body << "    vec2 " << current << " = vec2(0.0, 0.0);\n";
                body << "    float " << prevHeight << " = texture2DLod(" << sampler << ", "
                     << baseUV << " + " << current << ", " << lod << ")." << component
                     << ";\n";
                body << "    " << current << " += " << offsetPerStep << ";\n";
                body << "    float " << currHeight << " = texture2DLod(" << sampler << ", "
                     << baseUV << " + " << current << ", " << lod << ")." << component
                     << ";\n";
                body << "    float " << rayHeight << " = 1.0 - " << stepSize << ";\n";
                body << "    for (int stepIndex = 0; stepIndex < " << numSteps
                     << "; ++stepIndex)\n    {\n";
                body << "        if (" << currHeight << " > " << rayHeight << ")\n"
                     << "            break;\n";
                body << "        " << prevHeight << " = " << currHeight << ";\n";
                body << "        " << rayHeight << " -= " << stepSize << ";\n";
                body << "        " << current << " += " << offsetPerStep << ";\n";
                body << "        " << currHeight << " = texture2DLod(" << sampler << ", "
                     << baseUV << " + " << current << ", " << lod << ")." << component
                     << ";\n";
                body << "    }\n";
                body << "    float " << pt0 << " = " << rayHeight << " + " << stepSize << ";\n";
                body << "    float " << pt1 << " = " << rayHeight << ";\n";
                body << "    float " << delta0 << " = " << pt0 << " - " << prevHeight << ";\n";
                body << "    float " << delta1 << " = " << pt1 << " - " << currHeight << ";\n";
                body << "    vec2 " << offset << " = vec2(0.0, 0.0);\n";
                body << "    for (int secantIndex = 0; secantIndex < 3; ++secantIndex)\n    {\n";
                body << "        float " << ambient << " = (" << pt0 << " * " << delta1 << " - "
                     << pt1 << " * " << delta0 << ") / (" << delta1 << " - " << delta0
                     << ");\n";
                body << "        " << offset << " = (1.0 - " << ambient << ") * "
                     << offsetPerStep << " * float(" << numSteps << ");\n";
                body << "        " << currHeight << " = texture2DLod(" << sampler << ", "
                     << baseUV << " + " << offset << ", " << lod << ")." << component
                     << ";\n";
                body << "        float " << delta << " = " << ambient << " - " << currHeight
                     << ";\n";
                body << "        if (" << delta << " >= -0.01 && " << delta << " <= 0.01)\n"
                     << "            break;\n";
                body << "        if (" << delta << " < 0.0)\n        {\n"
                     << "            " << delta1 << " = " << delta << ";\n"
                     << "            " << pt1 << " = " << ambient << ";\n"
                     << "        }\n        else\n        {\n"
                     << "            " << delta0 << " = " << delta << ";\n"
                     << "            " << pt0 << " = " << ambient << ";\n"
                     << "        }\n";
                body << "    }\n";
                body << "    " << offset << " *= (1.0 - saturate(" << lod << " - "
                     << lodThreshold << "));\n";
                body << "    " << parallaxUV << " = " << baseUV << " + " << offset << ";\n";
                body << "    " << pixelDepth << " = (" << maxHeight << " - " << currHeight
                     << " * " << maxHeight << ") / max(" << ndotv << ", 0.0001);\n";
                return;
            }
            const std::string textureType = normalizedSetting(*node, "textureType", "default");
            const bool normalType = textureType == "normal";
            const std::string space = coordinateSpace(*node, "inputSpace", "world");
            const std::string normalOut = coordinateSpace(*node, "normalOutputSpace", "tangent");
            const std::string position = positionInput(*node, inputs, space);
            const std::string normal = normalInput(*node, inputs, space);
            const std::string uv = "hbg_tri_uv_" + suffix;
            const std::string normalValue = "hbg_tri_normal_" + suffix;
            const std::string weights = "hbg_tri_weights_" + suffix;
            const std::string output = m_outputs[node->id].at(0);
            const double maxExponent = node->concretePrecision == ConcretePrecision::Half ? 17.0 : 158.0;
            body << "    vec3 " << uv << " = " << position << " * " << inputs.at(5) << ";\n";
            body << "    vec3 " << normalValue << " = " << normal << ";\n";
            body << "    vec3 " << weights << " = pow(max(abs(" << normalValue
                 << "), vec3(1.192092896e-07, 1.192092896e-07, 1.192092896e-07)), min("
                 << inputs.at(6) << ", "
                 << formatFloat(static_cast<float>(maxExponent)) << "));\n";
            if (!normalType) {
                body << "    " << weights << " /= dot(" << weights << ", vec3(1.0, 1.0, 1.0));\n";
                body << "    vec4 hbg_tri_x_" << suffix << " = texture2D(" << sampler << ", "
                     << uv << ".zy);\n";
                body << "    vec4 hbg_tri_y_" << suffix << " = texture2D(" << sampler << ", "
                     << uv << ".xz);\n";
                body << "    vec4 hbg_tri_z_" << suffix << " = texture2D(" << sampler << ", "
                     << uv << ".xy);\n";
                body << "    " << output << " = hbg_tri_x_" << suffix << " * " << weights
                     << ".x + hbg_tri_y_" << suffix << " * " << weights << ".y + hbg_tri_z_"
                     << suffix << " * " << weights << ".z;\n";
                return;
            }
            provideUnpackNormal();
            body << "    " << weights << " /= (" << weights << ".x + " << weights << ".y + "
                 << weights << ".z);\n";
            const std::string nx = "hbg_tri_x_" + suffix;
            const std::string ny = "hbg_tri_y_" + suffix;
            const std::string nz = "hbg_tri_z_" + suffix;
            body << "    vec3 " << nx << " = hbgSGUnpackNormal(texture2D(" << sampler << ", "
                 << uv << ".zy));\n";
            body << "    vec3 " << ny << " = hbgSGUnpackNormal(texture2D(" << sampler << ", "
                 << uv << ".xz));\n";
            body << "    vec3 " << nz << " = hbgSGUnpackNormal(texture2D(" << sampler << ", "
                 << uv << ".xy));\n";
            body << "    " << nx << " = vec3(" << nx << ".x + " << normalValue << ".z, "
                 << nx << ".y + " << normalValue << ".y, abs(" << nx << ".z) * "
                 << normalValue << ".x);\n";
            body << "    " << ny << " = vec3(" << ny << ".x + " << normalValue << ".x, "
                 << ny << ".y + " << normalValue << ".z, abs(" << ny << ".z) * "
                 << normalValue << ".y);\n";
            body << "    " << nz << " = vec3(" << nz << ".x + " << normalValue << ".x, "
                 << nz << ".y + " << normalValue << ".y, abs(" << nz << ".z) * "
                 << normalValue << ".z);\n";
            const std::string composed = "hbg_tri_composed_" + suffix;
            body << "    vec3 " << composed << " = normalize(" << nx << ".zyx * " << weights
                 << ".x + " << ny << ".xzy * " << weights << ".y + " << nz << ".xyz * "
                 << weights << ".z);\n";
            if (normalOut == "tangent") {
                provideNormalHelpers();
            }
            const auto toWorld = [&](const std::string& value) -> std::string {
                if (space == "world") {
                    return value;
                }
                if (space == "object") {
                    return "normalize(mul(u_model[0], vec4(" + value + ", 0.0)).xyz)";
                }
                if (space == "view") {
                    return "normalize(mul(u_global_invView, vec4(" + value + ", 0.0)).xyz)";
                }
                if (space == "tangent") {
                    return "normalize(gi.tangentWS * (" + value + ").x + gi.bitangentWS * (" +
                           value + ").y + gi.normalWS * (" + value + ").z)";
                }
                return {};
            };
            const auto fromWorld = [&](const std::string& value) -> std::string {
                if (normalOut == "world") {
                    return value;
                }
                if (normalOut == "tangent") {
                    return "hbgSGTransformWorldToTangent(" + value +
                           ", mat3(gi.tangentWS, gi.bitangentWS, gi.normalWS))";
                }
                if (normalOut == "view") {
                    return "normalize(mul(u_global_view, vec4(" + value + ", 0.0)).xyz)";
                }
                return {};
            };
            std::string transformed;
            if (space == normalOut) {
                transformed = composed;
            } else {
                const std::string world = toWorld(composed);
                if (!world.empty()) {
                    transformed = fromWorld(world);
                }
            }
            if (transformed.empty()) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorTargetUnsupported;
                diagnostic.message = "Triplanar normal output space is not supported: " + space +
                                     " -> " + normalOut;
                diagnostic.node = node->id;
                m_diagnostics.push_back(std::move(diagnostic));
                return;
            }
            body << "    " << output << " = vec4(" << transformed << ", 1.0);\n";
            return;
        }
        if (type == "SimpleNoise" || type == "GradientNoise" || type == "Voronoi") {
            const std::string suffix = std::to_string(node->id);
            const std::string uv = uvInput(*node, inputs);
            const std::string hashType = noiseHashType(*node);
            if (type == "SimpleNoise") {
                const bool legacy = hashType == "legacySine";
                const std::string variant = legacy ? "LegacySine" : "Deterministic";
                const std::string hash = hash21Name(legacy ? "legacySine" : "deterministic");
                provideNoiseHash21(legacy ? "legacySine" : "deterministic");
                provideSimpleNoise(variant, hash);
                body << "    " << m_outputs[node->id].at(2) << " = hbgSGSimpleNoise" << variant
                     << "(" << uv << ", " << inputs.at(1) << ");\n";
                return;
            }
            if (type == "GradientNoise") {
                const bool legacy = hashType == "legacyMod";
                const std::string variant = legacy ? "LegacyMod" : "Deterministic";
                const std::string hash = hash21Name(legacy ? "legacyMod" : "deterministic");
                provideNoiseHash21(legacy ? "legacyMod" : "deterministic");
                provideGradientNoise(variant, hash);
                body << "    " << m_outputs[node->id].at(2) << " = hbgSGGradientNoise" << variant
                     << "(" << uv << ", " << inputs.at(1) << ");\n";
                return;
            }
            const bool legacy = hashType == "legacySine";
            const std::string variant = legacy ? "LegacySine" : "Deterministic";
            const std::string hash = hash22Name(legacy ? "legacySine" : "deterministic");
            provideNoiseHash22(legacy ? "legacySine" : "deterministic");
            provideVoronoi(variant, hash);
            body << "    vec2 hbg_voronoi_" << suffix << " = hbgSGVoronoi" << variant << "("
                 << uv << ", " << inputs.at(1) << ", " << inputs.at(2) << ");\n";
            body << "    " << m_outputs[node->id].at(3) << " = hbg_voronoi_" << suffix
                 << ".x;\n";
            body << "    " << m_outputs[node->id].at(4) << " = hbg_voronoi_" << suffix
                 << ".y;\n";
            return;
        }
        if (type == "Contrast" || type == "Saturation" || type == "Hue" ||
            type == "WhiteBalance" || type == "ReplaceColor" || type == "InvertColors" ||
            type == "ChannelMixer") {
            const std::string suffix = std::to_string(node->id);
            const std::string input = inputs.at(0);
            if (type == "Contrast") {
                const std::string midpoint = "hbg_contrast_midpoint_" + suffix;
                body << "    float " << midpoint << " = pow(0.5, 2.2);\n";
                body << "    " << m_outputs[node->id].at(2) << " = (" << input
                     << " - vec3(" << midpoint << ", " << midpoint << ", " << midpoint
                     << ")) * " << inputs.at(1) << " + vec3(" << midpoint << ", "
                     << midpoint << ", " << midpoint << ");\n";
                return;
            }
            if (type == "Saturation") {
                const std::string luma = "hbg_saturation_luma_" + suffix;
                body << "    float " << luma << " = dot(" << input
                     << ", vec3(0.2126729, 0.7151522, 0.0721750));\n";
                body << "    " << m_outputs[node->id].at(2) << " = vec3(" << luma << ", "
                     << luma << ", " << luma << ") + " << inputs.at(1) << " * (" << input
                     << " - vec3(" << luma << ", " << luma << ", " << luma << "));\n";
                return;
            }
            if (type == "ReplaceColor") {
                const std::string distance = "hbg_replace_distance_" + suffix;
                body << "    float " << distance << " = distance(" << inputs.at(1) << ", "
                     << input << ");\n";
                body << "    " << m_outputs[node->id].at(4) << " = lerp(" << inputs.at(2)
                     << ", " << input << ", saturate((" << distance << " - " << inputs.at(3)
                     << ") / max(" << inputs.at(5) << ", 1e-5)));\n";
                return;
            }
            if (type == "ChannelMixer") {
                const Vec3 red = settingVector3(*node, "outRed", Vec3{1.0f, 0.0f, 0.0f});
                const Vec3 green = settingVector3(*node, "outGreen", Vec3{0.0f, 1.0f, 0.0f});
                const Vec3 blue = settingVector3(*node, "outBlue", Vec3{0.0f, 0.0f, 1.0f});
                const std::string redName = "hbg_mixer_red_" + suffix;
                const std::string greenName = "hbg_mixer_green_" + suffix;
                const std::string blueName = "hbg_mixer_blue_" + suffix;
                const auto emitVector = [&](const std::string& name, const Vec3& value) {
                    body << "    vec3 " << name << " = vec3(" << formatFloat(value.x) << ", "
                         << formatFloat(value.y) << ", " << formatFloat(value.z) << ");\n";
                };
                emitVector(redName, red);
                emitVector(greenName, green);
                emitVector(blueName, blue);
                body << "    " << m_outputs[node->id].at(1) << " = vec3(dot(" << input
                     << ", " << redName << "), dot(" << input << ", " << greenName
                     << "), dot(" << input << ", " << blueName << "));\n";
                return;
            }
            if (type == "InvertColors") {
                const int count = std::max(1, channelCount(targetType(*node->findSlot(1))));
                std::vector<std::string> mask;
                mask.push_back(settingBool(*node, "redChannel", false) ? "1.0" : "0.0");
                mask.push_back(settingBool(*node, "greenChannel", false) ? "1.0" : "0.0");
                mask.push_back(settingBool(*node, "blueChannel", false) ? "1.0" : "0.0");
                mask.push_back(settingBool(*node, "alphaChannel", false) ? "1.0" : "0.0");
                const std::string maskValue =
                    count == 1 ? mask.front() : makeVectorLiteralFrom(mask);
                body << "    " << m_outputs[node->id].at(1) << " = abs(" << maskValue << " - "
                     << input << ");\n";
                return;
            }
            if (type == "WhiteBalance") {
                const std::string t1 = "hbg_wb_t1_" + suffix;
                const std::string t2 = "hbg_wb_t2_" + suffix;
                const std::string x = "hbg_wb_x_" + suffix;
                const std::string illuminantY = "hbg_wb_illuminant_y_" + suffix;
                const std::string y = "hbg_wb_y_" + suffix;
                const std::string bigY = "hbg_wb_big_y_" + suffix;
                const std::string bigX = "hbg_wb_big_x_" + suffix;
                const std::string bigZ = "hbg_wb_big_z_" + suffix;
                const std::string l = "hbg_wb_l_" + suffix;
                const std::string m = "hbg_wb_m_" + suffix;
                const std::string s = "hbg_wb_s_" + suffix;
                const std::string lms = "hbg_wb_lms_" + suffix;
                const std::string balance = "hbg_wb_balance_" + suffix;
                const std::string lin = "hbg_wb_lin2lms_" + suffix;
                const std::string lms2lin = "hbg_wb_lms2lin_" + suffix;
                body << "    float " << t1 << " = " << inputs.at(1) << " * 10.0 / 6.0;\n";
                body << "    float " << t2 << " = " << inputs.at(2) << " * 10.0 / 6.0;\n";
                body << "    float " << x << " = 0.31271 - " << t1 << " * (" << t1
                     << " < 0.0 ? 0.1 : 0.05);\n";
                body << "    float " << illuminantY << " = 2.87 * " << x << " - 3.0 * " << x
                     << " * " << x << " - 0.27509507;\n";
                body << "    float " << y << " = " << illuminantY << " + " << t2
                     << " * 0.05;\n";
                body << "    vec3 hbg_wb_w1_" << suffix
                     << " = vec3(0.949237, 1.03542, 1.08728);\n";
                body << "    float " << bigY << " = 1.0;\n";
                body << "    float " << bigX << " = " << bigY << " * " << x << " / " << y
                     << ";\n";
                body << "    float " << bigZ << " = " << bigY << " * (1.0 - " << x << " - "
                     << y << ") / " << y << ";\n";
                body << "    float " << l << " = 0.7328 * " << bigX << " + 0.4296 * " << bigY
                     << " - 0.1624 * " << bigZ << ";\n";
                body << "    float " << m << " = -0.7036 * " << bigX << " + 1.6975 * " << bigY
                     << " + 0.0061 * " << bigZ << ";\n";
                body << "    float " << s << " = 0.0030 * " << bigX << " + 0.0136 * " << bigY
                     << " + 0.9834 * " << bigZ << ";\n";
                body << "    vec3 hbg_wb_w2_" << suffix << " = vec3(" << l << ", " << m
                     << ", " << s << ");\n";
                body << "    vec3 " << balance << " = vec3(hbg_wb_w1_" << suffix << ".x / "
                     << "hbg_wb_w2_" << suffix << ".x, hbg_wb_w1_" << suffix << ".y / "
                     << "hbg_wb_w2_" << suffix << ".y, hbg_wb_w1_" << suffix << ".z / "
                     << "hbg_wb_w2_" << suffix << ".z);\n";
                body << "    mat3 " << lin << " = mat3(0.390405, 0.549941, 0.00892632, "
                     << "0.0708416, 0.963172, 0.00135775, 0.0231082, 0.128021, 0.936245);\n";
                body << "    mat3 " << lms2lin << " = mat3(2.85847, -1.62879, -0.0248910, "
                     << "-0.210182, 1.15820, 0.000324281, -0.0418120, -0.118169, 1.06867);\n";
                body << "    vec3 " << lms << " = mul(" << lin << ", " << input << ");\n";
                body << "    " << lms << " *= " << balance << ";\n";
                body << "    " << m_outputs[node->id].at(3) << " = mul(" << lms2lin << ", "
                     << lms << ");\n";
                return;
            }
            const bool normalized = settingString(*node, "range") == "normalized";
            const std::string k = "hbg_hue_k_" + suffix;
            const std::string p = "hbg_hue_p_" + suffix;
            const std::string q = "hbg_hue_q_" + suffix;
            const std::string d = "hbg_hue_d_" + suffix;
            const std::string e = "hbg_hue_e_" + suffix;
            const std::string v = "hbg_hue_v_" + suffix;
            const std::string hsv = "hbg_hue_hsv_" + suffix;
            const std::string hue = "hbg_hue_hue_" + suffix;
            const std::string k2 = "hbg_hue_k2_" + suffix;
            const std::string p2 = "hbg_hue_p2_" + suffix;
            body << "    vec4 " << k << " = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);\n";
            body << "    vec4 " << p << " = lerp(vec4(" << input << ".b, " << input
                 << ".g, " << k << ".w, " << k << ".z), vec4(" << input << ".g, " << input
                 << ".b, " << k << ".x, " << k << ".y), step(" << input << ".b, " << input
                 << ".g));\n";
            body << "    vec4 " << q << " = lerp(vec4(" << p << ".x, " << p << ".y, " << p
                 << ".w, " << input << ".r), vec4(" << input << ".r, " << p << ".y, " << p
                 << ".z, " << p << ".x), step(" << p << ".x, " << input << ".r));\n";
            body << "    float " << d << " = " << q << ".x - min(" << q << ".w, " << q
                 << ".y);\n";
            body << "    float " << e << " = 1e-4;\n";
            body << "    float " << v << " = (" << d << " == 0.0) ? " << q << ".x : (" << q
                 << ".x + " << e << ");\n";
            body << "    vec3 " << hsv << " = vec3(abs(" << q << ".z + (" << q << ".w - "
                 << q << ".y) / (6.0 * " << d << " + " << e << ")), " << d << " / (" << q
                 << ".x + " << e << "), " << v << ");\n";
            body << "    float " << hue << " = " << hsv << ".x + "
                 << (normalized ? inputs.at(1) : inputs.at(1) + " / 360.0") << ";\n";
            body << "    " << hsv << ".x = (" << hue << " < 0.0) ? " << hue
                 << " + 1.0 : ((" << hue << " > 1.0) ? " << hue << " - 1.0 : " << hue
                 << ");\n";
            body << "    vec4 " << k2 << " = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);\n";
            body << "    vec3 " << p2 << " = abs(frac(vec3(" << hsv << ".x, " << hsv
                 << ".x, " << hsv << ".x) + vec3(" << k2 << ".x, " << k2 << ".y, " << k2
                 << ".z)) * 6.0 - vec3(" << k2 << ".w, " << k2 << ".w, " << k2 << ".w));\n";
            body << "    " << m_outputs[node->id].at(2) << " = vec3(" << hsv << ".z, "
                 << hsv << ".z, " << hsv << ".z) * lerp(vec3(" << k2 << ".x, " << k2
                 << ".x, " << k2 << ".x), saturate(" << p2 << " - vec3(" << k2 << ".x, "
                 << k2 << ".x, " << k2 << ".x)), " << hsv << ".y);\n";
            return;
        }
        if (type == "Blend") {
            const std::string suffix = std::to_string(node->id);
            const std::string base = inputs.at(0);
            const std::string blend = inputs.at(1);
            const std::string opacity = inputs.at(3);
            const std::string output = m_outputs[node->id].at(2);
            const std::string valueType =
                typeName(targetType(*node->findSlot(2)), node->concretePrecision);
            const std::string mode = normalizedSetting(*node, "mode", "overlay");
            const auto emitLerp = [&](const std::string& source) {
                body << "    " << output << " = lerp(" << source << ", " << output << ", "
                     << opacity << ");\n";
            };
            const auto emitSimple = [&](const std::string& expression) {
                body << "    " << output << " = " << expression << ";\n";
                emitLerp(base);
            };
            const auto emitTemp = [&](const std::string& name, const std::string& expression) {
                body << "    " << valueType << " " << name << " = " << expression << ";\n";
            };
            const std::string result1 = "hbg_blend_result1_" + suffix;
            const std::string result2 = "hbg_blend_result2_" + suffix;
            const std::string zeroOrOne = "hbg_blend_zero_or_one_" + suffix;

            if (mode == "overwrite") {
                body << "    " << output << " = lerp(" << base << ", " << blend << ", "
                     << opacity << ");\n";
                return;
            }
            if (mode == "burn") {
                emitSimple("1.0 - (1.0 - " + blend + ") / (" + base + " + 0.000000000001)");
                return;
            }
            if (mode == "darken") {
                emitSimple("min(" + blend + ", " + base + ")");
                return;
            }
            if (mode == "difference") {
                emitSimple("abs(" + blend + " - " + base + ")");
                return;
            }
            if (mode == "dodge") {
                emitSimple(base + " / (1.0 - clamp(" + blend + ", 0.000001, 0.999999))");
                return;
            }
            if (mode == "divide") {
                emitSimple(base + " / (" + blend + " + 0.000000000001)");
                return;
            }
            if (mode == "exclusion") {
                emitSimple(blend + " + " + base + " - (2.0 * " + blend + " * " + base + ")");
                return;
            }
            if (mode == "hardlight") {
                emitTemp(result1, "1.0 - 2.0 * (1.0 - " + base + ") * (1.0 - " + blend + ")");
                emitTemp(result2, "2.0 * " + base + " * " + blend);
                emitTemp(zeroOrOne, "step(" + blend + ", 0.5)");
                body << "    " << output << " = " << result2 << " * " << zeroOrOne
                     << " + (1.0 - " << zeroOrOne << ") * " << result1 << ";\n";
                emitLerp(base);
                return;
            }
            if (mode == "hardmix") {
                emitSimple("step(1.0 - " + base + ", " + blend + ")");
                return;
            }
            if (mode == "lighten") {
                emitSimple("max(" + blend + ", " + base + ")");
                return;
            }
            if (mode == "linearburn") {
                emitSimple(base + " + " + blend + " - 1.0");
                return;
            }
            if (mode == "lineardodge") {
                emitSimple(base + " + " + blend);
                return;
            }
            if (mode == "linearlight") {
                emitSimple("lerp(max(" + base + " + (2.0 * " + blend + ") - 1.0, 0.0), min(" +
                           base + " + 2.0 * (" + blend + " - 0.5), 1.0), step(0.5, " +
                           blend + "))");
                return;
            }
            if (mode == "linearlightaddsub") {
                emitSimple(blend + " + 2.0 * " + base + " - 1.0");
                return;
            }
            if (mode == "multiply") {
                emitSimple(base + " * " + blend);
                return;
            }
            if (mode == "negation") {
                emitSimple("1.0 - abs(1.0 - " + blend + " - " + base + ")");
                return;
            }
            if (mode == "overlay") {
                emitTemp(result1, "1.0 - 2.0 * (1.0 - " + base + ") * (1.0 - " + blend + ")");
                emitTemp(result2, "2.0 * " + base + " * " + blend);
                emitTemp(zeroOrOne, "step(" + base + ", 0.5)");
                body << "    " << output << " = " << result2 << " * " << zeroOrOne
                     << " + (1.0 - " << zeroOrOne << ") * " << result1 << ";\n";
                emitLerp(base);
                return;
            }
            if (mode == "pinlight") {
                const std::string check = "hbg_blend_check_" + suffix;
                emitTemp(check, "step(0.5, " + blend + ")");
                emitTemp(result1, check + " * max(2.0 * (" + base + " - 0.5), " + blend + ")");
                body << "    " << output << " = " << result1 << " + (1.0 - " << check
                     << ") * min(2.0 * " << base << ", " << blend << ");\n";
                emitLerp(base);
                return;
            }
            if (mode == "screen") {
                emitSimple("1.0 - (1.0 - " + blend + ") * (1.0 - " + base + ")");
                return;
            }
            if (mode == "softlight") {
                emitTemp(result1, "2.0 * " + base + " * " + blend + " + " + base + " * " +
                                      base + " * (1.0 - 2.0 * " + blend + ")");
                emitTemp(result2, "sqrt(" + base + ") * (2.0 * " + blend + " - 1.0) + 2.0 * " +
                                      base + " * (1.0 - " + blend + ")");
                emitTemp(zeroOrOne, "step(0.5, " + blend + ")");
                body << "    " << output << " = " << result2 << " * " << zeroOrOne
                     << " + (1.0 - " << zeroOrOne << ") * " << result1 << ";\n";
                emitLerp(base);
                return;
            }
            if (mode == "subtract") {
                emitSimple(base + " - " + blend);
                return;
            }
            if (mode == "vividlight") {
                const std::string clampedBase = "hbg_blend_base_" + suffix;
                emitTemp(clampedBase, "clamp(" + base + ", 0.000001, 0.999999)");
                emitTemp(result1, "1.0 - (1.0 - " + blend + ") / (2.0 * " + clampedBase + ")");
                emitTemp(result2, blend + " / (2.0 * (1.0 - " + clampedBase + "))");
                emitTemp(zeroOrOne, "step(0.5, " + clampedBase + ")");
                body << "    " << output << " = " << result2 << " * " << zeroOrOne
                     << " + (1.0 - " << zeroOrOne << ") * " << result1 << ";\n";
                emitLerp(clampedBase);
                return;
            }
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorInvalidValue;
            diagnostic.message = "unknown blend mode: " + mode;
            diagnostic.node = node->id;
            m_diagnostics.push_back(std::move(diagnostic));
            return;
        }
        if (type == "Dither") {
            const std::string suffix = std::to_string(node->id);
            const SlotInstance* screenSlot = node->findSlot(1);
            const bool explicitScreen = m_graph.incomingEdge(node->id, 1) != nullptr ||
                                        (screenSlot != nullptr && screenSlot->hasExplicitValue());
            const std::string screen = explicitScreen ? inputs.at(1) : "gi.screenPos.xy";
            const std::string uv = "hbg_dither_uv_" + suffix;
            const std::string index = "hbg_dither_index_" + suffix;
            const std::string thresholds = "hbg_dither_thresholds_" + suffix;
            body << "    static const float " << thresholds << "[16] =\n"
                 << "    {\n"
                 << "        1.0 / 17.0,  9.0 / 17.0,  3.0 / 17.0, 11.0 / 17.0,\n"
                 << "        13.0 / 17.0,  5.0 / 17.0, 15.0 / 17.0,  7.0 / 17.0,\n"
                 << "        4.0 / 17.0, 12.0 / 17.0,  2.0 / 17.0, 10.0 / 17.0,\n"
                 << "        16.0 / 17.0,  8.0 / 17.0, 14.0 / 17.0,  6.0 / 17.0\n"
                 << "    };\n";
            body << "    vec2 " << uv << " = " << screen << " * u_global_screenSize.xy;\n";
            body << "    uint " << index << " = (uint(" << uv << ".x) % 4u) * 4u + uint("
                 << uv << ".y) % 4u;\n";
            body << "    " << m_outputs[node->id].at(2) << " = " << inputs.at(0) << " - "
                 << thresholds << "[" << index << "];\n";
            return;
        }
        if (type == "FadeTransition") {
            provideFadeTransition();
            body << "    " << m_outputs[node->id].at(0) << " = hbgSGFadeTransition("
                 << inputs.at(1) << ", " << inputs.at(2) << ", " << inputs.at(3) << ");\n";
            return;
        }
        if (type == "ChannelMask") {
            const std::string input = inputs.at(0);
            const std::string output = m_outputs[node->id].at(1);
            const int count =
                std::max(1, channelCount(targetType(*node->findSlot(1))));
            int mask = channelMaskSetting(*node);
            if (mask >= (1 << count)) {
                mask = -1;
            }
            if (mask == 0) {
                body << "    " << output << " = 0.0;\n";
                return;
            }
            if (mask == -1) {
                body << "    " << output << " = " << input << ";\n";
                return;
            }
            if (count == 1) {
                body << "    " << output << " = " << component(input, 0) << ";\n";
                return;
            }
            std::vector<std::string> values;
            for (int index = 0; index < count; ++index) {
                values.push_back((mask & (1 << index)) ? component(input, index) : "0.0");
            }
            body << "    " << output << " = " << makeVectorLiteralFrom(values) << ";\n";
            return;
        }
        if (type == "ColorMask") {
            const std::string suffix = std::to_string(node->id);
            const std::string distance = "hbg_color_mask_distance_" + suffix;
            body << "    float " << distance << " = distance(" << inputs.at(1) << ", "
                 << inputs.at(0) << ");\n";
            body << "    " << m_outputs[node->id].at(3) << " = saturate(1.0 - (" << distance
                 << " - " << inputs.at(2) << ") / max(" << inputs.at(4)
                 << ", 1e-5));\n";
            return;
        }
        if (type == "ColorspaceConversion") {
            const std::string suffix = std::to_string(node->id);
            const std::string from = normalizedSetting(*node, "from", "rgb");
            const std::string to = normalizedSetting(*node, "to", "rgb");
            const bool validFrom = from == "rgb" || from == "linear" || from == "hsv";
            const bool validTo = to == "rgb" || to == "linear" || to == "hsv";
            if (!validFrom || !validTo) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorInvalidValue;
                diagnostic.message = "unsupported colorspace conversion: " + from + " -> " + to;
                diagnostic.node = node->id;
                m_diagnostics.push_back(std::move(diagnostic));
                return;
            }
            const std::string input = "hbg_cs_in_" + suffix;
            const std::string output = m_outputs[node->id].at(1);
            body << "    vec3 " << input << " = " << inputs.at(0) << ";\n";
            if (from == to) {
                body << "    " << output << " = " << input << ";\n";
                return;
            }
            const auto emitRgbToLinear = [&](const std::string& source,
                                             const std::string& target) {
                const std::string lo = "hbg_cs_lo_" + suffix;
                const std::string hi = "hbg_cs_hi_" + suffix;
                body << "    vec3 " << lo << " = " << source << " / 12.92;\n";
                body << "    vec3 " << hi << " = pow(max(abs((" << source
                     << " + 0.055) / 1.055), 1.192092896e-07), vec3(2.4, 2.4, 2.4));\n";
                body << "    " << target << " = lerp(" << hi << ", " << lo << ", step("
                     << source << ", vec3(0.04045, 0.04045, 0.04045)));\n";
            };
            const auto emitLinearToRgb = [&](const std::string& source,
                                             const std::string& target) {
                const std::string lo = "hbg_cs_lo_" + suffix;
                const std::string hi = "hbg_cs_hi_" + suffix;
                body << "    vec3 " << lo << " = " << source << " * 12.92;\n";
                body << "    vec3 " << hi << " = (pow(max(abs(" << source
                     << "), 1.192092896e-07), vec3(1.0 / 2.4, 1.0 / 2.4, 1.0 / 2.4)) * 1.055) - 0.055;\n";
                body << "    " << target << " = lerp(" << hi << ", " << lo << ", step("
                     << source << ", vec3(0.0031308, 0.0031308, 0.0031308)));\n";
            };
            if (from == "rgb" && to == "linear") {
                emitRgbToLinear(input, output);
                return;
            }
            if (from == "linear" && to == "rgb") {
                emitLinearToRgb(input, output);
                return;
            }
            if (from == "hsv" && to == "rgb") {
                const std::string k = "hbg_cs_k_" + suffix;
                const std::string p = "hbg_cs_p_" + suffix;
                body << "    vec4 " << k << " = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);\n";
                body << "    vec3 " << p << " = abs(frac(vec3(" << input << ".x, " << input
                     << ".x, " << input << ".x) + vec3(" << k << ".x, " << k << ".y, "
                     << k << ".z)) * 6.0 - vec3(" << k << ".w, " << k << ".w, " << k
                     << ".w));\n";
                body << "    " << output << " = " << input << ".z * lerp(vec3(" << k
                     << ".x, " << k << ".x, " << k << ".x), saturate(" << p << " - vec3("
                     << k << ".x, " << k << ".x, " << k << ".x)), " << input << ".y);\n";
                return;
            }
            if (from == "hsv" && to == "linear") {
                const std::string k = "hbg_cs_k_" + suffix;
                const std::string p = "hbg_cs_p_" + suffix;
                const std::string rgb = "hbg_cs_rgb_" + suffix;
                body << "    vec4 " << k << " = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);\n";
                body << "    vec3 " << p << " = abs(frac(vec3(" << input << ".x, " << input
                     << ".x, " << input << ".x) + vec3(" << k << ".x, " << k << ".y, "
                     << k << ".z)) * 6.0 - vec3(" << k << ".w, " << k << ".w, " << k
                     << ".w));\n";
                body << "    vec3 " << rgb << " = " << input << ".z * lerp(vec3(" << k
                     << ".x, " << k << ".x, " << k << ".x), saturate(" << p << " - vec3("
                     << k << ".x, " << k << ".x, " << k << ".x)), " << input << ".y);\n";
                emitRgbToLinear(rgb, output);
                return;
            }
            const std::string k = "hbg_cs_k_" + suffix;
            const std::string p = "hbg_cs_p_" + suffix;
            const std::string q = "hbg_cs_q_" + suffix;
            const std::string d = "hbg_cs_d_" + suffix;
            const std::string e = "hbg_cs_e_" + suffix;
            std::string source = input;
            if (from == "linear") {
                const std::string rgb = "hbg_cs_rgb_" + suffix;
                body << "    vec3 " << rgb << ";\n";
                emitLinearToRgb(input, rgb);
                source = rgb;
            }
            body << "    vec4 " << k << " = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);\n";
            body << "    vec4 " << p << " = lerp(vec4(" << source << ".b, " << source
                 << ".g, " << k << ".w, " << k << ".z), vec4(" << source << ".g, " << source
                 << ".b, " << k << ".x, " << k << ".y), step(" << source << ".b, " << source
                 << ".g));\n";
            body << "    vec4 " << q << " = lerp(vec4(" << p << ".x, " << p << ".y, " << p
                 << ".w, " << source << ".r), vec4(" << source << ".r, " << p << ".y, " << p
                 << ".z, " << p << ".x), step(" << p << ".x, " << source << ".r));\n";
            body << "    float " << d << " = " << q << ".x - min(" << q << ".w, " << q
                 << ".y);\n";
            body << "    float " << e << " = 1e-10;\n";
            body << "    float hbg_cs_v_" << suffix << " = (" << d << " == 0.0) ? " << q
                 << ".x : (" << q << ".x + " << e << ");\n";
            body << "    " << output << " = vec3(abs(" << q << ".z + (" << q << ".w - "
                 << q << ".y) / (6.0 * " << d << " + " << e << ")), " << d << " / ("
                 << q << ".x + " << e << "), " << (to == "hsv" ? ("hbg_cs_v_" + suffix)
                                                                  : (q + ".x"))
                 << ");\n";
            return;
        }
        if (type == "NormalBlend" || type == "NormalStrength" ||
            type == "NormalReconstructZ" || type == "NormalFromHeight" ||
            type == "NormalFromTexture" || type == "NormalUnpack") {
            const std::string suffix = std::to_string(node->id);
            const std::string input = inputs.at(0);
            if (type == "NormalBlend") {
                provideNormalHelpers();
                const std::string output = m_outputs[node->id].at(2);
                if (settingString(*node, "mode") == "reoriented") {
                    const std::string t = "hbg_normal_blend_t_" + suffix;
                    const std::string u = "hbg_normal_blend_u_" + suffix;
                    body << "    vec3 " << t << " = " << input
                         << " + vec3(0.0, 0.0, 1.0);\n";
                    body << "    vec3 " << u << " = " << inputs.at(1)
                         << " * vec3(-1.0, -1.0, 1.0);\n";
                    body << "    " << output << " = (" << t << " / " << t << ".z) * dot("
                         << t << ", " << u << ") - " << u << ";\n";
                } else {
                    body << "    " << output << " = hbgSGSafeNormalize(vec3(" << input
                         << ".x + " << inputs.at(1) << ".x, " << input << ".y + "
                         << inputs.at(1) << ".y, " << input << ".z * " << inputs.at(1)
                         << ".z));\n";
                }
                return;
            }
            if (type == "NormalStrength") {
                body << "    " << m_outputs[node->id].at(2) << " = vec3(" << input
                     << ".x * " << inputs.at(1) << ", " << input << ".y * " << inputs.at(1)
                     << ", lerp(1.0, " << input << ".z, saturate(" << inputs.at(1)
                     << ")));\n";
                return;
            }
            if (type == "NormalReconstructZ") {
                const std::string reconstruct = "hbg_normal_reconstruct_z_" + suffix;
                body << "    float " << reconstruct << " = sqrt(1.0 - saturate(dot("
                     << input << ", " << input << ")));\n";
                body << "    " << m_outputs[node->id].at(2) << " = normalize(vec3(" << input
                     << ".x, " << input << ".y, " << reconstruct << "));\n";
                return;
            }
            if (type == "NormalUnpack") {
                if (settingString(*node, "space") == "object") {
                    body << "    " << m_outputs[node->id].at(1) << " = vec3(" << input
                         << ".x * 2.0 - 1.0, " << input << ".y * 2.0 - 1.0, " << input
                         << ".z * 2.0 - 1.0);\n";
                } else {
                    const std::string packedA = "hbg_normal_unpack_a_" + suffix;
                    const std::string xy = "hbg_normal_unpack_xy_" + suffix;
                    body << "    float " << packedA << " = " << input << ".w * " << input
                         << ".x;\n";
                    body << "    vec2 " << xy << " = vec2(" << packedA << ", " << input
                         << ".y) * 2.0 - 1.0;\n";
                    body << "    " << m_outputs[node->id].at(1) << " = vec3(" << xy
                         << ".x, " << xy << ".y, sqrt(max(1.0e-16, 1.0 - saturate(dot("
                         << xy << ", " << xy << ")))));\n";
                }
                return;
            }
            if (type == "NormalFromHeight") {
                provideNormalHelpers();
                const std::string tbn = "hbg_normal_height_tbn_" + suffix;
                const std::string dx = "hbg_normal_height_dx_" + suffix;
                const std::string dy = "hbg_normal_height_dy_" + suffix;
                const std::string crossX = "hbg_normal_height_cross_x_" + suffix;
                const std::string crossY = "hbg_normal_height_cross_y_" + suffix;
                const std::string d = "hbg_normal_height_d_" + suffix;
                const std::string sgn = "hbg_normal_height_sgn_" + suffix;
                const std::string surface = "hbg_normal_height_surface_" + suffix;
                const std::string dhdx = "hbg_normal_height_dhdx_" + suffix;
                const std::string dhdy = "hbg_normal_height_dhdy_" + suffix;
                const std::string gradient = "hbg_normal_height_gradient_" + suffix;
                const std::string output = m_outputs[node->id].at(1);
                body << "    mat3 " << tbn << " = mat3(gi.tangentWS, gi.bitangentWS, gi.normalWS);\n";
                body << "    vec3 " << dx << " = ddx(gi.positionWS);\n";
                body << "    vec3 " << dy << " = ddy(gi.positionWS);\n";
                body << "    vec3 " << crossX << " = cross(" << tbn << "[2], " << dx
                     << ");\n";
                body << "    vec3 " << crossY << " = cross(" << dy << ", " << tbn
                     << "[2]);\n";
                body << "    float " << d << " = dot(" << dx << ", " << crossY << ");\n";
                body << "    float " << sgn << " = " << d << " < 0.0 ? -1.0 : 1.0;\n";
                body << "    float " << surface << " = " << sgn
                     << " / max(0.000000000000001192093, abs(" << d << "));\n";
                body << "    float " << dhdx << " = ddx(" << input << ");\n";
                body << "    float " << dhdy << " = ddy(" << input << ");\n";
                body << "    vec3 " << gradient << " = " << surface << " * (" << dhdx
                     << " * " << crossY << " + " << dhdy << " * " << crossX << ");\n";
                body << "    " << output << " = hbgSGSafeNormalize(" << tbn << "[2] - "
                     << inputs.at(2) << " * " << gradient << ");\n";
                if (settingString(*node, "outputSpace") != "world") {
                    body << "    " << output << " = hbgSGTransformWorldToTangent(" << output
                         << ", " << tbn << ");\n";
                }
                return;
            }
            const PropertyInfo* property = findTexture(*node, ValueType::Texture2D, 0);
            if (property == nullptr) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorMissingInput;
                diagnostic.message = "normal from texture node has no texture property";
                diagnostic.node = node->id;
                m_diagnostics.push_back(std::move(diagnostic));
                return;
            }
            const SlotInstance* uvSlot = node->findSlot(1);
            const bool explicitUv = m_graph.incomingEdge(node->id, 1) != nullptr ||
                                    (uvSlot != nullptr && uvSlot->hasExplicitValue());
            const std::string uv = applyTextureTransform(
                explicitUv ? inputs.at(1) : "gi.uv0", textureTransformForInput(*node, 0));
            const std::string sampler = "s_sg_" + property->refName;
            const std::string offset = "hbg_normal_texture_offset_" + suffix;
            const std::string offsetU = "hbg_normal_texture_offset_u_" + suffix;
            const std::string offsetV = "hbg_normal_texture_offset_v_" + suffix;
            const std::string baseSample = "hbg_normal_texture_base_" + suffix;
            const std::string uSample = "hbg_normal_texture_u_" + suffix;
            const std::string vSample = "hbg_normal_texture_v_" + suffix;
            const std::string va = "hbg_normal_texture_va_" + suffix;
            const std::string vb = "hbg_normal_texture_vb_" + suffix;
            body << "    float " << offset << " = pow(" << inputs.at(3)
                 << ", 3.0) * 0.1;\n";
            body << "    vec2 " << offsetU << " = " << uv << " + vec2(" << offset
                 << ", 0.0);\n";
            body << "    vec2 " << offsetV << " = " << uv << " + vec2(0.0, " << offset
                 << ");\n";
            body << "    float " << baseSample << " = texture2D(" << sampler << ", " << uv
                 << ").r;\n";
            body << "    float " << uSample << " = texture2D(" << sampler << ", " << offsetU
                 << ").r;\n";
            body << "    float " << vSample << " = texture2D(" << sampler << ", " << offsetV
                 << ").r;\n";
            body << "    vec3 " << va << " = vec3(1.0, 0.0, (" << uSample << " - "
                 << baseSample << ") * " << inputs.at(4) << ");\n";
            body << "    vec3 " << vb << " = vec3(0.0, 1.0, (" << vSample << " - "
                 << baseSample << ") * " << inputs.at(4) << ");\n";
            body << "    " << m_outputs[node->id].at(5) << " = normalize(cross(" << va
                 << ", " << vb << "));\n";
            return;
        }
        if (type == "Checkerboard" || type == "Ellipse" || type == "Polygon" ||
            type == "Rectangle" || type == "RoundedPolygon" ||
            type == "RoundedRectangle") {
            const std::string suffix = std::to_string(node->id);
            const std::string uv = uvInput(*node, inputs);
            const std::string output = m_outputs[node->id].begin()->second;
            if (type == "Checkerboard") {
                const std::string p = "hbg_checker_p_" + suffix;
                const std::string d3 = "hbg_checker_d3_" + suffix;
                const std::string derivatives = "hbg_checker_derivatives_" + suffix;
                const std::string duv = "hbg_checker_duv_" + suffix;
                const std::string scale = "hbg_checker_scale_" + suffix;
                const std::string freq = "hbg_checker_freq_" + suffix;
                const std::string alpha2 = "hbg_checker_alpha2_" + suffix;
                const std::string alpha = "hbg_checker_alpha_" + suffix;
                body << "    vec2 " << p << " = (" << uv << " + 0.5) * " << inputs.at(3)
                     << ";\n";
                body << "    vec2 " << d3 << " = 4.0 * abs(frac(" << p
                     << " + 0.25) - 0.5) - 1.0;\n";
                body << "    vec4 " << derivatives << " = vec4(ddx(" << p << "), ddy(" << p
                     << "));\n";
                body << "    vec2 " << duv << " = sqrt(vec2(dot(" << derivatives << ".xz, "
                     << derivatives << ".xz), dot(" << derivatives << ".yw, " << derivatives
                     << ".yw)));\n";
                body << "    vec2 " << scale << " = 0.35 / max(" << duv
                     << ", vec2(1e-5, 1e-5));\n";
                body << "    float " << freq << " = sqrt(clamp(1.1 - max(" << duv << ".x, "
                     << duv << ".y), 0.0, 1.0));\n";
                body << "    vec2 " << alpha2 << " = clamp(" << d3 << " * " << scale
                     << ", vec2(-1.0, -1.0), vec2(1.0, 1.0));\n";
                body << "    float " << alpha << " = saturate(0.5 + 0.5 * " << alpha2
                     << ".x * " << alpha2 << ".y * " << freq << ");\n";
                body << "    " << output << " = lerp(" << inputs.at(1) << ", "
                     << inputs.at(2) << ", vec3(" << alpha << ", " << alpha << ", " << alpha << "));\n";
                return;
            }
            if (type == "Ellipse") {
                const std::string distance = "hbg_ellipse_d_" + suffix;
                body << "    float " << distance << " = length((" << uv
                     << " * 2.0 - 1.0) / vec2(" << inputs.at(2) << ", " << inputs.at(3)
                     << "));\n";
                body << "    " << output << " = saturate((1.0 - " << distance
                     << ") / max(fwidth(" << distance << "), 1e-5));\n";
                return;
            }
            if (type == "Polygon") {
                const std::string aWidth = "hbg_polygon_aw_" + suffix;
                const std::string aHeight = "hbg_polygon_ah_" + suffix;
                const std::string p = "hbg_polygon_p_" + suffix;
                const std::string coord = "hbg_polygon_coord_" + suffix;
                const std::string r = "hbg_polygon_r_" + suffix;
                const std::string distance = "hbg_polygon_d_" + suffix;
                body << "    float hbg_polygon_pi_" << suffix << " = HBG_PI;\n";
                body << "    float " << aWidth << " = " << inputs.at(2)
                     << " * cos(hbg_polygon_pi_" << suffix << " / " << inputs.at(1)
                     << ");\n";
                body << "    float " << aHeight << " = " << inputs.at(3)
                     << " * cos(hbg_polygon_pi_" << suffix << " / " << inputs.at(1)
                     << ");\n";
                body << "    vec2 " << p << " = (" << uv << " * 2.0 - 1.0) / vec2("
                     << aWidth << ", " << aHeight << ");\n";
                body << "    " << p << ".y *= -1.0;\n";
                body << "    float " << coord << " = atan2(" << p << ".x, " << p << ".y);\n";
                body << "    float " << r << " = 2.0 * hbg_polygon_pi_" << suffix << " / "
                     << inputs.at(1) << ";\n";
                body << "    float " << distance << " = cos(floor(0.5 + " << coord << " / "
                     << r << ") * " << r << " - " << coord << ") * length(" << p << ");\n";
                body << "    " << output << " = saturate((1.0 - " << distance
                     << ") / max(fwidth(" << distance << "), 1e-5));\n";
                return;
            }
            if (type == "Rectangle") {
                const bool nicest = settingString(*node, "clampType") == "nicest";
                if (!nicest) {
                    const std::string d = "hbg_rect_d_" + suffix;
                    body << "    vec2 " << d << " = abs(" << uv << " * 2.0 - 1.0) - vec2("
                         << inputs.at(1) << ", " << inputs.at(2) << ");\n";
                    body << "    " << d << " = saturate(1.0 - " << d
                         << " / max(fwidth(" << d << "), vec2(1e-5, 1e-5)));\n";
                    body << "    " << output << " = min(" << d << ".x, " << d << ".y);\n";
                } else {
                    const std::string p = "hbg_rect_p_" + suffix;
                    const std::string w = "hbg_rect_w_" + suffix;
                    const std::string f = "hbg_rect_f_" + suffix;
                    const std::string k = "hbg_rect_k_" + suffix;
                    const std::string o = "hbg_rect_o_" + suffix;
                    body << "    vec2 " << p << " = " << uv << " * 2.0 - 1.0;\n";
                    body << "    vec2 " << w << " = vec2(" << inputs.at(1) << ", "
                         << inputs.at(2) << ");\n";
                    body << "    vec2 " << f << " = min(fwidth(" << p << "), vec2(0.5, 0.5));\n";
                    body << "    vec2 " << k << " = 1.0 / " << f << ";\n";
                    body << "    vec2 " << o << " = saturate(vec2(0.5, 0.5) + " << k << " * ("
                         << w << " - abs(" << p << ")));\n";
                    body << "    " << o << " = min(" << o << ", " << k << " * " << w
                         << " * 2.0);\n";
                    body << "    " << output << " = " << o << ".x * " << o << ".y;\n";
                }
                return;
            }
            if (type == "RoundedRectangle") {
                const std::string radius = "hbg_rounded_rect_radius_" + suffix;
                const std::string p = "hbg_rounded_rect_p_" + suffix;
                const std::string distance = "hbg_rounded_rect_d_" + suffix;
                body << "    float " << radius << " = max(min(min(abs(" << inputs.at(3)
                     << " * 2.0), abs(" << inputs.at(1) << ")), abs(" << inputs.at(2)
                     << ")), 1e-5);\n";
                body << "    vec2 " << p << " = abs(" << uv << " * 2.0 - 1.0) - vec2("
                     << inputs.at(1) << ", " << inputs.at(2) << ") + " << radius << ";\n";
                body << "    float " << distance << " = length(max(vec2(0.0, 0.0), " << p
                     << ")) / " << radius << ";\n";
                body << "    " << output << " = saturate((1.0 - " << distance
                     << ") / max(fwidth(" << distance << "), 1e-5));\n";
                return;
            }
            const std::string p = "hbg_rp_p_" + suffix;
            const std::string roundness = "hbg_rp_roundness_" + suffix;
            const std::string iSides = "hbg_rp_i_sides_" + suffix;
            const std::string fullAngle = "hbg_rp_full_angle_" + suffix;
            const std::string halfAngle = "hbg_rp_half_angle_" + suffix;
            const std::string diagonal = "hbg_rp_diagonal_" + suffix;
            const std::string chamferAngle = "hbg_rp_chamfer_angle_" + suffix;
            const std::string remainingAngle = "hbg_rp_remaining_angle_" + suffix;
            const std::string ratio = "hbg_rp_ratio_" + suffix;
            const std::string chamferCenter = "hbg_rp_chamfer_center_" + suffix;
            const std::string distA = "hbg_rp_dist_a_" + suffix;
            const std::string distB = "hbg_rp_dist_b_" + suffix;
            const std::string polar = "hbg_rp_polar_" + suffix;
            const std::string angleRatio = "hbg_rp_angle_ratio_" + suffix;
            const std::string distC = "hbg_rp_dist_c_" + suffix;
            const std::string shape = "hbg_rp_shape_" + suffix;
            body << "    vec2 " << p << " = " << uv << " * 2.0 + vec2(-1.0, -1.0);\n";
            body << "    " << p << ".x /= " << inputs.at(1) << " + ("
                 << inputs.at(1) << " == 0.0 ? 1e-6 : 0.0);\n";
            body << "    " << p << ".y /= " << inputs.at(2) << " + ("
                 << inputs.at(2) << " == 0.0 ? 1e-6 : 0.0);\n";
            body << "    float " << roundness << " = clamp(" << inputs.at(4)
                 << ", 1e-6, 1.0);\n";
            body << "    float " << iSides << " = floor(abs(" << inputs.at(3) << "));\n";
            body << "    float " << fullAngle << " = 2.0 * HBG_PI / " << iSides << ";\n";
            body << "    float " << halfAngle << " = " << fullAngle << " / 2.0;\n";
            body << "    float " << diagonal << " = 1.0 / cos(" << halfAngle << ");\n";
            body << "    float " << chamferAngle << " = " << roundness << " * " << halfAngle
                 << ";\n";
            body << "    float " << remainingAngle << " = " << halfAngle << " - "
                 << chamferAngle << ";\n";
            body << "    float " << ratio << " = tan(" << remainingAngle << ") / tan("
                 << halfAngle << ");\n";
            body << "    vec2 " << chamferCenter << " = vec2(cos(" << halfAngle << "), sin("
                 << halfAngle << ")) * " << ratio << " * " << diagonal << ";\n";
            body << "    float " << distA << " = length(" << chamferCenter << ");\n";
            body << "    float " << distB << " = 1.0 - " << chamferCenter << ".x;\n";
            body << "    " << p << " *= " << diagonal << ";\n";
            body << "    vec2 " << polar << " = vec2(atan2(" << p << ".y, " << p
                 << ".x), length(" << p << "));\n";
            body << "    " << polar << ".x += HBG_PI * 0.5 + 2.0 * HBG_PI;\n";
            body << "    " << polar << ".x = fmod(" << polar << ".x + " << halfAngle << ", "
                 << fullAngle << ");\n";
            body << "    " << polar << ".x = abs(" << polar << ".x - " << halfAngle << ");\n";
            body << "    " << p << " = vec2(cos(" << polar << ".x), sin(" << polar << ".x)) * "
                 << polar << ".y;\n";
            body << "    float " << angleRatio << " = 1.0 - (" << polar << ".x - "
                 << remainingAngle << ") / " << chamferAngle << ";\n";
            body << "    float " << distC << " = sqrt(" << distA << " * " << distA << " + "
                 << distB << " * " << distB << " - 2.0 * " << distA << " * " << distB
                 << " * cos(HBG_PI - " << halfAngle << " * " << angleRatio << "));\n";
            body << "    float " << shape << " = lerp(" << p << ".x, " << polar << ".y / "
                 << distC << ", " << halfAngle << " - " << polar << ".x < " << chamferAngle
                 << " ? 1.0 : 0.0);\n";
            body << "    " << output << " = saturate((1.0 - " << shape
                 << ") / max(fwidth(" << shape << "), 1e-5));\n";
            return;
        }
        if (type == "ScreenPosition") {
            const std::string mode = normalizedSetting(*node, "mode", "default");
            const std::string output = m_outputs[node->id].begin()->second;
            if (mode == "raw") {
                body << "    " << output << " = gi.screenPos;\n";
            } else if (mode == "center") {
                body << "    " << output
                     << " = vec4(gi.screenPos.xy * 2.0 - 1.0, 0.0, 0.0);\n";
            } else if (mode == "tiled") {
                body << "    " << output
                     << " = frac(vec4((gi.screenPos.x * 2.0 - 1.0) * u_global_screenSize.x / "
                        "u_global_screenSize.y, gi.screenPos.y * 2.0 - 1.0, 0.0, 0.0));\n";
            } else if (mode == "pixel") {
                body << "    " << output
                     << " = vec4(gi.screenPos.xy * u_global_screenSize.xy, 0.0, 0.0);\n";
            } else {
                body << "    " << output << " = vec4(gi.screenPos.xy, 0.0, 0.0);\n";
            }
            return;
        }
        if (type == "Position" || type == "NormalVector" ||
            type == "TangentVector" || type == "BitangentVector" ||
            type == "ViewDirection" || type == "ViewVector") {
            const std::string space = coordinateSpace(*node, "space", "world");
            std::string expression;
            if (type == "Position") {
                if (space == "object") {
                    expression = "gi.positionOS";
                } else if (space == "view") {
                    expression = "mul(u_global_view, vec4(gi.positionWS, 1.0)).xyz";
                } else if (space == "tangent") {
                    expression = "vec3(0.0, 0.0, 0.0)";
                } else {
                    expression = "gi.positionWS";
                }
            } else if (type == "NormalVector") {
                if (space == "object") {
                    expression = "normalize(gi.normalOS)";
                } else if (space == "view") {
                    expression = "normalize(mul(u_global_view, vec4(gi.normalWS, 0.0)).xyz)";
                } else if (space == "tangent") {
                    expression = "normalize(vec3(dot(gi.normalWS, gi.tangentWS), "
                                 "dot(gi.normalWS, gi.bitangentWS), dot(gi.normalWS, gi.normalWS)))";
                } else {
                    expression = "normalize(gi.normalWS)";
                }
            } else if (type == "TangentVector") {
                if (space == "object") {
                    expression = "normalize(gi.tangentOS)";
                } else if (space == "view") {
                    expression = "normalize(mul(u_global_view, vec4(gi.tangentWS, 0.0)).xyz)";
                } else if (space == "tangent") {
                    expression = "vec3(1.0, 0.0, 0.0)";
                } else {
                    expression = "normalize(gi.tangentWS)";
                }
            } else if (type == "BitangentVector") {
                if (space == "object") {
                    expression = "normalize(gi.bitangentOS)";
                } else if (space == "view") {
                    expression = "normalize(mul(u_global_view, vec4(gi.bitangentWS, 0.0)).xyz)";
                } else if (space == "tangent") {
                    expression = "vec3(0.0, 1.0, 0.0)";
                } else {
                    expression = "normalize(gi.bitangentWS)";
                }
            } else if (type == "ViewDirection") {
                if (space == "object") {
                    expression =
                        "normalize(mul(u_sg_modelInverse, vec4(gi.viewDirWS, 0.0)).xyz)";
                } else if (space == "view") {
                    expression =
                        "normalize(mul(u_global_view, vec4(gi.viewDirWS, 0.0)).xyz)";
                } else if (space == "tangent") {
                    expression = "normalize(vec3(dot(gi.viewDirWS, gi.tangentWS), "
                                 "dot(gi.viewDirWS, gi.bitangentWS), "
                                 "dot(gi.viewDirWS, gi.normalWS)))";
                } else {
                    expression = "normalize(gi.viewDirWS)";
                }
            } else {
                if (space == "object") {
                    expression = "mul(u_sg_modelInverse, vec4(gi.viewDirWS, 0.0)).xyz";
                } else if (space == "view") {
                    expression = "mul(u_global_view, vec4(gi.viewDirWS, 0.0)).xyz";
                } else if (space == "tangent") {
                    expression = "vec3(dot(gi.viewDirWS, gi.tangentWS), "
                                 "dot(gi.viewDirWS, gi.bitangentWS), "
                                 "dot(gi.viewDirWS, gi.normalWS))";
                } else {
                    expression = "gi.viewDirWS";
                }
            }
            const auto output = m_outputs[node->id];
            body << "    " << output.begin()->second << " = " << expression << ";\n";
            return;
        }
        if (type == "SampleVirtualTexture") {
            emitSampleVirtualTexture(*node, inputs, body);
            return;
        }
        if (type == "SampleTexture2D" || type == "SampleTexture2DLOD" ||
            type == "SampleTexture2DArray" || type == "SampleTexture3D" ||
            type == "SampleCubemap" || type == "SampleReflectedCubemap") {
            emitSample(*node, inputs, body);
            return;
        }
        if (type == "SplitTextureTransform") {
            const TextureTransform transform = textureTransformForInput(*node, 0);
            const auto& outputs = m_outputs[node->id];
            const auto tiling = outputs.find(1);
            const auto offset = outputs.find(2);
            if (tiling != outputs.end()) {
                if (!transform.scaleTranslate.empty()) {
                    body << "    " << tiling->second << " = "
                         << transform.scaleTranslate << ".xy;\n";
                } else {
                    body << "    " << tiling->second << " = vec2("
                         << formatFloat(transform.tiling.x) << ", "
                         << formatFloat(transform.tiling.y) << ");\n";
                }
            }
            if (offset != outputs.end()) {
                if (!transform.scaleTranslate.empty()) {
                    body << "    " << offset->second << " = "
                         << transform.scaleTranslate << ".zw;\n";
                } else {
                    body << "    " << offset->second << " = vec2("
                         << formatFloat(transform.offset.x) << ", "
                         << formatFloat(transform.offset.y) << ");\n";
                }
            }
            return;
        }
        if (type == "TextureSize") {
            const PropertyInfo* property = findTexture(*node, ValueType::Texture2D, 1);
            if (property == nullptr) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorMissingInput;
                diagnostic.message = "texture size node has no texture property";
                diagnostic.node = node->id;
                m_diagnostics.push_back(std::move(diagnostic));
                return;
            }
            const auto& outputs = m_outputs[node->id];
            const std::string texel = "u_sg_" + property->refName + "_TexelSize";
            body << "    " << outputs.at(0) << " = " << texel << ".z;\n";
            body << "    " << outputs.at(2) << " = " << texel << ".w;\n";
            body << "    " << outputs.at(3) << " = " << texel << ".x;\n";
            body << "    " << outputs.at(4) << " = " << texel << ".y;\n";
            return;
        }
        if (type == "GatherTexture2D") {
            const PropertyInfo* property = findTexture(*node, ValueType::Texture2D, 1);
            if (property == nullptr) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorMissingInput;
                diagnostic.message = "gather texture node has no texture property";
                diagnostic.node = node->id;
                m_diagnostics.push_back(std::move(diagnostic));
                return;
            }
            const std::string sampler = "s_sg_" + property->refName;
            const std::string texel = "u_sg_" + property->refName + "_TexelSize";
            const std::string uv = inputs.at(2);
            const std::string offset = inputs.at(4);
            const std::string uvR = "(floor(" + uv + " * " + texel +
                                    ".zw + vec2(-0.5, 0.5)) + trunc(" + offset +
                                    ") + vec2(0.5, 0.5)) * " + texel + ".xy";
            const std::string uvG = "(floor(" + uv + " * " + texel +
                                    ".zw + vec2(0.5, 0.5)) + trunc(" + offset +
                                    ") + vec2(0.5, 0.5)) * " + texel + ".xy";
            const std::string uvB = "(floor(" + uv + " * " + texel +
                                    ".zw + vec2(0.5, -0.5)) + trunc(" + offset +
                                    ") + vec2(0.5, 0.5)) * " + texel + ".xy";
            const std::string uvA = "(floor(" + uv + " * " + texel +
                                    ".zw + vec2(-0.5, -0.5)) + trunc(" + offset +
                                    ") + vec2(0.5, 0.5)) * " + texel + ".xy";
            const auto& outputs = m_outputs[node->id];
            body << "    " << outputs.at(5) << " = texture2DLod(" << sampler << ", " << uvR
                 << ", 0.0).x;\n";
            body << "    " << outputs.at(6) << " = texture2DLod(" << sampler << ", " << uvG
                 << ", 0.0).x;\n";
            body << "    " << outputs.at(7) << " = texture2DLod(" << sampler << ", " << uvB
                 << ", 0.0).x;\n";
            body << "    " << outputs.at(8) << " = texture2DLod(" << sampler << ", " << uvA
                 << ", 0.0).x;\n";
            body << "    " << outputs.at(0) << " = vec4(" << outputs.at(5) << ", "
                 << outputs.at(6) << ", " << outputs.at(7) << ", " << outputs.at(8)
                 << ");\n";
            return;
        }
        if (type == "CalculateLevelOfDetailTexture2D") {
            const PropertyInfo* property = findTexture(*node, ValueType::Texture2D, 1);
            if (property == nullptr) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorMissingInput;
                diagnostic.message = "calculate LOD node has no texture property";
                diagnostic.node = node->id;
                m_diagnostics.push_back(std::move(diagnostic));
                return;
            }
            const std::string suffix = std::to_string(node->id);
            const std::string texel = "u_sg_" + property->refName + "_TexelSize";
            const std::string uv = inputs.at(2);
            const std::string dx = "hbg_lod_dx_" + suffix;
            const std::string dy = "hbg_lod_dy_" + suffix;
            const auto& outputs = m_outputs[node->id];
            body << "    vec2 " << dx << " = ddx(" << uv << " * " << texel << ".zw);\n";
            body << "    vec2 " << dy << " = ddy(" << uv << " * " << texel << ".zw);\n";
            body << "    " << outputs.at(0) << " = max(0.5 * log2(max(dot(" << dx << ", "
                 << dx << "), dot(" << dy << ", " << dy << "))), 0.0);\n";
            if (settingBool(*node, "clamp", true)) {
                body << "    " << outputs.at(0) << " = min(" << outputs.at(0)
                     << ", max(0.0, floor(log2(max(" << texel << ".z, " << texel
                     << ".w)))));\n";
            }
            return;
        }
        if (type == "Camera") {
            const auto& outputs = m_outputs[node->id];
            const std::string suffix = std::to_string(node->id);
            const std::string forward = "hbg_sg_camera_forward_" + suffix;
            const std::string nearPlane = "hbg_sg_camera_near_" + suffix;
            const std::string farPlane = "hbg_sg_camera_far_" + suffix;
            const std::string orthographic = "hbg_sg_camera_ortho_" + suffix;
            body << "    vec3 " << forward
                 << " = normalize(mul(u_global_invView, vec4(0.0, 0.0, -1.0, 0.0)).xyz);\n";
            body << "    float " << nearPlane
                 << " = max(-u_global_frustumPlanes[4].w - dot(u_global_frustumPlanes[4].xyz, "
                    "u_global_cameraPos.xyz), 0.0);\n";
            body << "    float " << farPlane
                 << " = max(u_global_frustumPlanes[5].w - dot(u_global_frustumPlanes[5].xyz, "
                    "u_global_cameraPos.xyz), " << nearPlane << ");\n";
            body << "    float " << orthographic
                 << " = clamp(mul(u_global_proj, vec4(0.0, 0.0, 0.0, 1.0)).w, 0.0, 1.0);\n";
            body << "    " << outputs.at(0) << " = u_global_cameraPos.xyz;\n";
            body << "    " << outputs.at(1) << " = " << forward << ";\n";
            body << "    " << outputs.at(2) << " = " << orthographic << ";\n";
            body << "    " << outputs.at(3) << " = " << nearPlane << ";\n";
            body << "    " << outputs.at(4) << " = " << farPlane << ";\n";
            body << "    " << outputs.at(5) << " = 1.0;\n";
            body << "    " << outputs.at(6) << " = " << orthographic
                 << " > 0.5 ? 2.0 / max(abs(mul(u_global_proj, vec4(1.0, 0.0, 0.0, 0.0)).x), "
                    "1.0e-6) : 1.0;\n";
            body << "    " << outputs.at(7) << " = " << orthographic
                 << " > 0.5 ? 2.0 / max(abs(mul(u_global_proj, vec4(0.0, 1.0, 0.0, 0.0)).y), "
                    "1.0e-6) : 1.0;\n";
            return;
        }
        if (type == "EyeIndex") {
            body << "    " << m_outputs[node->id].at(0) << " = 0.0;\n";
            return;
        }
        if (type == "Object") {
            const auto& outputs = m_outputs[node->id];
            body << "    vec3 hbg_sg_object_pos_" << node->id
                 << " = mul(u_model[0], vec4(0.0, 0.0, 0.0, 1.0)).xyz;\n";
            body << "    vec3 hbg_sg_object_scale_" << node->id
                 << " = vec3(length(mul(u_model[0], vec4(1.0, 0.0, 0.0, 0.0)).xyz),\n"
                    "                          length(mul(u_model[0], vec4(0.0, 1.0, 0.0, 0.0)).xyz),\n"
                    "                          length(mul(u_model[0], vec4(0.0, 0.0, 1.0, 0.0)).xyz));\n";
            body << "    vec3 hbg_sg_object_half_" << node->id << " = hbg_sg_object_scale_"
                 << node->id << " * 0.5;\n";
            body << "    " << outputs.at(0) << " = hbg_sg_object_pos_" << node->id << ";\n";
            body << "    " << outputs.at(1) << " = hbg_sg_object_scale_" << node->id << ";\n";
            body << "    " << outputs.at(2) << " = hbg_sg_object_pos_" << node->id
                 << " - hbg_sg_object_half_" << node->id << ";\n";
            body << "    " << outputs.at(3) << " = hbg_sg_object_pos_" << node->id
                 << " + hbg_sg_object_half_" << node->id << ";\n";
            body << "    " << outputs.at(4) << " = hbg_sg_object_scale_" << node->id << ";\n";
            return;
        }
        if (type == "Screen") {
            const auto& outputs = m_outputs[node->id];
            body << "    " << outputs.at(0) << " = u_global_screenSize.x;\n";
            body << "    " << outputs.at(1) << " = u_global_screenSize.y;\n";
            return;
        }
        if (type == "SceneColor") {
            provideSceneColor();
            const std::string uv =
                hasExplicitInput(*node, 0) ? "(" + inputs.at(0) + ").xy" : "gi.screenPos.xy";
            body << "    " << m_outputs[node->id].at(1) << " = hbgSGSampleSceneColor(" << uv
                 << ");\n";
            return;
        }
        if (type == "SceneDepth") {
            provideSceneDepth();
            const std::string uv =
                hasExplicitInput(*node, 0) ? "(" + inputs.at(0) + ").xy" : "gi.screenPos.xy";
            const std::string mode = normalizedSetting(*node, "mode", "linear01");
            if (mode == "raw") {
                body << "    " << m_outputs[node->id].at(1) << " = hbgSGSampleSceneDepth(" << uv
                     << ");\n";
                return;
            }
            const std::string raw = "hbg_sg_scene_depth_" + std::to_string(node->id);
            body << "    float " << raw << " = hbgSGSampleSceneDepth(" << uv << ");\n";
            body << "    " << m_outputs[node->id].at(1) << " = "
                 << (mode == "eye" ? "hbgSGLinearEyeDepth(" + raw + ", " + uv + ")"
                                   : "hbgSGLinear01Depth(" + raw + ", " + uv + ")")
                 << ";\n";
            return;
        }
        if (type == "SceneDepthDifference") {
            provideSceneDepth();
            const std::string suffix = std::to_string(node->id);
            const std::string raw = "hbg_sg_scene_raw_" + suffix;
            const std::string view = "hbg_sg_scene_view_" + suffix;
            const std::string distance = "hbg_sg_scene_dist_" + suffix;
            body << "    float " << raw << " = hbgSGSampleSceneDepth(gi.screenPos.xy);\n";
            body << "    vec3 " << view
                 << " = mul(u_global_view, vec4(gi.positionWS, 1.0)).xyz;\n";
            body << "    float " << distance << " = length(" << view << ");\n";
            const std::string mode = normalizedSetting(*node, "mode", "linear01");
            if (mode == "raw") {
                body << "    " << m_outputs[node->id].at(0) << " = " << raw
                     << " - gi.screenPos.z;\n";
                return;
            }
            if (mode == "eye") {
                body << "    " << m_outputs[node->id].at(0) << " = " << distance
                     << " - hbgSGLinearEyeDepth(" << raw << ", gi.screenPos.xy);\n";
                return;
            }
            const std::string nearFar = "hbg_sg_scene_near_far_" + suffix;
            const std::string scene01 = "hbg_sg_scene_01_" + suffix;
            const std::string current01 = "hbg_sg_scene_current_01_" + suffix;
            body << "    vec2 " << nearFar << " = hbgSGCameraNearFar();\n";
            body << "    float " << scene01 << " = hbgSGLinear01Depth(" << raw
                 << ", gi.screenPos.xy);\n";
            body << "    float " << current01 << " = clamp((" << distance << " - " << nearFar
                 << ".x) / max(" << nearFar << ".y - " << nearFar << ".x, 1.0e-5), 0.0, 1.0);\n";
            body << "    " << m_outputs[node->id].at(0) << " = " << current01 << " - " << scene01
                 << ";\n";
            return;
        }
        if (type == "Fog") {
            provideFog();
            const std::string position =
                hasExplicitInput(*node, 2) ? inputs.at(2) : "gi.positionOS";
            body << "    hbgSGFog(" << position << ", " << m_outputs[node->id].at(0) << ", "
                 << m_outputs[node->id].at(1) << ");\n";
            return;
        }
        if (type == "Ambient") {
            provideAmbientSH();
            const auto& outputs = m_outputs[node->id];
            body << "    " << outputs.at(0)
                 << " = hbgSGSampleAmbient(vec3(0.0, 1.0, 0.0));\n";
            body << "    " << outputs.at(1)
                 << " = hbgSGSampleAmbient(vec3(1.0, 0.0, 0.0));\n";
            body << "    " << outputs.at(2)
                 << " = hbgSGSampleAmbient(vec3(0.0, -1.0, 0.0));\n";
            return;
        }
        if (type == "BakedGI") {
            provideAmbientSH();
            const std::string normal =
                hasExplicitInput(*node, 0) ? "normalize(" + inputs.at(0) + ")"
                                           : "normalize(gi.normalWS)";
            body << "    " << m_outputs[node->id].at(1) << " = hbgSGSampleAmbient("
                 << normal << ");\n";
            return;
        }
        if (type == "MainLightDirection") {
            const std::string output = m_outputs[node->id].at(0);
            if (m_target.id == "preview") {
                body << "    " << output << " = vec3(-0.5, -0.5, 0.0);\n";
            } else {
                body << "    " << output
                     << " = u_pass_lightCounts.y > 0.5 ? u_pass_lightDirection.xyz : "
                        "vec3(0.0, 0.0, 0.0);\n";
            }
            return;
        }
        if (type == "ReflectionProbe") {
            const std::string output = m_outputs[node->id].at(3);
            const std::string viewDir =
                hasExplicitInput(*node, 0)
                    ? "normalize(mul(u_model[0], vec4(" + inputs.at(0) + ", 0.0)).xyz)"
                    : "normalize(gi.viewDirWS)";
            const std::string normal =
                hasExplicitInput(*node, 1)
                    ? "normalize(mul(u_model[0], vec4(" + inputs.at(1) + ", 0.0)).xyz)"
                    : "normalize(gi.normalWS)";
            body << "    float hbg_sg_probe_rough_" << node->id << " = saturate("
                 << inputs.at(2) << " / max(u_environment_skyParams.y, 1.0));\n";
            body << "    float hbg_sg_probe_weight_" << node->id << " = 0.0;\n";
            body << "    " << output << " = max(hbgSampleReflection(gi.positionWS, reflect(-"
                 << viewDir << ", " << normal << "), hbg_sg_probe_rough_" << node->id
                 << ", hbg_sg_probe_weight_" << node->id << "), vec3(0.0, 0.0, 0.0));\n";
            return;
        }
        if (type == "Matrix2x2" || type == "Matrix3x3" || type == "Matrix4x4") {
            const int dimension = type == "Matrix2x2" ? 2 : (type == "Matrix3x3" ? 3 : 4);
            const auto& outputs = m_outputs[node->id];
            if (outputs.empty()) {
                return;
            }
            Vec4 rows[4] = {};
            for (int row = 0; row < dimension; ++row) {
                const std::string key = "row" + std::to_string(row);
                rows[row] = settingVector4(*node, key.c_str(), Vec4{});
            }
            std::ostringstream expression;
            expression << "mat" << dimension << "(";
            bool first = true;
            for (int column = 0; column < dimension; ++column) {
                for (int row = 0; row < dimension; ++row) {
                    if (!first) {
                        expression << ", ";
                    }
                    first = false;
                    const float values[4] = {rows[row].x, rows[row].y, rows[row].z,
                                             rows[row].w};
                    expression << formatFloat(values[column]);
                }
            }
            expression << ")";
            body << "    " << outputs.begin()->second << " = " << expression.str() << ";\n";
            return;
        }
        if (type == "TransformationMatrix") {
            std::string kind = normalizedSetting(*node, "matrixType", "model");
            if (settingString(*node, "matrixType").empty() && node->setting("matrixType") != nullptr) {
                static const char* names[] = {"model", "inversemodel", "view", "inverseview",
                                              "projection", "inverseprojection", "viewprojection",
                                              "inverseviewprojection"};
                const int index = settingInteger(*node, "matrixType", -1);
                if (index >= 0 && index < static_cast<int>(sizeof(names) / sizeof(names[0]))) {
                    kind = names[index];
                }
            }
            std::string expression = "mat4(1.0)";
            if (kind == "model" || kind == "modelview" || kind == "objecttoworld") {
                expression = "u_model[0]";
            } else if (kind == "inversemodel" || kind == "worldtoobject") {
                expression = "u_sg_modelInverse";
            } else if (kind == "view") {
                expression = "u_global_view";
            } else if (kind == "inverseview") {
                expression = "u_global_invView";
            } else if (kind == "projection") {
                expression = "u_global_proj";
            } else if (kind == "inverseprojection") {
                expression = "u_global_invProj";
            } else if (kind == "viewprojection" || kind == "viewprojectionmatrix") {
                expression = "u_global_viewProj";
            } else if (kind == "inverseviewprojection" || kind == "inverseviewprojectionmatrix") {
                expression = "u_global_invViewProj";
            }
            body << "    " << m_outputs[node->id].begin()->second << " = " << expression
                 << ";\n";
            return;
        }
        if (type == "DielectricSpecular") {
            const std::string material = normalizedSetting(*node, "material", "common");
            std::string expression;
            if (material == "common") {
                expression = "lerp(0.034, 0.048, " +
                             formatFloat(settingFloat(*node, "range", 0.5f)) + ")";
            } else if (material == "custom") {
                const std::string ior =
                    formatFloat(settingFloat(*node, "indexOfRefraction", 1.0f));
                expression = "pow(" + ior + " - 1.0, 2.0) / pow(" + ior + " + 1.0, 2.0)";
            } else if (material == "rustedmetal") {
                expression = "0.03";
            } else if (material == "water") {
                expression = "0.02";
            } else if (material == "ice") {
                expression = "0.018";
            } else if (material == "glass") {
                expression = "0.04";
            } else {
                expression = "lerp(0.034, 0.048, 0.5)";
            }
            body << "    " << m_outputs[node->id].begin()->second << " = " << expression
                 << ";\n";
            return;
        }
        if (type == "MetalReflectance") {
            const std::string material = normalizedSetting(*node, "material", "iron");
            std::string expression = "vec3(0.56, 0.57, 0.58)";
            if (material == "silver") {
                expression = "vec3(0.972, 0.96, 0.915)";
            } else if (material == "aluminium" || material == "aluminum") {
                expression = "vec3(0.913, 0.921, 0.925)";
            } else if (material == "gold") {
                expression = "vec3(1.0, 0.766, 0.336)";
            } else if (material == "copper") {
                expression = "vec3(0.955, 0.637, 0.538)";
            } else if (material == "chromium") {
                expression = "vec3(0.55, 0.556, 0.554)";
            } else if (material == "nickel") {
                expression = "vec3(0.66, 0.609, 0.526)";
            } else if (material == "titanium") {
                expression = "vec3(0.542, 0.497, 0.449)";
            } else if (material == "cobalt") {
                expression = "vec3(0.662, 0.655, 0.634)";
            } else if (material == "platinum") {
                expression = "vec3(0.672, 0.637, 0.585)";
            }
            body << "    " << m_outputs[node->id].begin()->second << " = " << expression
                 << ";\n";
            return;
        }
        if (type == "Split") {
            const auto& outputs = m_outputs[node->id];
            const std::string input = adapt(inputs.at(0), targetType(*node->findSlot(0)),
                                            ConcreteValueType::Vector4, node->concretePrecision);
            body << "    " << outputs.at(1) << " = " << input << ".x;\n";
            body << "    " << outputs.at(2) << " = " << input << ".y;\n";
            body << "    " << outputs.at(3) << " = " << input << ".z;\n";
            body << "    " << outputs.at(4) << " = " << input << ".w;\n";
            return;
        }
        if (type == "Append") {
            emitAppend(*node, inputs, body);
            return;
        }
        if (type == "Swizzle") {
            emitSwizzle(*node, inputs, body);
            return;
        }
        if (type == "Flip") {
            emitFlip(*node, inputs, body);
            return;
        }
        if (type == "All" || type == "Any" || type == "IsInfinite" || type == "IsNaN") {
            const SlotInstance* input = node->findSlot(0);
            const std::string expression =
                adapt(inputs.at(0), targetType(*input), ConcreteValueType::Vector4,
                      node->concretePrecision);
            const std::string output = m_outputs[node->id].begin()->second;
            if (type == "All") {
                body << "    " << output << " = all(" << expression << ");\n";
            } else if (type == "Any") {
                body << "    " << output << " = any(" << expression << ");\n";
            } else if (type == "IsInfinite") {
                body << "    " << output << " = any(isinf(" << expression << "));\n";
            } else {
                body << "    " << output << " = any(isnan(" << expression << "));\n";
            }
            return;
        }
        if (type == "Normalize" &&
            (targetType(*node->findSlot(0)) == ConcreteValueType::Float ||
             targetType(*node->findSlot(0)) == ConcreteValueType::Integer)) {
            body << "    " << m_outputs[node->id].begin()->second << " = sign(" << inputs.at(0)
                 << ");\n";
            return;
        }
        if (type == "Length") {
            const std::string output = m_outputs[node->id].begin()->second;
            if (targetType(*node->findSlot(0)) == ConcreteValueType::Float ||
                targetType(*node->findSlot(0)) == ConcreteValueType::Integer) {
                body << "    " << output << " = abs(" << inputs.at(0) << ");\n";
            } else {
                body << "    " << output << " = length(" << inputs.at(0) << ");\n";
            }
            return;
        }
        if (type == "Transform") {
            const std::string fromSetting = settingString(*node, "from");
            const std::string toSetting = settingString(*node, "to");
            const std::string kind = settingString(*node, "type").empty()
                                         ? std::string("position")
                                         : settingString(*node, "type");
            const std::string from = fromSetting.empty() ? std::string("object") : fromSetting;
            const std::string to = toSetting.empty() ? std::string("world") : toSetting;
            const bool normalize = settingBool(*node, "normalize", true);
            if (kind != "position" && kind != "direction" && kind != "normal") {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorInvalidValue;
                diagnostic.message = "Transform conversion type is invalid: " + kind;
                diagnostic.node = node->id;
                m_diagnostics.push_back(std::move(diagnostic));
                return;
            }
            const bool position = kind == "position";
            const bool normal = kind == "normal";
            const std::string& input = inputs.at(0);
            const auto finish = [&](std::string expression, bool normalizeOutput = true) {
                if (!position && normalize && normalizeOutput) {
                    expression = "normalize(" + expression + ")";
                }
                body << "    " << m_outputs[node->id].begin()->second << " = " << expression
                     << ";\n";
            };
            const auto objectToWorld = [&](const std::string& value) {
                if (normal) {
                    return "mul(vec4(" + value + ", 0.0), u_sg_modelInverse).xyz";
                }
                return "mul(u_model[0], vec4(" + value + ", " +
                       (position ? "1.0" : "0.0") + ")).xyz";
            };
            const auto worldToObject = [&](const std::string& value) {
                if (position) {
                    return "mul(u_sg_modelInverse, vec4(" + value + ", 1.0)).xyz";
                }
                if (normal) {
                    return "mul(vec4(" + value + ", 0.0), u_model[0]).xyz";
                }
                return "mul(u_sg_modelInverse, vec4(" + value + ", 0.0)).xyz";
            };
            const auto worldToView = [&](const std::string& value) {
                return "mul(u_global_view, vec4(" + value + ", " +
                       (position ? "1.0" : "0.0") + ")).xyz";
            };
            const auto viewToWorld = [&](const std::string& value) {
                return "mul(u_global_invView, vec4(" + value + ", " +
                       (position ? "1.0" : "0.0") + ")).xyz";
            };
            const auto tangentToWorld = [&](const std::string& value) {
                std::string result = "gi.tangentWS * " + value + ".x + gi.bitangentWS * " +
                                     value + ".y + gi.normalWS * " + value + ".z";
                if (position) {
                    result = "gi.positionWS + (" + result + ")";
                }
                return result;
            };
            const auto worldToTangent = [&](const std::string& value) {
                const std::string delta =
                    position ? "(" + value + " - gi.positionWS)" : "(" + value + ")";
                return "vec3(dot(" + delta + ", normalize(gi.tangentWS)), dot(" + delta +
                       ", normalize(gi.bitangentWS)), dot(" + delta + ", normalize(gi.normalWS)))";
            };
            const auto screenToWorld = [&](const std::string& value) {
                provideScreenTransform();
                return "hbgSGWorldFromScreen(" + value + ")";
            };
            const auto screenToWorldDirection = [&](const std::string& value) {
                provideScreenTransform();
                return "hbgSGWorldDirectionFromScreen(" + value + ")";
            };
            const auto worldToScreen = [&](const std::string& value) {
                provideScreenTransform();
                return "hbgSGWorldToScreen(" + value + ")";
            };
            const auto worldToScreenDirection = [&](const std::string& value) {
                provideScreenTransform();
                const std::string direction =
                    normalize ? "normalize(" + value + ")" : value;
                return "hbgSGWorldDirectionToScreen(" + direction + ")";
            };

            const std::string effectiveFrom = from == "absoluteWorld" ? "world" : from;
            const std::string effectiveTo = to == "absoluteWorld" ? "world" : to;
            if (effectiveFrom == effectiveTo) {
                finish(input);
                return;
            }
            if (effectiveFrom == "screen" && effectiveTo == "world") {
                finish(position ? screenToWorld(input) : screenToWorldDirection(input), false);
                return;
            }
            if (effectiveFrom == "screen" && effectiveTo == "view") {
                const std::string world =
                    position ? screenToWorld(input) : screenToWorldDirection(input);
                finish(worldToView(world));
                return;
            }
            if (effectiveFrom == "screen" && effectiveTo == "object") {
                const std::string world =
                    position ? screenToWorld(input) : screenToWorldDirection(input);
                finish(worldToObject(world));
                return;
            }
            if (effectiveFrom == "screen" && effectiveTo == "tangent") {
                const std::string world =
                    position ? screenToWorld(input) : screenToWorldDirection(input);
                finish(worldToTangent(world));
                return;
            }
            if (effectiveFrom == "object" && effectiveTo == "screen") {
                finish(position ? worldToScreen(objectToWorld(input))
                                : worldToScreenDirection(objectToWorld(input)),
                       false);
                return;
            }
            if (effectiveFrom == "view" && effectiveTo == "screen") {
                finish(position ? worldToScreen(viewToWorld(input))
                                : worldToScreenDirection(viewToWorld(input)),
                       false);
                return;
            }
            if (effectiveFrom == "tangent" && effectiveTo == "screen") {
                finish(position ? worldToScreen(tangentToWorld(input))
                                : worldToScreenDirection(tangentToWorld(input)),
                       false);
                return;
            }
            if (effectiveFrom == "world" && effectiveTo == "screen") {
                finish(position ? worldToScreen(input) : worldToScreenDirection(input),
                       false);
                return;
            }
            if (effectiveFrom == "object" && effectiveTo == "world") {
                finish(objectToWorld(input));
                return;
            }
            if (effectiveFrom == "object" && effectiveTo == "view") {
                finish(worldToView(objectToWorld(input)));
                return;
            }
            if (effectiveFrom == "object" && effectiveTo == "tangent") {
                finish(worldToTangent(objectToWorld(input)));
                return;
            }
            if (effectiveFrom == "world" && effectiveTo == "object") {
                finish(worldToObject(input));
                return;
            }
            if (effectiveFrom == "view" && effectiveTo == "object") {
                finish(worldToObject(viewToWorld(input)));
                return;
            }
            if (effectiveFrom == "tangent" && effectiveTo == "object") {
                finish(worldToObject(tangentToWorld(input)));
                return;
            }
            if (effectiveFrom == "world" && effectiveTo == "view") {
                finish(worldToView(input));
                return;
            }
            if (effectiveFrom == "world" && effectiveTo == "tangent") {
                finish(worldToTangent(input));
                return;
            }
            if (effectiveFrom == "view" && effectiveTo == "world") {
                finish(viewToWorld(input));
                return;
            }
            if (effectiveFrom == "view" && effectiveTo == "tangent") {
                finish(worldToTangent(viewToWorld(input)));
                return;
            }
            if (effectiveFrom == "tangent" && effectiveTo == "world") {
                finish(tangentToWorld(input));
                return;
            }
            if (effectiveFrom == "tangent" && effectiveTo == "view") {
                finish(worldToView(tangentToWorld(input)));
                return;
            }
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorTargetUnsupported;
            diagnostic.message = "Transform conversion is not supported: " + from + " -> " + to;
            diagnostic.node = node->id;
            m_diagnostics.push_back(std::move(diagnostic));
            return;
        }
        if (type == "FresnelEffect") {
            const SlotInstance* normal = node->findSlot(0);
            const SlotInstance* view = node->findSlot(1);
            const bool normalInput =
                m_graph.incomingEdge(node->id, 0) != nullptr || normal->hasExplicitValue();
            const bool viewInput =
                m_graph.incomingEdge(node->id, 1) != nullptr || view->hasExplicitValue();
            const std::string normalExpression =
                normalInput ? inputs.at(0) : "normalize(gi.normalWS)";
            const std::string viewExpression =
                viewInput ? inputs.at(1) : "normalize(gi.viewDirWS)";
            body << "    " << m_outputs[node->id].begin()->second << " = pow(1.0 - saturate(dot("
                 << normalExpression << ", " << viewExpression << ")), " << inputs.at(2)
                 << ");\n";
            return;
        }
        if (type == "DotProduct") {
            const ConcreteValueType valueType = targetType(*node->findSlot(0));
            body << "    " << m_outputs[node->id].begin()->second << " = "
                 << dotExpression(inputs.at(0), inputs.at(1), valueType) << ";\n";
            return;
        }
        if (type == "Projection" || type == "Rejection") {
            const ConcreteValueType valueType = targetType(*node->findSlot(0));
            const std::string projection =
                inputs.at(1) + " * " + dotExpression(inputs.at(0), inputs.at(1), valueType) +
                " / max(" + dotExpression(inputs.at(1), inputs.at(1), valueType) + ", 1.0e-8)";
            body << "    " << m_outputs[node->id].begin()->second << " = "
                 << (type == "Projection" ? projection : inputs.at(0) + " - " + projection)
                 << ";\n";
            return;
        }
        if (type == "Reflection") {
            const ConcreteValueType valueType = targetType(*node->findSlot(0));
            const std::string output = m_outputs[node->id].begin()->second;
            if (valueType == ConcreteValueType::Float ||
                valueType == ConcreteValueType::Integer ||
                valueType == ConcreteValueType::Boolean) {
                body << "    " << output << " = " << inputs.at(0) << " - 2.0 * "
                     << dotExpression(inputs.at(0), inputs.at(1), valueType) << " * "
                     << inputs.at(1) << ";\n";
            } else {
                body << "    " << output << " = reflect(" << inputs.at(0) << ", " << inputs.at(1)
                     << ");\n";
            }
            return;
        }
        if (type == "RotateAboutAxis") {
            const std::string suffix = std::to_string(node->id);
            const std::string sine = "hbg_rot_s_" + suffix;
            const std::string cosine = "hbg_rot_c_" + suffix;
            const std::string axis = "hbg_rot_axis_" + suffix;
            std::string angle = inputs.at(2);
            if (settingString(*node, "unit") == "degrees") {
                angle = "radians(" + angle + ")";
            }
            const std::string& input = inputs.at(0);
            body << "    float " << sine << " = sin(" << angle << ");\n";
            body << "    float " << cosine << " = cos(" << angle << ");\n";
            body << "    vec3 " << axis << " = normalize(" << inputs.at(1) << ");\n";
            body << "    " << m_outputs[node->id].begin()->second << " = " << input << " * "
                 << cosine << " + cross(" << axis << ", " << input << ") * " << sine << " + "
                 << axis << " * dot(" << axis << ", " << input << ") * (1.0 - " << cosine
                 << ");\n";
            return;
        }
        if (type == "Refract") {
            const std::string suffix = std::to_string(node->id);
            const std::string eta = "hbg_refract_eta_" + suffix;
            const std::string cosine = "hbg_refract_cos_" + suffix;
            const std::string k = "hbg_refract_k_" + suffix;
            const auto& outputs = m_outputs[node->id];
            body << "    float " << eta << " = max(" << inputs.at(2) << ", 1.0) / max("
                 << inputs.at(3) << ", 1.0);\n";
            body << "    float " << cosine << " = dot(" << inputs.at(0) << ", " << inputs.at(1)
                 << ");\n";
            body << "    float " << k << " = 1.0 - " << eta << " * " << eta << " * (1.0 - "
                 << cosine << " * " << cosine << ");\n";
            if (settingString(*node, "mode") == "critical") {
                body << "    " << outputs.at(4) << " = " << k << " >= 0.0 ? " << eta << " * "
                     << inputs.at(0) << " - (" << eta << " * " << cosine << " + sqrt(" << k
                     << ")) * " << inputs.at(1) << " : reflect(" << inputs.at(0) << ", "
                     << inputs.at(1) << ");\n";
                body << "    " << outputs.at(5) << " = " << k
                     << " >= 0.0 ? saturate(pow(1.0 - abs(" << cosine << "), 5.0)) : 1.0;\n";
            } else {
                body << "    " << outputs.at(4) << " = " << eta << " * " << inputs.at(0)
                     << " - (" << eta << " * " << cosine << " + sqrt(max(" << k
                     << ", 0.0))) * " << inputs.at(1) << ";\n";
                body << "    " << outputs.at(5) << " = saturate(pow(1.0 - abs(" << cosine
                     << "), 5.0));\n";
            }
            return;
        }
        if (type == "MatrixConstruction") {
            const bool column = settingString(*node, "axis") == "column";
            const auto& outputs = m_outputs[node->id];
            const auto component = [&](int input, int channel) {
                static const char* names = "xyzw";
                return inputs.at(input) + "." + names[channel];
            };
            const auto makeMatrix = [&](int dimension) {
                std::ostringstream expression;
                expression << "mat" << dimension << "(";
                bool first = true;
                for (int row = 0; row < dimension; ++row) {
                    for (int columnIndex = 0; columnIndex < dimension; ++columnIndex) {
                        if (!first) {
                            expression << ", ";
                        }
                        first = false;
                        expression << component(column ? columnIndex : row,
                                                column ? row : columnIndex);
                    }
                }
                expression << ")";
                return expression.str();
            };
            body << "    " << outputs.at(4) << " = " << makeMatrix(4) << ";\n";
            body << "    " << outputs.at(5) << " = " << makeMatrix(3) << ";\n";
            body << "    " << outputs.at(6) << " = " << makeMatrix(2) << ";\n";
            return;
        }
        if (type == "MatrixSplit") {
            const SlotInstance* input = node->findSlot(0);
            const int dimension = matrixDimension(targetType(*input));
            const bool column = settingString(*node, "axis") == "column";
            const auto& outputs = m_outputs[node->id];
            for (int row = 0; row < 4; ++row) {
                std::string expression;
                if (row >= dimension) {
                    std::ostringstream zeros;
                    zeros << "vec" << dimension << "(";
                    for (int index = 0; index < dimension; ++index) {
                        if (index != 0) {
                            zeros << ", ";
                        }
                        zeros << "0.0";
                    }
                    zeros << ")";
                    expression = zeros.str();
                } else if (column) {
                    std::ostringstream values;
                    values << "vec" << dimension << "(";
                    for (int columnIndex = 0; columnIndex < dimension; ++columnIndex) {
                        if (columnIndex != 0) {
                            values << ", ";
                        }
                        static const char* names = "xyzw";
                        values << inputs.at(0) << "[" << columnIndex << "]."
                               << names[row];
                    }
                    values << ")";
                    expression = values.str();
                } else {
                    expression = inputs.at(0) + "[" + std::to_string(row) + "]";
                }
                body << "    " << outputs.at(1 + row) << " = " << expression << ";\n";
            }
            return;
        }
        if (type == "Comparison") {
            std::string comparison = "==";
            const auto found = node->settings.find("comparison");
            if (found != node->settings.end()) {
                if (const std::string* value = found->second.asString()) {
                    comparison = comparisonOperator(*value);
                }
            }
            body << "    " << m_outputs[node->id].begin()->second << " = (" << inputs.at(0) << " "
                 << comparison << " " << inputs.at(1) << ");\n";
            return;
        }
        if (type == "Switch") {
            emitSwitchNode(*node, inputs, body);
            return;
        }
        if (type == "Keyword") {
            emitKeywordNode(*node, inputs, body);
            return;
        }
        if (type == "Dropdown") {
            emitDropdownNode(*node, inputs, body);
            return;
        }
        if (type == "CustomFunction") {
            emitCustomFunctionNode(*node, inputs, body);
            return;
        }
        if (type == "RandomRange") {
            m_functions.provide(
                "hbgSGRandom",
                "float hbgSGRandom(float seed)\n{\n    return frac(sin(seed * 12.9898) * 43758.5453);\n}\n");
            body << "    " << m_outputs[node->id].begin()->second << " = lerp(" << inputs.at(1)
                 << ", " << inputs.at(2) << ", hbgSGRandom(" << inputs.at(0) << "));\n";
            return;
        }
        if (!definition->emitBody.empty()) {
            std::map<SlotId, std::string> tokens = inputs;
            const auto outputIt = m_outputs.find(node->id);
            if (outputIt != m_outputs.end()) {
                for (const auto& [slotId, name] : outputIt->second) {
                    tokens[slotId] = name;
                }
            }
            body << "    " << replaceTokens(definition->emitBody, tokens) << "\n";
        }
    }

    void emitSampleVirtualTexture(const NodeInstance& node,
                                  const std::map<SlotId, std::string>& inputs,
                                  std::ostringstream& body) {
        const auto source = virtualTextureSource(m_graph, node);
        if (!source) {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorMissingInput;
            diagnostic.message =
                "Sample Virtual Texture requires a connected Procedural Virtual Texture";
            diagnostic.node = node.id;
            m_diagnostics.push_back(std::move(diagnostic));
            return;
        }
        const auto outputIt = m_outputs.find(node.id);
        if (outputIt == m_outputs.end()) {
            return;
        }
        const std::string& uv = inputs.at(0);
        const std::string lodCalculation = source->lodCalculation;
        const bool lowQuality = source->sampleQuality == "low";
        if (lowQuality && (lodCalculation == "bias" || lodCalculation == "derivatives")) {
            provideVirtualTextureSamplingHelpers();
        }
        for (int layer = 0; layer < kMaxVirtualTextureLayers; ++layer) {
            const auto output = outputIt->second.find(11 + layer);
            if (layer >= source->layers) {
                if (output != outputIt->second.end()) {
                    body << "    " << output->second << " = vec4(0.0, 0.0, 0.0, 0.0);\n";
                }
                continue;
            }
            const std::string ref = source->layerRefNames[layer].empty()
                                        ? virtualTextureLayerRefName(source->name, layer)
                                        : source->layerRefNames[layer];
            const PropertyInfo* property = m_properties.find(ref);
            if (property == nullptr) {
                Diagnostic diagnostic;
                diagnostic.severity = Severity::Error;
                diagnostic.code = kErrorMissingInput;
                diagnostic.message = "virtual texture layer property '" + ref +
                                     "' is missing";
                diagnostic.node = node.id;
                m_diagnostics.push_back(std::move(diagnostic));
                continue;
            }
            const std::string sampler = "s_sg_" + property->refName;
            std::string sample;
            if (lodCalculation == "lod") {
                sample = "texture2DLod(" + sampler + ", " + uv + ", " + inputs.at(2) + ")";
            } else if (lodCalculation == "bias") {
                provideTextureSamplingHelpers();
                const std::string bias = source->enableGlobalMipBias
                                             ? "(" + inputs.at(3) +
                                                   " + u_sg_globalMipBias.x)"
                                             : inputs.at(3);
                if (lowQuality) {
                    sample = "texture2DLod(" + sampler + ", " + uv +
                             ", hbgSGVirtualTextureBiasLod(" + uv + ", " + bias + "))";
                } else {
                    sample = "hbgSGSampleTexture2DBias(" + sampler + ", " + uv + ", " + bias +
                             ")";
                }
            } else if (lodCalculation == "derivatives") {
                provideTextureSamplingHelpers();
                if (lowQuality) {
                    const std::string lod = "hbgSGVirtualTextureDerivativeLod(" +
                                            inputs.at(4) + ", " + inputs.at(5) + ")";
                    const std::string biased =
                        source->enableGlobalMipBias
                            ? "max(0.0, " + lod + " + u_sg_globalMipBias.x)"
                            : lod;
                    sample = "texture2DLod(" + sampler + ", " + uv + ", " + biased + ")";
                } else {
                    const std::string dx = source->enableGlobalMipBias
                                               ? "(" + inputs.at(4) +
                                                     " * u_sg_globalMipBias.y)"
                                               : inputs.at(4);
                    const std::string dy = source->enableGlobalMipBias
                                               ? "(" + inputs.at(5) +
                                                     " * u_sg_globalMipBias.y)"
                                               : inputs.at(5);
                    sample = "hbgSGSampleTexture2DGrad(" + sampler + ", " + uv + ", " + dx +
                             ", " + dy + ")";
                }
            } else if (source->enableGlobalMipBias) {
                provideTextureSamplingHelpers();
                sample = "hbgSGSampleTexture2DBias(" + sampler + ", " + uv +
                         ", u_sg_globalMipBias.x)";
            } else {
                sample = "texture2D(" + sampler + ", " + uv + ")";
            }
            const std::string temp =
                "t_vt_" + std::to_string(node.id) + "_" + std::to_string(layer);
            body << "    vec4 " << temp << " = " << sample << ";\n";
            if (source->layerTextureTypes[layer] == "normalTangentSpace") {
                provideUnpackNormal();
                body << "    " << temp << ".rgb = hbgSGUnpackNormal(" << temp << ");\n";
            } else if (source->layerTextureTypes[layer] == "normalObjectSpace") {
                body << "    " << temp << ".rgb = " << temp << ".rgb * 2.0 - 1.0;\n";
            }
            if (output != outputIt->second.end()) {
                body << "    " << output->second << " = " << temp << ";\n";
            }
        }
    }

    bool reachesPositionBlock(NodeId start) const {
        std::vector<NodeId> pending{start};
        std::set<NodeId> visited;
        while (!pending.empty()) {
            const NodeId id = pending.back();
            pending.pop_back();
            if (!visited.insert(id).second) {
                continue;
            }
            const NodeInstance* node = m_graph.findNode(id);
            if (node == nullptr) {
                continue;
            }
            const NodeDef* definition = m_registry.find(node->type);
            if (definition != nullptr && definition->blockNode &&
                definition->blockField == "positionOS") {
                return true;
            }
            for (const SlotInstance& slot : node->slots) {
                if (!slot.isOutput()) {
                    continue;
                }
                for (const Edge& edge : m_graph.outgoingEdges(node->id, slot.spec.id)) {
                    if (m_active.nodes.count(edge.toNode) != 0) {
                        pending.push_back(edge.toNode);
                    }
                }
            }
        }
        return false;
    }

    bool globalMipBiasEnabled(const NodeInstance& node, const std::string& type) const {
        if (type == "SampleTexture2D") {
            return settingBool(node, "enableGlobalMipBias", true);
        }
        if (type == "SampleTexture2DArray") {
            return settingBool(node, "enableGlobalMipBias", false);
        }
        return false;
    }

    bool usesGlobalMipBias() const {
        for (NodeId id : m_active.order) {
            const NodeInstance* node = m_graph.findNode(id);
            if (node == nullptr) {
                continue;
            }
            const std::string type = nodeTypeKey(node->type);
            if ((type == "SampleTexture2D" || type == "SampleTexture2DArray") &&
                mipSamplingMode(*node) != "lod" &&
                globalMipBiasEnabled(*node, type)) {
                return true;
            }
            if (type == "SampleVirtualTexture") {
                const auto source = virtualTextureSource(m_graph, *node);
                if (source && source->enableGlobalMipBias && source->lodCalculation != "lod") {
                    return true;
                }
            }
        }
        return false;
    }

    std::string mipSamplingMode(const NodeInstance& node) const {
        const std::string mode = normalizedSetting(node, "mipSamplingMode", "");
        if (mode == "lod" || mode == "lodlevel") {
            return "lod";
        }
        if (mode == "bias" || mode == "lodbias") {
            return "bias";
        }
        if (mode == "gradient" || mode == "derivatives" || mode == "ddx") {
            return "gradient";
        }
        if (!mode.empty()) {
            return mode;
        }
        switch (settingInteger(node, "mipSamplingMode", 0)) {
        case 1:
            return "lod";
        case 2:
            return "gradient";
        case 3:
            return "bias";
        default:
            return "standard";
        }
    }

    void emitSample(const NodeInstance& node, const std::map<SlotId, std::string>& inputs,
                    std::ostringstream& body) {
        const std::string type = nodeTypeKey(node.type);
        const ValueType expected = textureTypeForNode(type).value_or(ValueType::Texture2D);
        const SlotId textureSlot = 1;
        const PropertyInfo* property = findTexture(node, expected, textureSlot);
        if (property == nullptr) {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorMissingInput;
            diagnostic.message = "texture sample node has no " + std::string(toString(expected)) +
                                 " texture property";
            diagnostic.node = node.id;
            m_diagnostics.push_back(std::move(diagnostic));
            return;
        }
        const std::string sampler = "s_sg_" + property->refName;
        const std::string uv =
            (type == "SampleTexture2D" || type == "SampleTexture2DLOD" ||
             type == "SampleTexture2DArray")
                ? uvInput(node, inputs, 2)
                : inputs.at(2);
        const std::string transformedUv =
            (type == "SampleTexture2D" || type == "SampleTexture2DLOD")
                ? applyTextureTransform(uv, textureTransformForInput(node, textureSlot))
                : uv;
        std::string textureCall;
        if (type == "SampleTexture2D") {
            const std::string mode = mipSamplingMode(node);
            const bool globalBias = globalMipBiasEnabled(node, type);
            if (mode == "lod") {
                textureCall = "texture2DLod(" + sampler + ", " + transformedUv + ", " +
                              inputs.at(9) + ")";
            } else if (mode == "bias") {
                provideTextureSamplingHelpers();
                const std::string bias = globalBias
                                             ? "(" + inputs.at(8) +
                                                   " + u_sg_globalMipBias.x)"
                                             : inputs.at(8);
                textureCall = "hbgSGSampleTexture2DBias(" + sampler + ", " + transformedUv +
                              ", " + bias + ")";
            } else if (mode == "gradient") {
                provideTextureSamplingHelpers();
                const std::string dx = globalBias
                                           ? "(" + inputs.at(10) +
                                                 " * u_sg_globalMipBias.y)"
                                           : inputs.at(10);
                const std::string dy = globalBias
                                           ? "(" + inputs.at(11) +
                                                 " * u_sg_globalMipBias.y)"
                                           : inputs.at(11);
                textureCall = "hbgSGSampleTexture2DGrad(" + sampler + ", " + transformedUv +
                              ", " + dx + ", " + dy + ")";
            } else if (globalBias) {
                provideTextureSamplingHelpers();
                textureCall = "hbgSGSampleTexture2DBias(" + sampler + ", " + transformedUv +
                              ", u_sg_globalMipBias.x)";
            } else {
                textureCall = "texture2D(" + sampler + ", " + transformedUv + ")";
            }
        } else if (type == "SampleTexture2DLOD") {
            textureCall = "texture2DLod(" + sampler + ", " + transformedUv + ", " +
                          inputs.at(4) + ")";
        } else if (type == "SampleTexture2DArray") {
            const std::string mode = mipSamplingMode(node);
            const bool globalBias = globalMipBiasEnabled(node, type);
            const std::string uvw = "vec3(" + uv + ", " + inputs.at(8) + ")";
            if (mode == "lod") {
                textureCall = "texture2DArrayLod(" + sampler + ", " + uvw + ", " +
                              inputs.at(10) + ")";
            } else if (mode == "bias") {
                provideTextureSamplingHelpers();
                const std::string bias = globalBias
                                             ? "(" + inputs.at(9) +
                                                   " + u_sg_globalMipBias.x)"
                                             : inputs.at(9);
                textureCall = "hbgSGSampleTexture2DArrayBias(" + sampler + ", " + uvw +
                              ", " + bias + ")";
            } else if (mode == "gradient") {
                provideTextureSamplingHelpers();
                const std::string dx = globalBias
                                           ? "(" + inputs.at(11) +
                                                 " * u_sg_globalMipBias.y)"
                                           : inputs.at(11);
                const std::string dy = globalBias
                                           ? "(" + inputs.at(12) +
                                                 " * u_sg_globalMipBias.y)"
                                           : inputs.at(12);
                textureCall = "hbgSGSampleTexture2DArrayGrad(" + sampler + ", " + uvw +
                              ", " + dx + ", " + dy + ")";
            } else if (globalBias) {
                provideTextureSamplingHelpers();
                textureCall = "hbgSGSampleTexture2DArrayBias(" + sampler + ", " + uvw +
                              ", u_sg_globalMipBias.x)";
            } else {
                textureCall = "texture2DArray(" + sampler + ", " + uvw + ")";
            }
        } else if (type == "SampleTexture3D") {
            const std::string mode = mipSamplingMode(node);
            textureCall = mode == "lod"
                              ? "texture3DLod(" + sampler + ", " + uv + ", " + inputs.at(4) + ")"
                              : "texture3D(" + sampler + ", " + uv + ")";
        } else if (type == "SampleCubemap") {
            textureCall = "textureCubeLod(" + sampler + ", " + inputs.at(2) + ", " +
                          inputs.at(4) + ")";
        } else if (type == "SampleReflectedCubemap") {
            textureCall = "textureCubeLod(" + sampler + ", reflect(-" + inputs.at(2) + ", " +
                          inputs.at(3) + "), " + inputs.at(4) + ")";
        } else {
            return;
        }

        const std::string temp = "t_sample_" + std::to_string(node.id);
        body << "    vec4 " << temp << " = " << textureCall << ";\n";
        if (property->isHDR) {
            provideHDRDecodeHelper();
            const std::string hdrDecode = "u_sg_" + property->refName + "_HDR";
            body << "    if (" << hdrDecode << ".x > 0.0)\n";
            body << "    {\n";
            body << "        " << temp << " = hbgSGDecodeHDRSample(" << temp << ", "
                 << hdrDecode << ");\n";
            body << "    }\n";
        }
        if ((type == "SampleTexture2D" || type == "SampleTexture2DLOD") &&
            normalizedSetting(node, "textureType", "default") == "normal") {
            if (normalizedSetting(node, "normalMapSpace", "tangent") == "object") {
                body << "    " << temp << ".rgb = " << temp << ".rgb * 2.0 - 1.0;\n";
            } else {
                provideUnpackNormal();
                body << "    " << temp << ".rgb = hbgSGUnpackNormal(" << temp << ");\n";
            }
        }

        std::vector<SlotId> outputSlots;
        for (const SlotInstance& slot : node.slots) {
            if (slot.isOutput()) {
                outputSlots.push_back(slot.spec.id);
            }
        }
        const auto& outputs = m_outputs[node.id];
        const auto assign = [&](std::size_t index, const std::string& value) {
            if (index >= outputSlots.size()) {
                return;
            }
            const auto found = outputs.find(outputSlots[index]);
            if (found != outputs.end()) {
                body << "    " << found->second << " = " << value << ";\n";
            }
        };
        assign(0, temp);
        if (type != "SampleCubemap" && type != "SampleReflectedCubemap") {
            assign(1, temp + ".x");
            assign(2, temp + ".y");
            assign(3, temp + ".z");
            assign(4, temp + ".w");
        }
    }

    std::string component(const std::string& expression, int index) const {
        const char* components = "xyzw";
        if (index < 0 || index >= 4) {
            return "0.0";
        }
        return expression + "." + components[index];
    }

    std::string dotExpression(const std::string& left, const std::string& right,
                              ConcreteValueType type) const {
        if (type == ConcreteValueType::Float || type == ConcreteValueType::Integer ||
            type == ConcreteValueType::Boolean) {
            return "(" + left + " * " + right + ")";
        }
        return "dot(" + left + ", " + right + ")";
    }

    void emitAppend(const NodeInstance& node, const std::map<SlotId, std::string>& inputs,
                    std::ostringstream& body) {
        const SlotInstance* a = node.findSlot(0);
        const SlotInstance* b = node.findSlot(1);
        const int aCount = channelCount(targetType(*a));
        const int bCount = channelCount(targetType(*b));
        const int outCount = channelCount(targetType(*node.findSlot(2)));
        std::vector<std::string> values;
        for (int index = 0; index < outCount; ++index) {
            if (index < aCount) {
                values.push_back(aCount == 1 && index == 0 ? inputs.at(0)
                                                           : component(inputs.at(0), index));
            } else if (index - aCount < bCount) {
                values.push_back(bCount == 1 && index == aCount
                                     ? inputs.at(1)
                                     : component(inputs.at(1), index - aCount));
            } else {
                values.push_back("0.0");
            }
        }
        const std::string value = outCount == 1 ? (values.empty() ? "0.0" : values.front())
                                                : makeVectorLiteralFrom(values);
        body << "    " << m_outputs[node.id].begin()->second << " = " << value << ";\n";
    }

    std::string makeVectorLiteralFrom(const std::vector<std::string>& values) const {
        std::ostringstream stream;
        stream << "vec" << values.size() << "(";
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index != 0) stream << ", ";
            stream << values[index];
        }
        stream << ")";
        return stream.str();
    }

    void emitSwizzle(const NodeInstance& node, const std::map<SlotId, std::string>& inputs,
                     std::ostringstream& body) {
        const SlotInstance* input = node.findSlot(0);
        const SlotInstance* output = node.findSlot(1);
        std::string order = settingString(node, "order");
        if (order.empty()) {
            order = "xyzw";
        }
        const SwizzleMask mask = parseSwizzleMask(order);
        if (!mask.valid) {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorInvalidValue;
            diagnostic.message = "Swizzle node has an invalid mask";
            diagnostic.node = node.id;
            m_diagnostics.push_back(std::move(diagnostic));
            body << "    " << m_outputs[node.id].begin()->second << " = "
                 << literal(Value::makeFloat(0.0f), targetType(*output),
                            node.concretePrecision)
                 << ";\n";
            return;
        }

        const std::string expression = adapt(inputs.at(0), targetType(*input),
                                             ConcreteValueType::Vector4, node.concretePrecision);
        const int outCount = mask.outputCount;
        std::vector<std::string> values;
        for (int index = 0; index < outCount; ++index) {
            const char channel = mask.mask[static_cast<std::size_t>(index)];
            const auto position = std::string("xyzw").find(channel);
            values.push_back(component(expression, position == std::string::npos
                                                         ? 0
                                                         : static_cast<int>(position)));
        }
        const std::string value = outCount == 1 ? (values.empty() ? "0.0" : values.front())
                                                : makeVectorLiteralFrom(values);
        body << "    " << m_outputs[node.id].begin()->second << " = " << value << ";\n";
    }

    void emitFlip(const NodeInstance& node, const std::map<SlotId, std::string>& inputs,
                  std::ostringstream& body) {
        const SlotInstance* input = node.findSlot(0);
        const int fromCount = channelCount(targetType(*input));
        const int outCount = channelCount(targetType(*node.findSlot(1)));
        const bool channels[4] = {
            settingBool(node, "redChannel", false),
            settingBool(node, "greenChannel", false),
            settingBool(node, "blueChannel", false),
            settingBool(node, "alphaChannel", false),
        };

        bool anyEnabled = false;
        for (int index = 0; index < outCount; ++index) {
            if (index < fromCount && channels[index]) {
                anyEnabled = true;
                break;
            }
        }
        if (!anyEnabled) {
            body << "    " << m_outputs[node.id].begin()->second << " = " << inputs.at(0)
                 << ";\n";
            return;
        }

        std::vector<std::string> values;
        for (int index = 0; index < outCount; ++index) {
            if (index >= fromCount) {
                values.push_back("0.0");
                continue;
            }
            const std::string source =
                fromCount == 1 && index == 0 ? inputs.at(0) : component(inputs.at(0), index);
            values.push_back(channels[index] ? "(-1.0 * " + source + ")" : source);
        }
        const std::string value = outCount == 1 ? (values.empty() ? "0.0" : values.front())
                                                : makeVectorLiteralFrom(values);
        body << "    " << m_outputs[node.id].begin()->second << " = " << value << ";\n";
    }

    void emitBlock(const NodeInstance& node, const NodeDef& definition, std::ostringstream& body) {
        if (node.slots.empty()) {
            return;
        }
        const SlotInstance& input = node.slots.front();
        std::string expression;
        if (const Edge* edge = m_graph.incomingEdge(node.id, input.spec.id); edge != nullptr) {
            const NodeInstance* source = m_graph.findNode(edge->fromNode);
            if (source != nullptr) {
                expression = outputExpression(*source, edge->fromSlot);
                const SlotInstance* output = source->findSlot(edge->fromSlot);
                if (!expression.empty() && output != nullptr) {
                    expression = adapt(expression, output->concreteType, ConcreteValueType::Vector4,
                                       source->concretePrecision);
                }
            }
        } else if (input.state.value) {
            expression = literal(*input.state.value, ConcreteValueType::Vector4, node.concretePrecision);
        } else {
            const auto found = m_target.defaults.find(definition.blockField);
            if (found != m_target.defaults.end()) {
                expression = literal(found->second, ConcreteValueType::Vector4, node.concretePrecision);
            }
        }
        if (expression.empty()) {
            if (definition.blockField == "positionOS") {
                return;
            }
            expression = "vec4(0.0)";
        }
        const std::string field = "surface." + definition.blockField;
        if (definition.blockField == "positionOS") {
            body << "    " << field << " = " << expression << ".xyz;\n";
        } else if (definition.blockField == "baseColor" || definition.blockField == "emission") {
            body << "    " << field << " = " << expression << ".xyz;\n";
        } else if (definition.blockField == "normalTangent") {
            body << "    " << field << " = normalize(gi.tangentWS * (" << expression << ".x) + "
                 << "gi.bitangentWS * (" << expression << ".y) + gi.normalWS * (" << expression
                 << ".z));\n";
        } else if (definition.blockField == "normalObject") {
            body << "    " << field << " = normalize(mul(u_model[0], vec4(" << expression
                 << ".xyz, 0.0)).xyz);\n";
        } else if (definition.blockField == "normalWorld") {
            body << "    " << field << " = normalize(" << expression << ".xyz);\n";
        } else if (definition.blockField == "metallic" || definition.blockField == "smoothness" ||
                   definition.blockField == "occlusion" || definition.blockField == "alpha" ||
                   definition.blockField == "alphaClipThreshold") {
            body << "    " << field << " = " << expression << ".x;\n";
        }
    }

    const Graph& m_graph;
    const NodeRegistry& m_registry;
    const ActiveGraph& m_active;
    const TargetSpec& m_target;
    const PropertySet& m_properties;
    const KeywordSet& m_keywords;
    const VariantInfo& m_variant;
    std::map<NodeId, std::map<SlotId, std::string>> m_outputs;
    FunctionRegistry m_functions;
    std::vector<Diagnostic> m_diagnostics;
};

} // namespace

std::string typeName(ConcreteValueType type, ConcretePrecision precision) {
    const char* prefix = precision == ConcretePrecision::Half ? "half" : "float";
    switch (type) {
    case ConcreteValueType::Float:
        return prefix;
    case ConcreteValueType::Vector2:
        return "vec2";
    case ConcreteValueType::Vector3:
        return "vec3";
    case ConcreteValueType::Vector4:
        return "vec4";
    case ConcreteValueType::Matrix2:
        return "mat2";
    case ConcreteValueType::Matrix3:
        return "mat3";
    case ConcreteValueType::Matrix4:
        return "mat4";
    case ConcreteValueType::Boolean:
        return "bool";
    case ConcreteValueType::Integer:
        return "int";
    default:
        return prefix;
    }
}

std::string literal(const Value& value, ConcreteValueType target, ConcretePrecision precision) {
    (void)precision;
    if (target == ConcreteValueType::Boolean) {
        if (const bool* boolean = value.asBool()) {
            return *boolean ? "true" : "false";
        }
        const float* scalar = value.asFloat();
        return scalar != nullptr && *scalar != 0.0f ? "true" : "false";
    }
    if (target == ConcreteValueType::Integer) {
        if (const std::int64_t* integer = value.asInteger()) {
            return std::to_string(*integer);
        }
    }
    if (target == ConcreteValueType::Matrix2 || target == ConcreteValueType::Matrix3 ||
        target == ConcreteValueType::Matrix4) {
        if (const Mat2* matrix = value.asMatrix2()) {
            std::ostringstream stream;
            stream << "mat2(";
            for (int index = 0; index < 4; ++index) {
                if (index != 0) stream << ", ";
                stream << formatFloat(matrix->m[static_cast<std::size_t>(index)]);
            }
            stream << ")";
            return stream.str();
        }
        if (const Mat3* matrix = value.asMatrix3()) {
            std::ostringstream stream;
            stream << "mat3(";
            for (int index = 0; index < 9; ++index) {
                if (index != 0) stream << ", ";
                stream << formatFloat(matrix->m[static_cast<std::size_t>(index)]);
            }
            stream << ")";
            return stream.str();
        }
        if (const Mat4* matrix = value.asMatrix4()) {
            std::ostringstream stream;
            stream << "mat4(";
            for (int index = 0; index < 16; ++index) {
                if (index != 0) stream << ", ";
                stream << formatFloat(matrix->m[static_cast<std::size_t>(index)]);
            }
            stream << ")";
            return stream.str();
        }
        return target == ConcreteValueType::Matrix2
                   ? "mat2(1.0)"
                   : (target == ConcreteValueType::Matrix3 ? "mat3(1.0)" : "mat4(1.0)");
    }
    int count = channelCount(target);
    if (count == 0) {
        count = 1;
    }
    float values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    valueComponents(value, values, count);
    if (count == 1) {
        return formatFloat(values[0]);
    }
    return makeVectorLiteral(count, values);
}

std::string adapt(const std::string& expression, ConcreteValueType from, ConcreteValueType to,
                  ConcretePrecision precision) {
    (void)precision;
    if (expression.empty() || from == to) {
        return expression;
    }
    if (to == ConcreteValueType::Boolean) {
        if (from == ConcreteValueType::Float || from == ConcreteValueType::Integer) {
            return "bool(" + expression + ")";
        }
        return "bool((" + expression + ").x)";
    }
    if (from == ConcreteValueType::Boolean) {
        const std::string scalar = "float(" + expression + ")";
        if (to == ConcreteValueType::Float || to == ConcreteValueType::Integer) {
            return scalar;
        }
        if (to == ConcreteValueType::Vector2) {
            return "vec2(" + scalar + ", " + scalar + ")";
        }
        if (to == ConcreteValueType::Vector3) {
            return "vec3(" + scalar + ", " + scalar + ", " + scalar + ")";
        }
        return "vec4(" + scalar + ", " + scalar + ", " + scalar + ", " + scalar + ")";
    }
    if (to == ConcreteValueType::Float) {
        return "(" + expression + ").x";
    }
    if (to == ConcreteValueType::Integer) {
        return "int((" + expression + ").x)";
    }
    if (to == ConcreteValueType::Vector2) {
        if (from == ConcreteValueType::Vector3 || from == ConcreteValueType::Vector4) {
            return "(" + expression + ").xy";
        }
        return "vec2(" + expression + ", " + expression + ")";
    }
    if (to == ConcreteValueType::Vector3) {
        if (from == ConcreteValueType::Vector2) {
            return "vec3(" + expression + ", 0.0)";
        }
        if (from == ConcreteValueType::Vector4) {
            return "(" + expression + ").xyz";
        }
        return "vec3(" + expression + ", " + expression + ", " + expression + ")";
    }
    if (to == ConcreteValueType::Vector4) {
        if (from == ConcreteValueType::Vector2) {
            return "vec4(" + expression + ", 0.0, 1.0)";
        }
        if (from == ConcreteValueType::Vector3) {
            return "vec4(" + expression + ", 1.0)";
        }
        return "vec4(" + expression + ", " + expression + ", " + expression + ", " + expression + ")";
    }
    return expression;
}

std::string defineName(const std::string& refName) {
    std::string result;
    for (char character : refName) {
        const bool alpha = (character >= 'a' && character <= 'z') ||
                           (character >= 'A' && character <= 'Z') || character == '_';
        const bool digit = character >= '0' && character <= '9';
        if (alpha || (!result.empty() && digit)) {
            result.push_back(character);
        } else if (digit) {
            result.push_back('_');
            result.push_back(character);
        }
    }
    return result.empty() ? "_SG_KEYWORD" : result;
}

EmittedGraph emitSurface(const Graph& graph, const NodeRegistry& registry, const ActiveGraph& active,
                         const TargetSpec& target, const PropertySet& properties,
                         const KeywordSet& keywords, const VariantInfo& variant) {
    return Emitter(graph, registry, active, target, properties, keywords, variant).run();
}

} // namespace sg::detail
