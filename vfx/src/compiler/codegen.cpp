#include "codegen.h"

#include "vfx/expression.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace vfx::detail {
namespace {

constexpr std::uint32_t kThreadsPerGroup = 64u;
constexpr std::uint32_t kMaxParticleCapacity = 1u << 20;

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

std::string makeCurveFunction(const Property& property) {
    std::string code;
    code += "float vfxSampleCurve_" + property.name + "(float t)\n{\n";
    code += "    t = saturate(t);\n";
    if (property.curve.empty()) {
        code += "    return 0.0;\n}\n";
        return code;
    }
    if (property.curve.size() == 1) {
        code += "    return " + formatFloat(property.curve.front().value) + ";\n}\n";
        return code;
    }
    code += "    if (t <= " + formatFloat(property.curve.front().time) + ") return " +
            formatFloat(property.curve.front().value) + ";\n";
    for (std::size_t i = 1; i < property.curve.size(); ++i) {
        const CurveKey& a = property.curve[i - 1];
        const CurveKey& b = property.curve[i];
        const float span = b.time - a.time;
        code += "    if (t < " + formatFloat(b.time) + ") return lerp(" + formatFloat(a.value) + ", " +
                formatFloat(b.value) + ", (t - " + formatFloat(a.time) + ") / " + formatFloat(span) + ");\n";
    }
    code += "    return " + formatFloat(property.curve.back().value) + ";\n}\n";
    return code;
}

std::string makeGradientFunction(const Property& property) {
    std::string code;
    code += "vec4 vfxSampleGradient_" + property.name + "(float t)\n{\n";
    code += "    t = saturate(t);\n";
    if (property.gradient.empty()) {
        code += "    return vec4(0.0, 0.0, 0.0, 0.0);\n}\n";
        return code;
    }
    const auto formatColor = [](const Vec4& color) {
        return "vec4(" + formatFloat(color.x) + ", " + formatFloat(color.y) + ", " +
               formatFloat(color.z) + ", " + formatFloat(color.w) + ")";
    };
    if (property.gradient.size() == 1) {
        code += "    return " + formatColor(property.gradient.front().color) + ";\n}\n";
        return code;
    }
    code += "    if (t <= " + formatFloat(property.gradient.front().time) + ") return " +
            formatColor(property.gradient.front().color) + ";\n";
    for (std::size_t i = 1; i < property.gradient.size(); ++i) {
        const GradientKey& a = property.gradient[i - 1];
        const GradientKey& b = property.gradient[i];
        const float span = b.time - a.time;
        code += "    if (t < " + formatFloat(b.time) + ") return lerp(" + formatColor(a.color) + ", " +
                formatColor(b.color) + ", (t - " + formatFloat(a.time) + ") / " + formatFloat(span) + ");\n";
    }
    code += "    return " + formatColor(property.gradient.back().color) + ";\n}\n";
    return code;
}

std::string makeExpressionHelpers(const Graph& graph) {
    bool needsCurlNoise = false;
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
    code += "\n";
    code += "#define VFX_ATTR(name) a_##name\n";
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
    for (const Property& property : graph.properties) {
        if (property.type == PropertyType::Curve) {
            code += makeCurveFunction(property);
            code += "#define VFX_SAMPLE_CURVE_" + property.name + "(t) vfxSampleCurve_" + property.name +
                    "(t)\n";
        } else if (property.type == PropertyType::Gradient) {
            code += makeGradientFunction(property);
            code += "#define VFX_SAMPLE_GRADIENT_" + property.name + "(t) vfxSampleGradient_" +
                    property.name + "(t)\n";
        }
    }
    code += "#define VFX_SAMPLE_CURVE(name, t) VFX_SAMPLE_CURVE_##name(t)\n";
    code += "#define VFX_SAMPLE_GRADIENT(name, t) VFX_SAMPLE_GRADIENT_##name(t)\n";
    return code;
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
    if (block.type == "set_position_sphere" || block.type == "set_position_box") {
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

class BlockCompiler {
  public:
    BlockCompiler(const Graph& graph, const CompiledSystem& system, const std::string& systemPath,
                  std::string& code)
        : m_graph(graph), m_system(system), m_context(graph), m_systemPath(systemPath), m_code(code) {}

    const std::string& error() const noexcept { return m_error; }

    bool compileInitBlock(const Block& block, std::size_t index) {
        const std::string path = m_systemPath + ".init.blocks[" + std::to_string(index) + "]";
        const std::string seed = "a_seed ^ " + blockSeed(path, block.type);
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
                m_code += "        a_velocity = normalize(" + direction + ") * speed;\n";
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
            m_code += "    a_velocity = normalize(" + direction + ") * " + speed + ";\n";
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
                                    "' in V1.1.2; supported blocks are integrate/age/reap/gravity/drag/force/"
                                    "attractor/turbulence/over-life");
    }

  private:
    bool fail(const std::string& path, const std::string& message) {
        m_error = path + ": " + message;
        return false;
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
    std::string& m_code;
    std::string m_error;
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
    };
    source.source = R"(#include "bgfx_compute.sh"

BUFFER_RAW_RW(s_vfxSpawnCount, 0);
BUFFER_RW(s_vfxRenderCount, uint, 1);
uniform vec4 u_vfx_params;

NUM_THREADS(1, 1, 1)

void main()
{
    if (uint(gl_GlobalInvocationID.x) == 0u)
    {
        rawStoreUint(s_vfxSpawnCount, 0u, uint(u_vfx_params.z + 0.5));
        s_vfxRenderCount[0] = 0u;
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

    BlockCompiler compiler(graph, system, systemPath, code);
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
    code += makeAttributeStores(system);
    code += "}\n";
    source.source = std::move(code);
    return source;
}

ShaderSource makeUpdateSource(const CompiledSystem& system, const Graph& graph, const std::string& systemPath,
                              std::string& error) {
    ShaderSource source;
    source.stage = ShaderStage::Compute;
    source.name = "update";
    source.bindings = {
        {BindingKind::Buffer, 0, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::Attributes)},
        {BindingKind::Buffer, 1, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::DeadList)},
    };

    std::string code = makeComputePreamble(system, graph, true, 0);
    code += "BUFFER_RW(s_vfxDeadList, uint, 1);\n\n";
    code += "NUM_THREADS(64, 1, 1)\n\n";
    code += "void main()\n{\n";
    code += "    const uint particleIndex = uint(gl_GlobalInvocationID.x);\n";
    code += "    if (particleIndex >= " + std::to_string(system.layout.capacity) + "u)\n    {\n        return;\n    }\n";
    code += "    VfxAttributes attributes;\n";
    code += "    attributes.Init(particleIndex);\n";
    code += makeParameterLocals(system, false);
    code += "    if (!a_alive)\n    {\n        return;\n    }\n";

    BlockCompiler compiler(graph, system, systemPath, code);
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

ShaderSource makeOutputUpdateSource(const CompiledSystem& system, const Graph& graph) {
    ShaderSource source;
    source.stage = ShaderStage::Compute;
    source.name = "output_update";
    source.bindings = {
        {BindingKind::Buffer, 0, Access::Read, static_cast<std::uint32_t>(BufferKind::Attributes)},
        {BindingKind::Buffer, 1, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::Indirection)},
        {BindingKind::Buffer, 2, Access::ReadWrite, static_cast<std::uint32_t>(BufferKind::RenderCount)},
    };
    std::string code = makeComputePreamble(system, graph, false, 0);
    code += "BUFFER_RAW_RW(s_vfxIndirection, 1);\n";
    code += "BUFFER_RW(s_vfxRenderCount, uint, 2);\n\n";
    code += "NUM_THREADS(64, 1, 1)\n\n";
    code += "void main()\n{\n";
    code += "    const uint particleIndex = uint(gl_GlobalInvocationID.x);\n";
    code += "    if (particleIndex >= " + std::to_string(system.layout.capacity) + "u)\n    {\n        return;\n    }\n";
    code += "    VfxAttributes attributes;\n";
    code += "    attributes.Init(particleIndex);\n";
    code += "    if (!attributes.Load_alive())\n    {\n        return;\n    }\n";
    code += "    uint previousCount = 0u;\n";
    code += "    atomicFetchAndAdd(s_vfxRenderCount[0], 1u, previousCount);\n";
    code += "    if (previousCount < " + std::to_string(system.layout.capacity) + "u)\n    {\n";
    code += "        rawStoreUint(s_vfxIndirection, previousCount, particleIndex);\n";
    code += "    }\n";
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
    };
    source.source = R"(#include "bgfx_compute.sh"

BUFFER_RAW_WO(s_vfxIndirectArgs, 0);
BUFFER_RO(s_vfxRenderCount, uint, 1);

NUM_THREADS(1, 1, 1)

void main()
{
    if (uint(gl_GlobalInvocationID.x) != 0u)
    {
        return;
    }
    const uint count = s_vfxRenderCount[0];
    rawStoreUint(s_vfxIndirectArgs, 0u, count * 6u);
    rawStoreUint(s_vfxIndirectArgs, 1u, 1u);
    rawStoreUint(s_vfxIndirectArgs, 2u, 0u);
    rawStoreUint(s_vfxIndirectArgs, 3u, 0u);
    rawStoreUint(s_vfxIndirectArgs, 4u, 0u);
    rawStoreUint(s_vfxIndirectArgs, 5u, 0u);
    rawStoreUint(s_vfxIndirectArgs, 6u, 0u);
    rawStoreUint(s_vfxIndirectArgs, 7u, 0u);
}
)";
    (void)system;
    return source;
}

ShaderSource makeOutputVertexSource(const CompiledSystem& system, const std::string& name) {
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
    };
    std::string code;
    code += "$output v_uv, v_color, v_worldPos, v_normal\n\n";
    code += "#include \"bgfx_shader.sh\"\n\n";
    code += makeAttributePreamble(system, false, 12);
    code += "\nBUFFER_RAW_RO(s_vfxIndirection, 13);\n";
    code += "uniform mat4 u_global_invView;\n";
    code += "uniform mat4 u_global_viewProj;\n";
    code += "uniform vec4 u_global_cameraPos;\n";
    code += "uniform mat4 u_vfx_world;\n\n";
    code += "void main()\n{\n";
    code += "    const uint vertexID = uint(gl_VertexID);\n";
    code += "    const uint quadVertex = vertexID % 6u;\n";
    code += "    const uint drawParticle = vertexID / 6u;\n";
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
    code += "    const vec3 worldPosition = mul(u_vfx_world, vec4(localPosition, 1.0)).xyz;\n";
    code += "    gl_Position = mul(u_global_viewProj, vec4(worldPosition, 1.0));\n";
    code += "    v_uv = corner;\n";
    code += "    v_color = color;\n";
    code += "    v_worldPos = worldPosition;\n";
    code += "    v_normal = normalize(u_global_cameraPos.xyz - worldPosition);\n";
    code += "}\n";
    source.source = std::move(code);
    return source;
}

ShaderSource makeOutputFragmentSource(const std::string& name, std::uint32_t textureProperty) {
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

} // namespace

CompiledAsset generatePrograms(std::unique_ptr<Graph> graph) {
    CompiledAsset asset;
    asset.graph = std::move(graph);
    if (asset.graph == nullptr) {
        asset.error = "graph is null";
        return asset;
    }
    const Graph& source = *asset.graph;
    if (!source.subgraphs.empty()) {
        asset.error = "subgraphs[0]: subgraphs are not supported until V1.3";
        return asset;
    }

    std::uint32_t nextProgram = 0;
    for (std::size_t systemIndex = 0; systemIndex < source.systems.size(); ++systemIndex) {
        const SystemDesc& system = source.systems[systemIndex];
        const std::string systemPath = "systems[" + std::to_string(systemIndex) + "]";
        if (system.capacity == 0 || system.capacity > kMaxParticleCapacity) {
            asset.error = systemPath + ".capacity: must be in [1, 1048576]";
            return asset;
        }
        if (!system.events.receive.empty() || !system.events.emit.empty()) {
            asset.error = systemPath + ".events: events are not supported until V1.2";
            return asset;
        }
        if (system.sort.mode != SortMode::None) {
            asset.error = systemPath + ".sort.mode: sorting is not supported until V1.2";
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

        const std::uint32_t capacity = compiled.layout.capacity;
        const std::uint32_t groups = groupCount(capacity);

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
        init.shader = makeInitSource(compiled, source, systemPath, blockError);
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
        update.shader = makeUpdateSource(compiled, source, systemPath, blockError);
        if (!blockError.empty()) {
            asset.error = std::move(blockError);
            return asset;
        }
        compiled.kernels.push_back(std::move(update));

        KernelProgram outputUpdate;
        outputUpdate.system = &system;
        outputUpdate.task = KernelTask::OutputUpdate;
        outputUpdate.program = nextProgram++;
        outputUpdate.groups = {groups, 1, 1};
        outputUpdate.shader = makeOutputUpdateSource(compiled, source);
        compiled.kernels.push_back(std::move(outputUpdate));

        KernelProgram fillIndirect;
        fillIndirect.system = &system;
        fillIndirect.task = KernelTask::FillIndirect;
        fillIndirect.program = nextProgram++;
        fillIndirect.groups = {1, 1, 1};
        fillIndirect.shader = makeFillIndirectSource(compiled);
        compiled.kernels.push_back(std::move(fillIndirect));

        for (std::size_t outputIndex = 0; outputIndex < system.outputs.size(); ++outputIndex) {
            const OutputDesc& outputDesc = system.outputs[outputIndex];
            const std::string outputPath =
                systemPath + ".outputs[" + std::to_string(outputIndex) + "]";
            if (outputDesc.lit) {
                asset.error = outputPath + ".lit: lit output is not supported until V1.4";
                return asset;
            }
            if (outputDesc.softParticles) {
                asset.error = outputPath + ".softParticles: soft particles are not supported until V1.4";
                return asset;
            }
            if (outputDesc.sort.has_value() && outputDesc.sort->mode != SortMode::None) {
                asset.error = outputPath + ".sort: sorting is not supported until V1.2";
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
            output.sortMode = outputDesc.sort.has_value() ? outputDesc.sort->mode : SortMode::None;
            output.sortOrder = outputDesc.sort.has_value() ? outputDesc.sort->order : SortOrder::Ascending;
            output.textureProperty = textureProperty;
            const std::string outputName = "output" + std::to_string(outputIndex);
            output.vertex = makeOutputVertexSource(compiled, outputName);
            output.fragment = makeOutputFragmentSource(outputName, textureProperty);
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
        asset.systems.push_back(std::move(compiled));
    }
    return asset;
}

} // namespace vfx::detail
