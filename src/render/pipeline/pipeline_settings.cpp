#include "render/pipeline/pipeline_settings.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <fstream>
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

bool readBool(const Json& document, const char* key, bool& value, std::string& error) {
    const auto it = document.find(key);
    if (it == document.end()) {
        return true;
    }
    if (!it->is_boolean()) {
        error = std::string(key) + " must be a boolean";
        return false;
    }
    value = it->get<bool>();
    return true;
}

bool parseLitShaderMode(std::string_view text, SupportedLitShaderMode& value) {
    const std::string key = normalize(text);
    if (key == "forward" || key == "forwardonly") {
        value = SupportedLitShaderMode::ForwardOnly;
        return true;
    }
    if (key == "deferred" || key == "deferredonly") {
        value = SupportedLitShaderMode::DeferredOnly;
        return true;
    }
    if (key == "both") {
        value = SupportedLitShaderMode::Both;
        return true;
    }
    return false;
}

bool readLitShaderMode(const Json& document, const char* key, SupportedLitShaderMode& value, std::string& error) {
    const auto it = document.find(key);
    if (it == document.end()) {
        return true;
    }
    if (!it->is_string() || !parseLitShaderMode(it->get<std::string>(), value)) {
        error = std::string(key) + " must be forwardOnly, deferredOnly or both";
        return false;
    }
    return true;
}

bool readColorFormat(const Json& document, const char* key, ColorBufferFormat& value, std::string& error) {
    const auto it = document.find(key);
    if (it == document.end()) {
        return true;
    }
    if (!it->is_string()) {
        error = std::string(key) + " must be a string";
        return false;
    }
    const std::string name = normalize(it->get<std::string>());
    if (name == "r11g11b10f" || name == "rg11b10f") {
        value = ColorBufferFormat::R11G11B10F;
    } else if (name == "r16g16b16a16f" || name == "rgba16f") {
        value = ColorBufferFormat::R16G16B16A16F;
    } else {
        error = std::string(key) + " must be R11G11B10F or R16G16B16A16F";
        return false;
    }
    return true;
}

bool readDepthFormat(const Json& document, const char* key, DepthBufferFormat& value, std::string& error) {
    const auto it = document.find(key);
    if (it == document.end()) {
        return true;
    }
    if (!it->is_string()) {
        error = std::string(key) + " must be a string";
        return false;
    }
    const std::string name = normalize(it->get<std::string>());
    if (name == "auto") {
        value = DepthBufferFormat::Auto;
    } else if (name == "d24s8") {
        value = DepthBufferFormat::D24S8;
    } else if (name == "d32f") {
        value = DepthBufferFormat::D32F;
    } else if (name == "d16") {
        value = DepthBufferFormat::D16;
    } else {
        error = std::string(key) + " must be Auto, D24S8, D32F or D16";
        return false;
    }
    return true;
}

bool parseMSAAValue(const Json& value, MSAASamples& samples) {
    if (value.is_number_integer()) {
        switch (value.get<int>()) {
        case 1:
            samples = MSAASamples::None;
            return true;
        case 2:
            samples = MSAASamples::MSAA2X;
            return true;
        case 4:
            samples = MSAASamples::MSAA4X;
            return true;
        case 8:
            samples = MSAASamples::MSAA8X;
            return true;
        default:
            return false;
        }
    }
    if (!value.is_string()) {
        return false;
    }
    const std::string name = normalize(value.get<std::string>());
    if (name == "none" || name == "1") {
        samples = MSAASamples::None;
    } else if (name == "2x" || name == "msaa2x" || name == "2") {
        samples = MSAASamples::MSAA2X;
    } else if (name == "4x" || name == "msaa4x" || name == "4") {
        samples = MSAASamples::MSAA4X;
    } else if (name == "8x" || name == "msaa8x" || name == "8") {
        samples = MSAASamples::MSAA8X;
    } else {
        return false;
    }
    return true;
}

bool readMSAA(const Json& document, const char* key, MSAASamples& value, std::string& error) {
    const auto it = document.find(key);
    if (it == document.end()) {
        return true;
    }
    if (!parseMSAAValue(*it, value)) {
        error = std::string(key) + " must be None, 2X, 4X or 8X";
        return false;
    }
    return true;
}

const char* litShaderModeName(SupportedLitShaderMode value) {
    switch (value) {
    case SupportedLitShaderMode::ForwardOnly:
        return "forwardOnly";
    case SupportedLitShaderMode::DeferredOnly:
        return "deferredOnly";
    case SupportedLitShaderMode::Both:
        return "both";
    }
    return "forwardOnly";
}

const char* colorFormatName(ColorBufferFormat value) {
    return value == ColorBufferFormat::R16G16B16A16F ? "R16G16B16A16F" : "R11G11B10F";
}

const char* depthFormatName(DepthBufferFormat value) {
    switch (value) {
    case DepthBufferFormat::Auto:
        return "Auto";
    case DepthBufferFormat::D24S8:
        return "D24S8";
    case DepthBufferFormat::D32F:
        return "D32F";
    case DepthBufferFormat::D16:
        return "D16";
    }
    return "Auto";
}

const char* msaaName(MSAASamples value) {
    switch (value) {
    case MSAASamples::None:
        return "None";
    case MSAASamples::MSAA2X:
        return "2X";
    case MSAASamples::MSAA4X:
        return "4X";
    case MSAASamples::MSAA8X:
        return "8X";
    }
    return "None";
}

} // namespace

PipelineSettings PipelineSettings::defaults() {
    return PipelineSettings{};
}

bool parsePipelineSettings(std::string_view text, PipelineSettings& settings, std::string& error) {
    settings = PipelineSettings::defaults();
    error.clear();

    Json document;
    try {
        document = Json::parse(text.begin(), text.end());
    } catch (const Json::exception& exception) {
        error = exception.what();
        return false;
    }
    if (!document.is_object()) {
        error = "pipeline settings must be a JSON object";
        return false;
    }

    for (auto it = document.begin(); it != document.end(); ++it) {
        const std::string& key = it.key();
        bool ok = true;
        if (key == "supportedLitShaderMode") {
            ok = readLitShaderMode(document, key.c_str(), settings.supportedLitShaderMode, error);
        } else if (key == "colorBufferFormat") {
            ok = readColorFormat(document, key.c_str(), settings.colorBufferFormat, error);
        } else if (key == "depthBufferFormat") {
            ok = readDepthFormat(document, key.c_str(), settings.depthBufferFormat, error);
        } else if (key == "msaaSampleCount") {
            ok = readMSAA(document, key.c_str(), settings.msaaSampleCount, error);
        } else if (key == "supportShadows") {
            ok = readBool(document, key.c_str(), settings.supportShadows, error);
        } else if (key == "supportShadowMask") {
            ok = readBool(document, key.c_str(), settings.supportShadowMask, error);
        } else if (key == "supportContactShadows") {
            ok = readBool(document, key.c_str(), settings.supportContactShadows, error);
        } else if (key == "supportScreenSpaceShadows") {
            ok = readBool(document, key.c_str(), settings.supportScreenSpaceShadows, error);
        } else if (key == "supportSSR") {
            ok = readBool(document, key.c_str(), settings.supportSSR, error);
        } else if (key == "supportSSRTransparent") {
            ok = readBool(document, key.c_str(), settings.supportSSRTransparent, error);
        } else if (key == "supportSSAO") {
            ok = readBool(document, key.c_str(), settings.supportSSAO, error);
        } else if (key == "supportSSGI") {
            ok = readBool(document, key.c_str(), settings.supportSSGI, error);
        } else if (key == "supportSubsurfaceScattering") {
            ok = readBool(document, key.c_str(), settings.supportSubsurfaceScattering, error);
        } else if (key == "supportVolumetrics") {
            ok = readBool(document, key.c_str(), settings.supportVolumetrics, error);
        } else if (key == "supportVolumetricClouds") {
            ok = readBool(document, key.c_str(), settings.supportVolumetricClouds, error);
        } else if (key == "supportMotionVectors") {
            ok = readBool(document, key.c_str(), settings.supportMotionVectors, error);
        } else if (key == "supportTransparentDepthPrepass") {
            ok = readBool(document, key.c_str(), settings.supportTransparentDepthPrepass, error);
        } else if (key == "supportTransparentDepthPostpass") {
            ok = readBool(document, key.c_str(), settings.supportTransparentDepthPostpass, error);
        } else if (key == "supportLowResTransparent") {
            ok = readBool(document, key.c_str(), settings.supportLowResTransparent, error);
        } else if (key == "supportDecals") {
            ok = readBool(document, key.c_str(), settings.supportDecals, error);
        } else if (key == "supportDecalLayers") {
            ok = readBool(document, key.c_str(), settings.supportDecalLayers, error);
        } else if (key == "supportDistortion") {
            ok = readBool(document, key.c_str(), settings.supportDistortion, error);
        } else if (key == "supportLightLayers") {
            ok = readBool(document, key.c_str(), settings.supportLightLayers, error);
        } else if (key == "supportWater") {
            ok = readBool(document, key.c_str(), settings.supportWater, error);
        } else if (key == "supportWaterDecals") {
            ok = readBool(document, key.c_str(), settings.supportWaterDecals, error);
        } else if (key == "supportWaterExclusion") {
            ok = readBool(document, key.c_str(), settings.supportWaterExclusion, error);
        } else if (key == "supportRayTracing") {
            ok = readBool(document, key.c_str(), settings.supportRayTracing, error);
        } else if (key == "supportScreenSpaceLensFlare") {
            ok = readBool(document, key.c_str(), settings.supportScreenSpaceLensFlare, error);
        } else if (key == "supportDataDrivenLensFlare") {
            ok = readBool(document, key.c_str(), settings.supportDataDrivenLensFlare, error);
        } else if (key == "supportCustomPass") {
            ok = readBool(document, key.c_str(), settings.supportCustomPass, error);
        } else if (key == "supportVariableRateShading") {
            ok = readBool(document, key.c_str(), settings.supportVariableRateShading, error);
        } else if (key == "supportVirtualTexturing") {
            ok = readBool(document, key.c_str(), settings.supportVirtualTexturing, error);
        } else if (key == "supportProbeVolume") {
            ok = readBool(document, key.c_str(), settings.supportProbeVolume, error);
        } else if (key == "supportHighQualityLineRendering") {
            ok = readBool(document, key.c_str(), settings.supportHighQualityLineRendering, error);
        } else if (key == "supportComputeThickness") {
            ok = readBool(document, key.c_str(), settings.supportComputeThickness, error);
        } else if (key == "supportAsyncCompute") {
            ok = readBool(document, key.c_str(), settings.supportAsyncCompute, error);
        } else if (key == "renderingLayerMaskBuffer") {
            ok = readBool(document, key.c_str(), settings.renderingLayerMaskBuffer, error);
        } else {
            error = "unknown pipeline setting '" + key + "'";
            return false;
        }
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool loadPipelineSettings(const std::string& path, PipelineSettings& settings, std::string& error) {
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
    if (!parsePipelineSettings(contents.str(), settings, error)) {
        error = path + ": " + error;
        return false;
    }
    return true;
}

std::string pipelineSettingsToJson(const PipelineSettings& settings) {
    Json document = Json::object();
    document["supportedLitShaderMode"] = litShaderModeName(settings.supportedLitShaderMode);
    document["colorBufferFormat"] = colorFormatName(settings.colorBufferFormat);
    document["depthBufferFormat"] = depthFormatName(settings.depthBufferFormat);
    document["msaaSampleCount"] = msaaName(settings.msaaSampleCount);
    document["supportShadows"] = settings.supportShadows;
    document["supportShadowMask"] = settings.supportShadowMask;
    document["supportContactShadows"] = settings.supportContactShadows;
    document["supportScreenSpaceShadows"] = settings.supportScreenSpaceShadows;
    document["supportSSR"] = settings.supportSSR;
    document["supportSSRTransparent"] = settings.supportSSRTransparent;
    document["supportSSAO"] = settings.supportSSAO;
    document["supportSSGI"] = settings.supportSSGI;
    document["supportSubsurfaceScattering"] = settings.supportSubsurfaceScattering;
    document["supportVolumetrics"] = settings.supportVolumetrics;
    document["supportVolumetricClouds"] = settings.supportVolumetricClouds;
    document["supportMotionVectors"] = settings.supportMotionVectors;
    document["supportTransparentDepthPrepass"] = settings.supportTransparentDepthPrepass;
    document["supportTransparentDepthPostpass"] = settings.supportTransparentDepthPostpass;
    document["supportLowResTransparent"] = settings.supportLowResTransparent;
    document["supportDecals"] = settings.supportDecals;
    document["supportDecalLayers"] = settings.supportDecalLayers;
    document["supportDistortion"] = settings.supportDistortion;
    document["supportLightLayers"] = settings.supportLightLayers;
    document["supportWater"] = settings.supportWater;
    document["supportWaterDecals"] = settings.supportWaterDecals;
    document["supportWaterExclusion"] = settings.supportWaterExclusion;
    document["supportRayTracing"] = settings.supportRayTracing;
    document["supportScreenSpaceLensFlare"] = settings.supportScreenSpaceLensFlare;
    document["supportDataDrivenLensFlare"] = settings.supportDataDrivenLensFlare;
    document["supportCustomPass"] = settings.supportCustomPass;
    document["supportVariableRateShading"] = settings.supportVariableRateShading;
    document["supportVirtualTexturing"] = settings.supportVirtualTexturing;
    document["supportProbeVolume"] = settings.supportProbeVolume;
    document["supportHighQualityLineRendering"] = settings.supportHighQualityLineRendering;
    document["supportComputeThickness"] = settings.supportComputeThickness;
    document["supportAsyncCompute"] = settings.supportAsyncCompute;
    document["renderingLayerMaskBuffer"] = settings.renderingLayerMaskBuffer;
    return document.dump(2);
}

} // namespace hbg::render
