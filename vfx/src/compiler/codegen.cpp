#include "codegen.h"

#include "../core/custom_hlsl.h"
#include "../core/subgraph.h"
#include "vfx/expression.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace vfx::detail {
namespace {

constexpr std::uint32_t kThreadsPerGroup = 64u;
constexpr std::uint32_t kMaxParticleCapacity = 1u << 20;
constexpr std::uint32_t kParticlesPerDrawInstance = 2048u;
constexpr std::uint32_t kSortChunkSize = 4096u;
constexpr std::uint32_t kSortThreads = 1024u;
constexpr std::uint32_t kSortElementsPerThread = 4u;
constexpr std::uint32_t kMergeThreads = 64u;

const char* hlslType(ExprType type) noexcept {
    switch (type) {
    case ExprType::Float:
        return "float";
    case ExprType::Float2:
        return "float2";
    case ExprType::Float3:
        return "float3";
    case ExprType::Float4:
        return "float4";
    case ExprType::UInt:
        return "uint";
    case ExprType::Int:
        return "int";
    case ExprType::Bool:
        return "bool";
    case ExprType::Invalid:
        break;
    }
    return "float";
}

const char* hlslType(AttributeType type) noexcept {
    switch (type) {
    case AttributeType::Float:
        return "float";
    case AttributeType::Float2:
        return "float2";
    case AttributeType::Float3:
        return "float3";
    case AttributeType::Float4:
        return "float4";
    case AttributeType::UInt:
        return "uint";
    case AttributeType::Int:
        return "int";
    case AttributeType::Bool:
        return "bool";
    }
    return "float";
}

ExprType expressionType(AttributeType type) noexcept {
    switch (type) {
    case AttributeType::Float:
        return ExprType::Float;
    case AttributeType::Float2:
        return ExprType::Float2;
    case AttributeType::Float3:
        return ExprType::Float3;
    case AttributeType::Float4:
        return ExprType::Float4;
    case AttributeType::UInt:
        return ExprType::UInt;
    case AttributeType::Int:
        return ExprType::Int;
    case AttributeType::Bool:
        return ExprType::Bool;
    }
    return ExprType::Invalid;
}

std::string formatFloat(float value) {
    char buffer[64] = {};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general);
    std::string text;
    if (result.ec == std::errc()) {
        text.assign(buffer, result.ptr);
    } else {
        text = std::to_string(value);
    }
    if (text.find_first_of(".eE") == std::string::npos) {
        text += ".0";
    }
    return text;
}

std::string formatAttributeLiteral(const AttributeDesc& attribute) {
    const AttributeWords words = attributeDefaultWords(attribute);
    switch (attribute.type) {
    case AttributeType::Float:
        return formatFloat(std::bit_cast<float>(words[0]));
    case AttributeType::Float2:
        return "float2(" + formatFloat(std::bit_cast<float>(words[0])) + ", " +
               formatFloat(std::bit_cast<float>(words[1])) + ")";
    case AttributeType::Float3:
        return "float3(" + formatFloat(std::bit_cast<float>(words[0])) + ", " +
               formatFloat(std::bit_cast<float>(words[1])) + ", " +
               formatFloat(std::bit_cast<float>(words[2])) + ")";
    case AttributeType::Float4:
        return "float4(" + formatFloat(std::bit_cast<float>(words[0])) + ", " +
               formatFloat(std::bit_cast<float>(words[1])) + ", " +
               formatFloat(std::bit_cast<float>(words[2])) + ", " +
               formatFloat(std::bit_cast<float>(words[3])) + ")";
    case AttributeType::UInt:
        return std::to_string(words[0]) + "u";
    case AttributeType::Int:
        return std::to_string(static_cast<std::int32_t>(words[0]));
    case AttributeType::Bool:
        return words[0] != 0u ? "true" : "false";
    }
    return "0.0";
}

std::string indentLines(std::string_view code, std::string_view indent) {
    std::string result;
    std::size_t start = 0;
    while (start < code.size()) {
        const std::size_t end = code.find('\n', start);
        result += indent;
        if (end == std::string_view::npos) {
            result.append(code.substr(start));
            result.push_back('\n');
            break;
        }
        result.append(code.substr(start, end - start + 1));
        start = end + 1;
    }
    return result;
}

std::string localName(std::string_view name) {
    return "a_" + std::string(name);
}

std::string attributeLocal(const AttributeLayoutEntry& entry) {
    return localName(entry.attribute.name);
}

std::string attributeLoad(const AttributeLayoutEntry& entry) {
    return "attributes.Load_" + entry.attribute.name + "()";
}

std::string attributeStore(const AttributeLayoutEntry& entry) {
    return "attributes.Store_" + entry.attribute.name + "(" + attributeLocal(entry) + ");";
}

std::string blockSeed(std::string_view path, std::string_view type, std::uint32_t salt = 0) {
    std::uint32_t hash = 2166136261u;
    const auto feed = [&hash](std::string_view text) {
        for (const char character : text) {
            hash ^= static_cast<std::uint8_t>(character);
            hash *= 16777619u;
        }
    };
    feed(path);
    hash ^= 0x2eu;
    hash *= 16777619u;
    feed(type);
    hash ^= salt;
    hash *= 16777619u;
    return std::to_string(hash) + "u";
}

std::uint32_t groupCount(std::uint32_t count) {
    return std::max(1u, (count + kThreadsPerGroup - 1u) / kThreadsPerGroup);
}

std::string propertyMacroValue(const Property& property) {
    switch (property.type) {
    case PropertyType::Float:
        return "(u_vfxProp_" + property.name + ".x)";
    case PropertyType::Int:
        return "(int(u_vfxProp_" + property.name + ".x))";
    case PropertyType::Bool:
        return "(u_vfxProp_" + property.name + ".x != 0.0)";
    case PropertyType::Vector2:
        return "(u_vfxProp_" + property.name + ".xy)";
    case PropertyType::Vector3:
        return "(u_vfxProp_" + property.name + ".xyz)";
    case PropertyType::Vector4:
    case PropertyType::Color:
        return "(u_vfxProp_" + property.name + ")";
    case PropertyType::Texture:
    case PropertyType::Curve:
    case PropertyType::Gradient:
        return "(0.0)";
    }
    return "(0.0)";
}

std::string makeCurveFunction(const Property& property, std::uint32_t row, std::uint32_t rowCount) {
    const float rowV = (static_cast<float>(row) + 0.5f) / static_cast<float>(rowCount);
    std::string code;
    code += "float vfxSampleCurve_" + property.name + "(float t)\n{\n";
    code += "    const float u = (saturate(t) * 255.0 + 0.5) / 256.0;\n";
    code += "    return texture2DLod(s_vfxCurveLut, vec2(u, " + formatFloat(rowV) + "), 0.0).x;\n}\n";
    return code;
}

std::string makeGradientFunction(const Property& property, std::uint32_t row, std::uint32_t rowCount) {
    const float rowV = (static_cast<float>(row) + 0.5f) / static_cast<float>(rowCount);
    std::string code;
    code += "vec4 vfxSampleGradient_" + property.name + "(float t)\n{\n";
    code += "    const float u = (saturate(t) * 255.0 + 0.5) / 256.0;\n";
    code += "    return texture2DLod(s_vfxGradientLut, vec2(u, " + formatFloat(rowV) + "), 0.0);\n}\n";
    return code;
}

std::string makeExpressionHelpers(const Graph& graph) {
    bool needsCurlNoise = false;
    std::uint32_t curveCount = 0;
    std::uint32_t gradientCount = 0;
    for (const Property& property : graph.properties) {
        curveCount += property.type == PropertyType::Curve ? 1u : 0u;
        gradientCount += property.type == PropertyType::Gradient ? 1u : 0u;
    }
    for (const SystemDesc& system : graph.systems) {
        for (const Block& block : system.updateBlocks) {
            if (block.enabled && block.type == "turbulence") {
                needsCurlNoise = true;
                break;
            }
        }
    }

    std::string code;
    code += "uniform vec4 u_vfx_params;\n";
    code += "uniform vec4 u_vfx_ids;\n";
    for (const Property& property : graph.properties) {
        code += "uniform vec4 u_vfxProp_" + property.name + ";\n";
    }
    if (curveCount > 0u) {
        code += "SAMPLER2D(s_vfxCurveLut, 9);\n";
    }
    if (gradientCount > 0u) {
        code += "SAMPLER2D(s_vfxGradientLut, 10);\n";
    }
    code += "\n";
    code += "#define VFX_ATTR(name) a_##name\n";
    code += "#define VFX_SET_ATTR(name, value) a_##name = (value)\n";
    for (const Property& property : graph.properties) {
        code += "#define VFX_PROP_" + property.name + " " + propertyMacroValue(property) + "\n";
    }
    code += "#define VFX_PROP(name) VFX_PROP_##name\n";
    code += "#define VFX_TIME (u_vfx_params.x)\n";
    code += "#define VFX_DT (u_vfx_params.y)\n";
    code += "#define VFX_LIFETIME_RATIO (a_age / max(a_lifetime, 1e-6))\n";
    code += "\n";
    code += R"(uint vfxWangHash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed ^= seed >> 4u;
    seed *= 0x27d4eb2du;
    seed ^= seed >> 15u;
    return seed;
}

float vfxRand(uint seed)
{
    return float(vfxWangHash(seed) & 0x00ffffffu) / 16777215.0;
}

vec4 vfxRand4(uint seed)
{
    return vec4(vfxRand(seed), vfxRand(seed + 0x9e3779b9u),
                vfxRand(seed + 0x3c6ef372u), vfxRand(seed + 0xdaa66d2bu));
}

float vfxCellHash(vec3 cell, uint seed)
{
    return vfxRand(seed ^ uint(cell.x) ^ (uint(cell.y) << 10u) ^ (uint(cell.z) << 20u));
}

float vfxNoise3(vec3 position, float frequency, uint seed)
{
    const vec3 p = position * frequency;
    const vec3 cell = floor(p);
    const vec3 f = fract(p);
    const float n000 = vfxCellHash(cell, seed);
    const float n100 = vfxCellHash(cell + vec3(1.0, 0.0, 0.0), seed);
    const float n010 = vfxCellHash(cell + vec3(0.0, 1.0, 0.0), seed);
    const float n110 = vfxCellHash(cell + vec3(1.0, 1.0, 0.0), seed);
    const float n001 = vfxCellHash(cell + vec3(0.0, 0.0, 1.0), seed);
    const float n101 = vfxCellHash(cell + vec3(1.0, 0.0, 1.0), seed);
    const float n011 = vfxCellHash(cell + vec3(0.0, 1.0, 1.0), seed);
    const float n111 = vfxCellHash(cell + vec3(1.0, 1.0, 1.0), seed);
    const vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(n000, n100, u.x), mix(n010, n110, u.x), u.y),
               mix(mix(n001, n101, u.x), mix(n011, n111, u.x), u.y), u.z);
}

float vfxNoise(float position, float frequency, uint seed)
{
    return vfxNoise3(vec3(position, 0.0, 0.0), frequency, seed);
}

float vfxNoise(vec2 position, float frequency, uint seed)
{
    return vfxNoise3(vec3(position, 0.0), frequency, seed);
}

float vfxNoise(vec3 position, float frequency, uint seed)
{
    return vfxNoise3(position, frequency, seed);
}
)";
    if (needsCurlNoise) {
        code += R"(
float vfxNoiseCell(int x, int y)
{
    return vfxRand(uint(x) * 0x8da6b343u ^ uint(y) * 0xd8163841u);
}

vec4 vfxNoiseHash2D(vec2 cell)
{
    const int x = int(cell.x);
    const int y = int(cell.y);
    return vec4(vfxNoiseCell(x, y), vfxNoiseCell(x + 1, y), vfxNoiseCell(x, y + 1),
                vfxNoiseCell(x + 1, y + 1));
}

vec3 vfxValueNoise2D(vec2 coordinate)
{
    const vec2 cell = floor(coordinate);
    const vec2 f = coordinate - cell;
    const vec4 hash = vfxNoiseHash2D(cell);
    const float blendX = f.x * f.x * (f.x * (f.x * (f.x * 6.0 - 15.0) + 10.0));
    const float blendY = f.y * f.y * (f.y * (f.y * (f.y * 6.0 - 15.0) + 10.0));
    const float derivativeX = f.x * f.x * (f.x * (f.x * 30.0 - 60.0) + 30.0);
    const float derivativeY = f.y * f.y * (f.y * (f.y * 30.0 - 60.0) + 30.0);
    const float r0 = lerp(hash.x, hash.z, blendY);
    const float r1 = lerp(hash.y, hash.w, blendY);
    const float r2 = lerp(hash.x, hash.y, blendX);
    const float r3 = lerp(hash.z, hash.w, blendX);
    const float deltaX = r1 - r0;
    const float deltaY = r3 - r2;
    return vec3(r0 + deltaX * blendX, deltaX * derivativeX, deltaY * derivativeY);
}

#if BGFX_SHADER_LANGUAGE_HLSL
#pragma warning(disable : 3557)
#endif

vec3 vfxCurlNoise3D(vec3 coordinate, float frequency, int octaves, float persistence,
                    float lacunarity)
{
    const int count = clamp(octaves, 1, 8);
    float amplitude = 1.0;
    float totalAmplitude = 0.0;
    vec2 total0 = vec2(0.0, 0.0);
    vec2 total1 = vec2(0.0, 0.0);
    vec2 total2 = vec2(0.0, 0.0);
    for (int octave = 0; octave < count; ++octave)
    {
        const vec3 n0 = vfxValueNoise2D(vec2(coordinate.z, coordinate.y) * frequency);
        const vec3 n1 = vfxValueNoise2D((vec2(coordinate.x, coordinate.z) + 100.0) * frequency);
        const vec3 n2 = vfxValueNoise2D((vec2(coordinate.y, coordinate.x) + 200.0) * frequency);
        total0 += n0.yz * amplitude;
        total1 += n1.yz * amplitude;
        total2 += n2.yz * amplitude;
        totalAmplitude += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return vec3(total2.x - total1.y, total0.x - total2.y, total1.x - total0.y) /
           max(totalAmplitude, 1e-6);
}
)";
    }
    code += R"(
vec3 vfxSafeNormalize(vec3 value)
{
    return value / max(length(value), 1e-6);
}

vec3 vfxRandomUnitVector(uint seed)
{
    const float z = vfxRand(seed) * 2.0 - 1.0;
    const float angle = vfxRand(seed + 0x9e3779b9u) * 6.28318530718;
    const float radius = sqrt(max(0.0, 1.0 - z * z));
    return vec3(cos(angle) * radius, sin(angle) * radius, z);
}

vec3 vfxRandomCone(vec3 axis, float angle, uint seed)
{
    const vec3 normalizedAxis = normalize(axis);
    const vec3 tangent = normalize(cross(normalizedAxis,
        abs(normalizedAxis.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
    const vec3 bitangent = cross(normalizedAxis, tangent);
    const float cosMax = cos(radians(max(angle, 0.0)));
    const float z = lerp(cosMax, 1.0, vfxRand(seed));
    const float azimuth = vfxRand(seed + 0x9e3779b9u) * 6.28318530718;
    const float radius = sqrt(max(0.0, 1.0 - z * z));
    return normalize(tangent * (cos(azimuth) * radius) + bitangent * (sin(azimuth) * radius) +
                     normalizedAxis * z);
}

#define VFX_RAND(seed) vfxRand(seed)
#define VFX_RAND4(seed) vfxRand4(seed)
#define VFX_NOISE(position, frequency, seed) vfxNoise(position, frequency, seed)
)";
    std::uint32_t curveRow = 0;
    std::uint32_t gradientRow = 0;
    for (const Property& property : graph.properties) {
        if (property.type == PropertyType::Curve) {
            code += makeCurveFunction(property, curveRow++, curveCount);
            code += "#define VFX_SAMPLE_CURVE_" + property.name + "(t) vfxSampleCurve_" + property.name +
                    "(t)\n";
        } else if (property.type == PropertyType::Gradient) {
            code += makeGradientFunction(property, gradientRow++, gradientCount);
            code += "#define VFX_SAMPLE_GRADIENT_" + property.name + "(t) vfxSampleGradient_" +
                    property.name + "(t)\n";
        }
    }
    code += "#define VFX_SAMPLE_CURVE(name, t) VFX_SAMPLE_CURVE_##name(t)\n";
    code += "#define VFX_SAMPLE_GRADIENT(name, t) VFX_SAMPLE_GRADIENT_##name(t)\n";
    return code;
}

std::vector<KernelBinding> makeLutBindings(const Graph& graph) {
    std::uint32_t curveProperty = kInvalidIndex;
    std::uint32_t gradientProperty = kInvalidIndex;
    for (std::uint32_t i = 0; i < graph.properties.size(); ++i) {
        if (graph.properties[i].type == PropertyType::Curve && curveProperty == kInvalidIndex) {
            curveProperty = i;
        } else if (graph.properties[i].type == PropertyType::Gradient && gradientProperty == kInvalidIndex) {
            gradientProperty = i;
        }
    }
    std::vector<KernelBinding> bindings;
    if (curveProperty != kInvalidIndex) {
        bindings.push_back({BindingKind::Texture, 9, Access::Read, curveProperty});
    }
    if (gradientProperty != kInvalidIndex) {
        bindings.push_back({BindingKind::Texture, 10, Access::Read, gradientProperty});
    }
    return bindings;
}

void collectAttributeReads(const Value& value, std::set<std::string>& reads) {
    switch (value.kind) {
    case Value::Kind::Expression:
        if (value.expression != nullptr) {
            if (value.expression->op == "attr" || value.expression->op == "attribute") {
                for (const auto& [name, argument] : value.expression->arguments) {
                    if (name == "name" && argument.kind == Value::Kind::String) {
                        reads.insert(argument.string);
                    }
                }
            }
            for (const auto& [name, argument] : value.expression->arguments) {
                (void)name;
                collectAttributeReads(argument, reads);
            }
        }
        break;
    case Value::Kind::Array:
        for (const Value& item : value.array) {
            collectAttributeReads(item, reads);
        }
        break;
    case Value::Kind::Object:
        for (const auto& [name, member] : value.object) {
            (void)name;
            collectAttributeReads(member, reads);
        }
        break;
    case Value::Kind::Invalid:
    case Value::Kind::Number:
    case Value::Kind::Bool:
    case Value::Kind::String:
        break;
    }
}

void collectBlockUsage(const Block& block, AttributeUsage& usage) {
    if (block.parameters.kind == Value::Kind::Object) {
        for (const auto& [name, value] : block.parameters.object) {
            (void)name;
            collectAttributeReads(value, usage.reads);
        }
    }
    const auto write = [&usage](std::string_view name) {
        if (!name.empty()) {
            usage.writes.emplace(name);
        }
    };
    if (block.type == "custom_hlsl") {
        const auto addAttributes = [&](const char* field, std::set<std::string>& destination) {
            const Value* list = block.parameter(field);
            if (list == nullptr || list->kind != Value::Kind::Array) {
                return;
            }
            for (const Value& item : list->array) {
                if (item.kind == Value::Kind::String) {
                    destination.insert(item.string);
                }
            }
        };
        addAttributes("reads", usage.reads);
        addAttributes("writes", usage.writes);
    } else if (block.type == "set_position_sphere" || block.type == "set_position_box") {
        write("position");
    } else if (block.type == "set_velocity_random" || block.type == "set_velocity_direction") {
        write("velocity");
    } else if (block.type == "set_color") {
        write("color");
    } else if (block.type == "set_alpha") {
        write("alpha");
    } else if (block.type == "set_size") {
        write("size");
    } else if (block.type == "set_lifetime") {
        write("lifetime");
    } else if (block.type == "set_angle") {
        write("angle");
    } else if (block.type == "set_scale") {
        write("scale");
    } else if (block.type == "set_attribute" || block.type == "set_random") {
        const Value* attribute = block.parameter("attribute");
        if (attribute != nullptr && attribute->kind == Value::Kind::String) {
            write(attribute->string);
        }
    } else if (block.type == "integrate") {
        write("position");
    } else if (block.type == "turbulence") {
        usage.reads.emplace("position");
        write("velocity");
    } else if (block.type == "collision_plane" || block.type == "collision_sphere") {
        usage.reads.emplace("position");
        usage.reads.emplace("velocity");
        write("position");
        write("velocity");
        write("alive");
    } else if (block.type == "gravity" || block.type == "drag" || block.type == "force" ||
               block.type == "attractor") {
        write("velocity");
    } else if (block.type == "age") {
        write("age");
    } else if (block.type == "reap") {
        write("alive");
    } else if (block.type == "color_over_life") {
        write("color");
    } else if (block.type == "alpha_over_life") {
        write("alpha");
    } else if (block.type == "size_over_life") {
        write("size");
    }
}

void analyzeAttributeUsage(const SystemDesc& system, AttributeUsage& usage) {
    for (const Block& block : system.initBlocks) {
        if (block.enabled) {
            collectBlockUsage(block, usage);
        }
    }
    for (const Block& block : system.updateBlocks) {
        if (block.enabled) {
            collectBlockUsage(block, usage);
        }
    }
}

std::string makeCollisionResponseCode(std::string_view mode, const std::string& restitution,
                                      const std::string& friction) {
    std::string code;
    code += "        if (collisionHit)\n        {\n";
    if (mode == "kill") {
        code += "            a_alive = false;\n";
    } else if (mode == "stick") {
        code += "            a_position = collisionHitPos;\n";
        code += "            a_velocity = vec3(0.0, 0.0, 0.0);\n";
    } else {
        code += "            const float collisionRestitution = max((" + restitution + "), 0.0);\n";
        code += "            const float collisionFriction = max((" + friction + "), 0.0);\n";
        code += "            const float collisionNormalSpeed = dot(a_velocity, collisionNormal);\n";
        code += "            if (collisionNormalSpeed < 0.0)\n            {\n";
        code += "                a_velocity = a_velocity - (1.0 + collisionRestitution) * "
                "collisionNormalSpeed * collisionNormal;\n";
        code += "            }\n";
        code += "            const vec3 collisionTangent = a_velocity - collisionNormal * "
                "dot(a_velocity, collisionNormal);\n";
        code += "            a_velocity = a_velocity - (1.0 - exp(-collisionFriction * VFX_DT)) * "
                "collisionTangent;\n";
        code += "            a_position = collisionHitPos;\n";
    }
    code += "        }\n";
    return code;
}

class BlockCompiler {
  public:
    BlockCompiler(const Graph& graph, const CompiledSystem& system, const std::string& systemPath,
                  const CompiledEventLink* eventLink, std::string& code)
        : m_graph(graph), m_system(system), m_context(graph), m_systemPath(systemPath),
          m_eventLink(eventLink), m_code(code) {}

    const std::string& error() const noexcept { return m_error; }

    bool compileInitBlock(const Block& block, std::size_t index) {
        const std::string path = m_systemPath + ".init.blocks[" + std::to_string(index) + "]";
        const std::string seed = "a_seed ^ " + blockSeed(path, block.type);
        if (block.type == "custom_hlsl") {
            return compileCustomHlsl(block, path);
        }
        if (block.type == "set_position_sphere") {
            std::string center;
            std::string radius;
            if (!expression(*block.parameter("center"), path + ".center", ExprType::Float3, center) ||
                !expression(*block.parameter("radius"), path + ".radius", ExprType::Float, radius)) {
                return false;
            }
            const Value* surface = block.parameter("surface");
            const bool onSurface = surface != nullptr && surface->kind == Value::Kind::Bool && surface->boolean;
            m_code += "    {\n";
            m_code += "        const vec3 center = " + center + ";\n";
            m_code += "        const float radius = " + radius + ";\n";
            m_code += "        const vec3 direction = vfxRandomUnitVector(" + seed + ");\n";
            if (onSurface) {
                m_code += "        a_position = center + direction * radius;\n";
            } else {
                m_code += "        a_position = center + direction * (radius * pow(vfxRand(" + seed +
                          " ^ 0x9e3779b9u), 1.0 / 3.0));\n";
            }
            m_code += "    }\n";
            return true;
        }
        if (block.type == "set_position_box") {
            std::string center;
            std::string size;
            if (!expression(*block.parameter("center"), path + ".center", ExprType::Float3, center) ||
                !expression(*block.parameter("size"), path + ".size", ExprType::Float3, size)) {
                return false;
            }
            m_code += "    {\n";
            m_code += "        const vec3 center = " + center + ";\n";
            m_code += "        const vec3 size = " + size + ";\n";
            m_code += "        a_position = center + (vfxRand4(" + seed + ").xyz * 2.0 - 1.0) * size * 0.5;\n";
            m_code += "    }\n";
            return true;
        }
        if (block.type == "set_velocity_random") {
            std::string minSpeed;
            std::string maxSpeed;
            if (!expression(*block.parameter("minSpeed"), path + ".minSpeed", ExprType::Float, minSpeed) ||
                !expression(*block.parameter("maxSpeed"), path + ".maxSpeed", ExprType::Float, maxSpeed)) {
                return false;
            }
            const Value* directionValue = block.parameter("direction");
            const Value* coneValue = block.parameter("coneAngle");
            std::string direction;
            std::string cone;
            if (directionValue != nullptr &&
                !expression(*directionValue, path + ".direction", ExprType::Float3, direction)) {
                return false;
            }
            if (coneValue != nullptr && !expression(*coneValue, path + ".coneAngle", ExprType::Float, cone)) {
                return false;
            }
            m_code += "    {\n";
            m_code += "        const float speed = lerp(" + minSpeed + ", " + maxSpeed + ", vfxRand(" + seed +
                      " ^ 0x3c6ef372u));\n";
            if (direction.empty()) {
                m_code += "        a_velocity = vfxRandomUnitVector(" + seed + ") * speed;\n";
            } else if (cone.empty()) {
                m_code += "        a_velocity = vfxSafeNormalize(" + direction + ") * speed;\n";
            } else {
                m_code += "        a_velocity = vfxRandomCone(" + direction + ", " + cone + ", " + seed +
                          ") * speed;\n";
            }
            m_code += "    }\n";
            return true;
        }
        if (block.type == "set_velocity_direction") {
            std::string direction;
            std::string speed;
            if (!expression(*block.parameter("direction"), path + ".direction", ExprType::Float3, direction) ||
                !expression(*block.parameter("speed"), path + ".speed", ExprType::Float, speed)) {
                return false;
            }
            m_code += "    a_velocity = vfxSafeNormalize(" + direction + ") * " + speed + ";\n";
            return true;
        }
        if (block.type == "set_color") {
            std::string color;
            if (!expression(*block.parameter("color"), path + ".color", ExprType::Float3, color, true)) {
                return false;
            }
            m_code += "    a_color = " + color + ";\n";
            return true;
        }
        if (block.type == "set_alpha" || block.type == "set_size" || block.type == "set_lifetime" ||
            block.type == "set_angle") {
            const char* field = block.type == "set_alpha"   ? "alpha"
                                : block.type == "set_size"  ? "size"
                                : block.type == "set_lifetime" ? "lifetime"
                                                               : "angle";
            std::string value;
            if (!expression(*block.parameter(field), path + "." + field, ExprType::Float, value)) {
                return false;
            }
            m_code += "    " + localName(field) + " = " + value + ";\n";
            return true;
        }
        if (block.type == "set_scale") {
            std::string value;
            if (!expression(*block.parameter("scale"), path + ".scale", ExprType::Float3, value)) {
                return false;
            }
            m_code += "    a_scale = " + value + ";\n";
            return true;
        }
        if (block.type == "set_attribute") {
            const AttributeLayoutEntry* entry = targetAttribute(block, path);
            if (entry == nullptr) {
                return false;
            }
            std::string value;
            if (!expression(*block.parameter("value"), path + ".value", expressionType(entry->attribute.type),
                            value, entry->attribute.type == AttributeType::Float3)) {
                return false;
            }
            m_code += "    " + attributeLocal(*entry) + " = " + value + ";\n";
            return true;
        }
        if (block.type == "set_random") {
            const AttributeLayoutEntry* entry = targetAttribute(block, path);
            if (entry == nullptr) {
                return false;
            }
            const ExprType type = expressionType(entry->attribute.type);
            if (type != ExprType::Float && type != ExprType::Float2 && type != ExprType::Float3 &&
                type != ExprType::Float4) {
                return fail(path + ".attribute", "set_random requires a float attribute target");
            }
            std::string minimum;
            std::string maximum;
            if (!expression(*block.parameter("min"), path + ".min", type, minimum) ||
                !expression(*block.parameter("max"), path + ".max", type, maximum)) {
                return false;
            }
            std::string random = "vfxRand(" + seed + " ^ 0xdaa66d2bu)";
            if (type != ExprType::Float) {
                const std::uint8_t components = static_cast<std::uint8_t>(expressionComponentCount(type));
                random = std::string(hlslType(type)) + "(";
                for (std::uint8_t i = 0; i < components; ++i) {
                    if (i != 0) {
                        random += ", ";
                    }
                    random += "vfxRand(" + seed + " ^ " + std::to_string(0xdaa66d2bu + i) + "u)";
                }
                random += ")";
            }
            m_code += "    " + attributeLocal(*entry) + " = lerp(" + minimum + ", " + maximum + ", " + random +
                      ");\n";
            return true;
        }
        return fail(path + ".type", "unsupported init block '" + block.type +
                                    "' in V1.0b.5; supported blocks are position/velocity/color/alpha/size/"
                                    "lifetime/angle/scale/attribute/random");
    }

    bool compileUpdateBlock(const Block& block, std::size_t index) {
        const std::string path = m_systemPath + ".update.blocks[" + std::to_string(index) + "]";
        if (block.type == "custom_hlsl") {
            return compileCustomHlsl(block, path);
        }
        if (block.type == "trigger_event") {
            if (m_eventLink == nullptr) {
                return fail(path + ".type", "trigger_event requires an emit event on this system");
            }
            if (m_hasTrigger) {
                return fail(path + ".type", "only one trigger_event block is supported per system");
            }
            const Value* event = block.parameter("event");
            if (event == nullptr || event->kind != Value::Kind::String) {
                return fail(path + ".event", "must name an event");
            }
            if (event->string != m_eventLink->name) {
                return fail(path + ".event", "event '" + event->string + "' does not match emit event '" +
                                                 m_eventLink->name + "'");
            }
            m_triggerCount = "1.0";
            if (const Value* count = block.parameter("count"); count != nullptr &&
                !expression(*count, path + ".count", ExprType::Float, m_triggerCount)) {
                return false;
            }
            m_triggerCondition.clear();
            if (const Value* condition = block.parameter("condition"); condition != nullptr &&
                !expression(*condition, path + ".condition", ExprType::Bool, m_triggerCondition)) {
                return false;
            }
            m_hasTrigger = true;
            return true;
        }
        if (block.type == "integrate") {
            m_code += "    a_position = a_position + a_velocity * VFX_DT;\n";
            return true;
        }
        if (block.type == "age") {
            m_code += "    a_age = a_age + VFX_DT;\n";
            return true;
        }
        if (block.type == "reap") {
            std::string condition;
            const Value* value = block.parameter("condition");
            if (value == nullptr) {
                condition = "(a_age >= a_lifetime)";
            } else if (!expression(*value, path + ".condition", ExprType::Bool, condition)) {
                return false;
            }
            m_code += "    if (" + condition + ") a_alive = false;\n";
            return true;
        }
        if (block.type == "gravity") {
            std::string value;
            if (!expression(*block.parameter("gravity"), path + ".gravity", ExprType::Float3, value)) {
                return false;
            }
            m_code += "    a_velocity = a_velocity + " + value + " * VFX_DT;\n";
            return true;
        }
        if (block.type == "drag") {
            std::string value;
            if (!optionalExpression(block, "drag", ExprType::Float, path, value, "0.5")) {
                return false;
            }
            bool useSize = false;
            if (const Value* useSizeValue = block.parameter("useSize"); useSizeValue != nullptr) {
                if (useSizeValue->kind != Value::Kind::Bool) {
                    return fail(path + ".useSize", "must be a literal bool");
                }
                useSize = useSizeValue->boolean;
            }
            m_code += "    {\n";
            m_code += "        float drag = " + value + ";\n";
            if (useSize) {
                m_code += "        drag *= a_size * a_size * a_scale.x * a_scale.y;\n";
            }
            m_code += "        a_velocity = a_velocity * exp(-drag * VFX_DT);\n";
            m_code += "    }\n";
            return true;
        }
        if (block.type == "force") {
            std::string value;
            if (!expression(*block.parameter("force"), path + ".force", ExprType::Float3, value)) {
                return false;
            }
            bool relative = false;
            std::string drag;
            if (!relativeMode(block, false, path, relative) ||
                !optionalExpression(block, "drag", ExprType::Float, path, drag, "1.0")) {
                return false;
            }
            m_code += "    {\n";
            m_code += "        const vec3 forceValue = " + value + ";\n";
            if (relative) {
                m_code += "        a_velocity = a_velocity + (forceValue - a_velocity) * min(1.0, (" +
                          drag + ") * VFX_DT);\n";
            } else {
                m_code += "        a_velocity = a_velocity + forceValue * VFX_DT;\n";
            }
            m_code += "    }\n";
            return true;
        }
        if (block.type == "attractor") {
            std::string position;
            std::string strength;
            std::string falloff;
            if (!expression(*block.parameter("position"), path + ".position", ExprType::Float3, position) ||
                !expression(*block.parameter("strength"), path + ".strength", ExprType::Float, strength)) {
                return false;
            }
            const Value* falloffValue = block.parameter("falloff");
            if (falloffValue != nullptr &&
                !expression(*falloffValue, path + ".falloff", ExprType::Float, falloff)) {
                return false;
            }
            if (falloff.empty()) {
                falloff = "1.0";
            }
            m_code += "    {\n";
            m_code += "        const vec3 delta = a_position - (" + position + ");\n";
            m_code += "        const float distanceSquared = max(dot(delta, delta), 1e-4);\n";
            m_code += "        a_velocity = a_velocity + (delta / sqrt(distanceSquared)) * ((" + strength +
                      ") / (distanceSquared + (" + falloff + "))) * VFX_DT;\n";
            m_code += "    }\n";
            return true;
        }
        if (block.type == "turbulence") {
            std::string intensity;
            std::string frequency;
            std::string octaves;
            std::string roughness;
            std::string lacunarity;
            std::string scroll;
            if (!optionalExpression(block, "intensity", ExprType::Float, path, intensity, "1.0") ||
                !optionalExpression(block, "frequency", ExprType::Float, path, frequency, "1.0") ||
                !optionalExpression(block, "octaves", ExprType::Int, path, octaves, "1") ||
                !optionalExpression(block, "roughness", ExprType::Float, path, roughness, "0.5") ||
                !optionalExpression(block, "lacunarity", ExprType::Float, path, lacunarity, "2.0") ||
                !optionalExpression(block, "scroll", ExprType::Float3, path, scroll,
                                    "vec3(0.0, 0.0, 0.0)")) {
                return false;
            }
            bool relative = true;
            std::string drag;
            if (!relativeMode(block, true, path, relative) ||
                !optionalExpression(block, "drag", ExprType::Float, path, drag, "1.0")) {
                return false;
            }
            m_code += "    {\n";
            m_code += "        const vec3 fieldPosition = a_position + (" + scroll + ") * VFX_TIME + "
                      "vec3(0.5, 0.5, 0.5);\n";
            m_code += "        const vec3 fieldValue = vfxCurlNoise3D(fieldPosition, " + frequency + ", int(" +
                      octaves + "), " + roughness + ", " + lacunarity + ") * (" + intensity + ");\n";
            if (relative) {
                m_code += "        a_velocity = a_velocity + (fieldValue - a_velocity) * min(1.0, (" + drag +
                          ") * VFX_DT);\n";
            } else {
                m_code += "        a_velocity = a_velocity + fieldValue * VFX_DT;\n";
            }
            m_code += "    }\n";
            return true;
        }
        if (block.type == "collision_plane" || block.type == "collision_sphere") {
            std::string mode = "bounce";
            if (const Value* modeValue = block.parameter("mode"); modeValue != nullptr) {
                if (modeValue->kind != Value::Kind::String ||
                    (modeValue->string != "kill" && modeValue->string != "bounce" &&
                     modeValue->string != "stick")) {
                    return fail(path + ".mode", "expected 'kill', 'bounce', or 'stick'");
                }
                mode = modeValue->string;
            }
            std::string restitution = "0.1";
            std::string friction = "0.0";
            if (mode == "bounce" &&
                (!optionalExpression(block, "restitution", ExprType::Float, path, restitution, "0.1") ||
                 !optionalExpression(block, "friction", ExprType::Float, path, friction, "0.0"))) {
                return false;
            }
            m_code += "    {\n";
            if (block.type == "collision_plane") {
                std::string normal;
                std::string distance;
                if (!expression(*block.parameter("normal"), path + ".normal", ExprType::Float3, normal) ||
                    !expression(*block.parameter("distance"), path + ".distance", ExprType::Float,
                                distance)) {
                    return false;
                }
                m_code += "        const vec3 collisionNormalRaw = " + normal + ";\n";
                m_code += "        const float collisionNormalLength = length(collisionNormalRaw);\n";
                m_code += "        if (collisionNormalLength > 1e-6)\n        {\n";
                m_code += "            const vec3 collisionNormal = collisionNormalRaw / collisionNormalLength;\n";
                m_code += "            const float collisionDistance = " + distance + ";\n";
                m_code += "            const float collisionDistanceA = dot(a_position, collisionNormal) - "
                          "collisionDistance;\n";
                m_code += "            const float collisionDistanceB = collisionDistanceA + "
                          "dot(a_velocity, collisionNormal) * VFX_DT;\n";
                m_code += "            bool collisionHit = false;\n";
                m_code += "            vec3 collisionHitPos = a_position;\n";
                m_code += "            if (collisionDistanceA < 0.0 || (collisionDistanceA <= 1e-5 && "
                          "dot(a_velocity, collisionNormal) < 0.0))\n            {\n";
                m_code += "                collisionHit = true;\n";
                m_code += "                collisionHitPos = a_position - collisionNormal * collisionDistanceA;\n";
                m_code += "            }\n";
                m_code += "            else if (collisionDistanceA > 0.0 && collisionDistanceB < 0.0)\n            {\n";
                m_code += "                const float collisionT = clamp(collisionDistanceA / "
                          "(collisionDistanceA - collisionDistanceB), 0.0, 1.0);\n";
                m_code += "                collisionHit = true;\n";
                m_code += "                collisionHitPos = a_position + a_velocity * (collisionT * VFX_DT);\n";
                m_code += "            }\n";
            } else {
                std::string center;
                std::string radius;
                if (!expression(*block.parameter("center"), path + ".center", ExprType::Float3, center) ||
                    !expression(*block.parameter("radius"), path + ".radius", ExprType::Float, radius)) {
                    return false;
                }
                m_code += "        const vec3 collisionCenter = " + center + ";\n";
                m_code += "        const float collisionRadius = max(" + radius + ", 0.0);\n";
                m_code += "        if (collisionRadius > 0.0)\n        {\n";
                m_code += "            const vec3 collisionDelta = a_position - collisionCenter;\n";
                m_code += "            const float collisionDistanceSquared = dot(collisionDelta, collisionDelta);\n";
                m_code += "            const float collisionRadiusSquared = collisionRadius * collisionRadius;\n";
                m_code += "            bool collisionHit = false;\n";
                m_code += "            vec3 collisionHitPos = a_position;\n";
                m_code += "            vec3 collisionNormal = vec3(0.0, 0.0, 0.0);\n";
                m_code += "            if (collisionDistanceSquared < collisionRadiusSquared)\n            {\n";
                m_code += "                collisionHit = true;\n";
                m_code += "                const float collisionDistance = sqrt(collisionDistanceSquared);\n";
                m_code += "                if (collisionDistance > 1e-6)\n                {\n";
                m_code += "                    collisionNormal = collisionDelta / collisionDistance;\n";
                m_code += "                }\n";
                m_code += "                else\n                {\n";
                m_code += "                    collisionNormal = length(a_velocity) > 1e-6 ? "
                          "normalize(vec3(0.0, 0.0, 0.0) - a_velocity) : vec3(0.0, 1.0, 0.0);\n";
                m_code += "                }\n";
                m_code += "                collisionHitPos = collisionCenter + collisionNormal * collisionRadius;\n";
                m_code += "            }\n";
                m_code += "            else\n            {\n";
                m_code += "                const float collisionA = dot(a_velocity, a_velocity);\n";
                m_code += "                if (collisionA > 1e-12)\n                {\n";
                m_code += "                    const float collisionB = dot(collisionDelta, a_velocity);\n";
                m_code += "                    const float collisionC = collisionDistanceSquared - "
                          "collisionRadiusSquared;\n";
                m_code += "                    const float collisionDiscriminant = collisionB * collisionB - "
                          "collisionA * collisionC;\n";
                m_code += "                    if (collisionDiscriminant >= 0.0)\n                    {\n";
                m_code += "                        const float collisionT = (-collisionB - "
                          "sqrt(collisionDiscriminant)) / collisionA;\n";
                m_code += "                        if (collisionT >= 0.0 && collisionT <= VFX_DT)\n                        {\n";
                m_code += "                            collisionHit = true;\n";
                m_code += "                            collisionHitPos = a_position + a_velocity * "
                          "collisionT;\n";
                m_code += "                            collisionNormal = normalize(collisionHitPos - "
                          "collisionCenter);\n";
                m_code += "                        }\n";
                m_code += "                    }\n";
                m_code += "                }\n";
                m_code += "            }\n";
            }
            m_code += makeCollisionResponseCode(mode, restitution, friction);
            m_code += "        }\n";
            m_code += "    }\n";
            return true;
        }
        if (block.type == "color_over_life" || block.type == "alpha_over_life" ||
            block.type == "size_over_life") {
            const bool gradient = block.type == "color_over_life";
            const char* field = gradient ? "gradient" : "curve";
            const Value* value = block.parameter(field);
            if (value == nullptr || value->kind != Value::Kind::String) {
                return fail(path + "." + field, "must name a " + std::string(gradient ? "gradient" : "curve") +
                                                    " property");
            }
            const Property* property = m_context.findProperty(value->string);
            if (property == nullptr ||
                property->type != (gradient ? PropertyType::Gradient : PropertyType::Curve)) {
                return fail(path + "." + field, "unknown " + std::string(gradient ? "gradient" : "curve") +
                                                    " property '" + value->string + "'");
            }
            const std::string function =
                (gradient ? "vfxSampleGradient_" : "vfxSampleCurve_") + property->name + "(VFX_LIFETIME_RATIO)";
            if (gradient) {
                m_code += "    a_color = " + function + ".rgb;\n";
            } else if (block.type == "alpha_over_life") {
                m_code += "    a_alpha = " + function + ";\n";
            } else {
                m_code += "    a_size = " + function + ";\n";
            }
            return true;
        }
        return fail(path + ".type", "unsupported update block '" + block.type +
                                    "' in V1.1.3; supported blocks are integrate/age/reap/gravity/drag/force/"
                                    "attractor/turbulence/collision/over-life");
    }

  private:
    bool fail(const std::string& path, const std::string& message) {
        m_error = path + ": " + message;
        return false;
    }

    bool compileCustomHlsl(const Block& block, const std::string& path) {
        CustomHlslBlock program;
        CustomHlslError error;
        if (!readCustomHlslBlock(block, m_graph, path, program, error)) {
            m_error = formatCustomHlslError(error);
            return false;
        }
        m_code += "    {\n";
        m_code += indentLines(program.code, "        ");
        m_code += "    }\n";
        return true;
    }

    bool expression(const Value& value, const std::string& path, ExprType expected, std::string& out,
                    bool allowFloat4ToFloat3 = false) {
        CompiledExpression compiled = compileExpression(value, m_context, path, expected);
        if (compiled.ok()) {
            out = compiled.root->hlsl;
            return true;
        }
        if (allowFloat4ToFloat3) {
            CompiledExpression wide = compileExpression(value, m_context, path, ExprType::Float4);
            if (wide.ok()) {
                out = "(" + wide.root->hlsl + ").rgb";
                return true;
            }
        }
        if (!compiled.errors.empty()) {
            m_error = compiled.errors.front().path + ": " + compiled.errors.front().message;
        } else {
            m_error = path + ": failed to compile expression";
        }
        return false;
    }

    bool optionalExpression(const Block& block, std::string_view name, ExprType type,
                            const std::string& path, std::string& out, const char* fallback) {
        out = fallback;
        const Value* value = block.parameter(name);
        if (value == nullptr) {
            return true;
        }
        return expression(*value, path + "." + std::string(name), type, out);
    }

    bool relativeMode(const Block& block, bool defaultRelative, const std::string& path,
                      bool& out) {
        out = defaultRelative;
        const Value* value = block.parameter("mode");
        if (value == nullptr) {
            return true;
        }
        if (value->kind != Value::Kind::String ||
            (value->string != "absolute" && value->string != "relative")) {
            return fail(path + ".mode", "expected 'absolute' or 'relative'");
        }
        out = value->string == "relative";
        return true;
    }

    const AttributeLayoutEntry* targetAttribute(const Block& block, const std::string& path) {
        const Value* name = block.parameter("attribute");
        if (name == nullptr || name->kind != Value::Kind::String) {
            fail(path + ".attribute", "must name an attribute");
            return nullptr;
        }
        const AttributeLayoutEntry* entry = m_system.layout.find(name->string);
        if (entry == nullptr) {
            fail(path + ".attribute", "unknown attribute '" + name->string + "'");
        }
        return entry;
    }

    const Graph& m_graph;
    const CompiledSystem& m_system;
    ExpressionContext m_context;
    const std::string& m_systemPath;
    const CompiledEventLink* m_eventLink = nullptr;
    std::string& m_code;
    std::string m_error;
    bool m_hasTrigger = false;
    std::string m_triggerCount;
    std::string m_triggerCondition;

  public:
    bool hasTrigger() const noexcept { return m_hasTrigger; }
    const std::string& triggerCount() const noexcept { return m_triggerCount; }
    const std::string& triggerCondition() const noexcept { return m_triggerCondition; }
};

std::string makeAttributePreamble(const CompiledSystem& system, bool write, std::uint8_t stage) {
    std::string code;
    code += "#define VFX_ATTRIBUTE_BUFFER_STAGE " + std::to_string(stage) + "\n";
    if (write) {
        code += "#define VFX_ATTRIBUTE_BUFFER_WRITE 1\n";
    }
    code += generateAttributeShader(system.layout);
    return code;
}

std::string makeParameterLocals(const CompiledSystem& system, bool initializeDefaults) {
    std::string code;
    for (const AttributeLayoutEntry& entry : system.layout.attributes) {
        code += "    " + std::string(hlslType(entry.attribute.type)) + " " + attributeLocal(entry) + " = ";
        if (initializeDefaults) {
            code += formatAttributeLiteral(entry.attribute);
        } else {
            code += attributeLoad(entry);
        }
        code += ";\n";
    }
    return code;
}

std::string makeAttributeStores(const CompiledSystem& system) {
    std::string code;
    for (const AttributeLayoutEntry& entry : system.layout.attributes) {
        code += "    " + attributeStore(entry) + "\n";
    }
    return code;
}

std::string makeComputePreamble(const CompiledSystem& system, const Graph& graph, bool writeAttributes,
                                std::uint8_t attributeStage) {
    std::string code;
    code += "#include \"bgfx_compute.sh\"\n\n";
    code += makeAttributePreamble(system, writeAttributes, attributeStage);
    code += "\n";
    code += makeExpressionHelpers(graph);
    code += "\n";
    return code;
}

ShaderSource makeSpawnerSource(const CompiledSystem& system, std::uint32_t program) {
    ShaderSource source;
    source.stage = ShaderStage::Compute;
    source.name = "spawner";
    source.bindings = {
        {BindingKind::Buffer, 0, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::SpawnCount)},
        {BindingKind::Buffer, 1, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::RenderCount)},
        {BindingKind::Buffer, 2, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::RenderKVP)},
    };
    source.source = R"(#include "bgfx_compute.sh"

BUFFER_RAW_RW(s_vfxSpawnCount, 0);
BUFFER_RW(s_vfxRenderCount, uint, 1);
BUFFER_RAW_RW(s_vfxRenderKVP, 2);
uniform vec4 u_vfx_params;

NUM_THREADS(1, 1, 1)

void main()
{
    if (uint(gl_GlobalInvocationID.x) == 0u)
    {
        rawStoreUint(s_vfxSpawnCount, 0u, uint(u_vfx_params.z + 0.5));
        s_vfxRenderCount[0] = 0u;
        rawStoreUint(s_vfxRenderKVP, 1u, 0u);
    }
}
)";
    (void)system;
    (void)program;
    return source;
}

ShaderSource makeDeadListSource(const CompiledSystem& system) {
    ShaderSource source;
    source.stage = ShaderStage::Compute;
    source.name = "dead_list_init";
    source.bindings = {
        {BindingKind::Buffer, 0, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::DeadList)},
        {BindingKind::Buffer, 1, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::Attributes)},
        {BindingKind::Buffer, 2, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::Indirection)},
    };
    source.source = "#include \"bgfx_compute.sh\"\n\n";
    source.source += "BUFFER_RW(s_vfxDeadList, uint, 0);\n";
    source.source += "BUFFER_RAW_RW(s_vfxAttributes, 1);\n";
    source.source += "BUFFER_RAW_RW(s_vfxIndirection, 2);\n";
    source.source += "NUM_THREADS(64, 1, 1)\n\n";
    source.source += "void main()\n{\n";
    source.source += "    const uint index = uint(gl_GlobalInvocationID.x);\n";
    source.source += "    const uint capacity = " + std::to_string(system.layout.capacity) + "u;\n";
    source.source += "    if (index < capacity)\n    {\n";
    source.source += "        s_vfxDeadList[2u + index] = capacity - index - 1u;\n";
    source.source += "        const uint base = index * " + std::to_string(system.layout.strideWords) + "u;\n";
    source.source += "        for (uint word = 0u; word < " + std::to_string(system.layout.strideWords) + "u; ++word)\n";
    source.source += "        {\n";
    source.source += "            rawStoreUint(s_vfxAttributes, base + word, 0u);\n";
    source.source += "        }\n";
    source.source += "        rawStoreUint(s_vfxIndirection, index, 0u);\n";
    source.source += "    }\n";
    source.source += "    if (index == 0u)\n    {\n";
    source.source += "        s_vfxDeadList[0] = capacity;\n";
    source.source += "        s_vfxDeadList[1] = capacity;\n";
    source.source += "    }\n";
    source.source += "}\n";
    return source;
}

ShaderSource makeBoundsInitSource(const CompiledSystem& system) {
    ShaderSource source;
    source.stage = ShaderStage::Compute;
    source.name = "bounds_init";
    source.bindings = {
        {BindingKind::Buffer, 0, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::Bounds)},
    };
    source.source = "#include \"bgfx_compute.sh\"\n\n";
    source.source += "BUFFER_RW(s_vfxBounds, uint, 0);\n\n";
    source.source += "NUM_THREADS(1, 1, 1)\n\n";
    source.source += "void main()\n{\n";
    source.source += "    s_vfxBounds[0] = 0xffffffffu;\n";
    source.source += "    s_vfxBounds[1] = 0xffffffffu;\n";
    source.source += "    s_vfxBounds[2] = 0xffffffffu;\n";
    source.source += "    s_vfxBounds[3] = 0u;\n";
    source.source += "    s_vfxBounds[4] = 0u;\n";
    source.source += "    s_vfxBounds[5] = 0u;\n";
    source.source += "}\n";
    (void)system;
    return source;
}

ShaderSource makeBoundsSource(const CompiledSystem& system) {
    ShaderSource source;
    source.stage = ShaderStage::Compute;
    source.name = "bounds";
    source.bindings = {
        {BindingKind::Buffer, 0, Access::Read, static_cast<std::uint32_t>(BufferKind::Attributes)},
        {BindingKind::Buffer, 1, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::Bounds)},
    };
    const AttributeLayoutEntry* position = system.layout.find("position");
    const AttributeLayoutEntry* size = system.layout.find("size");
    const AttributeLayoutEntry* scale = system.layout.find("scale");
    const AttributeLayoutEntry* angle = system.layout.find("angle");
    const AttributeLayoutEntry* alive = system.layout.find("alive");
    const std::uint32_t positionOffset = position != nullptr ? position->offsetWords : 0u;
    const std::uint32_t sizeOffset = size != nullptr ? size->offsetWords : 0u;
    const std::uint32_t scaleOffset = scale != nullptr ? scale->offsetWords : 0u;
    const std::uint32_t angleOffset = angle != nullptr ? angle->offsetWords : 0u;
    const std::uint32_t aliveOffset = alive != nullptr ? alive->offsetWords : 0u;
    source.source = "#include \"bgfx_compute.sh\"\n\n";
    source.source += "BUFFER_RAW_RO(s_vfxAttributes, 0);\n";
    source.source += "BUFFER_RW(s_vfxBounds, uint, 1);\n\n";
    source.source += "uniform vec4 u_vfxBoundsParams;\n";
    source.source += "uniform vec4 u_vfxBoundsRight;\n";
    source.source += "uniform vec4 u_vfxBoundsUp;\n\n";
    source.source += "uint vfxFloatFlip(uint value)\n{\n";
    source.source += "    const uint mask = uint(-int(value >> 31)) | 0x80000000u;\n";
    source.source += "    return value ^ mask;\n";
    source.source += "}\n\n";
    source.source += "NUM_THREADS(64, 1, 1)\n\n";
    source.source += "void main()\n{\n";
    source.source += "    const uint index = uint(gl_GlobalInvocationID.x);\n";
    source.source += "    if (index >= " + std::to_string(system.layout.capacity) + "u)\n    {\n        return;\n    }\n";
    source.source += "    const uint base = index * " + std::to_string(system.layout.strideWords) + "u;\n";
    if (alive != nullptr) {
        source.source += "    if (rawLoadUint(s_vfxAttributes, base + " + std::to_string(aliveOffset) +
                          "u) == 0u)\n    {\n        return;\n    }\n";
    }
    source.source += "    const vec3 position = vec3(rawLoadFloat(s_vfxAttributes, base + " +
                      std::to_string(positionOffset) + "u),\n";
    source.source += "        rawLoadFloat(s_vfxAttributes, base + " + std::to_string(positionOffset + 1u) +
                      "u),\n";
    source.source += "        rawLoadFloat(s_vfxAttributes, base + " + std::to_string(positionOffset + 2u) +
                      "u));\n";
    source.source += "    const float size = rawLoadFloat(s_vfxAttributes, base + " +
                      std::to_string(sizeOffset) + "u);\n";
    source.source += "    const vec3 scale = vec3(rawLoadFloat(s_vfxAttributes, base + " +
                      std::to_string(scaleOffset) + "u),\n";
    source.source += "        rawLoadFloat(s_vfxAttributes, base + " + std::to_string(scaleOffset + 1u) +
                      "u),\n";
    source.source += "        rawLoadFloat(s_vfxAttributes, base + " + std::to_string(scaleOffset + 2u) +
                      "u));\n";
    if (angle != nullptr) {
        source.source += "    const float angle = rawLoadFloat(s_vfxAttributes, base + " +
                          std::to_string(angleOffset) + "u);\n";
    } else {
        source.source += "    const float angle = 0.0;\n";
    }
    source.source += "    const float radius = 0.5 * abs(size) * length(scale.xy);\n";
    source.source += "    if (!(radius >= 0.0))\n    {\n        return;\n    }\n";
    source.source += "    vec3 extents = vec3(radius, radius, radius);\n";
    source.source += "    if (u_vfxBoundsParams.x > 0.5)\n    {\n";
    source.source += "        const float cosine = cos(angle);\n";
    source.source += "        const float sine = sin(angle);\n";
    source.source += "        const vec3 billboardRight = u_vfxBoundsRight.xyz * cosine +\n";
    source.source += "            u_vfxBoundsUp.xyz * sine;\n";
    source.source += "        const vec3 billboardUp = -u_vfxBoundsRight.xyz * sine +\n";
    source.source += "            u_vfxBoundsUp.xyz * cosine;\n";
    source.source += "        const float halfSize = 0.5 * abs(size);\n";
    source.source += "        extents = halfSize * (abs(billboardRight) * abs(scale.x) +\n";
    source.source += "            abs(billboardUp) * abs(scale.y));\n";
    source.source += "    }\n";
    source.source += "    const vec3 boundsMin = position - extents;\n";
    source.source += "    const vec3 boundsMax = position + extents;\n";
    source.source += "    atomicMin(s_vfxBounds[0], vfxFloatFlip(asuint(boundsMin.x)));\n";
    source.source += "    atomicMin(s_vfxBounds[1], vfxFloatFlip(asuint(boundsMin.y)));\n";
    source.source += "    atomicMin(s_vfxBounds[2], vfxFloatFlip(asuint(boundsMin.z)));\n";
    source.source += "    atomicMax(s_vfxBounds[3], vfxFloatFlip(asuint(boundsMax.x)));\n";
    source.source += "    atomicMax(s_vfxBounds[4], vfxFloatFlip(asuint(boundsMax.y)));\n";
    source.source += "    atomicMax(s_vfxBounds[5], vfxFloatFlip(asuint(boundsMax.z)));\n";
    source.source += "}\n";
    return source;
}

std::string eventPayloadExpression(ExprType type, const std::string& value) {
    switch (type) {
    case ExprType::Float:
        return "float4(" + value + ", 0.0, 0.0, 0.0)";
    case ExprType::Float2:
        return "float4(" + value + ", 0.0, 0.0)";
    case ExprType::Float3:
        return "float4(" + value + ", 0.0)";
    case ExprType::Float4:
        return value;
    case ExprType::UInt:
    case ExprType::Int:
        return "float4(float(" + value + "), 0.0, 0.0, 0.0)";
    case ExprType::Bool:
        return "float4((" + value + ") ? 1.0 : 0.0, 0.0, 0.0, 0.0)";
    case ExprType::Invalid:
        break;
    }
    return "float4(0.0, 0.0, 0.0, 0.0)";
}

std::string eventStorePayload(const CompiledEventLink& link, std::uint32_t payloadIndex,
                              const std::string& value) {
    std::string code;
    code += "                {\n";
    code += "                    const float4 vfxEventPayload = " + value + ";\n";
    code += "                    const uint vfxEventWord0 = asuint(vfxEventPayload.x);\n";
    code += "                    const uint vfxEventWord1 = asuint(vfxEventPayload.y);\n";
    code += "                    const uint vfxEventWord2 = asuint(vfxEventPayload.z);\n";
    code += "                    const uint vfxEventWord3 = asuint(vfxEventPayload.w);\n";
    code += "                    const uint vfxEventPayloadOffset = " +
            std::to_string(link.payloadOffsetWords) + "u + vfxEventSlot * 16u + " +
            std::to_string(payloadIndex) + "u * 4u;\n";
    code += "                    s_vfxEvents[vfxEventPayloadOffset] = vfxEventWord0;\n";
    code += "                    s_vfxEvents[vfxEventPayloadOffset + 1u] = vfxEventWord1;\n";
    code += "                    s_vfxEvents[vfxEventPayloadOffset + 2u] = vfxEventWord2;\n";
    code += "                    s_vfxEvents[vfxEventPayloadOffset + 3u] = vfxEventWord3;\n";
    code += "                }\n";
    return code;
}

std::string makeEventPayloadLoad(const std::string& name, const std::string& base) {
    std::string code;
    code += "    const float4 " + name + " = float4(asfloat(s_vfxEvents[" + base +
            "]), asfloat(s_vfxEvents[" + base + " + 1u]), asfloat(s_vfxEvents[" + base +
            " + 2u]), asfloat(s_vfxEvents[" + base + " + 3u]));\n";
    return code;
}

std::string eventPayloadAssignment(const AttributeLayoutEntry& entry, std::uint32_t receiveIndex) {
    const std::string value = "vfxEventValue" + std::to_string(receiveIndex);
    switch (entry.attribute.type) {
    case AttributeType::Float:
        return "    " + attributeLocal(entry) + " = " + value + ".x;\n";
    case AttributeType::Float2:
        return "    " + attributeLocal(entry) + " = " + value + ".xy;\n";
    case AttributeType::Float3:
        return "    " + attributeLocal(entry) + " = " + value + ".xyz;\n";
    case AttributeType::Float4:
        return "    " + attributeLocal(entry) + " = " + value + ";\n";
    case AttributeType::UInt:
        return "    " + attributeLocal(entry) + " = uint(max(" + value + ".x, 0.0));\n";
    case AttributeType::Int:
        return "    " + attributeLocal(entry) + " = int(" + value + ".x);\n";
    case AttributeType::Bool:
        return "    " + attributeLocal(entry) + " = " + value + ".x != 0.0;\n";
    }
    return {};
}

ShaderSource makeEventCopySource(const CompiledAsset& asset) {
    ShaderSource source;
    source.stage = ShaderStage::Compute;
    source.name = "event_cpu_upload";
    source.bindings = {
        {BindingKind::CpuEvent, 0, Access::Read, 0},
        {BindingKind::Event, 1, Access::ReadWrite, 0},
    };
    std::string code = "#include \"bgfx_compute.sh\"\n\n";
    code += "BUFFER_RO(s_vfxCpuEvents, uint, 0);\n";
    code += "BUFFER_RW(s_vfxEvents, uint, 1);\n";
    code += "NUM_THREADS(64, 1, 1)\n\n";
    code += "void main()\n{\n";
    code += "    const uint index = uint(gl_GlobalInvocationID.x);\n";
    std::uint64_t totalWords = 0u;
    for (const CompiledEventLink& link : asset.eventLinks) {
        if (link.cpu) {
            totalWords += 5u + link.capacity + link.capacity * 16u;
        }
    }
    code += "    if (index < " + std::to_string(totalWords) + "u)\n    {\n";
    std::uint64_t rangeStart = 0u;
    bool first = true;
    for (const CompiledEventLink& link : asset.eventLinks) {
        if (!link.cpu) {
            continue;
        }
        const std::uint64_t words = 5u + link.capacity + link.capacity * 16u;
        const std::string rangeEnd = std::to_string(rangeStart + words);
        if (first) {
            code += "        if (index < " + rangeEnd + "u)\n        {\n";
            first = false;
        } else {
            code += "        else if (index < " + rangeEnd + "u)\n        {\n";
        }
        code += "            const uint localIndex = index - " + std::to_string(rangeStart) + "u;\n";
        code += "            const uint targetIndex = " + std::to_string(link.bufferOffsetWords) +
                "u + localIndex;\n";
        code += "            s_vfxEvents[targetIndex] = s_vfxCpuEvents[targetIndex];\n";
        code += "        }\n";
        rangeStart += words;
    }
    code += "    }\n";
    code += "}\n";
    source.source = std::move(code);
    return source;
}

ShaderSource makeEventResetSource(std::uint32_t wordCount) {
    ShaderSource source;
    source.stage = ShaderStage::Compute;
    source.name = "event_reset";
    source.bindings = {
        {BindingKind::Event, 0, Access::ReadWrite, 0},
    };
    std::string code = "#include \"bgfx_compute.sh\"\n\n";
    code += "BUFFER_RW(s_vfxEvents, uint, 0);\n";
    code += "NUM_THREADS(64, 1, 1)\n\n";
    code += "void main()\n{\n";
    code += "    const uint index = uint(gl_GlobalInvocationID.x);\n";
    code += "    if (index < " + std::to_string(wordCount) + "u)\n    {\n";
    code += "        s_vfxEvents[index] = 0u;\n";
    code += "    }\n";
    code += "}\n";
    source.source = std::move(code);
    return source;
}

ShaderSource makeEventPrefixSource(const CompiledAsset& asset) {
    ShaderSource source;
    source.stage = ShaderStage::Compute;
    source.name = "event_prefix_sum";
    source.bindings = {
        {BindingKind::Event, 0, Access::ReadWrite, 0},
    };
    std::string code = "#include \"bgfx_compute.sh\"\n\n";
    code += "BUFFER_RW(s_vfxEvents, uint, 0);\n";
    code += "SHARED float vfxEventPrefix[128];\n\n";
    code += "NUM_THREADS(128, 1, 1)\n\n";
    code += "void main()\n{\n";
    code += "    const uint threadIndex = uint(gl_LocalInvocationID.x);\n";
    code += "    uint count = 0u;\n";
    code += "    uint offset = 0u;\n";

    for (std::size_t i = 0; i < asset.eventLinks.size(); ++i) {
        const CompiledEventLink& link = asset.eventLinks[i];
        code += "    if (threadIndex == " + std::to_string(i) + "u)\n    {\n";
        code += "        offset = " + std::to_string(link.bufferOffsetWords) + "u;\n";

        code += "        count = min(s_vfxEvents[offset], " + std::to_string(link.capacity) + "u);\n";
        code += "    }\n";
    }
    code += "    vfxEventPrefix[threadIndex] = float(count);\n";
    code += "    barrier();\n";
    code += "    for (uint step = 1u; step < 128u; step <<= 1u)\n    {\n";
    code += "        barrier();\n";
    code += "        if ((threadIndex & step) != 0u)\n        {\n";
    code += "            vfxEventPrefix[threadIndex] += vfxEventPrefix[(threadIndex & ~(step - 1u)) - 1u];\n";
    code += "        }\n    }\n";
    code += "    barrier();\n";
    code += "    if (threadIndex < " + std::to_string(asset.eventLinks.size()) + "u)\n    {\n";
    code += "        s_vfxEvents[offset + 4u] = count;\n";
    code += "        s_vfxEvents[offset + 2u] = uint(vfxEventPrefix[threadIndex]) - count;\n";
    code += "        s_vfxEvents[offset] = 0u;\n";
    code += "    }\n";
    code += "}\n";
    source.source = std::move(code);
    return source;
}

ShaderSource makeInitSource(const CompiledSystem& system, const Graph& graph, const std::string& systemPath,
                            std::string& error) {
    ShaderSource source;
    source.stage = ShaderStage::Compute;
    source.name = "init";
    source.bindings = {
        {BindingKind::Buffer, 0, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::Attributes)},
        {BindingKind::Buffer, 1, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::DeadList)},
        {BindingKind::Buffer, 2, Access::Read, static_cast<std::uint32_t>(BufferKind::SpawnCount)},
    };
    const std::vector<KernelBinding> initLutBindings = makeLutBindings(graph);
    source.bindings.insert(source.bindings.end(), initLutBindings.begin(), initLutBindings.end());

    std::string code = makeComputePreamble(system, graph, true, 0);
    code += "BUFFER_RW(s_vfxDeadList, uint, 1);\n";
    code += "BUFFER_RAW_RO(s_vfxSpawnCount, 2);\n\n";
    code += "NUM_THREADS(64, 1, 1)\n\n";
    code += "void main()\n{\n";
    code += "    const uint spawnIndex = uint(gl_GlobalInvocationID.x);\n";
    code += "    const uint spawnCount = rawLoadUint(s_vfxSpawnCount, 0u);\n";
    code += "    if (spawnIndex >= spawnCount)\n    {\n        return;\n    }\n";
    code += "    const uint deadCount = s_vfxDeadList[1];\n";
    code += "    if (spawnIndex >= deadCount)\n    {\n        return;\n    }\n";
    code += "    uint previousCount = 0u;\n";
    code += "    atomicFetchAndAdd(s_vfxDeadList[0], 0xffffffffu, previousCount);\n";
    code += "    if (previousCount == 0u)\n    {\n        return;\n    }\n";
    code += "    const uint particleIndex = s_vfxDeadList[2u + previousCount - 1u];\n";
    code += "    if (particleIndex >= " + std::to_string(system.layout.capacity) + "u)\n    {\n        return;\n    }\n";
    code += "    VfxAttributes attributes;\n";
    code += "    attributes.Init(particleIndex);\n";
    code += makeParameterLocals(system, true);
    code += "    a_seed = vfxWangHash((" + std::string("uint(u_vfx_ids.x) + spawnIndex") + ") ^ uint(u_vfx_params.w));\n";
    code += "    a_particleId = uint(u_vfx_ids.x) + spawnIndex;\n";
    code += "    a_spawnIndex = spawnIndex;\n";
    code += "    a_age = 0.0;\n";
    code += "    a_alive = true;\n";

    BlockCompiler compiler(graph, system, systemPath, nullptr, code);
    for (std::size_t i = 0; i < system.desc->initBlocks.size(); ++i) {
        const Block& block = system.desc->initBlocks[i];
        if (!block.enabled) {
            continue;
        }
        if (!compiler.compileInitBlock(block, i)) {
            error = compiler.error();
            source.source.clear();
            source.name = "error:init:" + std::to_string(i);
            return source;
        }
    }
    code += "    a_oldPosition = a_position;\n";
    code += makeAttributeStores(system);
    code += "}\n";
    source.source = std::move(code);
    return source;
}

ShaderSource makeEventInitSource(const CompiledSystem& system, const Graph& graph,
                                 const std::string& systemPath, const CompiledEventLink& eventLink,
                                 std::string& error) {
    ShaderSource source;
    source.stage = ShaderStage::Compute;
    source.name = "event_init";
    source.bindings = {
        {BindingKind::Buffer, 0, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::Attributes)},
        {BindingKind::Buffer, 1, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::DeadList)},
        {BindingKind::Event, 2, Access::Read, 0},
    };
    const std::vector<KernelBinding> initLutBindings = makeLutBindings(graph);
    source.bindings.insert(source.bindings.end(), initLutBindings.begin(), initLutBindings.end());

    std::string code = makeComputePreamble(system, graph, true, 0);
    code += "BUFFER_RW(s_vfxDeadList, uint, 1);\n";
    code += "BUFFER_RO(s_vfxEvents, uint, 2);\n\n";
    code += "NUM_THREADS(64, 1, 1)\n\n";
    code += "void main()\n{\n";
    code += "    const uint spawnIndex = uint(gl_GlobalInvocationID.x);\n";
    code += "    const uint eventCount = min(s_vfxEvents[" +
            std::to_string(eventLink.bufferOffsetWords) + "u + 4u], " +
            std::to_string(eventLink.capacity) + "u);\n";
    code += "    if (spawnIndex >= eventCount)\n    {\n        return;\n    }\n";
    code += "    const uint deadCount = s_vfxDeadList[1];\n";
    code += "    if (spawnIndex >= deadCount)\n    {\n        return;\n    }\n";
    code += "    uint previousCount = 0u;\n";
    code += "    atomicFetchAndAdd(s_vfxDeadList[0], 0xffffffffu, previousCount);\n";
    code += "    if (previousCount == 0u)\n    {\n        return;\n    }\n";
    code += "    const uint particleIndex = s_vfxDeadList[2u + previousCount - 1u];\n";
    code += "    if (particleIndex >= " + std::to_string(system.layout.capacity) +
            "u)\n    {\n        return;\n    }\n";
    code += "    const uint eventBase = s_vfxEvents[" +
            std::to_string(eventLink.bufferOffsetWords) + "u + 2u];\n";
    code += "    VfxAttributes attributes;\n";
    code += "    attributes.Init(particleIndex);\n";
    code += makeParameterLocals(system, true);
    code += "    a_seed = vfxWangHash((eventBase + spawnIndex) ^ uint(u_vfx_params.w));\n";
    code += "    a_particleId = eventBase + spawnIndex;\n";
    code += "    a_spawnIndex = spawnIndex;\n";
    code += "    a_age = 0.0;\n";
    code += "    a_alive = true;\n";

    BlockCompiler compiler(graph, system, systemPath, &eventLink, code);
    for (std::size_t i = 0; i < system.desc->initBlocks.size(); ++i) {
        const Block& block = system.desc->initBlocks[i];
        if (!block.enabled) {
            continue;
        }
        if (!compiler.compileInitBlock(block, i)) {
            error = compiler.error();
            source.source.clear();
            source.name = "error:event_init:" + std::to_string(i);
            return source;
        }
    }
    for (std::size_t i = 0; i < eventLink.receives.size(); ++i) {
        const EventReceiveMapping& receive = eventLink.receives[i];
        const AttributeLayoutEntry* entry = system.layout.find(receive.attribute);
        if (entry == nullptr) {
            error = systemPath + ".events.receive[0].attributes." + receive.attribute +
                    ": unknown consumer attribute";
            source.source.clear();
            source.name = "error:event_init:receive";
            return source;
        }
        const std::string valueName = "vfxEventValue" + std::to_string(i);
        const std::string base = std::to_string(eventLink.payloadOffsetWords) +
                                 "u + spawnIndex * 16u + " + std::to_string(receive.slot) + "u * 4u";
        code += makeEventPayloadLoad(valueName, base);
        code += eventPayloadAssignment(*entry, static_cast<std::uint32_t>(i));
    }
    code += "    a_oldPosition = a_position;\n";
    code += makeAttributeStores(system);
    code += "}\n";
    source.source = std::move(code);
    return source;
}

ShaderSource makeUpdateSource(const CompiledSystem& system, const Graph& graph, const std::string& systemPath,
                              const CompiledEventLink* eventLink, std::string& error) {
    ShaderSource source;
    source.stage = ShaderStage::Compute;
    source.name = "update";
    source.bindings = {
        {BindingKind::Buffer, 0, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::Attributes)},
        {BindingKind::Buffer, 1, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::DeadList)},
    };
    const std::vector<KernelBinding> updateLutBindings = makeLutBindings(graph);
    source.bindings.insert(source.bindings.end(), updateLutBindings.begin(), updateLutBindings.end());
    if (eventLink != nullptr) {
        source.bindings.push_back({BindingKind::Event, 2, Access::ReadWrite, 0});
    }

    std::string code = makeComputePreamble(system, graph, true, 0);
    code += "BUFFER_RW(s_vfxDeadList, uint, 1);\n";
    if (eventLink != nullptr) {
        code += "BUFFER_RW(s_vfxEvents, uint, 2);\n";
    }
    code += "\n";
    code += "NUM_THREADS(64, 1, 1)\n\n";
    code += "void main()\n{\n";
    code += "    const uint particleIndex = uint(gl_GlobalInvocationID.x);\n";
    code += "    if (particleIndex >= " + std::to_string(system.layout.capacity) + "u)\n    {\n        return;\n    }\n";
    code += "    VfxAttributes attributes;\n";
    code += "    attributes.Init(particleIndex);\n";
    code += makeParameterLocals(system, false);
    code += "    if (!a_alive)\n    {\n        return;\n    }\n";
    code += "    a_oldPosition = a_position;\n";
    code += "    const bool vfxWasAlive = a_alive;\n";

    BlockCompiler compiler(graph, system, systemPath, eventLink, code);
    for (std::size_t i = 0; i < system.desc->updateBlocks.size(); ++i) {
        const Block& block = system.desc->updateBlocks[i];
        if (!block.enabled) {
            continue;
        }
        if (!compiler.compileUpdateBlock(block, i)) {
            error = compiler.error();
            source.source.clear();
            source.name = "error:update:" + std::to_string(i);
            return source;
        }
    }
    code += makeAttributeStores(system);
    if (eventLink != nullptr) {
        code += "    uint vfxEventCount = 0u;\n";
        if (compiler.hasTrigger()) {
            if (compiler.triggerCondition().empty()) {
                code += "    if (vfxWasAlive && !a_alive)\n    {\n";
            } else {
                code += "    if (" + compiler.triggerCondition() + ")\n    {\n";
            }
            code += "        vfxEventCount = uint(max(" + compiler.triggerCount() + ", 0.0));\n";
            code += "    }\n";
        } else {
            code += "    if (vfxWasAlive && !a_alive)\n    {\n        vfxEventCount = 1u;\n    }\n";
        }
        code += "    vfxEventCount = min(vfxEventCount, " + std::to_string(eventLink->capacity) + "u);\n";
        code += "    vfxEventCount = min(vfxEventCount, min(" + std::to_string(eventLink->capacity) +
                "u, s_vfxEvents[" + std::to_string(eventLink->bufferOffsetWords) + "u + 3u] + " +
                std::to_string(eventLink->capacity) + "u));\n";
        code += "    for (uint vfxEvent = 0u; vfxEvent < vfxEventCount; ++vfxEvent)\n    {\n";
        code += "        uint vfxEventSlot = 0u;\n";
        code += "        atomicFetchAndAdd(s_vfxEvents[" + std::to_string(eventLink->bufferOffsetWords) +
                "u], 1u, vfxEventSlot);\n";
        code += "        if (vfxEventSlot < " + std::to_string(eventLink->capacity) + "u)\n        {\n";
        code += "            s_vfxEvents[" + std::to_string(eventLink->bufferOffsetWords) +
                "u + 5u + vfxEventSlot] = particleIndex;\n";
        code += "            uint vfxEventIgnored = 0u;\n";
        code += "            atomicFetchAndAdd(s_vfxEvents[" + std::to_string(eventLink->bufferOffsetWords) +
                "u + 1u], 1u, vfxEventIgnored);\n";
        const auto& emitAttributes = system.desc->events.emit[0].attributes;
        const ExpressionContext context(graph);
        for (std::size_t payloadIndex = 0; payloadIndex < eventLink->payload.size(); ++payloadIndex) {
            const EventPayloadAttribute& payload = eventLink->payload[payloadIndex];
            const Value* value = emitAttributes.find(payload.name);
            std::string expressionText = "0.0";
            if (value != nullptr) {
                const CompiledExpression compiled = compileExpression(
                    *value, context, systemPath + ".events.emit[0].attributes." + payload.name,
                    ExprType::Invalid);
                if (compiled.ok()) {
                    expressionText = compiled.root->hlsl;
                }
            }
            code += eventStorePayload(*eventLink, payload.slot,
                                      eventPayloadExpression(payload.type, expressionText));
        }
        code += "        }\n";
        code += "        else\n        {\n";
        code += "            uint vfxEventDropped = 0u;\n";
        code += "            atomicFetchAndAdd(s_vfxEvents[" + std::to_string(eventLink->bufferOffsetWords) +
                "u + 3u], 1u, vfxEventDropped);\n";
        code += "        }\n";
        code += "    }\n";
    }
    code += "    if (!a_alive)\n    {\n";
    code += "        uint previousCount = 0u;\n";
    code += "        atomicFetchAndAdd(s_vfxDeadList[0], 1u, previousCount);\n";
    code += "        if (previousCount < " + std::to_string(system.layout.capacity) + "u)\n        {\n";
    code += "            s_vfxDeadList[2u + previousCount] = particleIndex;\n";
    code += "        }\n";
    code += "    }\n";
    code += "}\n";
    source.source = std::move(code);
    return source;
}

ShaderSource makeSortResetSource(const CompiledSystem& system) {
    ShaderSource source;
    source.stage = ShaderStage::Compute;
    source.name = "sort_reset";
    source.bindings = {
        {BindingKind::Buffer, 0, Access::ReadWrite,
         static_cast<std::uint32_t>(BufferKind::RenderCount)},
    };
    source.source = R"(#include "bgfx_compute.sh"

BUFFER_RW(s_vfxRenderCount, uint, 0);

NUM_THREADS(1, 1, 1)

void main()
{
    if (uint(gl_GlobalInvocationID.x) == 0u)
    {
        s_vfxRenderCount[0] = 0u;
    }
}
)";
    (void)system;
    return source;
}

ShaderSource makeOutputUpdateSource(const CompiledSystem& system, const Graph& graph,
                                    SortMode sortMode, bool useSortUniforms,
                                    const AttributeLayoutEntry* customSortKey) {
    ShaderSource source;
    source.stage = ShaderStage::Compute;
    source.name = "output_update";
    source.bindings = {
        {BindingKind::Buffer, 0, Access::Read, static_cast<std::uint32_t>(BufferKind::Attributes)},
        {BindingKind::Buffer, 1, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::RenderKVP)},
        {BindingKind::Buffer, 2, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::Indirection)},
        {BindingKind::Buffer, 3, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::RenderCount)},
    };
    const std::string worldUniform = useSortUniforms ? "u_vfxSortWorld" : "u_vfx_world";
    const std::string cameraPosUniform =
        useSortUniforms ? "u_vfxSortCameraPos" : "u_global_cameraPos";
    const std::string invViewUniform = useSortUniforms ? "u_vfxSortInvView" : "u_global_invView";
    std::string code = makeComputePreamble(system, graph, false, 0);
    if (sortMode == SortMode::Distance || sortMode == SortMode::CameraDepth) {
        code += "uniform mat4 " + worldUniform + ";\n";
        code += "uniform vec4 " + cameraPosUniform + ";\n";
    }
    if (sortMode == SortMode::CameraDepth) {
        code += "uniform mat4 " + invViewUniform + ";\n";
    }
    if (sortMode == SortMode::Distance || sortMode == SortMode::CameraDepth) {
        code += "\n";
    }
    code += "BUFFER_RAW_RW(s_vfxRenderKVP, 1);\n";
    code += "BUFFER_RAW_RW(s_vfxIndirection, 2);\n";
    code += "BUFFER_RW(s_vfxRenderCount, uint, 3);\n\n";
    code += "NUM_THREADS(64, 1, 1)\n\n";
    code += "void main()\n{\n";
    code += "    const uint particleIndex = uint(gl_GlobalInvocationID.x);\n";
    code += "    if (particleIndex >= " + std::to_string(system.layout.capacity) + "u)\n    {\n        return;\n    }\n";
    code += "    VfxAttributes attributes;\n";
    code += "    attributes.Init(particleIndex);\n";
    code += "    if (!attributes.Load_alive())\n    {\n        return;\n    }\n";
    code += "    float sortKey = 0.0;\n";
    if (sortMode == SortMode::Distance) {
        code += "    const vec3 localPosition = attributes.Load_position();\n";
        code += "    const vec3 worldPosition = mul(" + worldUniform +
                ", vec4(localPosition, 1.0)).xyz;\n";
        code += "    const vec3 cameraToPosition = worldPosition - " + cameraPosUniform + ".xyz;\n";
        code += "    sortKey = dot(cameraToPosition, cameraToPosition);\n";
    } else if (sortMode == SortMode::CameraDepth) {
        code += "    const vec3 localPosition = attributes.Load_position();\n";
        code += "    const vec3 worldPosition = mul(" + worldUniform +
                ", vec4(localPosition, 1.0)).xyz;\n";
        code += "    const vec3 cameraToPosition = worldPosition - " + cameraPosUniform + ".xyz;\n";
        code += "    const vec3 cameraForward = -" + invViewUniform + "[2].xyz;\n";
        code += "    sortKey = dot(cameraToPosition, cameraForward);\n";
    } else if (sortMode == SortMode::YoungestInFront) {
        code += "    sortKey = attributes.Load_age();\n";
    } else if (sortMode == SortMode::Custom && customSortKey != nullptr) {
        code += "    sortKey = float(attributes.Load_" + customSortKey->attribute.name + "());\n";
    }
    code += "    uint previousCount = 0u;\n";
    code += "    atomicFetchAndAdd(s_vfxRenderCount[0], 1u, previousCount);\n";
    code += "    if (previousCount < " + std::to_string(system.layout.capacity) + "u)\n    {\n";
    code += "        const uint kvpOffset = 2u + previousCount * 2u;\n";
    code += "        rawStoreFloat(s_vfxRenderKVP, kvpOffset, sortKey);\n";
    code += "        rawStoreUint(s_vfxRenderKVP, kvpOffset + 1u, particleIndex);\n";
    code += "        rawStoreUint(s_vfxIndirection, previousCount, particleIndex);\n";
    code += "    }\n";
    code += "}\n";
    source.source = std::move(code);
    return source;
}

std::string makeSortComparator(SortOrder order) {
    std::string code;
    code += "struct SortKVP\n{\n";
    code += "    float key;\n";
    code += "    uint value;\n";
    code += "};\n\n";
    code += "bool sortLess(SortKVP a, SortKVP b)\n{\n";
    code += "    if (a.key == b.key)\n    {\n";
    code += "        return a.value < b.value;\n";
    code += "    }\n";
    if (order == SortOrder::Descending) {
        code += "    return a.key > b.key;\n";
    } else {
        code += "    return a.key < b.key;\n";
    }
    code += "}\n\n";
    return code;
}

ShaderSource makeSortBitonicSource(const CompiledSystem& system, bool finalPass,
                                   std::uint32_t chunkSize, std::uint32_t threads) {
    ShaderSource source;
    source.stage = ShaderStage::Compute;
    source.name = finalPass ? "sort_bitonic_final" : "sort_bitonic_prepass";
    source.bindings = {
        {BindingKind::Buffer, 0, Access::Read, static_cast<std::uint32_t>(BufferKind::RenderKVP)},
        {BindingKind::Buffer, 1, finalPass ? Access::Write : Access::ReadWrite,
         static_cast<std::uint32_t>(finalPass ? BufferKind::Indirection : BufferKind::SortScratch)},
        {BindingKind::Buffer, 2, Access::Read, static_cast<std::uint32_t>(BufferKind::RenderCount)},
    };

    std::string code;
    code += "#include \"bgfx_compute.sh\"\n\n";
    if (!finalPass) {
        code += "uniform vec4 u_vfx_ids;\n";
    }
    code += "BUFFER_RAW_RO(s_vfxSortSource, 0);\n";
    if (finalPass) {
        code += "BUFFER_RAW_WO(s_vfxIndirection, 1);\n";
    } else {
        code += "BUFFER_RAW_RW(s_vfxSortScratch, 1);\n";
    }
    code += "BUFFER_RO(s_vfxRenderCount, uint, 2);\n\n";
    code += "SHARED float s_sortKeys[" + std::to_string(chunkSize) + "];\n";
    code += "SHARED uint s_sortValues[" + std::to_string(chunkSize) + "];\n\n";
    code += makeSortComparator(system.desc->sort.order);
    const std::string rejected =
        system.desc->sort.order == SortOrder::Descending ? "0xff7fffffu" : "0x7f7fffffu";
    code += "NUM_THREADS(" + std::to_string(threads) + ", 1, 1)\n\n";
    code += "void main()\n{\n";
    code += "    const uint globalThread = uint(gl_GlobalInvocationID.x);\n";
    code += "    const uint localThread = globalThread & " + std::to_string(threads - 1u) + "u;\n";
    if (finalPass) {
        code += "    const uint chunk = globalThread / " + std::to_string(threads) + "u;\n";
    } else {
        code += "    const uint chunk = uint(u_vfx_ids.z) + globalThread / " +
                std::to_string(threads) + "u;\n";
    }
    code += "    const uint chunkStart = chunk * " + std::to_string(chunkSize) + "u;\n";
    code += "    const uint count = s_vfxRenderCount[0];\n";
    code += "    const float rejectedKey = uintBitsToFloat(" + rejected + ");\n";
    code += "    for (uint loadItem = 0u; loadItem < " + std::to_string(kSortElementsPerThread) + "u; ++loadItem)\n    {\n";
    code += "        const uint loadElement = localThread + loadItem * " + std::to_string(threads) + "u;\n";
    code += "        const uint loadGlobalIndex = chunkStart + loadElement;\n";
    code += "        float key = rejectedKey;\n";
    code += "        uint value = 0xffffffffu;\n";
    code += "        if (loadGlobalIndex < count)\n        {\n";
    code += "            const uint sourceOffset = 2u + loadGlobalIndex * 2u;\n";
    code += "            key = rawLoadFloat(s_vfxSortSource, sourceOffset);\n";
    code += "            value = rawLoadUint(s_vfxSortSource, sourceOffset + 1u);\n";
    code += "        }\n";
    code += "        s_sortKeys[loadElement] = key;\n";
    code += "        s_sortValues[loadElement] = value;\n";
    code += "    }\n";
    code += "    barrier();\n";
    code += "    for (uint size = 2u; size <= " + std::to_string(chunkSize) + "u; size <<= 1u)\n    {\n";
    code += "        for (uint stride = size >> 1u; stride > 0u; stride >>= 1u)\n        {\n";
    code += "            for (uint sortItem = 0u; sortItem < " + std::to_string(kSortElementsPerThread) + "u; ++sortItem)\n            {\n";
    code += "                const uint sortElement = localThread + sortItem * " + std::to_string(threads) + "u;\n";
    code += "                const uint partner = sortElement ^ stride;\n";
    code += "                if (partner > sortElement)\n                {\n";
    code += "                    SortKVP a;\n";
    code += "                    a.key = s_sortKeys[sortElement];\n";
    code += "                    a.value = s_sortValues[sortElement];\n";
    code += "                    SortKVP b;\n";
    code += "                    b.key = s_sortKeys[partner];\n";
    code += "                    b.value = s_sortValues[partner];\n";
    code += "                    const bool ascending = (sortElement & size) == 0u;\n";
    code += "                    const bool swap = ascending ? sortLess(b, a) : sortLess(a, b);\n";
    code += "                    if (swap)\n                    {\n";
    code += "                        s_sortKeys[sortElement] = b.key;\n";
    code += "                        s_sortKeys[partner] = a.key;\n";
    code += "                        s_sortValues[sortElement] = b.value;\n";
    code += "                        s_sortValues[partner] = a.value;\n";
    code += "                    }\n";
    code += "                }\n";
    code += "            }\n";
    code += "            barrier();\n";
    code += "        }\n";
    code += "    }\n";
    code += "    for (uint storeItem = 0u; storeItem < " + std::to_string(kSortElementsPerThread) + "u; ++storeItem)\n    {\n";
    code += "        const uint storeElement = localThread + storeItem * " + std::to_string(threads) + "u;\n";
    code += "        const uint storeGlobalIndex = chunkStart + storeElement;\n";
    code += "        if (storeGlobalIndex < count)\n        {\n";
    if (finalPass) {
        code += "            rawStoreUint(s_vfxIndirection, storeGlobalIndex, s_sortValues[storeElement]);\n";
    } else {
        code += "            const uint destinationOffset = storeGlobalIndex * 2u;\n";
        code += "            rawStoreFloat(s_vfxSortScratch, destinationOffset, s_sortKeys[storeElement]);\n";
        code += "            rawStoreUint(s_vfxSortScratch, destinationOffset + 1u, s_sortValues[storeElement]);\n";
    }
    code += "        }\n";
    code += "    }\n";
    code += "}\n";
    source.source = std::move(code);
    return source;
}

ShaderSource makeSortMergeSource(const CompiledSystem& system, std::uint32_t subArraySize,
                                 BufferKind sourceKind, BufferKind destinationKind, bool finalPass) {
    ShaderSource source;
    source.stage = ShaderStage::Compute;
    source.name = "sort_merge_" + std::to_string(subArraySize) + (finalPass ? "_final" : "");
    source.bindings = {
        {BindingKind::Buffer, 0, Access::Read, static_cast<std::uint32_t>(sourceKind)},
        {BindingKind::Buffer, 1, finalPass ? Access::Write : Access::ReadWrite,
         static_cast<std::uint32_t>(destinationKind)},
        {BindingKind::Buffer, 2, Access::Read, static_cast<std::uint32_t>(BufferKind::RenderCount)},
    };

    const std::uint32_t sourceOffset = sourceKind == BufferKind::RenderKVP ? 2u : 0u;
    const std::uint32_t destinationOffset = destinationKind == BufferKind::RenderKVP ? 2u : 0u;
    std::string code;
    code += "#include \"bgfx_compute.sh\"\n\n";
    code += "BUFFER_RAW_RO(s_vfxSortSource, 0);\n";
    if (finalPass) {
        code += "BUFFER_RAW_WO(s_vfxIndirection, 1);\n";
    } else {
        code += "BUFFER_RAW_RW(s_vfxSortScratch, 1);\n";
    }
    code += "BUFFER_RO(s_vfxRenderCount, uint, 2);\n\n";
    code += makeSortComparator(system.desc->sort.order);
    code += "SortKVP loadSource(uint index)\n{\n";
    code += "    const uint offset = " + std::to_string(sourceOffset) + "u + index * 2u;\n";
    code += "    SortKVP result;\n";
    code += "    result.key = rawLoadFloat(s_vfxSortSource, offset);\n";
    code += "    result.value = rawLoadUint(s_vfxSortSource, offset + 1u);\n";
    code += "    return result;\n";
    code += "}\n\n";
    code += "NUM_THREADS(" + std::to_string(kMergeThreads) + ", 1, 1)\n\n";
    code += "void main()\n{\n";
    code += "    const uint index = uint(gl_GlobalInvocationID.x);\n";
    code += "    const uint count = s_vfxRenderCount[0];\n";
    code += "    if (index >= count)\n    {\n        return;\n    }\n";
    code += "    const uint subSize = " + std::to_string(subArraySize) + "u;\n";
    code += "    const uint arrayStart = (index / (subSize * 2u)) * (subSize * 2u);\n";
    code += "    const uint arrayIndex = index - arrayStart;\n";
    code += "    const uint rightStart = arrayStart + subSize;\n";
    code += "    const uint leftCount = min(subSize, count - arrayStart);\n";
    code += "    const uint rightCount = count > rightStart ? min(subSize, count - rightStart) : 0u;\n";
    code += "    SortKVP result;\n";
    code += "    if (rightCount == 0u)\n    {\n";
    code += "        result = loadSource(arrayStart + arrayIndex);\n";
    code += "    }\n";
    code += "    else\n    {\n";
    code += "        const uint rank = arrayIndex;\n";
    code += "        uint low = rank > rightCount ? rank - rightCount : 0u;\n";
    code += "        uint high = min(rank, leftCount);\n";
    code += "        while (low < high)\n        {\n";
    code += "            const uint middle = (low + high + 1u) >> 1u;\n";
    code += "            const bool partition = (rank - middle >= rightCount) ||\n";
    code += "                !sortLess(loadSource(rightStart + (rank - middle)), loadSource(arrayStart + middle - 1u));\n";
    code += "            if (partition)\n            {\n                low = middle;\n            }\n";
    code += "            else\n            {\n                high = middle - 1u;\n            }\n";
    code += "        }\n";
    code += "        const uint leftIndex = low;\n";
    code += "        const uint rightIndex = rank - low;\n";
    code += "        const bool takeLeft = leftIndex < leftCount &&\n";
    code += "            (rightIndex >= rightCount || !sortLess(loadSource(rightStart + rightIndex),\n";
    code += "                                                   loadSource(arrayStart + leftIndex)));\n";
    code += "        if (takeLeft)\n        {\n";
    code += "            result = loadSource(arrayStart + leftIndex);\n";
    code += "        }\n        else\n        {\n";
    code += "            result = loadSource(rightStart + rightIndex);\n";
    code += "        }\n";
    code += "    }\n";
    if (finalPass) {
        code += "    rawStoreUint(s_vfxIndirection, index, result.value);\n";
    } else {
        code += "    const uint destination = " + std::to_string(destinationOffset) + "u + index * 2u;\n";
        code += "    rawStoreFloat(s_vfxSortScratch, destination, result.key);\n";
        code += "    rawStoreUint(s_vfxSortScratch, destination + 1u, result.value);\n";
    }
    code += "}\n";
    source.source = std::move(code);
    return source;
}

ShaderSource makeFillIndirectSource(const CompiledSystem& system) {
    ShaderSource source;
    source.stage = ShaderStage::Compute;
    source.name = "fill_indirect";
    source.bindings = {
        {BindingKind::Buffer, 0, Access::Write, static_cast<std::uint32_t>(BufferKind::IndirectArgs)},
        {BindingKind::Buffer, 1, Access::Read, static_cast<std::uint32_t>(BufferKind::RenderCount)},
        {BindingKind::Buffer, 2, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::RenderKVP)},
    };
    source.source = "#include \"bgfx_compute.sh\"\n\n";
    source.source += "BUFFER_RAW_WO(s_vfxIndirectArgs, 0);\n";
    source.source += "BUFFER_RO(s_vfxRenderCount, uint, 1);\n";
    source.source += "BUFFER_RAW_RW(s_vfxRenderKVP, 2);\n\n";
    source.source += "uniform vec4 u_vfxInstanceParams;\n\n";
    source.source += "NUM_THREADS(1, 1, 1)\n\n";
    source.source += "void main()\n{\n";
    source.source += "    if (uint(gl_GlobalInvocationID.x) != 0u)\n    {\n        return;\n    }\n";
    source.source += "    const uint particlesPerInstance = " +
                    std::to_string(kParticlesPerDrawInstance) + "u;\n";
    source.source += "    const uint count = s_vfxRenderCount[0];\n";
    source.source += "    const uint particleChunks = (count + particlesPerInstance - 1u) / particlesPerInstance;\n";
    source.source += "    const uint effectInstances = max(uint(u_vfxInstanceParams.x + 0.5), 1u);\n";
    source.source += "    rawStoreUint(s_vfxRenderKVP, 0u, 0u);\n";
    source.source += "    rawStoreUint(s_vfxRenderKVP, 1u, count);\n";
    source.source += "    rawStoreUint(s_vfxIndirectArgs, 0u, min(count, particlesPerInstance) * 6u);\n";
    source.source += "    rawStoreUint(s_vfxIndirectArgs, 1u, particleChunks * effectInstances);\n";
    source.source += "    rawStoreUint(s_vfxIndirectArgs, 2u, 0u);\n";
    source.source += "    rawStoreUint(s_vfxIndirectArgs, 3u, 0u);\n";
    source.source += "    rawStoreUint(s_vfxIndirectArgs, 4u, 0u);\n";
    source.source += "    rawStoreUint(s_vfxIndirectArgs, 5u, 0u);\n";
    source.source += "    rawStoreUint(s_vfxIndirectArgs, 6u, 0u);\n";
    source.source += "    rawStoreUint(s_vfxIndirectArgs, 7u, 0u);\n";
    source.source += "}\n";
    (void)system;
    return source;
}

ShaderSource makeOutputVertexSource(const CompiledSystem& system, const std::string& name,
                                    bool reverseSort) {
    ShaderSource source;
    source.stage = ShaderStage::Vertex;
    source.name = name + ".vs";
    source.varyings = R"(vec2 v_uv       : TEXCOORD0 = vec2(0.0, 0.0);
vec4 v_color    : COLOR0    = vec4(1.0, 1.0, 1.0, 1.0);
vec3 v_worldPos : TEXCOORD1 = vec3(0.0, 0.0, 0.0);
vec3 v_normal   : NORMAL    = vec3(0.0, 0.0, 1.0);
)";
    source.bindings = {
        {BindingKind::Buffer, 12, Access::Read, static_cast<std::uint32_t>(BufferKind::Attributes)},
        {BindingKind::Buffer, 13, Access::Read, static_cast<std::uint32_t>(BufferKind::Indirection)},
        {BindingKind::Buffer, 14, Access::Read, static_cast<std::uint32_t>(BufferKind::RenderCount)},
    };
    std::string code;
    code += "$output v_uv, v_color, v_worldPos, v_normal\n\n";
    code += "#include \"bgfx_shader.sh\"\n\n";
    code += makeAttributePreamble(system, false, 12);
    code += "\nBUFFER_RAW_RO(s_vfxIndirection, 13);\n";
    code += "BUFFER_RAW_RO(s_vfxRenderCount, 14);\n";
    code += "BUFFER_RAW_RO(s_vfxInstances, 15);\n";
    code += "uniform mat4 u_global_invView;\n";
    code += "uniform mat4 u_global_viewProj;\n";
    code += "uniform vec4 u_global_cameraPos;\n";
    code += "uniform mat4 u_vfx_world;\n";
    code += "uniform vec4 u_vfxInstanceParams;\n\n";
    code += "void main()\n{\n";
    code += "    const uint vertexID = uint(gl_VertexID);\n";
    code += "    const uint renderCount = rawLoadUint(s_vfxRenderCount, 0u);\n";
    code += "    const uint particleChunks = max((renderCount + " +
            std::to_string(kParticlesPerDrawInstance) + "u - 1u) / " +
            std::to_string(kParticlesPerDrawInstance) + "u, 1u);\n";
    code += "    const uint instanceIndex = uint(gl_InstanceID) / particleChunks;\n";
    code += "    const uint particleChunk = uint(gl_InstanceID) - instanceIndex * particleChunks;\n";
    code += "    const uint linearParticle = vertexID / 6u + particleChunk * " +
            std::to_string(kParticlesPerDrawInstance) + "u;\n";
    code += "    uint effectInstance = instanceIndex;\n";
    code += "    bool instanceActive = true;\n";
    code += "    if (u_vfxInstanceParams.w > 0.5)\n    {\n";
    code += "        instanceActive = instanceIndex < uint(u_vfxInstanceParams.y + 0.5);\n";
    code += "        if (instanceActive)\n        {\n";
    code += "            effectInstance = rawLoadUint(s_vfxInstances,\n";
    code += "                uint(u_vfxInstanceParams.z + 0.5) + instanceIndex);\n";
    code += "        }\n";
    code += "    }\n";
    code += "    gl_Position = vec4(0.0, 0.0, 0.0, 0.0);\n";
    code += "    v_uv = vec2(0.0, 0.0);\n";
    code += "    v_color = vec4(0.0, 0.0, 0.0, 0.0);\n";
    code += "    v_worldPos = vec3(0.0, 0.0, 0.0);\n";
    code += "    v_normal = vec3(0.0, 0.0, 1.0);\n";
    code += "    if (linearParticle < renderCount && instanceActive)\n    {\n";
    code += "    const uint quadVertex = vertexID % 6u;\n";
    if (reverseSort) {
        code += "    uint drawParticle = linearParticle;\n";
        code += "    drawParticle = renderCount - 1u - drawParticle;\n";
    } else {
        code += "    const uint drawParticle = linearParticle;\n";
    }
    code += "    const uint particle = rawLoadUint(s_vfxIndirection, drawParticle);\n";
    code += "    VfxAttributes attributes;\n";
    code += "    attributes.Init(particle);\n";
    code += "    const vec3 position = attributes.Load_position();\n";
    code += "    const float size = attributes.Load_size();\n";
    code += "    const float angle = attributes.Load_angle();\n";
    code += "    const vec3 scale = attributes.Load_scale();\n";
    code += "    const vec4 color = vec4(attributes.Load_color(), attributes.Load_alpha());\n";
    code += "    vec2 corner;\n";
    code += "    if (quadVertex == 0u)\n    {\n        corner = vec2(0.0, 0.0);\n    }\n";
    code += "    else if (quadVertex == 1u)\n    {\n        corner = vec2(1.0, 0.0);\n    }\n";
    code += "    else if (quadVertex == 2u)\n    {\n        corner = vec2(1.0, 1.0);\n    }\n";
    code += "    else if (quadVertex == 3u)\n    {\n        corner = vec2(0.0, 0.0);\n    }\n";
    code += "    else if (quadVertex == 4u)\n    {\n        corner = vec2(1.0, 1.0);\n    }\n";
    code += "    else\n    {\n        corner = vec2(0.0, 1.0);\n    }\n";
    code += "    const vec3 cameraRight = u_global_invView[0].xyz;\n";
    code += "    const vec3 cameraUp = u_global_invView[1].xyz;\n";
    code += "    const float cosine = cos(angle);\n";
    code += "    const float sine = sin(angle);\n";
    code += "    const vec3 billboardRight = cameraRight * cosine + cameraUp * sine;\n";
    code += "    const vec3 billboardUp = -cameraRight * sine + cameraUp * cosine;\n";
    code += "    const vec2 offset = corner - 0.5;\n";
    code += "    const vec3 localPosition = position + (billboardRight * (offset.x * scale.x) +\n";
    code += "        billboardUp * (offset.y * scale.y)) * size;\n";
    code += "    vec3 worldPosition = mul(u_vfx_world, vec4(localPosition, 1.0)).xyz;\n";
    code += "    if (u_vfxInstanceParams.x > 1.5 && instanceActive)\n    {\n";
    code += "        const uint instanceBase = effectInstance * 32u;\n";
    code += "        const vec3 instanceX = vec3(rawLoadFloat(s_vfxInstances, instanceBase + 0u),\n";
    code += "            rawLoadFloat(s_vfxInstances, instanceBase + 1u),\n";
    code += "            rawLoadFloat(s_vfxInstances, instanceBase + 2u));\n";
    code += "        const vec3 instanceY = vec3(rawLoadFloat(s_vfxInstances, instanceBase + 4u),\n";
    code += "            rawLoadFloat(s_vfxInstances, instanceBase + 5u),\n";
    code += "            rawLoadFloat(s_vfxInstances, instanceBase + 6u));\n";
    code += "        const vec3 instanceZ = vec3(rawLoadFloat(s_vfxInstances, instanceBase + 8u),\n";
    code += "            rawLoadFloat(s_vfxInstances, instanceBase + 9u),\n";
    code += "            rawLoadFloat(s_vfxInstances, instanceBase + 10u));\n";
    code += "        const vec3 instanceTranslation = vec3(rawLoadFloat(s_vfxInstances, instanceBase + 12u),\n";
    code += "            rawLoadFloat(s_vfxInstances, instanceBase + 13u),\n";
    code += "            rawLoadFloat(s_vfxInstances, instanceBase + 14u));\n";
    code += "        worldPosition = instanceX * localPosition.x + instanceY * localPosition.y +\n";
    code += "            instanceZ * localPosition.z + instanceTranslation;\n";
    code += "    }\n";
    code += "    gl_Position = mul(u_global_viewProj, vec4(worldPosition, 1.0));\n";
    code += "    v_uv = corner;\n";
    code += "    v_color = color;\n";
    code += "    v_worldPos = worldPosition;\n";
    code += "    v_normal = normalize(u_global_cameraPos.xyz - worldPosition);\n";
    code += "    }\n";
    code += "}\n";
    source.source = std::move(code);
    return source;
}

std::string outputMaterialValue(float constant, const std::string& property) {
    return property.empty() ? formatFloat(constant) : "(u_vfxProp_" + property + ".x)";
}

std::string softParticleFunction() {
    return R"(
uniform vec4 u_vfxSoftParams;

SAMPLER2D(s_vfxSceneDepth, 4);

float hbgVfxSoftFade(vec3 worldPos, vec2 fragCoord)
{
    const vec2 depthUV = fragCoord * u_vfxSoftParams.yz;
    const float rawDepth = texture2D(s_vfxSceneDepth, depthUV).r;
    vec4 sceneWorld = mul(u_global_invViewProj, vec4(depthUV * 2.0 - 1.0, rawDepth, 1.0));
    sceneWorld.xyz /= max(sceneWorld.w, 1.0e-6);
    const float sceneZ = max(-mul(u_global_view, vec4(sceneWorld.xyz, 1.0)).z, 0.0);
    const float selfZ = max(-mul(u_global_view, vec4(worldPos, 1.0)).z, 0.0);
    float fade = hbgSaturate((sceneZ - selfZ) / max(u_vfxSoftParams.x, 0.001));
    return fade * fade * (3.0 - 2.0 * fade);
}
)";
}

ShaderSource makeOutputFragmentSource(const std::string& name, const OutputDesc& output,
                                      std::uint32_t textureProperty) {
    ShaderSource source;
    source.stage = ShaderStage::Fragment;
    source.name = name + ".fs";
    source.varyings = R"(vec2 v_uv       : TEXCOORD0 = vec2(0.0, 0.0);
vec4 v_color    : COLOR0    = vec4(1.0, 1.0, 1.0, 1.0);
vec3 v_worldPos : TEXCOORD1 = vec3(0.0, 0.0, 0.0);
vec3 v_normal   : NORMAL    = vec3(0.0, 0.0, 1.0);
)";
    source.bindings = {
        {BindingKind::Texture, 0, Access::Read, textureProperty},
    };
    if (output.softParticles) {
        source.bindings.push_back({BindingKind::Texture, 4, Access::Read, kSceneDepthBindingIndex});
    }
    if (!output.lit) {
        if (!output.softParticles) {
            source.source = R"($input v_uv, v_color, v_worldPos, v_normal

#include "bgfx_shader.sh"

SAMPLER2D(s_vfxMain, 0);

void main()
{
    const vec4 texel = texture2D(s_vfxMain, v_uv);
    gl_FragColor = vec4(v_color.rgb * texel.rgb, v_color.a * texel.a);
}
)";
            return source;
        }

        std::string code;
        code += "$input v_uv, v_color, v_worldPos, v_normal\n\n";
        code += "#include \"common/common.sc\"\n\n";
        code += "SAMPLER2D(s_vfxMain, 0);\n";
        code += softParticleFunction();
        code += "\nvoid main()\n{\n";
        code += "    const vec4 texel = texture2D(s_vfxMain, v_uv);\n";
        code += "    vec4 color = vec4(v_color.rgb * texel.rgb, v_color.a * texel.a);\n";
        code += "    const float softFade = hbgVfxSoftFade(v_worldPos, gl_FragCoord.xy);\n";
        if (output.blend == BlendMode::Additive || output.blend == BlendMode::Premultiply) {
            code += "    color *= softFade;\n";
        } else {
            code += "    color.a *= softFade;\n";
        }
        code += "    gl_FragColor = color;\n";
        code += "}\n";
        source.source = std::move(code);
        return source;
    }

    std::string code;
    code += "$input v_uv, v_color, v_worldPos, v_normal\n\n";
    code += "#include \"bgfx_shader.sh\"\n";
    code += "#include \"material/surface_data_types.sc\"\n";
    code += "#define HBG_CONTACT_SHADOW_TEXTURE 1\n";
    code += "#include \"lighting/contact_shadow.sc\"\n";
    code += "#include \"lighting/light_loop.sc\"\n\n";
    code += "SAMPLER2D(s_vfxMain, 0);\n";
    if (output.softParticles) {
        code += softParticleFunction();
    }
    if (!output.metallicProperty.empty()) {
        code += "uniform vec4 u_vfxProp_" + output.metallicProperty + ";\n";
    }
    if (!output.roughnessProperty.empty() && output.roughnessProperty != output.metallicProperty) {
        code += "uniform vec4 u_vfxProp_" + output.roughnessProperty + ";\n";
    }
    code += "\nvoid main()\n{\n";
    code += "    const vec4 texel = texture2D(s_vfxMain, v_uv);\n";
    code += "    SurfaceData surfaceData;\n";
    code += "    surfaceData.baseColor = v_color.rgb * texel.rgb;\n";
    code += "    surfaceData.alpha = v_color.a * texel.a;\n";
    code += "    surfaceData.metallic = hbgSaturate(" +
            outputMaterialValue(output.metallic, output.metallicProperty) + ");\n";
    code += "    surfaceData.perceptualRoughness = hbgSaturate(" +
            outputMaterialValue(output.roughness, output.roughnessProperty) + ");\n";
    code += "    surfaceData.roughness = max(surfaceData.perceptualRoughness * "
            "surfaceData.perceptualRoughness, 0.001);\n";
    code += "    surfaceData.ambientOcclusion = 1.0;\n";
    code += "    surfaceData.materialAmbientOcclusion = 1.0;\n";
    code += "    const vec3 normalWS = normalize(v_normal);\n";
    code += "    surfaceData.normalWS = normalWS;\n";
    code += "    surfaceData.geometricNormalWS = normalWS;\n";
    code += "    const vec3 tangentHint = abs(normalWS.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : "
            "vec3(1.0, 0.0, 0.0);\n";
    code += "    surfaceData.tangentWS = normalize(cross(normalWS, tangentHint));\n";
    code += "    surfaceData.emissive = vec3(0.0, 0.0, 0.0);\n";
    code += "    const vec2 contactShadowData = hbgSampleContactShadow(gl_FragCoord.xy * "
            "u_global_screenSize.zw);\n";
    code += "    surfaceData.contactShadowMask = contactShadowData.x;\n";
    code += "    surfaceData.contactShadowFade = contactShadowData.y;\n";
    code += "    surfaceData.lightLayerMask = 65535.0;\n";
    code += "    const vec3 viewWS = u_global_cameraPos.xyz - v_worldPos;\n";
    code += "    const float shadowVisibility = hbgSampleDirectionalShadow(v_worldPos, normalWS);\n";
    code += "    vec3 color = hbgEvaluateLightLoop(surfaceData, viewWS, v_worldPos, "
            "shadowVisibility) + surfaceData.emissive;\n";
    if (output.softParticles) {
        code += "    const float softFade = hbgVfxSoftFade(v_worldPos, gl_FragCoord.xy);\n";
        if (output.blend == BlendMode::Additive || output.blend == BlendMode::Premultiply) {
            code += "    color *= softFade;\n";
        }
        code += "    surfaceData.alpha *= softFade;\n";
    }
    code += "    gl_FragColor = vec4(max(color, vec3(0.0, 0.0, 0.0)), surfaceData.alpha);\n";
    code += "}\n";
    source.source = std::move(code);
    return source;
}

} // namespace

CompiledAsset generatePrograms(std::unique_ptr<Graph> graph) {
    CompiledAsset asset;
    asset.graph = std::move(graph);
    if (asset.graph == nullptr) {
        asset.error = "graph is null";
        return asset;
    }
    std::vector<ParseError> subgraphErrors;
    if (!flattenSubgraphs(*asset.graph, subgraphErrors)) {
        asset.error = subgraphErrors.empty() ? "failed to flatten subgraphs"
                                             : subgraphErrors.front().path + ": " +
                                                   subgraphErrors.front().message;
        return asset;
    }
    const Graph& source = *asset.graph;

    for (std::size_t systemIndex = 0; systemIndex < source.systems.size(); ++systemIndex) {
        const SystemDesc& system = source.systems[systemIndex];
        const std::string systemPath = "systems[" + std::to_string(systemIndex) + "]";
        if (system.capacity == 0 || system.capacity > kMaxParticleCapacity) {
            asset.error = systemPath + ".capacity: must be in [1, 1048576]";
            return asset;
        }
        if (system.events.receive.size() > 1u || system.events.emit.size() > 1u) {
            asset.error = systemPath + ".events: at most one receive and one emit event are supported";
            return asset;
        }
        if (!system.events.receive.empty() && !system.events.emit.empty()) {
            asset.error = systemPath + ".events: event chains (system both receives and emits) are not supported until V1.3";
            return asset;
        }
        if (system.outputs.empty()) {
            asset.error = systemPath + ".outputs: at least one output is required";
            return asset;
        }

        CompiledSystem compiled;
        compiled.desc = &system;
        compiled.attributeSet = defaultAttributeSet(source);
        compiled.layout = buildAttributeLayout(compiled.attributeSet, system.capacity);
        analyzeAttributeUsage(system, compiled.usage);

        const AttributeLayoutEntry* customSortKey = compiled.layout.find("sortKey");
        const auto validateCustomSort = [&](SortMode mode, const std::string& path) {
            if (mode != SortMode::Custom) {
                return true;
            }
            const AttributeType type = customSortKey != nullptr ? customSortKey->attribute.type
                                                                 : AttributeType::Float;
            if (customSortKey == nullptr ||
                (type != AttributeType::Float && type != AttributeType::UInt &&
                 type != AttributeType::Int && type != AttributeType::Bool)) {
                asset.error = path + ": custom sorting requires a scalar 'sortKey' attribute";
                return false;
            }
            return true;
        };
        if (!validateCustomSort(system.sort.mode, systemPath + ".sort.mode")) {
            return asset;
        }
        for (std::size_t outputIndex = 0; outputIndex < system.outputs.size(); ++outputIndex) {
            const OutputDesc& output = system.outputs[outputIndex];
            const std::string outputPath =
                systemPath + ".outputs[" + std::to_string(outputIndex) + "]";
            if (system.sort.mode == SortMode::None) {
                if (output.sort.has_value() && output.sort->mode != SortMode::None) {
                    asset.error = outputPath + ".sort: requires systems[" +
                                  std::to_string(systemIndex) + "].sort";
                    return asset;
                }
            } else if (!output.sort.has_value() || output.sort->mode == SortMode::None) {
                asset.error = outputPath + ".sort: must select a criterion when systems[" +
                              std::to_string(systemIndex) + "].sort is enabled";
                return asset;
            } else if (!validateCustomSort(output.sort->mode, outputPath + ".sort")) {
                return asset;
            }
            if (output.sortOrder.has_value() && system.sort.mode == SortMode::None) {
                asset.error = outputPath + ".sortOrder: requires systems[" +
                              std::to_string(systemIndex) + "].sort";
                return asset;
            }
        }
        asset.systems.push_back(std::move(compiled));
    }

    std::map<std::string, std::size_t> emitByName;
    std::map<std::string, std::size_t> receiveByName;
    for (std::size_t systemIndex = 0; systemIndex < source.systems.size(); ++systemIndex) {
        const SystemDesc& system = source.systems[systemIndex];
        for (const EventDesc& event : system.events.emit) {
            if (!emitByName.emplace(event.name, systemIndex).second) {
                asset.error = "systems[" + std::to_string(systemIndex) + "].events.emit: duplicate event '" +
                              event.name + "'";
                return asset;
            }
        }
        for (const EventDesc& event : system.events.receive) {
            if (!receiveByName.emplace(event.name, systemIndex).second) {
                asset.error = "systems[" + std::to_string(systemIndex) +
                              "].events.receive: duplicate event '" + event.name + "'";
                return asset;
            }
        }
    }
    for (const auto& [name, producer] : emitByName) {
        if (receiveByName.find(name) == receiveByName.end()) {
            asset.error = "systems[" + std::to_string(producer) + "].events.emit: event '" + name +
                          "' has no receiver";
            return asset;
        }
    }
    std::uint32_t eventBufferWords = 0;
    const auto appendEventLink = [&](CompiledEventLink link, const std::string& linkPath) {
        link.bufferOffsetWords = eventBufferWords;
        link.payloadOffsetWords = eventBufferWords + 5u + link.capacity;
        const std::uint64_t nextWords = static_cast<std::uint64_t>(link.payloadOffsetWords) +
                                        static_cast<std::uint64_t>(link.capacity) * 16u;
        if (nextWords > std::numeric_limits<std::uint32_t>::max() / 4u) {
            asset.error = linkPath + ": event buffer is too large";
            return false;
        }
        eventBufferWords = static_cast<std::uint32_t>(nextWords);
        if (link.cpu) {
            asset.systems[link.consumerSystem].eventIn =
                static_cast<std::int32_t>(asset.eventLinks.size());
        } else {
            asset.systems[link.producerSystem].eventOut =
                static_cast<std::int32_t>(asset.eventLinks.size());
            asset.systems[link.consumerSystem].eventIn =
                static_cast<std::int32_t>(asset.eventLinks.size());
        }
        asset.eventLinks.push_back(std::move(link));
        return true;
    };
    for (std::size_t systemIndex = 0; systemIndex < source.systems.size(); ++systemIndex) {
        const SystemDesc& producerDesc = source.systems[systemIndex];
        for (const EventDesc& emitEvent : producerDesc.events.emit) {
            const auto consumerIt = receiveByName.find(emitEvent.name);
            if (consumerIt == receiveByName.end()) {
                continue;
            }
            const std::size_t consumerIndex = consumerIt->second;
            const SystemDesc& consumerDesc = source.systems[consumerIndex];
            const CompiledSystem& consumer = asset.systems[consumerIndex];
            const std::string linkPath = "systems[" + std::to_string(systemIndex) +
                                         "].events.emit['" + emitEvent.name + "']";
            if (consumerDesc.spawner.rate != 0.0f || !consumerDesc.spawner.bursts.empty()) {
                asset.error = "systems[" + std::to_string(consumerIndex) +
                              "].spawner: GPU event receivers must not have a CPU spawner until V1.3";
                return asset;
            }
            if (consumer.eventIn >= 0 || asset.systems[systemIndex].eventOut >= 0) {
                asset.error = linkPath + ": each event must have exactly one producer and one consumer";
                return asset;
            }

            CompiledEventLink link;
            link.producerSystem = static_cast<std::uint32_t>(systemIndex);
            link.consumerSystem = static_cast<std::uint32_t>(consumerIndex);
            link.name = emitEvent.name;
            link.capacity = consumer.layout.capacity;
            if (emitEvent.attributes.kind != Value::Kind::Object) {
                asset.error = linkPath + ".attributes: must be an object";
                return asset;
            }
            const ExpressionContext context(source);
            for (const auto& [attributeName, value] : emitEvent.attributes.object) {
                if (link.payload.size() >= 4u) {
                    asset.error = linkPath + ".attributes: at most 4 event attributes are supported";
                    return asset;
                }
                if (value.kind == Value::Kind::String) {
                    asset.error = linkPath + ".attributes." + attributeName +
                                  ": string values are not valid payload expressions";
                    return asset;
                }
                const CompiledExpression compiled = compileExpression(
                    value, context, linkPath + ".attributes." + attributeName, ExprType::Invalid);
                if (!compiled.ok()) {
                    asset.error = compiled.errors.empty()
                                      ? linkPath + ".attributes." + attributeName +
                                            ": failed to compile payload expression"
                                      : compiled.errors.front().path + ": " +
                                            compiled.errors.front().message;
                    return asset;
                }
                link.payload.push_back(EventPayloadAttribute{
                    attributeName, static_cast<std::uint32_t>(link.payload.size()),
                    compiled.root->type});
            }

            const EventDesc& receiveEvent = consumerDesc.events.receive[0];
            if (receiveEvent.attributes.kind != Value::Kind::Object) {
                asset.error = "systems[" + std::to_string(consumerIndex) +
                              "].events.receive[0].attributes: must be an object";
                return asset;
            }
            if (!receiveEvent.attributes.object.empty()) {
                for (const auto& [consumerAttribute, payloadName] : receiveEvent.attributes.object) {
                    if (payloadName.kind != Value::Kind::String) {
                        asset.error = "systems[" + std::to_string(consumerIndex) +
                                      "].events.receive[0].attributes." + consumerAttribute +
                                      ": value must name an emit attribute";
                        return asset;
                    }
                    if (consumer.layout.find(consumerAttribute) == nullptr) {
                        asset.error = "systems[" + std::to_string(consumerIndex) +
                                      "].events.receive[0].attributes." + consumerAttribute +
                                      ": unknown attribute";
                        return asset;
                    }
                    const auto payload = std::find_if(
                        link.payload.begin(), link.payload.end(), [&](const EventPayloadAttribute& candidate) {
                            return candidate.name == payloadName.string;
                        });
                    if (payload == link.payload.end()) {
                        asset.error = "systems[" + std::to_string(consumerIndex) +
                                      "].events.receive[0].attributes." + consumerAttribute +
                                      ": unknown emit attribute '" + payloadName.string + "'";
                        return asset;
                    }
                    link.receives.push_back(EventReceiveMapping{
                        consumerAttribute, payload->slot});
                }
            } else {
                for (const EventPayloadAttribute& payload : link.payload) {
                    if (consumer.layout.find(payload.name) != nullptr) {
                        link.receives.push_back(EventReceiveMapping{payload.name, payload.slot});
                    }
                }
            }

            if (!appendEventLink(std::move(link), linkPath)) {
                return asset;
            }
        }
    }
    for (std::size_t consumerIndex = 0; consumerIndex < source.systems.size(); ++consumerIndex) {
        const SystemDesc& consumerDesc = source.systems[consumerIndex];
        if (consumerDesc.events.receive.empty()) {
            continue;
        }
        const EventDesc& receiveEvent = consumerDesc.events.receive[0];
        if (emitByName.find(receiveEvent.name) != emitByName.end()) {
            continue;
        }
        const CompiledSystem& consumer = asset.systems[consumerIndex];
        const std::string linkPath = "systems[" + std::to_string(consumerIndex) +
                                     "].events.receive['" + receiveEvent.name + "']";
        if (consumerDesc.spawner.rate != 0.0f || !consumerDesc.spawner.bursts.empty()) {
            asset.error = "systems[" + std::to_string(consumerIndex) +
                          "].spawner: CPU event receivers must not have a CPU spawner";
            return asset;
        }
        if (consumer.eventIn >= 0) {
            asset.error = linkPath + ": each event must have exactly one receiver";
            return asset;
        }
        if (receiveEvent.attributes.kind != Value::Kind::Object ||
            receiveEvent.attributes.object.empty()) {
            asset.error = linkPath + ".attributes: CPU events require an explicit attribute mapping";
            return asset;
        }
        CompiledEventLink link;
        link.cpu = true;
        link.producerSystem = 0xffffffffu;
        link.consumerSystem = static_cast<std::uint32_t>(consumerIndex);
        link.name = receiveEvent.name;
        link.capacity = consumer.layout.capacity;
        for (const auto& [consumerAttribute, payloadValue] : receiveEvent.attributes.object) {
            if (payloadValue.kind != Value::Kind::String) {
                asset.error = linkPath + ".attributes." + consumerAttribute +
                              ": value must name an event attribute";
                return asset;
            }
            const AttributeLayoutEntry* entry = consumer.layout.find(consumerAttribute);
            if (entry == nullptr) {
                asset.error = linkPath + ".attributes." + consumerAttribute + ": unknown attribute";
                return asset;
            }
            auto payload = std::find_if(link.payload.begin(), link.payload.end(),
                                        [&](const EventPayloadAttribute& candidate) {
                                            return candidate.name == payloadValue.string;
                                        });
            if (payload == link.payload.end()) {
                if (link.payload.size() >= 4u) {
                    asset.error = linkPath + ".attributes: at most 4 event attributes are supported";
                    return asset;
                }
                EventPayloadAttribute attribute;
                attribute.name = payloadValue.string;
                attribute.slot = static_cast<std::uint32_t>(link.payload.size());
                attribute.type = ExprType::Invalid;
                attribute.targetType = entry->attribute.type;
                attribute.defaultWords = attributeDefaultWords(entry->attribute);
                link.payload.push_back(std::move(attribute));
                payload = std::prev(link.payload.end());
            }
            link.receives.push_back(EventReceiveMapping{consumerAttribute, payload->slot});
        }
        if (!appendEventLink(std::move(link), linkPath)) {
            return asset;
        }
    }

    if (eventBufferWords >= std::numeric_limits<std::uint32_t>::max() / 4u) {
        asset.error = "event buffer is too large";
        return asset;
    }
    asset.eventBufferSize = eventBufferWords * 4u;
    if (asset.eventLinks.size() > 128u) {
        asset.error = "events: at most 128 GPU event links are supported";
        return asset;
    }
    if (asset.eventBufferSize > 64u * 1024u) {
        asset.error = "events: event buffer exceeds the V1 64 KiB single-storage limit";
        return asset;
    }

    std::uint32_t nextProgram = 0;
    for (std::size_t systemIndex = 0; systemIndex < source.systems.size(); ++systemIndex) {
        CompiledSystem& compiled = asset.systems[systemIndex];
        const SystemDesc& system = *compiled.desc;
        const std::string systemPath = "systems[" + std::to_string(systemIndex) + "]";
        const std::uint32_t capacity = compiled.layout.capacity;
        const std::uint32_t groups = groupCount(capacity);
        const AttributeLayoutEntry* customSortKey = compiled.layout.find("sortKey");

        KernelProgram spawner;
        spawner.system = &system;
        spawner.task = KernelTask::Spawner;
        spawner.program = nextProgram++;
        spawner.groups = {1, 1, 1};
        spawner.shader = makeSpawnerSource(compiled, spawner.program);
        compiled.kernels.push_back(std::move(spawner));

        KernelProgram deadList;
        deadList.system = &system;
        deadList.task = KernelTask::DeadListInit;
        deadList.program = nextProgram++;
        deadList.groups = {groups, 1, 1};
        deadList.shader = makeDeadListSource(compiled);
        compiled.kernels.push_back(std::move(deadList));

        std::string blockError;
        KernelProgram init;
        init.system = &system;
        init.task = KernelTask::Init;
        init.program = nextProgram++;
        init.groups = {groups, 1, 1};
        if (compiled.eventIn >= 0) {
            init.shader = makeEventInitSource(compiled, source, systemPath,
                                              asset.eventLinks[static_cast<std::size_t>(compiled.eventIn)],
                                              blockError);
        } else {
            init.shader = makeInitSource(compiled, source, systemPath, blockError);
        }
        if (!blockError.empty()) {
            asset.error = std::move(blockError);
            return asset;
        }
        compiled.kernels.push_back(std::move(init));

        KernelProgram update;
        update.system = &system;
        update.task = KernelTask::Update;
        update.program = nextProgram++;
        update.groups = {groups, 1, 1};
        const CompiledEventLink* eventOut =
            compiled.eventOut >= 0 ? &asset.eventLinks[static_cast<std::size_t>(compiled.eventOut)]
                                   : nullptr;
        update.shader = makeUpdateSource(compiled, source, systemPath, eventOut, blockError);
        if (!blockError.empty()) {
            asset.error = std::move(blockError);
            return asset;
        }
        compiled.kernels.push_back(std::move(update));

        if (system.bounds.mode == BoundsMode::Automatic) {
            KernelProgram boundsInit;
            boundsInit.system = &system;
            boundsInit.task = KernelTask::BoundsInit;
            boundsInit.program = nextProgram++;
            boundsInit.groups = {1, 1, 1};
            boundsInit.shader = makeBoundsInitSource(compiled);
            compiled.kernels.push_back(std::move(boundsInit));

            KernelProgram bounds;
            bounds.system = &system;
            bounds.task = KernelTask::Bounds;
            bounds.program = nextProgram++;
            bounds.groups = {groups, 1, 1};
            bounds.shader = makeBoundsSource(compiled);
            compiled.kernels.push_back(std::move(bounds));
        }

        std::vector<SortMode> extraSortModes;
        if (system.sort.mode != SortMode::None) {
            for (const OutputDesc& output : system.outputs) {
                if (!output.sort.has_value() || output.sort->mode == SortMode::None ||
                    output.sort->mode == system.sort.mode) {
                    continue;
                }
                if (std::find(extraSortModes.begin(), extraSortModes.end(), output.sort->mode) ==
                    extraSortModes.end()) {
                    extraSortModes.push_back(output.sort->mode);
                }
            }
        }
        const bool useSortUniforms = !extraSortModes.empty();

        const auto appendSortPipeline = [&](SortMode mode, bool tagged) {
            const SortMode kernelMode = tagged ? mode : SortMode::None;
            if (tagged) {
                KernelProgram reset;
                reset.system = &system;
                reset.task = KernelTask::SortReset;
                reset.program = nextProgram++;
                reset.groups = {1, 1, 1};
                reset.sortMode = kernelMode;
                reset.shader = makeSortResetSource(compiled);
                compiled.kernels.push_back(std::move(reset));
            }

            KernelProgram outputUpdate;
            outputUpdate.system = &system;
            outputUpdate.task = KernelTask::OutputUpdate;
            outputUpdate.program = nextProgram++;
            outputUpdate.groups = {groups, 1, 1};
            outputUpdate.sortMode = kernelMode;
            outputUpdate.shader = makeOutputUpdateSource(
                compiled, source, mode, useSortUniforms,
                mode == SortMode::Custom ? customSortKey : nullptr);
            compiled.kernels.push_back(std::move(outputUpdate));

            if (mode == SortMode::None) {
                return;
            }
            if (capacity <= kSortChunkSize) {
                KernelProgram bitonic;
                bitonic.system = &system;
                bitonic.task = KernelTask::SortBitonic;
                bitonic.program = nextProgram++;
                bitonic.groups = {1, 1, 1};
                bitonic.sortMode = kernelMode;
                bitonic.shader = makeSortBitonicSource(compiled, true, kSortChunkSize, kSortThreads);
                compiled.kernels.push_back(std::move(bitonic));
                return;
            }

            KernelProgram prepass;
            prepass.system = &system;
            prepass.task = KernelTask::SortPrepass;
            prepass.program = nextProgram++;
            prepass.groups = {1, 1, 1};
            prepass.sortMode = kernelMode;
            prepass.shader = makeSortBitonicSource(compiled, false, kSortChunkSize, kSortThreads);
            compiled.kernels.push_back(std::move(prepass));

            BufferKind sourceKind = BufferKind::SortScratch;
            BufferKind destinationKind = BufferKind::RenderKVP;
            for (std::uint32_t subArraySize = kSortChunkSize; subArraySize < capacity;
                 subArraySize <<= 1u) {
                const bool finalPass = subArraySize * 2u >= capacity;
                KernelProgram merge;
                merge.system = &system;
                merge.task = KernelTask::SortMerge;
                merge.program = nextProgram++;
                merge.groups = {groups, 1, 1};
                merge.sortMode = kernelMode;
                merge.shader = makeSortMergeSource(compiled, subArraySize, sourceKind,
                                                   finalPass ? BufferKind::Indirection
                                                             : destinationKind,
                                                   finalPass);
                compiled.kernels.push_back(std::move(merge));
                if (finalPass) {
                    break;
                }
                sourceKind = destinationKind;
                destinationKind = destinationKind == BufferKind::RenderKVP ? BufferKind::SortScratch
                                                                           : BufferKind::RenderKVP;
            }
        };

        appendSortPipeline(system.sort.mode, false);

        KernelProgram fillIndirect;
        fillIndirect.system = &system;
        fillIndirect.task = KernelTask::FillIndirect;
        fillIndirect.program = nextProgram++;
        fillIndirect.groups = {1, 1, 1};
        fillIndirect.shader = makeFillIndirectSource(compiled);
        compiled.kernels.push_back(std::move(fillIndirect));

        for (const SortMode mode : extraSortModes) {
            appendSortPipeline(mode, true);
        }

        for (std::size_t outputIndex = 0; outputIndex < system.outputs.size(); ++outputIndex) {
            const OutputDesc& outputDesc = system.outputs[outputIndex];
            const std::string outputPath =
                systemPath + ".outputs[" + std::to_string(outputIndex) + "]";
            if (outputDesc.softParticles &&
                (!std::isfinite(outputDesc.softDistance) || outputDesc.softDistance < 0.001f)) {
                asset.error = outputPath + ".softDistance: must be at least 0.001";
                return asset;
            }
            if (outputDesc.softParticles && outputDesc.blend == BlendMode::Opaque) {
                asset.error = outputPath + ".softParticles: requires a transparent blend mode";
                return asset;
            }
            std::uint32_t textureProperty = kInvalidIndex;
            if (!outputDesc.texture.empty()) {
                bool found = false;
                for (std::size_t propertyIndex = 0; propertyIndex < source.properties.size(); ++propertyIndex) {
                    const Property& property = source.properties[propertyIndex];
                    if (property.name != outputDesc.texture) {
                        continue;
                    }
                    found = true;
                    if (property.type != PropertyType::Texture) {
                        asset.error = outputPath + ".texture: property '" + property.name + "' is not a texture";
                        return asset;
                    }
                    textureProperty = static_cast<std::uint32_t>(propertyIndex);
                    break;
                }
                if (!found) {
                    asset.error = outputPath + ".texture: unknown property '" + outputDesc.texture + "'";
                    return asset;
                }
            }

            OutputProgram output;
            output.system = &system;
            output.program = nextProgram++;
            output.blend = outputDesc.blend;
            output.lit = outputDesc.lit;
            output.softParticles = outputDesc.softParticles;
            output.castShadows = outputDesc.castShadows;
            output.generateMotionVector = outputDesc.generateMotionVector;
            output.sortPriority = outputDesc.sortPriority;
            output.softDistance = outputDesc.softDistance;
            output.metallic = outputDesc.metallic;
            output.roughness = outputDesc.roughness;
            output.metallicProperty = outputDesc.metallicProperty;
            output.roughnessProperty = outputDesc.roughnessProperty;
            output.sortMode = outputDesc.sort.has_value() ? outputDesc.sort->mode : system.sort.mode;
            output.sortOrder = outputDesc.sortOrder.value_or(system.sort.order);
            output.textureProperty = textureProperty;
            const std::string outputName = "output" + std::to_string(outputIndex);
            const bool reverseSort = output.sortMode != SortMode::None &&
                                     output.sortOrder != system.sort.order;
            output.vertex = makeOutputVertexSource(compiled, outputName, reverseSort);
            output.fragment = makeOutputFragmentSource(outputName, outputDesc, textureProperty);
            compiled.outputs.push_back(std::move(output));

            KernelProgram vertex;
            vertex.system = &system;
            vertex.task = KernelTask::OutputVertex;
            vertex.program = compiled.outputs.back().program;
            vertex.groups = {groups, 1, 1};
            vertex.shader = compiled.outputs.back().vertex;
            compiled.kernels.push_back(std::move(vertex));

            KernelProgram fragment;
            fragment.system = &system;
            fragment.task = KernelTask::OutputFragment;
            fragment.program = compiled.outputs.back().program;
            fragment.groups = {groups, 1, 1};
            fragment.shader = compiled.outputs.back().fragment;
            compiled.kernels.push_back(std::move(fragment));
        }

        for (KernelProgram& kernel : compiled.kernels) {
            if (kernel.shader.source.empty()) {
                asset.error = "failed to generate " + kernel.shader.name;
                return asset;
            }
        }
    }

    if (!asset.eventLinks.empty()) {
        std::uint32_t cpuEventWords = 0u;
        for (const CompiledEventLink& link : asset.eventLinks) {
            if (link.cpu) {
                cpuEventWords += 5u + link.capacity + link.capacity * 16u;
            }
        }
        if (cpuEventWords > 0u) {
            KernelProgram copy;
            copy.task = KernelTask::EventCopy;
            copy.program = nextProgram++;
            copy.groups = {groupCount(cpuEventWords), 1, 1};
            copy.shader = makeEventCopySource(asset);
            asset.globalKernels.push_back(std::move(copy));
        }

        const std::uint32_t eventWords = asset.eventBufferSize / 4u;
        KernelProgram reset;
        reset.task = KernelTask::EventReset;
        reset.program = nextProgram++;
        reset.groups = {groupCount(eventWords), 1, 1};
        reset.shader = makeEventResetSource(eventWords);
        asset.globalKernels.push_back(std::move(reset));

        KernelProgram prefix;
        prefix.task = KernelTask::EventPrefix;
        prefix.program = nextProgram++;
        prefix.groups = {1, 1, 1};
        prefix.shader = makeEventPrefixSource(asset);
        asset.globalKernels.push_back(std::move(prefix));
    }
    return asset;
}} // namespace vfx::detail
