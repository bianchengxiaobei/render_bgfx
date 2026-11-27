#include "render/atmosphere/volumetric_fog_settings.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <iterator>

namespace hbg::render {
namespace {

using Json = nlohmann::json;

constexpr float kDefaultMeanFreePath = 400.0f;
constexpr float kDefaultBaseHeight = 0.0f;
constexpr float kDefaultMaximumHeight = 50.0f;
constexpr float kDefaultAnisotropy = 0.0f;
constexpr float kDefaultDepthExtent = 64.0f;
constexpr float kDefaultGlobalLightProbeDimmer = 1.0f;
constexpr float kDefaultResolutionScale = 0.5f;
constexpr float kDefaultTemporalHistoryWeight = 0.85f;
constexpr uint32_t kDefaultMaxPunctualLights = 8;

float finiteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

bool readBool(const Json& object, const char* key, bool& value, std::string& error) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return true;
    }
    if (!it->is_boolean()) {
        error = std::string(key) + " must be a boolean";
        return false;
    }
    value = it->get<bool>();
    return true;
}

bool readFloat(const Json& object, const char* key, float& value, std::string& error) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return true;
    }
    if (!it->is_number()) {
        error = std::string(key) + " must be a number";
        return false;
    }
    value = it->get<float>();
    return true;
}

bool readUint(const Json& object, const char* key, uint32_t& value, std::string& error) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return true;
    }
    if (!it->is_number_integer()) {
        error = std::string(key) + " must be an integer";
        return false;
    }
    const int parsed = it->get<int>();
    if (parsed < 0) {
        error = std::string(key) + " must be non-negative";
        return false;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool readVec3(const Json& object, const char* key, Vec3& value, std::string& error) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return true;
    }
    if (!it->is_array() || it->size() != 3 || !(*it)[0].is_number() || !(*it)[1].is_number() ||
        !(*it)[2].is_number()) {
        error = std::string(key) + " must be an array of three numbers";
        return false;
    }
    value = Vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
    return true;
}

} // namespace

bool VolumetricFogSettings::active() const {
    return enabled && std::isfinite(meanFreePath) && meanFreePath > 0.0f && std::isfinite(depthExtent) &&
           depthExtent > 0.0f && stepCount > 0 && std::isfinite(anisotropy);
}

float VolumetricFogSettings::scaleHeight() const {
    const float layerDepth = std::max(maximumHeight - baseHeight, 0.01f);
    return layerDepth * 0.144765f;
}

float VolumetricFogSettings::extinction() const {
    return 1.0f / std::max(meanFreePath, 1.0f);
}

Vec3 VolumetricFogSettings::scattering() const {
    return albedo * extinction();
}

void sanitizeVolumetricFogSettings(VolumetricFogSettings& settings) {
    settings.meanFreePath = std::clamp(finiteOr(settings.meanFreePath, kDefaultMeanFreePath), 1.0f, 1000000.0f);
    settings.albedo = Vec3(std::clamp(finiteOr(settings.albedo.x, 1.0f), 0.0f, 4.0f),
                           std::clamp(finiteOr(settings.albedo.y, 1.0f), 0.0f, 4.0f),
                           std::clamp(finiteOr(settings.albedo.z, 1.0f), 0.0f, 4.0f));
    settings.baseHeight = std::clamp(finiteOr(settings.baseHeight, kDefaultBaseHeight), -10000.0f, 10000.0f);
    settings.maximumHeight = std::clamp(finiteOr(settings.maximumHeight, kDefaultMaximumHeight),
                                        settings.baseHeight + 0.01f, 20000.0f);
    settings.anisotropy = std::clamp(finiteOr(settings.anisotropy, kDefaultAnisotropy), -0.99f, 0.99f);
    settings.depthExtent = std::clamp(finiteOr(settings.depthExtent, kDefaultDepthExtent), 0.1f, 5000.0f);
    settings.stepCount = std::clamp(settings.stepCount, 4u, 64u);
    settings.globalLightProbeDimmer =
        std::clamp(finiteOr(settings.globalLightProbeDimmer, kDefaultGlobalLightProbeDimmer), 0.0f, 2.0f);
    settings.resolutionScale =
        std::clamp(finiteOr(settings.resolutionScale, kDefaultResolutionScale), 0.25f, 1.0f);
    settings.temporalHistoryWeight = std::clamp(
        finiteOr(settings.temporalHistoryWeight, kDefaultTemporalHistoryWeight), 0.0f, 0.95f);
    settings.maxPunctualLights = std::clamp(settings.maxPunctualLights, 0u, 16u);
}

bool parseVolumetricFogSettings(std::string_view json, VolumetricFogSettings& settings, std::string& error) {
    Json root;
    try {
        root = Json::parse(json.begin(), json.end());
    } catch (const std::exception& exception) {
        error = std::string("failed to parse volumetric fog settings: ") + exception.what();
        return false;
    }
    if (!root.is_object()) {
        error = "volumetric fog settings must be a JSON object";
        return false;
    }

    if (!readBool(root, "enabled", settings.enabled, error) ||
        !readFloat(root, "meanFreePath", settings.meanFreePath, error) ||
        !readVec3(root, "albedo", settings.albedo, error) ||
        !readFloat(root, "baseHeight", settings.baseHeight, error) ||
        !readFloat(root, "maximumHeight", settings.maximumHeight, error) ||
        !readFloat(root, "anisotropy", settings.anisotropy, error) ||
        !readFloat(root, "depthExtent", settings.depthExtent, error) ||
        !readUint(root, "stepCount", settings.stepCount, error) ||
        !readBool(root, "directionalShadows", settings.directionalShadows, error) ||
        !readFloat(root, "globalLightProbeDimmer", settings.globalLightProbeDimmer, error) ||
        !readFloat(root, "resolutionScale", settings.resolutionScale, error) ||
        !readBool(root, "temporal", settings.temporal, error) ||
        !readFloat(root, "temporalHistoryWeight", settings.temporalHistoryWeight, error) ||
        !readBool(root, "temporalJitter", settings.temporalJitter, error) ||
        !readBool(root, "punctualLights", settings.punctualLights, error) ||
        !readBool(root, "punctualShadows", settings.punctualShadows, error) ||
        !readUint(root, "maxPunctualLights", settings.maxPunctualLights, error) ||
        !readBool(root, "localVolumes", settings.localVolumes, error)) {
        return false;
    }

    sanitizeVolumetricFogSettings(settings);
    return true;
}

bool loadVolumetricFogSettings(const std::string& path, VolumetricFogSettings& settings, std::string& error) {
    std::ifstream file(path);
    if (!file.is_open()) {
        error = "failed to open volumetric fog settings '" + path + "'";
        return false;
    }

    std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return parseVolumetricFogSettings(json, settings, error);
}

} // namespace hbg::render
