#pragma once

#include "render/debug/debug_display.h"
#include "render/postprocess/post_process_settings.h"

#include <bgfx/bgfx.h>

#include <optional>
#include <string>
#include <string_view>

namespace hbg {

std::optional<bgfx::RendererType::Enum> parseBackend(std::string_view name);
std::string_view backendName(bgfx::RendererType::Enum type);

struct AppOptions {
    bgfx::RendererType::Enum backend = bgfx::RendererType::Count;
    int width = 1280;
    int height = 720;
    bool vsync = true;
    bool verbose = false;
    bool bgfxDebug = false;
    bool gfxTest = false;
    bool debugLightData = false;
    bool debugWireframe = false;
    render::DebugDisplayMode debugDisplayMode = render::DebugDisplayMode::None;
    bool disableShadows = false;
    bool debugUi = true;
    bool cameraOrbit = false;
    bool framesSpecified = false;
    float renderScale = 1.0f;
    std::string assetTestPath;
    std::string scenePath;
    std::string renderTestPath;
    std::string screenshotPath;
    std::string pipelineSettingsPath;
    std::string frameSettingsPath;
    std::string skyPath;
    float skyIntensity = 1.0f;
    float skyRotation = 0.0f;
    render::PostProcessSettings postProcess;
    int frames = 3;
};

struct CommandLineResult {
    bool ok = false;
    bool help = false;
    AppOptions options;
};

CommandLineResult parseCommandLine(int argc, const char* const* argv);
const char* usage();

} // namespace hbg
