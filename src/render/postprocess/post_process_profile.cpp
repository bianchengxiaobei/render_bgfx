#include "render/postprocess/post_process_profile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>

namespace hbg::render {
namespace {

using Json = nlohmann::json;

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

bool checkKeys(const Json& object, std::initializer_list<const char*> allowed, std::string& error,
               const char* group) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        const auto found = std::find_if(allowed.begin(), allowed.end(),
                                        [&it](const char* key) { return it.key() == key; });
        if (found == allowed.end()) {
            error = std::string(group) + " has unknown key '" + it.key() + "'";
            return false;
        }
    }
    return true;
}

bool readGroup(const Json& parent, const char* key, Json& object, std::string& error) {
    const auto it = parent.find(key);
    if (it == parent.end()) {
        object = Json::object();
        return true;
    }
    if (!it->is_object()) {
        error = std::string(key) + " must be an object";
        return false;
    }
    object = *it;
    return true;
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

bool readString(const Json& object, const char* key, std::string& value, std::string& error) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return true;
    }
    if (!it->is_string()) {
        error = std::string(key) + " must be a string";
        return false;
    }
    value = it->get<std::string>();
    return true;
}

bool readVec2(const Json& object, const char* key, Vec2& value, std::string& error) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return true;
    }
    if (!it->is_array() || it->size() != 2) {
        error = std::string(key) + " must be an array of two numbers";
        return false;
    }
    if (!(*it)[0].is_number() || !(*it)[1].is_number()) {
        error = std::string(key) + " must be an array of two numbers";
        return false;
    }
    value = Vec2((*it)[0].get<float>(), (*it)[1].get<float>());
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
    if (!(*it)[0].is_number() || !(*it)[1].is_number() || !(*it)[2].is_number()) {
        error = std::string(key) + " must be an array of three numbers";
        return false;
    }
    value = Vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
    return true;
}

bool readVec4(const Json& object, const char* key, Vec4& value, std::string& error) {
    const auto it = object.find(key);
    if (it == object.end()) {
        return true;
    }
    if (!it->is_array() || it->size() != 4) {
        error = std::string(key) + " must be an array of four numbers";
        return false;
    }
    if (!(*it)[0].is_number() || !(*it)[1].is_number() || !(*it)[2].is_number() || !(*it)[3].is_number()) {
        error = std::string(key) + " must be an array of four numbers";
        return false;
    }
    value = Vec4((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>(), (*it)[3].get<float>());
    return true;
}

bool parseExposureMode(std::string_view text, ExposureMode& mode) {
    const std::string key = normalize(text);
    if (key == "fixed") {
        mode = ExposureMode::Fixed;
        return true;
    }
    if (key == "automatic" || key == "auto") {
        mode = ExposureMode::Automatic;
        return true;
    }
    if (key == "histogram" || key == "automatichistogram") {
        mode = ExposureMode::Histogram;
        return true;
    }
    return false;
}

bool parseTonemappingMode(std::string_view text, TonemappingMode& mode) {
    const std::string key = normalize(text);
    if (key == "none" || key == "off") {
        mode = TonemappingMode::None;
        return true;
    }
    if (key == "neutral") {
        mode = TonemappingMode::Neutral;
        return true;
    }
    if (key == "aces") {
        mode = TonemappingMode::ACES;
        return true;
    }
    return false;
}

bool parseMeteringMode(std::string_view text, MeteringMode& mode) {
    const std::string key = normalize(text);
    if (key == "average" || key == "avg") {
        mode = MeteringMode::Average;
        return true;
    }
    if (key == "centerweighted" || key == "center") {
        mode = MeteringMode::CenterWeighted;
        return true;
    }
    return false;
}

bool readExposure(const Json& root, PostProcessSettings& settings, std::string& error) {
    Json group;
    if (!readGroup(root, "exposure", group, error)) {
        return false;
    }
    if (!checkKeys(group,
                   {"mode", "fixedEv100", "compensation", "limitMin", "limitMax", "debugExposure", "metering",
                    "adaptationSpeedDarkToLight", "adaptationSpeedLightToDark", "histogramMinPercentile",
                    "histogramMaxPercentile", "proceduralCenter", "proceduralRadii", "proceduralSoftness"},
                   error, "exposure")) {
        return false;
    }

    std::string mode;
    if (!readString(group, "mode", mode, error)) {
        return false;
    }
    if (!mode.empty() && !parseExposureMode(mode, settings.exposureMode)) {
        error = "unknown exposure mode '" + mode + "'";
        return false;
    }
    std::string metering;
    if (!readString(group, "metering", metering, error)) {
        return false;
    }
    if (!metering.empty() && !parseMeteringMode(metering, settings.metering)) {
        error = "unknown exposure metering '" + metering + "'";
        return false;
    }

    return readFloat(group, "fixedEv100", settings.fixedEv100, error) &&
           readFloat(group, "compensation", settings.compensation, error) &&
           readFloat(group, "limitMin", settings.limitMin, error) &&
           readFloat(group, "limitMax", settings.limitMax, error) &&
           readFloat(group, "debugExposure", settings.debugExposure, error) &&
           readFloat(group, "adaptationSpeedDarkToLight", settings.adaptationSpeedDarkToLight, error) &&
           readFloat(group, "adaptationSpeedLightToDark", settings.adaptationSpeedLightToDark, error) &&
           readFloat(group, "histogramMinPercentile", settings.histogramMinPercentile, error) &&
           readFloat(group, "histogramMaxPercentile", settings.histogramMaxPercentile, error) &&
           readVec3(group, "proceduralCenter", settings.proceduralCenter, error) &&
           readVec3(group, "proceduralRadii", settings.proceduralRadii, error) &&
           readFloat(group, "proceduralSoftness", settings.proceduralSoftness, error);
}

bool readTaa(const Json& root, TaaSettings& taa, std::string& error) {
    Json group;
    if (!readGroup(root, "taa", group, error)) {
        return false;
    }
    if (!checkKeys(group,
                   {"enabled", "upscale", "injection", "quality", "motionVectorRejection", "velocityRejection",
                    "sharpening", "jitterScale"},
                   error, "taa")) {
        return false;
    }
    std::string injection;
    if (!readString(group, "injection", injection, error)) {
        return false;
    }
    if (!injection.empty() && !parseTaaUpsampleInjection(injection, taa.upscaleInjection)) {
        error = "taa.injection must be beforePost, afterDepthOfField or afterPost";
        return false;
    }
    return readBool(group, "enabled", taa.enabled, error) && readBool(group, "upscale", taa.upscale, error) &&
           readUint(group, "quality", taa.quality, error) &&
           readFloat(group, "motionVectorRejection", taa.motionVectorRejection, error) &&
           readFloat(group, "velocityRejection", taa.velocityRejection, error) &&
           readFloat(group, "sharpening", taa.sharpening, error) &&
           readFloat(group, "jitterScale", taa.jitterScale, error);
}

bool readBloom(const Json& root, BloomSettings& bloom, std::string& error) {
    Json group;
    if (!readGroup(root, "bloom", group, error)) {
        return false;
    }
    if (!checkKeys(group,
                   {"enabled", "threshold", "intensity", "scatter", "tint", "highQuality",
                    "highQualityPrefiltering", "highQualityFiltering", "resolution"},
                   error, "bloom")) {
        return false;
    }
    bool legacyHighQuality = false;
    const bool hasLegacyHighQuality = group.contains("highQuality");
    if (!readBool(group, "enabled", bloom.enabled, error) ||
        !readFloat(group, "threshold", bloom.threshold, error) ||
        !readFloat(group, "intensity", bloom.intensity, error) ||
        !readFloat(group, "scatter", bloom.scatter, error) || !readVec3(group, "tint", bloom.tint, error) ||
        !readUint(group, "resolution", bloom.resolution, error)) {
        return false;
    }
    if (hasLegacyHighQuality && !readBool(group, "highQuality", legacyHighQuality, error)) {
        return false;
    }
    if (group.contains("highQualityPrefiltering")) {
        if (!readBool(group, "highQualityPrefiltering", bloom.highQualityPrefiltering, error)) {
            return false;
        }
    } else if (hasLegacyHighQuality) {
        bloom.highQualityPrefiltering = legacyHighQuality;
    }
    if (group.contains("highQualityFiltering")) {
        if (!readBool(group, "highQualityFiltering", bloom.highQualityFiltering, error)) {
            return false;
        }
    } else if (hasLegacyHighQuality) {
        bloom.highQualityFiltering = legacyHighQuality;
    }
    return true;
}

bool readColorGrading(const Json& root, ColorGradingSettings& grading, std::string& error) {
    Json group;
    if (!readGroup(root, "colorGrading", group, error)) {
        return false;
    }
    if (!checkKeys(group,
                   {"enabled", "postExposure", "contrast", "colorFilter", "hueShift", "saturation", "temperature",
                    "tint", "channelMixerRed", "channelMixerGreen", "channelMixerBlue", "lift", "gamma", "gain",
                    "shadows", "midtones", "highlights", "shadowsMidtonesHighlightsLimits", "splitShadows",
                    "splitHighlights", "splitBalance"},
                   error, "colorGrading")) {
        return false;
    }
    return readBool(group, "enabled", grading.enabled, error) &&
           readFloat(group, "postExposure", grading.postExposure, error) &&
           readFloat(group, "contrast", grading.contrast, error) &&
           readVec3(group, "colorFilter", grading.colorFilter, error) &&
           readFloat(group, "hueShift", grading.hueShift, error) &&
           readFloat(group, "saturation", grading.saturation, error) &&
           readFloat(group, "temperature", grading.temperature, error) &&
           readFloat(group, "tint", grading.tint, error) &&
           readVec3(group, "channelMixerRed", grading.channelMixerRed, error) &&
           readVec3(group, "channelMixerGreen", grading.channelMixerGreen, error) &&
           readVec3(group, "channelMixerBlue", grading.channelMixerBlue, error) &&
           readVec3(group, "lift", grading.lift, error) && readVec3(group, "gamma", grading.gamma, error) &&
           readVec3(group, "gain", grading.gain, error) && readVec3(group, "shadows", grading.shadows, error) &&
           readVec3(group, "midtones", grading.midtones, error) &&
           readVec3(group, "highlights", grading.highlights, error) &&
           readVec4(group, "shadowsMidtonesHighlightsLimits", grading.shadowsMidtonesHighlightsLimits, error) &&
           readVec3(group, "splitShadows", grading.splitShadows, error) &&
           readVec3(group, "splitHighlights", grading.splitHighlights, error) &&
           readFloat(group, "splitBalance", grading.splitBalance, error);
}

bool readVignette(const Json& root, VignetteSettings& vignette, std::string& error) {
    Json group;
    if (!readGroup(root, "vignette", group, error)) {
        return false;
    }
    if (!checkKeys(group, {"center", "intensity", "smoothness", "roundness", "rounded", "color", "opacity"}, error,
                   "vignette")) {
        return false;
    }
    return readVec2(group, "center", vignette.center, error) &&
           readFloat(group, "intensity", vignette.intensity, error) &&
           readFloat(group, "smoothness", vignette.smoothness, error) &&
           readFloat(group, "roundness", vignette.roundness, error) &&
           readBool(group, "rounded", vignette.rounded, error) && readVec3(group, "color", vignette.color, error) &&
           readFloat(group, "opacity", vignette.opacity, error);
}

bool readDepthOfField(const Json& root, DepthOfFieldSettings& dof, std::string& error) {
    Json group;
    if (!readGroup(root, "depthOfField", group, error)) {
        return false;
    }
    if (!checkKeys(group,
                   {"enabled", "focusDistance", "nearFocusStart", "nearFocusEnd", "farFocusStart", "farFocusEnd",
                    "nearMaxBlur", "farMaxBlur", "resolution", "highQuality"},
                   error, "depthOfField")) {
        return false;
    }
    return readBool(group, "enabled", dof.enabled, error) &&
           readFloat(group, "focusDistance", dof.focusDistance, error) &&
           readFloat(group, "nearFocusStart", dof.nearFocusStart, error) &&
           readFloat(group, "nearFocusEnd", dof.nearFocusEnd, error) &&
           readFloat(group, "farFocusStart", dof.farFocusStart, error) &&
           readFloat(group, "farFocusEnd", dof.farFocusEnd, error) &&
           readFloat(group, "nearMaxBlur", dof.nearMaxBlur, error) &&
           readFloat(group, "farMaxBlur", dof.farMaxBlur, error) &&
           readUint(group, "resolution", dof.resolution, error) &&
           readBool(group, "highQuality", dof.highQuality, error);
}

bool readMotionBlur(const Json& root, MotionBlurSettings& motion, std::string& error) {
    Json group;
    if (!readGroup(root, "motionBlur", group, error)) {
        return false;
    }
    if (!checkKeys(group,
                   {"enabled", "intensity", "maximumVelocity", "minimumVelocity", "cameraMotionBlur",
                    "sampleCount", "depthComparisonExtent"},
                   error, "motionBlur")) {
        return false;
    }
    return readBool(group, "enabled", motion.enabled, error) &&
           readFloat(group, "intensity", motion.intensity, error) &&
           readFloat(group, "maximumVelocity", motion.maximumVelocity, error) &&
           readFloat(group, "minimumVelocity", motion.minimumVelocity, error) &&
           readBool(group, "cameraMotionBlur", motion.cameraMotionBlur, error) &&
           readUint(group, "sampleCount", motion.sampleCount, error) &&
           readFloat(group, "depthComparisonExtent", motion.depthComparisonExtent, error);
}

bool readFxaa(const Json& root, FxaaSettings& fxaa, std::string& error) {
    Json group;
    if (!readGroup(root, "fxaa", group, error)) {
        return false;
    }
    if (!checkKeys(group, {"enabled"}, error, "fxaa")) {
        return false;
    }
    return readBool(group, "enabled", fxaa.enabled, error);
}

} // namespace

PostProcessProfile baselinePostProcessProfile() {
    PostProcessProfile profile;
    profile.name = "baseline";
    return profile;
}

bool parsePostProcessProfile(std::string_view text, PostProcessProfile& profile, std::string& error) {
    profile = baselinePostProcessProfile();
    error.clear();

    Json root;
    try {
        root = Json::parse(text.begin(), text.end());
    } catch (const Json::exception& exception) {
        error = exception.what();
        return false;
    }
    if (!root.is_object()) {
        error = "post-process profile must be a JSON object";
        return false;
    }
    if (!checkKeys(root,
                   {"name", "exposure", "tonemapping", "taa", "bloom", "colorGrading", "vignette", "depthOfField",
                    "motionBlur", "fxaa"},
                   error, "post-process profile")) {
        return false;
    }

    std::string name;
    if (!readString(root, "name", name, error)) {
        return false;
    }
    if (!name.empty()) {
        profile.name = name;
    }

    std::string tonemapping;
    if (!readString(root, "tonemapping", tonemapping, error)) {
        return false;
    }
    if (!tonemapping.empty() && !parseTonemappingMode(tonemapping, profile.settings.tonemapping)) {
        error = "unknown tonemapping mode '" + tonemapping + "'";
        return false;
    }

    if (!readExposure(root, profile.settings, error) || !readTaa(root, profile.settings.taa, error) ||
        !readBloom(root, profile.settings.bloom, error) ||
        !readColorGrading(root, profile.settings.colorGrading, error) ||
        !readVignette(root, profile.settings.vignette, error) ||
        !readDepthOfField(root, profile.settings.depthOfField, error) ||
        !readMotionBlur(root, profile.settings.motionBlur, error) ||
        !readFxaa(root, profile.settings.fxaa, error)) {
        return false;
    }

    sanitizePostProcessSettings(profile.settings);
    return true;
}

bool loadPostProcessProfile(const std::string& path, PostProcessProfile& profile, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "failed to open '" + path + "'";
        return false;
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        error = "failed to read '" + path + "'";
        return false;
    }
    if (!parsePostProcessProfile(contents.str(), profile, error)) {
        error = path + ": " + error;
        return false;
    }
    return true;
}

} // namespace hbg::render
