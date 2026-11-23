#include "render/shadows/shadow_settings.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>

namespace hbg::render {
namespace {

using Json = nlohmann::json;

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
    if (!it->is_array() || it->size() != 3) {
        error = std::string(key) + " must be an array of three numbers";
        return false;
    }
    for (const auto& component : *it) {
        if (!component.is_number()) {
            error = std::string(key) + " must be an array of three numbers";
            return false;
        }
    }
    value = Vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
    return true;
}

bool readFilter(const Json& object, const char* key, ShadowFilter& value, std::string& error) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return true;
    }
    if (!it->is_string() || !parseShadowFilter(it->get<std::string>(), value)) {
        error = std::string(key) + " must be off, pcf3x3, pcf5x5, pcf7x7, pcss, evsm, vsm or msm";
        return false;
    }
    return true;
}

bool readMicroShadows(const Json& object, bool& enabled, float& opacity, std::string& error) {
    const auto it = object.find("microShadows");
    if (it == object.end()) {
        return true;
    }
    if (!it->is_object()) {
        error = "microShadows must be an object";
        return false;
    }
    return readBool(*it, "enabled", enabled, error) && readFloat(*it, "opacity", opacity, error);
}

bool readContactShadows(const Json& object, ContactShadowSettings& contact, std::string& error) {
    const auto it = object.find("contactShadows");
    if (it == object.end()) {
        return true;
    }
    if (!it->is_object()) {
        error = "contactShadows must be an object";
        return false;
    }
    return readBool(*it, "enabled", contact.enabled, error) &&
           readBool(*it, "directionalOnly", contact.directionalOnly, error) &&
           readBool(*it, "halfResolution", contact.halfResolution, error) &&
           readFloat(*it, "length", contact.length, error) &&
           readFloat(*it, "opacity", contact.opacity, error) &&
           readFloat(*it, "distanceScaleFactor", contact.distanceScaleFactor, error) &&
           readFloat(*it, "minDistance", contact.minDistance, error) &&
           readFloat(*it, "maxDistance", contact.maxDistance, error) &&
           readFloat(*it, "fadeDistance", contact.fadeDistance, error) &&
           readFloat(*it, "fadeInDistance", contact.fadeInDistance, error) &&
           readFloat(*it, "rayBias", contact.rayBias, error) &&
           readFloat(*it, "thicknessScale", contact.thicknessScale, error) &&
           readUint(*it, "sampleCount", contact.sampleCount, error);
}

bool readSplits(const Json& object, std::array<float, kMaxDirectionalCascades>& splits, std::string& error) {
    const auto it = object.find("cascadeSplits");
    if (it == object.end()) {
        return true;
    }
    if (!it->is_array() || it->empty() || it->size() > kMaxDirectionalCascades) {
        error = "cascadeSplits must be an array of one to four numbers";
        return false;
    }
    for (std::size_t index = 0; index < it->size(); ++index) {
        if (!(*it)[index].is_number()) {
            error = "cascadeSplits must contain only numbers";
            return false;
        }
        splits[index] = (*it)[index].get<float>();
    }
    for (std::size_t index = it->size(); index < splits.size(); ++index) {
        splits[index] = 1.0f;
    }
    return true;
}

std::string normalize(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (char ch : text) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if (std::isalnum(value) != 0) {
            result.push_back(static_cast<char>(std::tolower(value)));
        }
    }
    return result;
}

float finiteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

uint32_t sanitizeResolution(uint32_t value, uint32_t fallback) {
    if (value < 256 || value > 4096) {
        return fallback;
    }
    return value;
}

} // namespace

const char* shadowFilterName(ShadowFilter filter) {
    switch (filter) {
    case ShadowFilter::Off:
        return "off";
    case ShadowFilter::Pcf3x3:
        return "pcf3x3";
    case ShadowFilter::Pcf5x5:
        return "pcf5x5";
    case ShadowFilter::Pcf7x7:
        return "pcf7x7";
    case ShadowFilter::Pcss:
        return "pcss";
    case ShadowFilter::Evsm:
        return "evsm";
    case ShadowFilter::Vsm:
        return "vsm";
    case ShadowFilter::Msm:
        return "msm";
    case ShadowFilter::Ims:
        return "ims";
    }
    return "pcf3x3";
}

bool parseShadowFilter(std::string_view text, ShadowFilter& filter) {
    const std::string key = normalize(text);
    if (key == "off" || key == "none") {
        filter = ShadowFilter::Off;
        return true;
    }
    if (key == "pcf3x3" || key == "low3x3") {
        filter = ShadowFilter::Pcf3x3;
        return true;
    }
    if (key == "pcf5x5" || key == "low") {
        filter = ShadowFilter::Pcf5x5;
        return true;
    }
    if (key == "pcf7x7" || key == "medium") {
        filter = ShadowFilter::Pcf7x7;
        return true;
    }
    if (key == "pcss" || key == "high") {
        filter = ShadowFilter::Pcss;
        return true;
    }
    if (key == "evsm") {
        filter = ShadowFilter::Evsm;
        return true;
    }
    if (key == "vsm") {
        filter = ShadowFilter::Vsm;
        return true;
    }
    if (key == "msm") {
        filter = ShadowFilter::Msm;
        return true;
    }
    if (key == "ims") {
        filter = ShadowFilter::Ims;
        return true;
    }
    return false;
}

uint32_t shadowFilterTapMode(ShadowFilter filter) {
    switch (filter) {
    case ShadowFilter::Off:
        return 0;
    case ShadowFilter::Pcf3x3:
        return 1;
    case ShadowFilter::Pcf5x5:
        return 2;
    case ShadowFilter::Pcf7x7:
        return 3;
    case ShadowFilter::Pcss:
        return 4;
    case ShadowFilter::Evsm:
        return 5;
    case ShadowFilter::Vsm:
        return 6;
    case ShadowFilter::Msm:
        return 7;
    case ShadowFilter::Ims:
        return 8;
    }
    return 1;
}

const char* shadowUpdateModeName(ShadowUpdateMode mode) {
    switch (mode) {
    case ShadowUpdateMode::EveryFrame:
        return "everyFrame";
    case ShadowUpdateMode::OnEnable:
        return "onEnable";
    case ShadowUpdateMode::OnDemand:
        return "onDemand";
    }
    return "everyFrame";
}

bool parseShadowUpdateMode(std::string_view text, ShadowUpdateMode& mode) {
    const std::string key = normalize(text);
    if (key == "everyframe" || key == "dynamic") {
        mode = ShadowUpdateMode::EveryFrame;
        return true;
    }
    if (key == "onenable" || key == "static") {
        mode = ShadowUpdateMode::OnEnable;
        return true;
    }
    if (key == "ondemand" || key == "manual") {
        mode = ShadowUpdateMode::OnDemand;
        return true;
    }
    return false;
}

void sanitizeLightShadowSettings(LightShadowSettings& settings) {
    settings.dimmer = std::clamp(finiteOr(settings.dimmer, 1.0f), 0.0f, 1.0f);
    settings.fadeDistance = std::clamp(finiteOr(settings.fadeDistance, 10000.0f), 0.0f, 100000.0f);
    settings.nearPlane = std::clamp(finiteOr(settings.nearPlane, 0.1f), 0.01f, 100.0f);
    if (settings.resolution != 0 && settings.resolution != 256 && settings.resolution != 512 &&
        settings.resolution != 1024 && settings.resolution != 2048) {
        settings.resolution = 512;
    }
    settings.tint.x = std::clamp(finiteOr(settings.tint.x, 0.0f), 0.0f, 1.0f);
    settings.tint.y = std::clamp(finiteOr(settings.tint.y, 0.0f), 0.0f, 1.0f);
    settings.tint.z = std::clamp(finiteOr(settings.tint.z, 0.0f), 0.0f, 1.0f);
    settings.softness = std::clamp(finiteOr(settings.softness, 0.5f), 0.0f, 4.0f);
    settings.blockerSamples = std::clamp(settings.blockerSamples, 1u, 64u);
    settings.filterSamples = std::clamp(settings.filterSamples, 1u, 64u);
    settings.evsmExponent =
        std::clamp(finiteOr(settings.evsmExponent, 15.0f), kEvsmMinExponent, kEvsmMaxExponent);
    settings.evsmLightLeakBias =
        std::clamp(finiteOr(settings.evsmLightLeakBias, 0.0f), kEvsmMinLeakBias, kEvsmMaxLeakBias);
    settings.evsmVarianceBias = std::clamp(finiteOr(settings.evsmVarianceBias, 1.0e-5f),
                                           kEvsmMinVarianceBias, kEvsmMaxVarianceBias);
    settings.evsmBlurPasses =
        std::clamp(settings.evsmBlurPasses, kEvsmMinBlurPasses, kEvsmMaxBlurPasses);
}

LightShadowSettings resolveLightShadow(const Light& light, const ShadowSettings& settings) {
    if (light.shadowConfigured) {
        LightShadowSettings resolved = light.shadow;
        sanitizeLightShadowSettings(resolved);
        return resolved;
    }

    LightShadowSettings resolved;
    resolved.dimmer = settings.shadowDimmer;
    resolved.fadeDistance = settings.shadowFadeDistance;
    resolved.nearPlane = settings.shadowNearPlane;
    resolved.resolution = 0;
    resolved.tint = settings.shadowTint;
    resolved.penumbraTint = false;
    resolved.updateMode = ShadowUpdateMode::EveryFrame;
    resolved.contactShadows = true;
    resolved.softness = settings.softness;
    resolved.blockerSamples = settings.blockerSamples;
    resolved.filterSamples = settings.filterSamples;
    sanitizeLightShadowSettings(resolved);
    return resolved;
}

void sanitizeShadowSettings(ShadowSettings& settings) {
    settings.maxDistance = std::clamp(finiteOr(settings.maxDistance, 200.0f), 0.01f, 10000.0f);
    settings.cascadeCount = std::clamp(settings.cascadeCount, 1u, kMaxDirectionalCascades);
    settings.cascadeFade = std::clamp(finiteOr(settings.cascadeFade, 0.10f), 0.0f, 1.0f);

    const std::array<float, kMaxDirectionalCascades> fallback{0.10f, 0.25f, 0.50f, 1.0f};
    bool splitsValid = true;
    for (uint32_t index = 0; index < settings.cascadeCount; ++index) {
        const float value = settings.cascadeSplits[index];
        if (!std::isfinite(value) || value <= 0.0f || value > 1.0f) {
            splitsValid = false;
            break;
        }
        if (index > 0 && value <= settings.cascadeSplits[index - 1]) {
            splitsValid = false;
            break;
        }
    }
    if (!splitsValid) {
        settings.cascadeSplits = fallback;
    } else {
        settings.cascadeSplits[settings.cascadeCount - 1] = 1.0f;
    }
    for (uint32_t index = settings.cascadeCount; index < kMaxDirectionalCascades; ++index) {
        settings.cascadeSplits[index] = 1.0f;
    }

    settings.directionalResolution = sanitizeResolution(settings.directionalResolution, 1024);
    settings.pointResolution = sanitizeResolution(settings.pointResolution, 256);
    settings.spotResolution = sanitizeResolution(settings.spotResolution, 512);
    settings.areaResolution = sanitizeResolution(settings.areaResolution, kDefaultAreaShadowResolution);
    if (settings.atlasSize != 1024 && settings.atlasSize != 2048 && settings.atlasSize != 4096) {
        settings.atlasSize = kDefaultShadowAtlasSize;
    }
    if (settings.areaAtlasSize != 1024 && settings.areaAtlasSize != 2048 &&
        settings.areaAtlasSize != 4096) {
        settings.areaAtlasSize = kDefaultAreaShadowAtlasSize;
    }
    settings.maxRequests = std::clamp(settings.maxRequests, 1u, kMaxShadowRequests);
    settings.normalBias = std::clamp(finiteOr(settings.normalBias, 0.2f), 0.0f, 2.0f);
    settings.slopeBias = std::clamp(finiteOr(settings.slopeBias, 0.5f), 0.0f, 10.0f);
    settings.shadowNearPlane = std::clamp(finiteOr(settings.shadowNearPlane, 0.1f), 0.01f, 100.0f);
    settings.directionalLightAngle =
        std::clamp(finiteOr(settings.directionalLightAngle, 1.0f), 0.0f, 9.0f);
    settings.directionalMaxDepthBias =
        std::clamp(finiteOr(settings.directionalMaxDepthBias, 0.001f), 0.0001f, 0.01f);
    settings.imsKernelSize = std::clamp(settings.imsKernelSize, 1u, 32u);
    settings.softness = std::clamp(finiteOr(settings.softness, 0.5f), 0.0f, 4.0f);
    if (settings.punctualFilter == ShadowFilter::Ims) {
        settings.punctualFilter = ShadowFilter::Pcf5x5;
    }
    if (settings.areaFilter == ShadowFilter::Ims) {
        settings.areaFilter = ShadowFilter::Pcf5x5;
    }
    settings.blockerSamples = std::clamp(settings.blockerSamples, 1u, 64u);
    settings.filterSamples = std::clamp(settings.filterSamples, 1u, 64u);
    settings.minFilterSize = std::clamp(finiteOr(settings.minFilterSize, 0.01f), 0.0f, 1.0f);
    settings.microShadowOpacity = std::clamp(finiteOr(settings.microShadowOpacity, 1.0f), 0.0f, 1.0f);
    settings.shadowDimmer = std::clamp(finiteOr(settings.shadowDimmer, 1.0f), 0.0f, 1.0f);
    settings.shadowFadeDistance =
        std::clamp(finiteOr(settings.shadowFadeDistance, 10000.0f), 0.0f, 100000.0f);
    settings.shadowTint.x = std::clamp(finiteOr(settings.shadowTint.x, 0.0f), 0.0f, 1.0f);
    settings.shadowTint.y = std::clamp(finiteOr(settings.shadowTint.y, 0.0f), 0.0f, 1.0f);
    settings.shadowTint.z = std::clamp(finiteOr(settings.shadowTint.z, 0.0f), 0.0f, 1.0f);
    sanitizeContactShadowSettings(settings.contactShadows);
}

void computeShadowFade(float distance, float border, float& scale, float& bias) {
    const float distanceSq = distance * distance;
    if (border < 0.0001f) {
        constexpr float multiplier = 1000.0f;
        scale = multiplier;
        bias = -distanceSq * multiplier;
        return;
    }
    const float fadeBorder = 1.0f - border;
    const float nearSq = fadeBorder * fadeBorder * distanceSq;
    const float denominator = std::max(distanceSq - nearSq, 1.0e-6f);
    scale = 1.0f / denominator;
    bias = -nearSq * scale;
}

bool parseShadowSettings(std::string_view json, ShadowSettings& settings, std::string& error) {
    Json root;
    try {
        root = Json::parse(json.begin(), json.end());
    } catch (const std::exception& exception) {
        error = std::string("failed to parse shadow settings: ") + exception.what();
        return false;
    }
    if (!root.is_object()) {
        error = "shadow settings must be a JSON object";
        return false;
    }

    settings = ShadowSettings{};
    if (!readFloat(root, "maxDistance", settings.maxDistance, error) ||
        !readUint(root, "cascadeCount", settings.cascadeCount, error) ||
        !readSplits(root, settings.cascadeSplits, error) ||
        !readFloat(root, "cascadeFade", settings.cascadeFade, error) ||
        !readFilter(root, "directionalFilter", settings.directionalFilter, error) ||
        !readFilter(root, "punctualFilter", settings.punctualFilter, error) ||
        !readFilter(root, "areaFilter", settings.areaFilter, error) ||
        !readUint(root, "directionalResolution", settings.directionalResolution, error) ||
        !readUint(root, "pointResolution", settings.pointResolution, error) ||
        !readUint(root, "spotResolution", settings.spotResolution, error) ||
        !readUint(root, "areaResolution", settings.areaResolution, error) ||
        !readUint(root, "atlasSize", settings.atlasSize, error) ||
        !readUint(root, "areaAtlasSize", settings.areaAtlasSize, error) ||
        !readUint(root, "maxRequests", settings.maxRequests, error) ||
        !readFloat(root, "normalBias", settings.normalBias, error) ||
        !readFloat(root, "slopeBias", settings.slopeBias, error) ||
        !readFloat(root, "shadowNearPlane", settings.shadowNearPlane, error) ||
        !readFloat(root, "directionalLightAngle", settings.directionalLightAngle, error) ||
        !readFloat(root, "directionalMaxDepthBias", settings.directionalMaxDepthBias, error) ||
        !readUint(root, "imsKernelSize", settings.imsKernelSize, error) ||
        !readFloat(root, "softness", settings.softness, error) ||
        !readUint(root, "blockerSamples", settings.blockerSamples, error) ||
        !readUint(root, "filterSamples", settings.filterSamples, error) ||
        !readFloat(root, "minFilterSize", settings.minFilterSize, error) ||
        !readMicroShadows(root, settings.microShadows, settings.microShadowOpacity, error) ||
        !readContactShadows(root, settings.contactShadows, error) ||
        !readFloat(root, "shadowDimmer", settings.shadowDimmer, error) ||
        !readFloat(root, "shadowFadeDistance", settings.shadowFadeDistance, error) ||
        !readVec3(root, "shadowTint", settings.shadowTint, error) ||
        !readBool(root, "cachedDirectionalShadows", settings.cachedDirectionalShadows, error) ||
        !readBool(root, "cachedDirectionalMixedShadows", settings.cachedDirectionalMixedShadows, error) ||
        !readBool(root, "cachedPunctualShadows", settings.cachedPunctualShadows, error) ||
        !readBool(root, "cachedAreaShadows", settings.cachedAreaShadows, error)) {
        return false;
    }

    sanitizeShadowSettings(settings);
    return true;
}

bool loadShadowSettings(const std::string& path, ShadowSettings& settings, std::string& error) {
    std::ifstream file(path);
    if (!file.is_open()) {
        error = "failed to open shadow settings '" + path + "'";
        return false;
    }
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return parseShadowSettings(json, settings, error);
}

std::string shadowSettingsToJson(const ShadowSettings& settings) {
    Json root;
    root["maxDistance"] = settings.maxDistance;
    root["cascadeCount"] = settings.cascadeCount;
    root["cascadeSplits"] = settings.cascadeSplits;
    root["cascadeFade"] = settings.cascadeFade;
    root["directionalFilter"] = shadowFilterName(settings.directionalFilter);
    root["punctualFilter"] = shadowFilterName(settings.punctualFilter);
    root["areaFilter"] = shadowFilterName(settings.areaFilter);
    root["directionalResolution"] = settings.directionalResolution;
    root["pointResolution"] = settings.pointResolution;
    root["spotResolution"] = settings.spotResolution;
    root["areaResolution"] = settings.areaResolution;
    root["atlasSize"] = settings.atlasSize;
    root["areaAtlasSize"] = settings.areaAtlasSize;
    root["maxRequests"] = settings.maxRequests;
    root["normalBias"] = settings.normalBias;
    root["slopeBias"] = settings.slopeBias;
    root["shadowNearPlane"] = settings.shadowNearPlane;
    root["directionalLightAngle"] = settings.directionalLightAngle;
    root["directionalMaxDepthBias"] = settings.directionalMaxDepthBias;
    root["imsKernelSize"] = settings.imsKernelSize;
    root["softness"] = settings.softness;
    root["blockerSamples"] = settings.blockerSamples;
    root["filterSamples"] = settings.filterSamples;
    root["minFilterSize"] = settings.minFilterSize;
    root["microShadows"] = {{"enabled", settings.microShadows}, {"opacity", settings.microShadowOpacity}};
    root["contactShadows"] = {
        {"enabled", settings.contactShadows.enabled},
        {"directionalOnly", settings.contactShadows.directionalOnly},
        {"halfResolution", settings.contactShadows.halfResolution},
        {"length", settings.contactShadows.length},
        {"opacity", settings.contactShadows.opacity},
        {"distanceScaleFactor", settings.contactShadows.distanceScaleFactor},
        {"minDistance", settings.contactShadows.minDistance},
        {"maxDistance", settings.contactShadows.maxDistance},
        {"fadeDistance", settings.contactShadows.fadeDistance},
        {"fadeInDistance", settings.contactShadows.fadeInDistance},
        {"rayBias", settings.contactShadows.rayBias},
        {"thicknessScale", settings.contactShadows.thicknessScale},
        {"sampleCount", settings.contactShadows.sampleCount},
    };
    root["shadowDimmer"] = settings.shadowDimmer;
    root["shadowFadeDistance"] = settings.shadowFadeDistance;
    root["shadowTint"] = {settings.shadowTint.x, settings.shadowTint.y, settings.shadowTint.z};
    root["cachedDirectionalShadows"] = settings.cachedDirectionalShadows;
    root["cachedDirectionalMixedShadows"] = settings.cachedDirectionalMixedShadows;
    root["cachedPunctualShadows"] = settings.cachedPunctualShadows;
    root["cachedAreaShadows"] = settings.cachedAreaShadows;
    return root.dump(2);
}

} // namespace hbg::render
