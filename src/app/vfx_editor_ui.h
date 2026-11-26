#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace hbg {

struct VfxEditorCompileOptions {
    std::filesystem::path shaderc;
    std::filesystem::path bgfxSource;
    std::filesystem::path shaderPack;
    bool compileOnSave = true;
};

enum class VfxEditorPlaybackAction {
    TogglePlayPause,
    Step,
    Restart,
    SetPlayRate,
};

struct VfxEditorPlaybackStatus {
    bool available = false;
    bool paused = false;
    float playRate = 1.0f;
    std::uint32_t randomSeed = 0;
    std::uint32_t instances = 0;
    bool preview = false;
    bool boundsValid = false;
    float boundsMin[3] = {0.0f, 0.0f, 0.0f};
    float boundsMax[3] = {0.0f, 0.0f, 0.0f};
};

struct VfxEditorRuntimeDiagnostics {
    bool active = false;
    std::uint32_t effects = 0;
    std::uint32_t systems = 0;
    std::uint32_t particles = 0;
    std::uint32_t draws = 0;
    std::uint32_t dispatches = 0;
    std::uint32_t sortPasses = 0;
    std::uint32_t events = 0;
    float updateMs = 0.0f;
    float simulationMs = 0.0f;
    float outputMs = 0.0f;
    float totalMs = 0.0f;
};

using VfxEditorReloadCallback =
    std::function<bool(const std::filesystem::path& binaryPath, std::string& error)>;
using VfxEditorPreviewCallback = std::function<bool(
    const std::filesystem::path& sourceAssetPath, const std::filesystem::path& binaryPath,
    std::string& error)>;
using VfxEditorPlaybackCallback = std::function<bool(
    const std::filesystem::path& assetPath, VfxEditorPlaybackAction action, float value,
    std::string& error)>;
using VfxEditorPlaybackStatusProvider =
    std::function<VfxEditorPlaybackStatus(const std::filesystem::path& assetPath)>;
using VfxEditorDiagnosticsProvider = std::function<VfxEditorRuntimeDiagnostics()>;

class VfxEditorUI {
  public:
    VfxEditorUI();
    ~VfxEditorUI();

    VfxEditorUI(const VfxEditorUI&) = delete;
    VfxEditorUI& operator=(const VfxEditorUI&) = delete;

    void setCompileOptions(const VfxEditorCompileOptions& options);
    void setReloadCallback(VfxEditorReloadCallback callback);
    void setPreviewCallback(VfxEditorPreviewCallback callback);
    void setPlaybackCallback(VfxEditorPlaybackCallback callback);
    void setPlaybackStatusProvider(VfxEditorPlaybackStatusProvider provider);
    void setDiagnosticsProvider(VfxEditorDiagnosticsProvider provider);
    bool open(const std::filesystem::path& assetPath, std::string& error);
    bool compile(std::string& error);
    bool preview(std::string& error);
    bool saveRecovery(std::string& error);
    bool valid() const;
    bool dirty() const;
    void requestClose();
    bool closeApproved() const;
    void draw();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hbg
