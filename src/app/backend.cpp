#include "app/backend.h"

#include "core/log.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>

namespace hbg {
namespace {

std::string toLower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return result;
}

std::optional<std::string_view> optionValue(std::string_view arg, std::string_view name) {
    if (arg.size() > name.size() && arg.substr(0, name.size()) == name && arg[name.size()] == '=') {
        return arg.substr(name.size() + 1);
    }
    return std::nullopt;
}

bool parseDimension(std::string_view text, int& out) {
    int value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc() || ptr != end || value <= 0 || value > 16384) {
        return false;
    }
    out = value;
    return true;
}

bool parseFloat(std::string_view text, float& out) {
    if (text.empty()) {
        return false;
    }
    const std::string value(text);
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || end != value.c_str() + value.size() || !std::isfinite(parsed)) {
        return false;
    }
    out = parsed;
    return true;
}

bool parseRenderScale(std::string_view text, float& out) {
    if (text.empty()) {
        return false;
    }
    const std::string value(text);
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || end != value.c_str() + value.size() || !std::isfinite(parsed) || parsed < 0.1f ||
        parsed > 1.0f) {
        return false;
    }
    out = parsed;
    return true;
}

std::optional<render::ExposureMode> parseExposureMode(std::string_view text) {
    const std::string key = toLower(text);
    if (key == "fixed") {
        return render::ExposureMode::Fixed;
    }
    if (key == "auto" || key == "automatic") {
        return render::ExposureMode::Automatic;
    }
    return std::nullopt;
}

std::optional<render::TonemappingMode> parseTonemappingMode(std::string_view text) {
    const std::string key = toLower(text);
    if (key == "none" || key == "off") {
        return render::TonemappingMode::None;
    }
    if (key == "neutral") {
        return render::TonemappingMode::Neutral;
    }
    if (key == "aces") {
        return render::TonemappingMode::ACES;
    }
    return std::nullopt;
}

std::optional<render::DebugDisplayMode> parseDebugDisplayMode(std::string_view text) {
    const std::string key = toLower(text);
    if (key == "off" || key == "none") {
        return render::DebugDisplayMode::None;
    }
    if (key == "light-data" || key == "lightdata") {
        return render::DebugDisplayMode::LightData;
    }
    if (key == "normal" || key == "normal-buffer") {
        return render::DebugDisplayMode::Normal;
    }
    if (key == "motion-vectors" || key == "motion" || key == "motionvectors") {
        return render::DebugDisplayMode::MotionVectors;
    }
    if (key == "depth") {
        return render::DebugDisplayMode::Depth;
    }
    if (key == "depth-pyramid" || key == "depthpyramid") {
        return render::DebugDisplayMode::DepthPyramid;
    }
    if (key == "light-cluster" || key == "lightcluster" || key == "cluster" || key == "cluster-tiles") {
        return render::DebugDisplayMode::LightCluster;
    }
    if (key == "directional-shadow-atlas" || key == "shadow-atlas-directional" || key == "directional-shadows") {
        return render::DebugDisplayMode::ShadowAtlasDirectional;
    }
    if (key == "punctual-shadow-atlas" || key == "shadow-atlas-punctual" || key == "punctual-shadows") {
        return render::DebugDisplayMode::ShadowAtlasPunctual;
    }
    if (key == "albedo") {
        return render::DebugDisplayMode::Albedo;
    }
    if (key == "roughness") {
        return render::DebugDisplayMode::Roughness;
    }
    return std::nullopt;
}

} // namespace

std::optional<bgfx::RendererType::Enum> parseBackend(std::string_view name) {
    const std::string lower = toLower(name);

    if (lower.empty() || lower == "auto" || lower == "count") {
        return bgfx::RendererType::Count;
    }
    if (lower == "d3d11" || lower == "dx11" || lower == "direct3d11") {
        return bgfx::RendererType::Direct3D11;
    }
    if (lower == "d3d12" || lower == "dx12" || lower == "direct3d12") {
        return bgfx::RendererType::Direct3D12;
    }
    if (lower == "vulkan" || lower == "vk") {
        return bgfx::RendererType::Vulkan;
    }
    if (lower == "opengl" || lower == "gl") {
        return bgfx::RendererType::OpenGL;
    }
    if (lower == "metal") {
        return bgfx::RendererType::Metal;
    }

    return std::nullopt;
}

std::string_view backendName(bgfx::RendererType::Enum type) {
    switch (type) {
    case bgfx::RendererType::Count:
        return "auto";
    case bgfx::RendererType::Direct3D11:
        return "d3d11";
    case bgfx::RendererType::Direct3D12:
        return "d3d12";
    case bgfx::RendererType::Vulkan:
        return "vulkan";
    case bgfx::RendererType::OpenGL:
        return "opengl";
    case bgfx::RendererType::Metal:
        return "metal";
    default:
        return "unknown";
    }
}

CommandLineResult parseCommandLine(int argc, const char* const* argv) {
    CommandLineResult result;
    result.ok = true;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] != nullptr ? argv[i] : "";

        if (arg == "--help" || arg == "-h") {
            result.help = true;
            return result;
        }
        if (arg == "--no-vsync") {
            result.options.vsync = false;
            continue;
        }
        if (arg == "--vsync") {
            result.options.vsync = true;
            continue;
        }
        if (arg == "--verbose") {
            result.options.verbose = true;
            continue;
        }
        if (arg == "--bgfx-debug") {
            result.options.bgfxDebug = true;
            continue;
        }
        if (arg == "--gfx-test") {
            result.options.gfxTest = true;
            continue;
        }
        if (arg == "--debug-light-data") {
            result.options.debugLightData = true;
            result.options.debugDisplayMode = render::DebugDisplayMode::LightData;
            continue;
        }
        if (const auto value = optionValue(arg, "--debug-view")) {
            const auto mode = parseDebugDisplayMode(*value);
            if (!mode) {
                HBG_LOGE("unknown debug view '{}' (expected off, light-data, normal, motion-vectors, depth, "
                         "depth-pyramid, light-cluster, directional-shadow-atlas, punctual-shadow-atlas, albedo or "
                         "roughness)",
                         *value);
                result.ok = false;
            } else {
                result.options.debugDisplayMode = *mode;
                result.options.debugLightData = *mode == render::DebugDisplayMode::LightData;
            }
            continue;
        }
        if (arg == "--debug-wireframe") {
            result.options.debugWireframe = true;
            continue;
        }
        if (arg == "--no-shadows") {
            result.options.disableShadows = true;
            continue;
        }
        if (arg == "--debug-ui") {
            result.options.debugUi = true;
            continue;
        }
        if (arg == "--no-debug-ui") {
            result.options.debugUi = false;
            continue;
        }
        if (arg == "--camera-orbit") {
            result.options.cameraOrbit = true;
            continue;
        }

        if (const auto value = optionValue(arg, "--asset-test")) {
            if (value->empty()) {
                HBG_LOGE("--asset-test requires a path");
                result.ok = false;
            } else {
                result.options.assetTestPath = std::string(*value);
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--scene")) {
            if (value->empty()) {
                HBG_LOGE("--scene requires a path");
                result.ok = false;
            } else {
                result.options.scenePath = std::string(*value);
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--render-test")) {
            if (value->empty()) {
                HBG_LOGE("--render-test requires a path");
                result.ok = false;
            } else {
                result.options.renderTestPath = std::string(*value);
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--frames")) {
            if (!parseDimension(*value, result.options.frames)) {
                HBG_LOGE("invalid --frames value '{}'", *value);
                result.ok = false;
            } else {
                result.options.framesSpecified = true;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--screenshot")) {
            if (value->empty()) {
                HBG_LOGE("--screenshot requires a path");
                result.ok = false;
            } else {
                result.options.screenshotPath = std::string(*value);
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--render-scale")) {
            if (!parseRenderScale(*value, result.options.renderScale)) {
                HBG_LOGE("invalid --render-scale value '{}' (expected 0.1..1.0)", *value);
                result.ok = false;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--pipeline-settings")) {
            if (value->empty()) {
                HBG_LOGE("--pipeline-settings requires a path");
                result.ok = false;
            } else {
                result.options.pipelineSettingsPath = std::string(*value);
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--frame-settings")) {
            if (value->empty()) {
                HBG_LOGE("--frame-settings requires a path");
                result.ok = false;
            } else {
                result.options.frameSettingsPath = std::string(*value);
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--sky")) {
            if (value->empty()) {
                HBG_LOGE("--sky requires a path");
                result.ok = false;
            } else {
                result.options.skyPath = std::string(*value);
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--sky-intensity")) {
            if (!parseFloat(*value, result.options.skyIntensity) || result.options.skyIntensity < 0.0f ||
                result.options.skyIntensity > 100.0f) {
                HBG_LOGE("invalid --sky-intensity value '{}' (expected 0..100)", *value);
                result.ok = false;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--sky-rotation")) {
            if (!parseFloat(*value, result.options.skyRotation)) {
                HBG_LOGE("invalid --sky-rotation value '{}'", *value);
                result.ok = false;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--exposure-mode")) {
            const auto mode = parseExposureMode(*value);
            if (!mode) {
                HBG_LOGE("unknown exposure mode '{}' (expected fixed or auto)", *value);
                result.ok = false;
            } else {
                result.options.postProcess.exposureMode = *mode;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--exposure-ev100")) {
            float ev100 = 0.0f;
            if (!parseFloat(*value, ev100) || ev100 < -20.0f || ev100 > 20.0f) {
                HBG_LOGE("invalid --exposure-ev100 value '{}' (expected -20..20)", *value);
                result.ok = false;
            } else {
                result.options.postProcess.fixedEv100 = ev100;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--exposure-compensation")) {
            float compensation = 0.0f;
            if (!parseFloat(*value, compensation) || compensation < -20.0f || compensation > 20.0f) {
                HBG_LOGE("invalid --exposure-compensation value '{}' (expected -20..20)", *value);
                result.ok = false;
            } else {
                result.options.postProcess.compensation = compensation;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--exposure-limit-min")) {
            float limitMin = 0.0f;
            if (!parseFloat(*value, limitMin) || limitMin < -20.0f || limitMin > 30.0f) {
                HBG_LOGE("invalid --exposure-limit-min value '{}' (expected -20..30)", *value);
                result.ok = false;
            } else {
                result.options.postProcess.limitMin = limitMin;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--exposure-limit-max")) {
            float limitMax = 0.0f;
            if (!parseFloat(*value, limitMax) || limitMax < -20.0f || limitMax > 30.0f) {
                HBG_LOGE("invalid --exposure-limit-max value '{}' (expected -20..30)", *value);
                result.ok = false;
            } else {
                result.options.postProcess.limitMax = limitMax;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--tonemap")) {
            const auto mode = parseTonemappingMode(*value);
            if (!mode) {
                HBG_LOGE("unknown tonemap mode '{}' (expected aces, neutral or none)", *value);
                result.ok = false;
            } else {
                result.options.postProcess.tonemapping = *mode;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--debug-exposure")) {
            float exposure = 0.0f;
            if (!parseFloat(*value, exposure) || exposure < -20.0f || exposure > 20.0f) {
                HBG_LOGE("invalid --debug-exposure value '{}' (expected -20..20)", *value);
                result.ok = false;
            } else {
                result.options.postProcess.debugExposure = exposure;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--backend")) {
            const auto backend = parseBackend(*value);
            if (!backend) {
                HBG_LOGE("unknown backend '{}' (expected d3d11, d3d12, vulkan, opengl, metal or auto)", *value);
                result.ok = false;
                continue;
            }
            result.options.backend = *backend;
            continue;
        }
        if (arg == "--backend") {
            HBG_LOGE("--backend requires a value");
            result.ok = false;
            continue;
        }

        if (const auto value = optionValue(arg, "--width")) {
            if (!parseDimension(*value, result.options.width)) {
                HBG_LOGE("invalid --width value '{}'", *value);
                result.ok = false;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--height")) {
            if (!parseDimension(*value, result.options.height)) {
                HBG_LOGE("invalid --height value '{}'", *value);
                result.ok = false;
            }
            continue;
        }

        HBG_LOGE("unknown argument '{}'", arg);
        result.ok = false;
    }

    return result;
}

const char* usage() {
    return "hdrp-bgfx\n"
           "  --backend=<d3d11|d3d12|vulkan|opengl|metal|auto>  Select renderer backend (default: auto)\n"
           "  --width=<pixels>                                  Window width (default: 1280)\n"
           "  --height=<pixels>                                 Window height (default: 720)\n"
           "  --no-vsync                                        Disable vertical sync\n"
           "  --verbose                                         Enable debug logging (including bgfx trace)\n"
           "  --bgfx-debug                                      Enable bgfx debug/validation layers\n"
           "  --gfx-test                                        Run the M0.2 resource/draw smoke test and exit\n"
           "  --debug-ui / --no-debug-ui                        Enable/disable the interactive debug panel\n"
           "  --debug-light-data                                Start with the GPU light-data debug view\n"
           "  --debug-view=<off|light-data|normal|motion-vectors|depth|depth-pyramid|light-cluster|\n"
           "                directional-shadow-atlas|punctual-shadow-atlas|albedo|roughness>\n"
           "                                                    Select a fullscreen debug view\n"
           "  --debug-wireframe                                 Start with the wireframe overlay enabled\n"
           "  --camera-orbit                                   Orbit the render-test camera to exercise motion "
           "vectors\n"
           "  --no-shadows                                      Start with shadow sampling disabled\n"
           "  --asset-test=<path>                               Load a glTF scene, log its contents and exit\n"
           "  --scene=<path>                                    Load a glTF scene and render it interactively\n"
           "  --render-test=<path>                              Render a glTF scene for --frames frames and exit\n"
           "  --frames=<count>                                  Render-test frames; with --scene, stop after count "
           "frames\n"
           "  --render-scale=<0.1..1.0>                         Dynamic-resolution render scale (default: 1.0)\n"
           "  --screenshot=<path>                               Write the last frame to a PNG file\n"
           "  --pipeline-settings=<path>                        Load PipelineSettings JSON\n"
           "  --frame-settings=<path>                           Load FrameSettings JSON\n"
           "  --sky=<path>                                      Load an HDR equirectangular sky\n"
           "  --sky-intensity=<value>                           Sky/IBL intensity (default: 1.0)\n"
           "  --sky-rotation=<degrees>                          Rotate the sky around Y (default: 0)\n"
           "  --exposure-mode=<fixed|auto>                      Exposure mode (default: fixed)\n"
           "  --exposure-ev100=<value>                          Fixed exposure EV100 (default: -1.263)\n"
           "  --exposure-compensation=<ev>                      Exposure compensation in EV\n"
           "  --exposure-limit-min=<ev> / --exposure-limit-max=<ev>\n"
           "                                                    Automatic exposure EV100 limits\n"
           "  --tonemap=<aces|neutral|none>                     Tonemapping mode (default: aces)\n"
           "  --debug-exposure=<ev>                             Debug exposure override in EV\n"
           "  --help                                            Show this help\n";
}

} // namespace hbg
