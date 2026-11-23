#include "hbg/dawn/dawn_config.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <utility>

namespace hbg::dawn {

namespace {

using Json = nlohmann::json;

bool rejectUnknownKeys(const Json& object, std::initializer_list<std::string_view> allowed, const char* context,
                       std::string& error) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        bool known = false;
        for (const std::string_view key : allowed) {
            if (it.key() == key) {
                known = true;
                break;
            }
        }
        if (!known) {
            error = std::string(context) + ": unknown setting '" + it.key() + "'";
            return false;
        }
    }
    return true;
}

bool requireObject(const Json& parent, const char* key, const char* context, const Json** object, std::string& error) {
    const auto it = parent.find(key);
    if (it == parent.end()) {
        error = std::string(context) + ": missing '" + key + "'";
        return false;
    }
    if (!it->is_object()) {
        error = std::string(context) + "." + key + " must be an object";
        return false;
    }
    *object = &*it;
    return true;
}

bool readBool(const Json& object, const char* key, const char* context, bool& value, std::string& error) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_boolean()) {
        error = std::string(context) + "." + key + " must be a boolean";
        return false;
    }
    value = it->get<bool>();
    return true;
}

bool readUint(const Json& object, const char* key, const char* context, uint32_t& value, std::string& error) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer()) {
        error = std::string(context) + "." + key + " must be an integer";
        return false;
    }
    const int64_t parsed = it->get<int64_t>();
    if (parsed < 0 || static_cast<uint64_t>(parsed) > std::numeric_limits<uint32_t>::max()) {
        error = std::string(context) + "." + key + " is outside the uint32 range";
        return false;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool readInt(const Json& object, const char* key, const char* context, int& value, std::string& error) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer()) {
        error = std::string(context) + "." + key + " must be an integer";
        return false;
    }
    value = it->get<int>();
    return true;
}

bool readFloat(const Json& object, const char* key, const char* context, float& value, std::string& error) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number()) {
        error = std::string(context) + "." + key + " must be a number";
        return false;
    }
    value = it->get<float>();
    if (!std::isfinite(value)) {
        error = std::string(context) + "." + key + " must be finite";
        return false;
    }
    return true;
}

bool readString(const Json& object, const char* key, const char* context, std::string& value, std::string& error) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        error = std::string(context) + "." + key + " must be a string";
        return false;
    }
    value = it->get<std::string>();
    return true;
}

} // namespace

bool parseDawnConfig(std::string_view json, DawnConfig& config, std::string& error) {
    Json document;
    try {
        document = Json::parse(json.begin(), json.end());
    } catch (const Json::parse_error& exception) {
        error = std::string("invalid JSON: ") + exception.what();
        return false;
    }
    if (!document.is_object()) {
        error = "dawn config must be an object";
        return false;
    }
    if (!rejectUnknownKeys(document, {"version", "lightmap", "lighting"}, "dawn config", error)) {
        return false;
    }

    DawnConfig parsed;
    if (!readInt(document, "version", "dawn config", parsed.version, error)) {
        return false;
    }
    if (parsed.version != 1) {
        error = "dawn config.version must be 1";
        return false;
    }

    const Json* lightmap = nullptr;
    if (!requireObject(document, "lightmap", "dawn config", &lightmap, error) ||
        !rejectUnknownKeys(*lightmap,
                           {"uv2VFlip", "texelsPerUnit", "maxLightmapSize", "atlasSize", "maxAtlases", "padding",
                            "dilationIterations", "directional", "format", "layers"},
                           "dawn config.lightmap", error)) {
        return false;
    }
    if (!readBool(*lightmap, "uv2VFlip", "dawn config.lightmap", parsed.lightmap.uv2VFlip, error) ||
        !readFloat(*lightmap, "texelsPerUnit", "dawn config.lightmap", parsed.lightmap.texelsPerUnit, error) ||
        !readUint(*lightmap, "maxLightmapSize", "dawn config.lightmap", parsed.lightmap.maxLightmapSize, error) ||
        !readUint(*lightmap, "atlasSize", "dawn config.lightmap", parsed.lightmap.atlasSize, error) ||
        !readUint(*lightmap, "maxAtlases", "dawn config.lightmap", parsed.lightmap.maxAtlases, error) ||
        !readUint(*lightmap, "padding", "dawn config.lightmap", parsed.lightmap.padding, error) ||
        !readUint(*lightmap, "dilationIterations", "dawn config.lightmap", parsed.lightmap.dilationIterations, error) ||
        !readBool(*lightmap, "directional", "dawn config.lightmap", parsed.lightmap.directional, error) ||
        !readString(*lightmap, "format", "dawn config.lightmap", parsed.lightmap.format, error) ||
        !readUint(*lightmap, "layers", "dawn config.lightmap", parsed.lightmap.layers, error)) {
        return false;
    }

    const Json* lighting = nullptr;
    if (!requireObject(document, "lighting", "dawn config", &lighting, error) ||
        !rejectUnknownKeys(*lighting,
                           {"colorSpace", "directionalLightmapScale", "lightUnitScale", "pointFalloff",
                            "lightPowerAffectsRadiance"},
                           "dawn config.lighting", error)) {
        return false;
    }
    if (!readString(*lighting, "colorSpace", "dawn config.lighting", parsed.lighting.colorSpace, error) ||
        !readFloat(*lighting, "directionalLightmapScale", "dawn config.lighting",
                   parsed.lighting.directionalLightmapScale, error) ||
        !readFloat(*lighting, "lightUnitScale", "dawn config.lighting", parsed.lighting.lightUnitScale, error) ||
        !readString(*lighting, "pointFalloff", "dawn config.lighting", parsed.lighting.pointFalloff, error) ||
        !readBool(*lighting, "lightPowerAffectsRadiance", "dawn config.lighting",
                  parsed.lighting.lightPowerAffectsRadiance, error)) {
        return false;
    }

    if (parsed.lightmap.texelsPerUnit <= 0.0f || parsed.lightmap.texelsPerUnit > 64.0f) {
        error = "dawn config.lightmap.texelsPerUnit must be in (0, 64]";
        return false;
    }
    if (parsed.lightmap.maxLightmapSize < 4 || parsed.lightmap.maxLightmapSize > 4096) {
        error = "dawn config.lightmap.maxLightmapSize must be in [4, 4096]";
        return false;
    }
    if (parsed.lightmap.atlasSize != 1024 && parsed.lightmap.atlasSize != 2048 &&
        parsed.lightmap.atlasSize != 4096) {
        error = "dawn config.lightmap.atlasSize must be 1024, 2048 or 4096";
        return false;
    }
    if (parsed.lightmap.maxLightmapSize > parsed.lightmap.atlasSize) {
        error = "dawn config.lightmap.maxLightmapSize must not exceed atlasSize";
        return false;
    }
    if (parsed.lightmap.maxAtlases == 0 || parsed.lightmap.maxAtlases > 16) {
        error = "dawn config.lightmap.maxAtlases must be in [1, 16]";
        return false;
    }
    if (parsed.lightmap.padding > 64) {
        error = "dawn config.lightmap.padding must not exceed 64";
        return false;
    }
    if (parsed.lightmap.dilationIterations > 32) {
        error = "dawn config.lightmap.dilationIterations must not exceed 32";
        return false;
    }
    if (parsed.lightmap.format != "rgba16f") {
        error = "dawn config.lightmap.format must be 'rgba16f'";
        return false;
    }
    if (parsed.lightmap.layers != 3) {
        error = "dawn config.lightmap.layers must be 3";
        return false;
    }
    if (parsed.lighting.colorSpace != "linear") {
        error = "dawn config.lighting.colorSpace must be 'linear'";
        return false;
    }
    if (parsed.lighting.directionalLightmapScale <= 0.0f || parsed.lighting.lightUnitScale <= 0.0f) {
        error = "dawn config.lighting scales must be positive";
        return false;
    }
    if (parsed.lighting.pointFalloff != "inverse-square") {
        error = "dawn config.lighting.pointFalloff must be 'inverse-square'";
        return false;
    }

    config = std::move(parsed);
    return true;
}

bool loadDawnConfig(const std::filesystem::path& path, DawnConfig& config, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "failed to open '" + path.string() + "'";
        return false;
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        error = "failed to read '" + path.string() + "'";
        return false;
    }
    if (!parseDawnConfig(contents.str(), config, error)) {
        error = path.string() + ": " + error;
        return false;
    }
    return true;
}

} // namespace hbg::dawn
