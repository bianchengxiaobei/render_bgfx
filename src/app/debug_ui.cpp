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
                render::exposureModeAt(static_cast<std::size_t>(std::clamp(exposureMode, 0, 1)));
        }
        if (m_settings.postProcess.exposureMode == render::ExposureMode::Fixed) {
            ImGui::SliderFloat("Fixed EV100", &m_settings.postProcess.fixedEv100, -10.0f, 20.0f, "%.2f");
        } else {
            ImGui::SliderFloat("Limit min EV", &m_settings.postProcess.limitMin, -10.0f, 20.0f, "%.2f");
            ImGui::SliderFloat("Limit max EV", &m_settings.postProcess.limitMax, -10.0f, 30.0f, "%.2f");
            ImGui::TextDisabled("Auto metering: 4x4 center-weighted grid");
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
        const std::size_t modeCount = render::debugDisplayModeCount();
        const std::size_t shownModes = std::min<std::size_t>(modeCount, 16);
        const char* modeNames[16]{};
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
