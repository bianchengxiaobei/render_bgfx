#pragma once

#include "gfx/resource_manager.h"
#include "render/atmosphere/volumetric_fog_settings.h"
#include "render/debug/debug_display.h"
#include "render/pipeline/frame_settings.h"
#include "render/pipeline/pipeline_settings.h"
#include "render/postprocess/post_process_profile.h"
#include "render/postprocess/ssao_settings.h"
#include "render/postprocess/ssr_settings.h"
#include "render/postprocess/ssgi_settings.h"
#include "render/shadows/shadow_settings.h"

#include <cstdint>
#include <string>

namespace hbg {

bool runRenderTest(gfx::ResourceManager& resources, const std::string& path, uint16_t width, uint16_t height,
                   uint32_t frames, const std::string& screenshotPath, render::DebugDisplayMode debugMode,
                   bool debugLightmapMask, int lightClusterSlice, bool lightClusterEnabled, bool cameraOrbit,
                   bool shadowsEnabled,
                   bool clusterStatsReadback,
                   const render::PipelineSettings& pipelineSettings,
                   const render::FrameSettings& frameSettings, const render::ShadowSettings& shadowSettings,
                   const render::SsaoSettings& ssaoSettings, const render::SsrSettings& ssrSettings,
                   const render::SsgiSettings& ssgiSettings,
                   const render::PostProcessProfile& postProcessProfile,
                   const render::VolumetricFogSettings& volumetricFogSettings, float renderScale,
                   const std::string& skyPath, float skyIntensity, float skyRotation, bool resizeTest);

} // namespace hbg
