#pragma once

#include "gfx/resource_manager.h"
#include "render/debug/debug_display.h"
#include "render/pipeline/frame_settings.h"
#include "render/pipeline/pipeline_settings.h"
#include "render/postprocess/post_process_settings.h"

#include <cstdint>
#include <string>

namespace hbg {

bool runRenderTest(gfx::ResourceManager& resources, const std::string& path, uint16_t width, uint16_t height,
                   uint32_t frames, const std::string& screenshotPath, render::DebugDisplayMode debugMode,
                   bool cameraOrbit, const render::PipelineSettings& pipelineSettings,
                   const render::FrameSettings& frameSettings, const render::PostProcessSettings& postProcessSettings,
                   float renderScale, const std::string& skyPath, float skyIntensity, float skyRotation);

} // namespace hbg
