#include "app/debug_ui.h"

#include "core/log.h"
#include "core/math.h"
#include "render/graph/render_graph.h"

#include <imgui.h>

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hbg {
namespace {

constexpr bgfx::ViewId kDebugUiView = render::kDebugUiView;

constexpr uint64_t kUiState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
                              BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA,
                                                             BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);

bgfx::VertexLayout makeLayout() {
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    return layout;
}

const bgfx::VertexLayout kUiLayout = makeLayout();

void* uiAlloc(std::size_t size, void*) {
    return std::malloc(size);
}

void uiFree(void* pointer, void*) {
    std::free(pointer);
}

std::string formatBytes(int64_t bytes) {
    char buffer[64];
    const double value = static_cast<double>(bytes);
    if (bytes >= 1024LL * 1024LL * 1024LL) {
        std::snprintf(buffer, sizeof(buffer), "%.2f GiB", value / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024LL * 1024LL) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MiB", value / (1024.0 * 1024.0));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.0f KiB", value / 1024.0);
    }
    return buffer;
}

} // namespace

bool DebugUI::init(gfx::ResourceManager& resources, shader::ShaderLibrary& shaderLibrary) {
    if (m_valid) {
        return true;
    }
    m_resources = &resources;
    m_shaderLibrary = &shaderLibrary;

    IMGUI_CHECKVERSION();
    ImGui::SetAllocatorFunctions(uiAlloc, uiFree);
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding = 3.0f;
    style.WindowRounding = 5.0f;
    style.WindowBorderSize = 1.0f;

    io.Fonts->AddFontDefault();

    m_program = shaderLibrary.program(shader::PassId::Ui, 0);
    m_sampler = resources.createUniform({"s_tex", gfx::UniformType::Sampler, 1});
    if (!m_program.valid() || !m_sampler.valid()) {
        HBG_LOGE("debug ui: failed to create ImGui program/sampler");
        shutdown(resources);
        return false;
    }

#if defined(_WIN32)
    m_renderDocLoaded = GetModuleHandleA("renderdoc.dll") != nullptr;
#else
    m_renderDocLoaded = false;
#endif

    m_valid = true;
    HBG_LOGI("debug ui ready: renderdoc={}", m_renderDocLoaded ? "loaded" : "unavailable");
    return true;
}

void DebugUI::shutdown(gfx::ResourceManager& resources) {
    for (gfx::TextureHandle& texture : m_textures) {
        if (texture.valid()) {
            resources.destroyTexture(texture);
        }
    }
    m_textures.clear();

    if (m_sampler.valid()) {
        resources.destroyUniform(m_sampler);
    }
    m_sampler = {};

    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui::DestroyContext();
    }

    m_program = {};
    m_resources = nullptr;
    m_shaderLibrary = nullptr;
    m_valid = false;
    m_screenshotRequested = false;
}

void DebugUI::beginFrame(const WindowInput& input, uint16_t width, uint16_t height, double deltaSeconds) {
    if (!m_valid) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DeltaTime = deltaSeconds > 0.0 ? static_cast<float>(deltaSeconds) : 1.0f / 60.0f;

    io.AddMousePosEvent(input.mouseX, input.mouseY);
    for (int button = 0; button < 3; ++button) {
        io.AddMouseButtonEvent(button, input.mouseDown[button]);
    }
    if (input.scrollX != 0.0f || input.scrollY != 0.0f) {
        io.AddMouseWheelEvent(input.scrollX, input.scrollY);
    }

    io.AddKeyEvent(ImGuiMod_Shift, input.keyShift);
    io.AddKeyEvent(ImGuiMod_Ctrl, input.keyCtrl);
    io.AddKeyEvent(ImGuiMod_Alt, input.keyAlt);
    io.AddKeyEvent(ImGuiKey_F1, input.keyF1);
    io.AddKeyEvent(ImGuiKey_F11, input.keyF11);
    io.AddKeyEvent(ImGuiKey_F12, input.keyF12);

    ImGui::NewFrame();
    m_profiler.update(deltaSeconds);

    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
        m_visible = !m_visible;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F12, false)) {
        m_screenshotRequested = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) {
        if (m_renderDocLoaded) {
            HBG_LOGI("debug ui: F11 RenderDoc capture requested");
        } else {
            HBG_LOGW("debug ui: F11 ignored, renderdoc.dll is not loaded");
        }
    }

    if (m_visible) {
        drawPanel();
    }
    m_settings.showPanel = m_visible;
}

void DebugUI::drawPanel() {
    const ProfilerStats& stats = m_profiler.stats();
    const bgfx::Caps* caps = bgfx::getCaps();
    const bgfx::RendererType::Enum rendererType = bgfx::getRendererType();
    const char* rendererName = bgfx::getRendererName(rendererType);

    ImGui::SetNextWindowSize(ImVec2(370.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("hdrp-bgfx Debug", &m_visible, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Backend: %s", rendererName != nullptr ? rendererName : "unknown");
        if (caps != nullptr) {
            ImGui::Text("Max texture: %u  Views: %u", caps->limits.maxTextureSize, caps->limits.maxViews);
            ImGui::Text("Samplers: %u", caps->limits.maxTextureSamplers);
        }
        ImGui::Text("Swapchain: %ux%u", stats.width, stats.height);

        ImGui::Separator();
        ImGui::Text("FPS: %.1f  (frame %.2f ms)", stats.fps, stats.cpuFrameMs);
        ImGui::Text("CPU submit: %.2f ms", stats.cpuFrameMs);
        ImGui::Text("GPU: %.2f ms  wait: %.2f ms", stats.gpuFrameMs, stats.waitRenderMs);
        ImGui::Text("Draws: %u  Compute: %u  GPU frame: %u", stats.drawCalls, stats.computeCalls, stats.gpuFrame);
        ImGui::Text("Textures: %u  FBOs: %u  Programs: %u", stats.textures, stats.frameBuffers, stats.programs);
        ImGui::Text("Texture mem: %s", formatBytes(stats.textureMemoryBytes).c_str());
        ImGui::Text("RT mem: %s", formatBytes(stats.renderTargetMemoryBytes).c_str());

        ImGui::Separator();
        ImGui::TextUnformatted("Rendering");
        ImGui::Checkbox("Shadows", &m_settings.shadows);
        ImGui::SameLine();
        ImGui::Checkbox("Wireframe", &m_settings.wireframe);
        ImGui::Checkbox("GPU light list", &m_settings.gpuLightList);
        if (m_gpuLightListRequested) {
            ImGui::Text("Light list: %s (%s)  indirect=%s  passes=%u  cpu=%.3f ms",
                        m_gpuLightListActive ? "GPU" : "CPU fallback",
                        m_gpuLightListBufferPath ? "buffer" : "texture", m_gpuLightListIndirect ? "true" : "false",
                        m_gpuLightListPasses, static_cast<double>(m_gpuLightListCpuMs));
        } else {
            ImGui::TextUnformatted("Light list: CPU");
        }
        ImGui::Text("Light capacity: requested=%u effective=%u stride=%u truncated=%u clamped=%s",
                    m_lightListCapacityRequested, m_lightListCapacityEffective, m_lightListStride,
                    m_lightListTruncated, m_lightListCapacityClamped ? "true" : "false");
        if (ImGui::Button("Read GPU cluster stats")) {
            m_clusterStatsReadbackRequested = true;
        }
        ImGui::SameLine();
        if (m_clusterStatsPending) {
            ImGui::TextDisabled("readback pending");
        } else if (m_clusterStatsValid) {
            ImGui::Text("Cluster stats (%s): clusters=%u active=%u overflow_clusters=%u overflow_lights=%u "
                        "max_overflow=%u max_lights=%u",
                        m_clusterStatsGpu ? "GPU" : "CPU", m_clusterStatsClusters, m_clusterStatsActiveClusters,
                        m_clusterStatsOverflowClusters, m_clusterStatsOverflowLights, m_clusterStatsMaxOverflow,
                        m_clusterStatsMaxLights);
        } else {
            ImGui::TextDisabled("cluster stats: not read");
        }
        ImGui::Text("Shadow cull: %s  indirect=%s", m_gpuShadowCulling ? "GPU" : "CPU",
                    m_gpuShadowIndirect ? "true" : "false");
        if (m_asyncRequested) {
            ImGui::Text("Async light list: requested=true  active=%s", m_asyncActive ? "true" : "false");
        }
        ImGui::Checkbox("Freeze time", &m_settings.freezeTime);
        ImGui::SliderFloat("Render scale", &m_settings.renderScale, 0.1f, 1.0f, "%.2f");

        ImGui::Separator();
        ImGui::TextUnformatted("Final output");
        const std::size_t exposureModeCount = render::exposureModeCount();
        const char* exposureModeNames[8]{};
        for (std::size_t index = 0; index < std::min<std::size_t>(exposureModeCount, 8); ++index) {
            exposureModeNames[index] = render::exposureModeName(render::exposureModeAt(index));
        }
        int exposureMode = static_cast<int>(m_settings.postProcess.exposureMode);
        if (ImGui::Combo("Exposure", &exposureMode, exposureModeNames,
                         static_cast<int>(std::min<std::size_t>(exposureModeCount, 8)))) {
            m_settings.postProcess.exposureMode =
                render::exposureModeAt(static_cast<std::size_t>(std::clamp(exposureMode, 0, 2)));
        }
        if (m_settings.postProcess.exposureMode == render::ExposureMode::Fixed) {
            ImGui::SliderFloat("Fixed EV100", &m_settings.postProcess.fixedEv100, -10.0f, 20.0f, "%.2f");
        } else {
            ImGui::SliderFloat("Limit min EV", &m_settings.postProcess.limitMin, -10.0f, 20.0f, "%.2f");
            ImGui::SliderFloat("Limit max EV", &m_settings.postProcess.limitMax, -10.0f, 30.0f, "%.2f");
            ImGui::SliderFloat("Adapt dark to light", &m_settings.postProcess.adaptationSpeedDarkToLight, 0.01f,
                               10.0f, "%.2f");
            ImGui::SliderFloat("Adapt light to dark", &m_settings.postProcess.adaptationSpeedLightToDark, 0.01f,
                               10.0f, "%.2f");
            ImGui::SliderFloat("Histogram min", &m_settings.postProcess.histogramMinPercentile, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Histogram max", &m_settings.postProcess.histogramMaxPercentile, 0.0f, 1.0f, "%.2f");
            ImGui::TextDisabled("Compute exposure: 256 -> 32 -> 1; histogram: 128 bins");
        }
        ImGui::SliderFloat("Compensation", &m_settings.postProcess.compensation, -10.0f, 10.0f, "%.2f EV");
        ImGui::SliderFloat("Debug exposure", &m_settings.postProcess.debugExposure, -10.0f, 10.0f, "%.2f EV");

        const std::size_t tonemapCount = render::tonemappingModeCount();
        const char* tonemapNames[8]{};
        for (std::size_t index = 0; index < std::min<std::size_t>(tonemapCount, 8); ++index) {
            tonemapNames[index] = render::tonemappingModeName(render::tonemappingModeAt(index));
        }
        int tonemap = static_cast<int>(m_settings.postProcess.tonemapping);
        if (ImGui::Combo("Tonemap", &tonemap, tonemapNames, static_cast<int>(std::min<std::size_t>(tonemapCount, 8)))) {
            m_settings.postProcess.tonemapping =
                render::tonemappingModeAt(static_cast<std::size_t>(std::clamp(tonemap, 0, 2)));
        }
        ImGui::TextDisabled("Final output is linear-to-sRGB encoded");

        ImGui::Separator();
        ImGui::TextUnformatted("Post-process M3");
        ImGui::TextDisabled("Effects require support flags in pipeline.json");
        if (ImGui::Checkbox("TAA", &m_settings.postProcess.taa.enabled) && m_settings.postProcess.taa.enabled) {
            m_settings.postProcess.fxaa.enabled = false;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("FXAA", &m_settings.postProcess.fxaa.enabled) &&
            m_settings.postProcess.fxaa.enabled) {
            m_settings.postProcess.taa.enabled = false;
        }
        if (m_settings.postProcess.taa.enabled) {
            int quality = static_cast<int>(m_settings.postProcess.taa.quality);
            if (ImGui::SliderInt("TAA quality", &quality, 1, 5)) {
                m_settings.postProcess.taa.quality = static_cast<uint32_t>(quality);
            }
            ImGui::SliderFloat("TAA motion rejection", &m_settings.postProcess.taa.motionVectorRejection, 0.0f, 1.0f,
                               "%.2f");
            ImGui::SliderFloat("TAA sharpening", &m_settings.postProcess.taa.sharpening, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("TAA jitter scale", &m_settings.postProcess.taa.jitterScale, 0.0f, 1.0f, "%.2f");
        }
        ImGui::Checkbox("Bloom", &m_settings.postProcess.bloom.enabled);
        if (m_settings.postProcess.bloom.enabled) {
            ImGui::SliderFloat("Bloom threshold", &m_settings.postProcess.bloom.threshold, 0.0f, 4.0f, "%.2f");
            ImGui::SliderFloat("Bloom intensity", &m_settings.postProcess.bloom.intensity, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Bloom scatter", &m_settings.postProcess.bloom.scatter, 0.0f, 1.0f, "%.2f");
            ImGui::ColorEdit3("Bloom tint", &m_settings.postProcess.bloom.tint.x);
            int resolutionIndex = m_settings.postProcess.bloom.resolution == 4u ? 1 : 0;
            if (ImGui::Combo("Bloom resolution", &resolutionIndex, "Half\0Quarter\0")) {
                m_settings.postProcess.bloom.resolution = resolutionIndex == 1 ? 4u : 2u;
            }
            ImGui::Checkbox("HQ bloom prefilter", &m_settings.postProcess.bloom.highQualityPrefiltering);
            ImGui::Checkbox("HQ bloom filtering", &m_settings.postProcess.bloom.highQualityFiltering);
        }
        ImGui::Checkbox("Color grading", &m_settings.postProcess.colorGrading.enabled);
        if (m_settings.postProcess.colorGrading.enabled) {
            ImGui::SliderFloat("Post exposure", &m_settings.postProcess.colorGrading.postExposure, -8.0f, 8.0f,
                               "%.2f EV");
            ImGui::SliderFloat("Contrast", &m_settings.postProcess.colorGrading.contrast, -100.0f, 100.0f, "%.0f");
            ImGui::SliderFloat("Saturation", &m_settings.postProcess.colorGrading.saturation, -100.0f, 100.0f,
                               "%.0f");
            ImGui::SliderFloat("Hue shift", &m_settings.postProcess.colorGrading.hueShift, -180.0f, 180.0f, "%.0f");
        }
        ImGui::SliderFloat("Vignette", &m_settings.postProcess.vignette.intensity, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox("Depth of field", &m_settings.postProcess.depthOfField.enabled);
        if (m_settings.postProcess.depthOfField.enabled) {
            render::DepthOfFieldSettings& dof = m_settings.postProcess.depthOfField;
            ImGui::SliderFloat("Near focus start", &dof.nearFocusStart, 0.0f, 50.0f, "%.1f");
            ImGui::SliderFloat("Near focus end", &dof.nearFocusEnd, 0.0f, 50.0f, "%.1f");
            ImGui::SliderFloat("Far focus start", &dof.farFocusStart, 0.0f, 100.0f, "%.1f");
            ImGui::SliderFloat("Far focus end", &dof.farFocusEnd, 0.0f, 100.0f, "%.1f");
            ImGui::SliderFloat("Near max blur", &dof.nearMaxBlur, 0.0f, 8.0f, "%.1f px");
            ImGui::SliderFloat("Far max blur", &dof.farMaxBlur, 0.0f, 16.0f, "%.1f px");
            int resolutionIndex = dof.resolution == 1u ? 0 : (dof.resolution == 4u ? 2 : 1);
            if (ImGui::Combo("DOF resolution", &resolutionIndex, "Full\0Half\0Quarter\0")) {
                dof.resolution = resolutionIndex == 0 ? 1u : (resolutionIndex == 2 ? 4u : 2u);
            }
            ImGui::Checkbox("HQ DOF gather", &dof.highQuality);
        }
        ImGui::Checkbox("Motion blur", &m_settings.postProcess.motionBlur.enabled);
        if (m_settings.postProcess.motionBlur.enabled) {
            render::MotionBlurSettings& motion = m_settings.postProcess.motionBlur;
            ImGui::SliderFloat("Motion intensity", &motion.intensity, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Max velocity", &motion.maximumVelocity, 0.0f, 500.0f, "%.0f px");
            ImGui::SliderFloat("Min velocity", &motion.minimumVelocity, 0.0f, 64.0f, "%.0f px");
            int samples = static_cast<int>(motion.sampleCount);
            if (ImGui::SliderInt("Motion samples", &samples, 2, 32)) {
                motion.sampleCount = static_cast<uint32_t>(samples);
            }
            ImGui::SliderFloat("Motion depth extent", &motion.depthComparisonExtent, 0.0f, 20.0f, "%.2f");
            ImGui::Checkbox("Camera motion blur", &motion.cameraMotionBlur);
        }

        ImGui::Separator();
        constexpr std::size_t kMaxDebugModes = 64;
        const std::size_t modeCount = render::debugDisplayModeCount();
        const std::size_t shownModes = std::min<std::size_t>(modeCount, kMaxDebugModes);
        const char* modeNames[kMaxDebugModes]{};
        for (std::size_t index = 0; index < shownModes; ++index) {
            modeNames[index] = render::debugDisplayModeNameAt(index);
        }
        int mode = 0;
        for (std::size_t index = 0; index < shownModes; ++index) {
            if (render::debugDisplayModeAt(index) == m_settings.fullscreen) {
                mode = static_cast<int>(index);
                break;
            }
        }
        if (ImGui::Combo("Debug view", &mode, modeNames, static_cast<int>(shownModes)) && mode >= 0 &&
            static_cast<std::size_t>(mode) < shownModes) {
            m_settings.fullscreen = render::debugDisplayModeAt(static_cast<std::size_t>(mode));
        }
        if (m_settings.fullscreen == render::DebugDisplayMode::LightCluster) {
            ImGui::SliderInt("Cluster slice", &m_settings.lightClusterSlice, -2, 31);
            ImGui::TextDisabled("-2: FPTL legacy, -1: per-pixel depth slice, 0..31: fixed slice");
        }

        ImGui::Separator();
        if (ImGui::Button("Screenshot (F12)")) {
            m_screenshotRequested = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("F1: toggle panel");
        ImGui::Text("RenderDoc: %s (F11 capture)", m_renderDocLoaded ? "loaded" : "not found");
        if (stats.fps > 0.0) {
            ImGui::TextDisabled("Panel submits as view %u", static_cast<unsigned>(kDebugUiView));
        }
    }
    ImGui::End();
}

gfx::TextureHandle DebugUI::textureFor(uint64_t id) const {
    if (id == 0) {
        return {};
    }
    const std::size_t index = static_cast<std::size_t>(id - 1);
    return index < m_textures.size() ? m_textures[index] : gfx::TextureHandle{};
}

void DebugUI::syncTextures(ImDrawData& drawData) {
    if (drawData.Textures == nullptr || m_resources == nullptr) {
        return;
    }

    for (ImTextureData* texture : *drawData.Textures) {
        if (texture == nullptr) {
            continue;
        }

        switch (texture->Status) {
        case ImTextureStatus_WantCreate: {
            if (texture->Format != ImTextureFormat_RGBA32 || texture->Pixels == nullptr) {
                HBG_LOGW("debug ui: unsupported ImGui texture format {}", static_cast<int>(texture->Format));
                continue;
            }
            gfx::TextureDesc desc;
            desc.width = static_cast<uint16_t>(texture->Width);
            desc.height = static_cast<uint16_t>(texture->Height);
            desc.format = gfx::TextureFormat::RGBA8;
            desc.flags = BGFX_SAMPLER_UVW_CLAMP;
            desc.data = texture->GetPixels();
            desc.dataSize = static_cast<uint32_t>(texture->GetSizeInBytes());
            desc.name = "debugui.fontAtlas";
            const gfx::TextureHandle handle = m_resources->createTexture(desc);
            if (!handle.valid()) {
                HBG_LOGE("debug ui: failed to create font atlas texture");
                continue;
            }
            const std::size_t slot = m_textures.size();
            m_textures.push_back(handle);
            texture->SetTexID(static_cast<ImTextureID>(slot + 1));
            texture->SetStatus(ImTextureStatus_OK);
            break;
        }
        case ImTextureStatus_WantUpdates: {
            const gfx::TextureHandle handle = textureFor(static_cast<uint64_t>(texture->GetTexID()));
            if (!handle.valid()) {
                break;
            }
            const uint32_t pitch = static_cast<uint32_t>(texture->GetPitch());
            for (const ImTextureRect& rect : texture->Updates) {
                if (rect.w == 0 || rect.h == 0) {
                    continue;
                }
                const void* pixels = texture->GetPixelsAt(rect.x, rect.y);
                m_resources->updateTexture(handle, pixels, pitch * rect.h, rect.w, rect.h, static_cast<uint16_t>(pitch),
                                           0, 0, rect.x, rect.y);
            }
            texture->SetStatus(ImTextureStatus_OK);
            break;
        }
        case ImTextureStatus_WantDestroy: {
            const gfx::TextureHandle handle = textureFor(static_cast<uint64_t>(texture->GetTexID()));
            if (handle.valid()) {
                m_resources->destroyTexture(handle);
                const std::size_t index = static_cast<std::size_t>(texture->GetTexID() - 1);
                if (index < m_textures.size()) {
                    m_textures[index] = {};
                }
            }
            texture->SetTexID(ImTextureID_Invalid);
            texture->SetStatus(ImTextureStatus_Destroyed);
            break;
        }
        case ImTextureStatus_OK:
        case ImTextureStatus_Destroyed:
        default:
            break;
        }
    }
}

void DebugUI::endFrame(uint16_t width, uint16_t height) {
    if (!m_valid || m_resources == nullptr) {
        return;
    }

    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData == nullptr) {
        return;
    }
    syncTextures(*drawData);

    bgfx::setViewName(kDebugUiView, "DebugUI");
    bgfx::setViewMode(kDebugUiView, bgfx::ViewMode::Sequential);
    bgfx::setViewClear(kDebugUiView, BGFX_CLEAR_NONE, 0x00000000, 1.0f, 0);
    bgfx::setViewRect(kDebugUiView, 0, 0, width, height);

    const Mat4 projection = orthographicRH(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f,
                                           1000.0f, bgfx::getCaps() != nullptr && bgfx::getCaps()->homogeneousDepth);
    bgfx::setViewTransform(kDebugUiView, nullptr, projection.data());
    bgfx::touch(kDebugUiView);

    if (drawData->CmdListsCount > 0) {
        const gfx::Program* program = m_resources->program(m_program);
        const gfx::Uniform* sampler = m_resources->uniform(m_sampler);
        if (program == nullptr || !program->valid() || sampler == nullptr || !sampler->valid()) {
            return;
        }

        const ImVec2 displayPosition = drawData->DisplayPos;
        const ImVec2 displayScale = drawData->FramebufferScale;
        for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex) {
            const ImDrawList* drawList = drawData->CmdLists[listIndex];
            const uint32_t numVertices = static_cast<uint32_t>(drawList->VtxBuffer.Size);
            const uint32_t numIndices = static_cast<uint32_t>(drawList->IdxBuffer.Size);
            const bool index32 = sizeof(ImDrawIdx) == 4;

            if (bgfx::getAvailTransientVertexBuffer(numVertices, kUiLayout) < numVertices ||
                bgfx::getAvailTransientIndexBuffer(numIndices, index32) < numIndices) {
                HBG_LOGW("debug ui: transient buffer exhausted");
                break;
            }

            bgfx::TransientVertexBuffer vertexBuffer{};
            bgfx::TransientIndexBuffer indexBuffer{};
            bgfx::allocTransientVertexBuffer(&vertexBuffer, numVertices, kUiLayout);
            bgfx::allocTransientIndexBuffer(&indexBuffer, numIndices, index32);
            if (vertexBuffer.data == nullptr || indexBuffer.data == nullptr) {
                break;
            }

            std::memcpy(vertexBuffer.data, drawList->VtxBuffer.Data,
                        static_cast<std::size_t>(numVertices) * sizeof(ImDrawVert));
            std::memcpy(indexBuffer.data, drawList->IdxBuffer.Data,
                        static_cast<std::size_t>(numIndices) * sizeof(ImDrawIdx));

            for (const ImDrawCmd& command : drawList->CmdBuffer) {
                if (command.UserCallback != nullptr) {
                    command.UserCallback(drawList, &command);
                    continue;
                }
                if (command.ElemCount == 0) {
                    continue;
                }

                ImVec4 clipRect;
                clipRect.x = (command.ClipRect.x - displayPosition.x) * displayScale.x;
                clipRect.y = (command.ClipRect.y - displayPosition.y) * displayScale.y;
                clipRect.z = (command.ClipRect.z - displayPosition.x) * displayScale.x;
                clipRect.w = (command.ClipRect.w - displayPosition.y) * displayScale.y;
                if (clipRect.x >= static_cast<float>(width) || clipRect.y >= static_cast<float>(height) ||
                    clipRect.z <= 0.0f || clipRect.w <= 0.0f) {
                    continue;
                }

                const int32_t x = std::max(0, static_cast<int32_t>(clipRect.x));
                const int32_t y = std::max(0, static_cast<int32_t>(clipRect.y));
                const int32_t right = std::min<int32_t>(width, static_cast<int32_t>(clipRect.z));
                const int32_t bottom = std::min<int32_t>(height, static_cast<int32_t>(clipRect.w));
                if (right <= x || bottom <= y) {
                    continue;
                }

                bgfx::setScissor(static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(right - x),
                                 static_cast<uint16_t>(bottom - y));
                bgfx::setState(kUiState);
                const gfx::TextureHandle texture = textureFor(static_cast<uint64_t>(command.GetTexID()));
                if (texture.valid()) {
                    bgfx::setTexture(0, sampler->native(), m_resources->native(texture), BGFX_SAMPLER_UVW_CLAMP);
                }
                bgfx::setVertexBuffer(0, &vertexBuffer, command.VtxOffset, numVertices);
                bgfx::setIndexBuffer(&indexBuffer, command.IdxOffset, command.ElemCount);
                bgfx::submit(kDebugUiView, program->native());
            }
        }
    }

    if (m_screenshotRequested) {
        char path[64];
        std::snprintf(path, sizeof(path), "debug_ui_%04u.png", m_screenshotCount++);
        bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path);
        HBG_LOGI("debug ui: screenshot requested '{}'", path);
        m_screenshotRequested = false;
    }
}

} // namespace hbg
