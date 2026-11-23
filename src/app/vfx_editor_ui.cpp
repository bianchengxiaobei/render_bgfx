#include "app/vfx_editor_ui.h"

#include "core/log.h"
#include "vfx/compiler.h"
#include "vfx/editor.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hbg {
namespace {

using vfx::BlendMode;
using vfx::PropertyType;
using vfx::SortMode;
using vfx::SortOrder;
using vfx::SystemSpace;
using vfx::editor::ContextKind;
using vfx::editor::ContextRef;

constexpr float kNodeWidth = 270.0f;
constexpr float kHeaderHeight = 30.0f;
constexpr float kRowHeight = 21.0f;
constexpr float kNodePadding = 6.0f;

std::filesystem::path previewOutputPath(const std::filesystem::path& assetPath) {
    std::error_code error;
    std::filesystem::path directory = std::filesystem::temp_directory_path(error);
    if (error) {
        directory = assetPath.parent_path();
    }
    const std::filesystem::path absolute = std::filesystem::absolute(assetPath, error).lexically_normal();
    const std::string normalized = error ? assetPath.lexically_normal().string() : absolute.string();
    const std::size_t hash = std::hash<std::string>{}(normalized);
    return directory / ("hbg_vfx_preview_" + std::to_string(hash) + ".vfxb");
}

ImU32 contextColor(ContextKind kind) {
    switch (kind) {
    case ContextKind::Spawner:
        return IM_COL32(190, 135, 55, 255);
    case ContextKind::Init:
        return IM_COL32(58, 109, 168, 255);
    case ContextKind::Update:
        return IM_COL32(63, 137, 87, 255);
    case ContextKind::Output:
        return IM_COL32(130, 83, 158, 255);
    }
    return IM_COL32(100, 100, 100, 255);
}

const char* contextLabel(ContextKind kind) {
    switch (kind) {
    case ContextKind::Spawner:
        return "Spawn";
    case ContextKind::Init:
        return "Initialize";
    case ContextKind::Update:
        return "Update";
    case ContextKind::Output:
        return "Output";
    }
    return "Context";
}

const char* propertyTypeName(PropertyType type) {
    switch (type) {
    case PropertyType::Float:
        return "float";
    case PropertyType::Int:
        return "int";
    case PropertyType::Bool:
        return "bool";
    case PropertyType::Vector2:
        return "vector2";
    case PropertyType::Vector3:
        return "vector3";
    case PropertyType::Vector4:
        return "vector4";
    case PropertyType::Color:
        return "color";
    case PropertyType::Texture:
        return "texture";
    case PropertyType::Curve:
        return "curve";
    case PropertyType::Gradient:
        return "gradient";
    }
    return "unknown";
}

constexpr PropertyType kPropertyTypes[] = {
    PropertyType::Float,   PropertyType::Int,   PropertyType::Bool,    PropertyType::Vector2, PropertyType::Vector3,
    PropertyType::Vector4, PropertyType::Color, PropertyType::Texture, PropertyType::Curve,   PropertyType::Gradient,
};

PropertyType propertyTypeAt(int index) {
    const int count = static_cast<int>(std::size(kPropertyTypes));
    if (index < 0 || index >= count) {
        return PropertyType::Float;
    }
    return kPropertyTypes[index];
}

std::string formatValue(const vfx::Value& value, int depth = 0);

std::string formatValueObject(const std::vector<std::pair<std::string, vfx::Value>>& object, int depth) {
    std::ostringstream out;
    out << "{";
    bool first = true;
    for (const auto& [key, value] : object) {
        if (!first) {
            out << ", ";
        }
        first = false;
        out << key << ": " << formatValue(value, depth + 1);
    }
    out << "}";
    return out.str();
}

std::string formatValue(const vfx::Value& value, int depth) {
    if (depth > 3) {
        return "...";
    }
    switch (value.kind) {
    case vfx::Value::Kind::Invalid:
        return "invalid";
    case vfx::Value::Kind::Number: {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.5g", value.number);
        return buffer;
    }
    case vfx::Value::Kind::Bool:
        return value.boolean ? "true" : "false";
    case vfx::Value::Kind::String:
        return "\"" + value.string + "\"";
    case vfx::Value::Kind::Array: {
        std::ostringstream out;
        out << "[";
        for (std::size_t i = 0; i < value.array.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << formatValue(value.array[i], depth + 1);
        }
        out << "]";
        return out.str();
    }
    case vfx::Value::Kind::Object:
        return formatValueObject(value.object, depth);
    case vfx::Value::Kind::Expression:
        if (!value.expression) {
            return "expression";
        }
        if (value.expression->arguments.empty()) {
            return "{\"op\": \"" + value.expression->op + "\"}";
        }
        return "{\"op\": \"" + value.expression->op + "\", " +
               formatValueObject(value.expression->arguments, depth + 1).substr(1);
    }
    return "?";
}

void textClipped(ImDrawList* drawList, ImVec2 position, ImU32 color, const std::string& text, float size) {
    if (size < 6.0f) {
        return;
    }
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
    drawList->AddText(ImGui::GetFont(), size, position, color, buffer);
}

} // namespace

struct VfxEditorUI::Impl {
    enum class SelectionKind {
        None,
        Context,
        Block,
        Property,
    };

    struct Selection {
        SelectionKind kind = SelectionKind::None;
        ContextRef context;
        std::uint32_t block = 0;
        std::string property;
    };

    struct Row {
        ImVec2 min;
        ImVec2 max;
        bool block = false;
        std::uint32_t blockIndex = 0;
    };

    struct Node {
        ContextRef context;
        ImVec2 min;
        ImVec2 max;
        ImVec2 headerMax;
        ImU32 accent = 0;
        bool collapsed = false;
        std::vector<Row> rows;
    };

    std::unique_ptr<vfx::editor::Document> document;
    Selection selection;
    std::string status;
    std::array<char, 128> newPropertyName{};
    int newPropertyType = 0;
    std::string blockTypeChoice;
    std::uint32_t nameSystem = vfx::editor::kAppend;
    std::string nameBuffer;
    std::unordered_map<std::string, std::string> textEdits;
    bool dragging = false;
    ContextRef dragContext;
    ImVec2 dragStartMouse;
    ImVec2 dragStartPosition;

    VfxEditorCompileOptions compileOptions;
    VfxEditorReloadCallback reloadCallback;
    VfxEditorPreviewCallback previewCallback;
    VfxEditorPlaybackCallback playbackCallback;
    VfxEditorPlaybackStatusProvider playbackStatusProvider;
    VfxEditorDiagnosticsProvider diagnosticsProvider;
    std::vector<vfx::ParseError> compileErrors;
    std::string compileError;
    std::string compileStatus;
    std::string playbackError;
    std::filesystem::path compiledBinaryPath;
    std::filesystem::path previewBinaryPath;
    double compileMilliseconds = 0.0;
    int focusedCompileError = -1;
    bool compiled = false;
    bool previewing = false;
    std::optional<ContextRef> pendingFocus;

    bool valid() const { return document != nullptr; }

    bool dirty() const { return document != nullptr && (document->dirty() || document->layoutDirty()); }

    void resetTextEdits() {
        textEdits.clear();
        nameSystem = vfx::editor::kAppend;
        nameBuffer.clear();
    }

    std::string& textBuffer(const std::string& key, std::string_view current) {
        auto found = textEdits.find(key);
        if (found == textEdits.end()) {
            found = textEdits.emplace(key, std::string(current)).first;
        }
        return found->second;
    }

    bool textField(const char* label, const std::string& key, std::string_view current, std::string& committed,
                   ImGuiInputTextFlags flags = 0, bool multiline = false) {
        std::string& buffer = textBuffer(key, current);
        std::array<char, 32768> data{};
        std::snprintf(data.data(), data.size(), "%s", buffer.c_str());
        bool submitted = false;
        if (multiline) {
            ImGui::InputTextMultiline(label, data.data(), data.size(), ImVec2(-1.0f, 96.0f), flags);
        } else {
            submitted = ImGui::InputText(label, data.data(), data.size(), flags | ImGuiInputTextFlags_EnterReturnsTrue);
        }
        const bool changed = ImGui::IsItemEdited();
        if (changed) {
            buffer = data.data();
        }
        if (ImGui::IsItemDeactivatedAfterEdit() || submitted) {
            buffer = data.data();
            committed = buffer;
            return true;
        }
        return false;
    }

    void clampSelection() {
        const vfx::Graph& graph = document->graph();
        if (selection.kind == SelectionKind::Property) {
            const bool exists =
                std::any_of(graph.properties.begin(), graph.properties.end(),
                            [&](const vfx::Property& property) { return property.name == selection.property; });
            if (!exists) {
                selection = Selection{};
            }
            return;
        }
        if (selection.kind == SelectionKind::None) {
            return;
        }
        if (selection.context.system >= graph.systems.size()) {
            selection = Selection{};
            return;
        }
        const vfx::SystemDesc& system = graph.systems[selection.context.system];
        if (selection.context.kind == ContextKind::Output) {
            if (selection.context.output >= system.outputs.size()) {
                selection = Selection{};
            }
            return;
        }
        if (selection.kind == SelectionKind::Block) {
            const auto& blocks = selection.context.kind == ContextKind::Init ? system.initBlocks : system.updateBlocks;
            if (selection.block >= blocks.size()) {
                selection.kind = SelectionKind::Context;
                selection.block = 0;
            }
        }
    }

    void undoEdit() {
        if (!document->undo()) {
            status = "nothing to undo";
            return;
        }
        resetTextEdits();
        clampSelection();
        status = "undo";
    }

    void redoEdit() {
        if (!document->redo()) {
            status = "nothing to redo";
            return;
        }
        resetTextEdits();
        clampSelection();
        status = "redo";
    }

    bool saveAll(std::string& error) {
        if (!document->save(error)) {
            return false;
        }
        return document->saveLayout(error);
    }

    bool writeBinary(const std::filesystem::path& path, const std::vector<std::byte>& binary,
                     std::string& error) const {
        std::filesystem::path temporary = path;
        temporary += ".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file.good()) {
                error = "failed to write '" + temporary.string() + "'";
                return false;
            }
            file.write(reinterpret_cast<const char*>(binary.data()),
                       static_cast<std::streamsize>(binary.size()));
            if (!file.good()) {
                error = "failed to write '" + temporary.string() + "'";
                return false;
            }
        }
        std::error_code filesystemError;
        std::filesystem::remove(path, filesystemError);
        filesystemError.clear();
        std::filesystem::rename(temporary, path, filesystemError);
        if (filesystemError) {
            std::filesystem::remove(temporary, filesystemError);
            error = "failed to replace '" + path.string() + "': " + filesystemError.message();
            return false;
        }
        return true;
    }

    bool compileJson(std::string_view json, const std::filesystem::path& sourcePath,
                     const std::filesystem::path& outputPath, bool preview, std::string& error) {
        error.clear();
        compileErrors.clear();
        compileError.clear();
        compileStatus.clear();
        compiledBinaryPath.clear();
        focusedCompileError = -1;
        compiled = false;

        if (compileOptions.shaderc.empty()) {
            compileError = "shaderc is not configured (set --vfx-shaderc or HBG_SHADERC)";
            compileStatus = "compile unavailable";
            HBG_LOGW("VFX editor compile unavailable: {}", compileError);
            error = compileError;
            return false;
        }
        if (compileOptions.bgfxSource.empty()) {
            compileError = "bgfx source is not configured (set --vfx-bgfx-src or HBG_BGFX_SOURCE_DIR)";
            compileStatus = "compile unavailable";
            HBG_LOGW("VFX editor compile unavailable: {}", compileError);
            error = compileError;
            return false;
        }

        vfx::CompileOptions options;
        options.shaderc = compileOptions.shaderc;
        options.bgfxSource = compileOptions.bgfxSource;
        options.shaderPack = compileOptions.shaderPack;

        const auto start = std::chrono::steady_clock::now();
        vfx::CompileResult result = vfx::compileGraph(json, options, sourcePath.string());
        compileMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        compileErrors = result.errors;
        if (!result.ok()) {
            compileError = result.error;
            compileStatus = preview ? "preview compile failed" : "compile failed";
            HBG_LOGW("VFX editor {} failed after {} ms: {}", preview ? "preview" : "compile",
                     static_cast<int>(compileMilliseconds), compileError);
            error = compileError;
            return false;
        }

        if (!writeBinary(outputPath, result.binary, error)) {
            compileError = error;
            compileStatus = preview ? "preview write failed" : "compile write failed";
            return false;
        }
        compiledBinaryPath = outputPath;
        compiled = true;
        previewing = preview;
        if (!preview) {
            previewBinaryPath.clear();
        }
        compileStatus = std::string(preview ? "preview compiled " : "compiled ") +
                        std::to_string(result.binary.size()) + " bytes in " +
                        std::to_string(static_cast<int>(compileMilliseconds)) + " ms";

        if (preview) {
            previewBinaryPath = outputPath;
            if (!previewCallback) {
                compileError = "preview callback is not configured";
                compileStatus += "; preview unavailable";
                compiled = false;
                error = compileError;
                return false;
            }
            std::string previewError;
            if (previewCallback(document->assetPath(), outputPath, previewError)) {
                compileStatus += "; preview loaded";
            } else {
                compileError = previewError.empty() ? "preview effect replacement failed" : previewError;
                compileStatus += "; preview failed: " + compileError;
                compiled = false;
                error = compileError;
                return false;
            }
        } else if (reloadCallback) {
            std::string reloadError;
            if (reloadCallback(compiledBinaryPath, reloadError)) {
                compileStatus += "; hot reloaded";
            } else if (!reloadError.empty()) {
                compileStatus += "; hot reload skipped: " + reloadError;
            }
        }

        HBG_LOGI("VFX editor {}: '{}' -> '{}': {}", preview ? "preview" : "compile",
                 document->assetPath().string(), compiledBinaryPath.string(), compileStatus);
        return true;
    }

    bool compileGraph(std::string& error) {
        std::ifstream file(document->assetPath(), std::ios::binary);
        if (!file.good()) {
            error = "failed to read '" + document->assetPath().string() + "'";
            compileError = error;
            compileStatus = "compile read failed";
            return false;
        }
        std::ostringstream contents;
        contents << file.rdbuf();
        if (!file.good() && !file.eof()) {
            error = "failed to read '" + document->assetPath().string() + "'";
            compileError = error;
            compileStatus = "compile read failed";
            return false;
        }
        return compileJson(contents.str(), document->assetPath(),
                           vfx::editor::defaultBinaryPath(document->assetPath()), false, error);
    }

    bool previewGraph(std::string& error) {
        return compileJson(document->serialize(), document->assetPath(),
                           previewOutputPath(document->assetPath()), true, error);
    }

    void saveGraph(bool compileAfterSave) {
        std::string error;
        if (!saveAll(error)) {
            status = "save failed: " + error;
            return;
        }
        status = "saved graph and layout";
        if (compileAfterSave && compileOptions.compileOnSave) {
            if (!compileGraph(error)) {
                status = "saved; compile failed: " + error;
            } else {
                status = compileStatus;
            }
        }
    }

    void compileAndSave() {
        std::string error;
        if (dirty()) {
            if (!saveAll(error)) {
                status = "save failed: " + error;
                return;
            }
        }
        if (!compileGraph(error)) {
            status = "compile failed: " + error;
        } else {
            status = compileStatus;
        }
    }

    void focusCompileError(const vfx::ParseError& error) {
        const auto focus = vfx::editor::errorFocus(error.path);
        if (!focus.has_value()) {
            status = "error has no focusable graph path";
            return;
        }
        const vfx::Graph& graph = document->graph();
        if (focus->property >= 0) {
            const auto index = static_cast<std::size_t>(focus->property);
            if (index < graph.properties.size()) {
                selection.kind = SelectionKind::Property;
                selection.property = graph.properties[index].name;
                selection.context = ContextRef{};
                selection.block = 0;
                pendingFocus.reset();
                resetTextEdits();
                status = "focused " + error.path;
            }
            return;
        }
        if (!focus->context.has_value() || focus->context->system >= graph.systems.size()) {
            return;
        }
        selection.kind = SelectionKind::Context;
        selection.context = *focus->context;
        selection.block = 0;
        selection.property.clear();
        if (focus->block != vfx::editor::kAppend) {
            const vfx::SystemDesc& system = graph.systems[focus->context->system];
            const auto& blocks = focus->context->kind == ContextKind::Init ? system.initBlocks
                                                                           : system.updateBlocks;
            if (focus->block < blocks.size()) {
                selection.kind = SelectionKind::Block;
                selection.block = focus->block;
            }
        }
        pendingFocus = focus->context;
        resetTextEdits();
        status = "focused " + error.path;
    }

    bool performPlayback(VfxEditorPlaybackAction action, float value) {
        if (!playbackCallback) {
            playbackError = "playback is not connected to a scene VFX runtime";
            return false;
        }
        std::string error;
        if (!playbackCallback(document->assetPath(), action, value, error)) {
            playbackError = error.empty() ? "playback command failed" : error;
            status = playbackError;
            return false;
        }
        playbackError.clear();
        return true;
    }

    void drawPlaybackPanel() {
        if (!ImGui::CollapsingHeader("Playback", ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }

        VfxEditorPlaybackStatus playback;
        if (playbackStatusProvider) {
            playback = playbackStatusProvider(document->assetPath());
        }

        if (!playback.available) {
            ImGui::TextDisabled("No scene VFX instance uses this asset.");
            ImGui::TextDisabled("Use Preview to create an editor-only effect.");
        }
        ImGui::BeginDisabled(!playback.available);
        if (ImGui::Button(playback.paused ? "Play" : "Pause")) {
            performPlayback(VfxEditorPlaybackAction::TogglePlayPause, 0.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Step")) {
            performPlayback(VfxEditorPlaybackAction::Step, 0.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Replay")) {
            performPlayback(VfxEditorPlaybackAction::Restart, 0.0f);
        }
        ImGui::EndDisabled();

        float playRate = playback.playRate;
        ImGui::BeginDisabled(!playback.available);
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::SliderFloat("Time scale", &playRate, 0.0f, 4.0f, "%.2fx")) {
            performPlayback(VfxEditorPlaybackAction::SetPlayRate, playRate);
        }
        ImGui::EndDisabled();
        if (playback.instances > 0) {
            ImGui::TextDisabled("instances: %u", playback.instances);
            ImGui::SameLine();
            ImGui::TextDisabled("seed: %u", playback.randomSeed);
            if (playback.preview) {
                ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.6f, 1.0f), "preview binary");
            }
        } else {
            ImGui::TextDisabled("seed: unavailable");
        }
        if (playback.boundsValid) {
            ImGui::Text("Bounds min (%.2f, %.2f, %.2f)", playback.boundsMin[0], playback.boundsMin[1],
                        playback.boundsMin[2]);
            ImGui::Text("Bounds max (%.2f, %.2f, %.2f)", playback.boundsMax[0], playback.boundsMax[1],
                        playback.boundsMax[2]);
        }
        if (!playbackError.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "%s", playbackError.c_str());
        }
    }

    void previewCurrent() {
        std::string error;
        if (!previewGraph(error)) {
            status = "preview failed: " + error;
        } else {
            status = compileStatus;
        }
    }

    void drawCompilePanel() {
        const bool hasError = !compileErrors.empty() || !compileError.empty();
        const ImGuiTreeNodeFlags flags =
            hasError ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
        ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
        if (!ImGui::CollapsingHeader("Compile", flags)) {
            return;
        }
        if (ImGui::Button("Compile")) {
            compileAndSave();
        }
        ImGui::SameLine();
        if (ImGui::Button("Preview")) {
            previewCurrent();
        }
        ImGui::SameLine();
        ImGui::Checkbox("On Save", &compileOptions.compileOnSave);
        if (!compiledBinaryPath.empty()) {
            ImGui::TextDisabled("%s%s", previewing ? "preview binary: " : "binary: ",
                                compiledBinaryPath.string().c_str());
        }
        if (!compileStatus.empty()) {
            if (compiled) {
                ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.6f, 1.0f), "%s", compileStatus.c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.35f, 1.0f), "%s", compileStatus.c_str());
            }
        }
        if (!compileError.empty()) {
            if (!compileErrors.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "Errors");
                for (std::size_t i = 0; i < compileErrors.size(); ++i) {
                    const vfx::ParseError& error = compileErrors[i];
                    const std::string label =
                        vfx::formatParseError(error) + "##compile_error_" + std::to_string(i);
                    const bool selected = focusedCompileError == static_cast<int>(i);
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        focusedCompileError = static_cast<int>(i);
                        focusCompileError(error);
                    }
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "Error");
                ImGui::TextWrapped("%s", compileError.c_str());
            }
        }
    }

    void draw() {
        if (!document) {
            return;
        }
        drawGraphWindow();
        drawInspectorWindow();
    }

    void drawGraphWindow() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float height = std::max(viewport->WorkSize.y, 240.0f);
        const float inspectorWidth = std::clamp(viewport->WorkSize.x * 0.30f, 320.0f, 440.0f);
        const float graphWidth = std::max(viewport->WorkSize.x - inspectorWidth, 240.0f);
        ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(graphWidth, height), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("VFX Graph", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            drawToolbar();
            drawCanvas(ImVec2(0.0f, 0.0f), ImVec2(0.0f, 0.0f));
        }
        ImGui::End();
    }

    void drawToolbar() {
        if (ImGui::Button("Save")) {
            saveGraph(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Compile")) {
            compileAndSave();
        }
        ImGui::SameLine();
        if (ImGui::Button("Undo")) {
            undoEdit();
        }
        ImGui::SameLine();
        if (ImGui::Button("Redo")) {
            redoEdit();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add System")) {
            if (document->addSystem()) {
                clampSelection();
                selection = Selection{};
                selection.kind = SelectionKind::Context;
                selection.context = ContextRef{static_cast<std::uint32_t>(document->graph().systems.size() - 1),
                                               ContextKind::Update, 0};
                resetTextEdits();
                status = "system added";
            } else {
                status = "add system failed: " + document->lastError();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Layout")) {
            document->resetLayout();
            status = "layout reset";
        }
        ImGui::SameLine();
        if (dirty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.35f, 1.0f), "unsaved changes");
        } else {
            ImGui::TextDisabled("saved");
        }
        if (!status.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", status.c_str());
        }
        ImGui::SameLine();
        ImGui::TextDisabled("  MMB/right pan, wheel zoom, double-click header collapse");

        const ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput && io.KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
                if (io.KeyShift) {
                    redoEdit();
                } else {
                    undoEdit();
                }
            } else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
                redoEdit();
            } else if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
                saveGraph(true);
            }
        }
    }

    void drawCanvas(ImVec2, ImVec2) {
        ImGui::BeginChild("graph_canvas", ImVec2(0.0f, 0.0f), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 canvasMax(origin.x + available.x, origin.y + available.y);
        drawList->AddRectFilled(origin, canvasMax, IM_COL32(28, 29, 32, 255));

        vfx::editor::ViewLayout view = document->view();
        bool viewChanged = false;
        const ImGuiIO& io = ImGui::GetIO();
        const bool canvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

        if (canvasHovered && io.MouseWheel != 0.0f) {
            const ImVec2 local(io.MousePos.x - origin.x, io.MousePos.y - origin.y);
            const float oldZoom = view.zoom;
            const float worldX = local.x / oldZoom - view.pan.x;
            const float worldY = local.y / oldZoom - view.pan.y;
            view.zoom = std::clamp(oldZoom * std::pow(1.1f, io.MouseWheel), 0.25f, 2.5f);
            view.pan.x = local.x / view.zoom - worldX;
            view.pan.y = local.y / view.zoom - worldY;
            viewChanged = true;
        }

        if (canvasHovered &&
            (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right))) {
            view.pan.x += io.MouseDelta.x / view.zoom;
            view.pan.y += io.MouseDelta.y / view.zoom;
            viewChanged = true;
        }
        if (viewChanged) {
            document->setViewLayout(view);
        }

        drawList->PushClipRect(origin, canvasMax, true);
        std::vector<Node> nodes = buildNodes(origin, view);
        if (pendingFocus.has_value()) {
            const Node* target = findNode(nodes, *pendingFocus);
            if (target != nullptr) {
                const ImVec2 nodeCenter((target->min.x + target->max.x) * 0.5f,
                                        (target->min.y + target->max.y) * 0.5f);
                const ImVec2 canvasCenter(origin.x + available.x * 0.5f, origin.y + available.y * 0.5f);
                const float zoom = view.zoom > 0.0f ? view.zoom : 1.0f;
                view.pan.x += (canvasCenter.x - nodeCenter.x) / zoom;
                view.pan.y += (canvasCenter.y - nodeCenter.y) / zoom;
                document->setViewLayout(view);
                nodes = buildNodes(origin, view);
            }
            pendingFocus.reset();
        }
        drawGrid(drawList, origin, available, view);
        drawEdges(drawList, nodes);
        for (const Node& node : nodes) {
            drawNode(drawList, node);
        }
        handleCanvasInput(nodes, origin, available, view);
        drawList->PopClipRect();

        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(available);
        ImGui::EndChild();
    }

    void drawGrid(ImDrawList* drawList, ImVec2 origin, ImVec2 available, const vfx::editor::ViewLayout& view) {
        const float step = 40.0f * view.zoom;
        if (step < 8.0f) {
            return;
        }
        const ImU32 color = IM_COL32(42, 44, 49, 255);
        float startX = std::fmod(view.pan.x * view.zoom, step);
        if (startX < 0.0f) {
            startX += step;
        }
        for (float x = startX; x < available.x; x += step) {
            drawList->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, origin.y + available.y), color);
        }
        float startY = std::fmod(view.pan.y * view.zoom, step);
        if (startY < 0.0f) {
            startY += step;
        }
        for (float y = startY; y < available.y; y += step) {
            drawList->AddLine(ImVec2(origin.x, origin.y + y), ImVec2(origin.x + available.x, origin.y + y), color);
        }
    }

    ImVec2 toScreen(ImVec2 origin, const vfx::editor::ViewLayout& view, vfx::Vec2 position) const {
        return ImVec2(origin.x + (position.x + view.pan.x) * view.zoom,
                      origin.y + (position.y + view.pan.y) * view.zoom);
    }

    std::vector<Node> buildNodes(ImVec2 origin, const vfx::editor::ViewLayout& view) {
        std::vector<Node> nodes;
        const vfx::Graph& graph = document->graph();
        nodes.reserve(document->layout().size());
        for (const vfx::editor::NodeLayout& layoutNode : document->layout()) {
            const ContextRef context = layoutNode.context;
            if (context.system >= graph.systems.size()) {
                continue;
            }
            const vfx::SystemDesc& system = graph.systems[context.system];
            if (context.kind == ContextKind::Output && context.output >= system.outputs.size()) {
                continue;
            }
            Node node;
            node.context = context;
            node.collapsed = layoutNode.collapsed;
            node.accent = contextColor(context.kind);
            const ImVec2 position = toScreen(origin, view, layoutNode.position);
            const float zoom = view.zoom;
            const float rowHeight = kRowHeight * zoom;
            const float padding = kNodePadding * zoom;
            const float headerHeight = kHeaderHeight * zoom;
            node.min = position;
            node.headerMax = ImVec2(position.x + kNodeWidth * zoom, position.y + headerHeight);
            std::vector<std::string> rows;
            bool hasBlocks = false;
            if (!node.collapsed) {
                if (context.kind == ContextKind::Spawner) {
                    char buffer[96];
                    std::snprintf(buffer, sizeof(buffer), "rate %.2f", system.spawner.rate);
                    rows.emplace_back(buffer);
                    std::snprintf(buffer, sizeof(buffer), "bursts %zu", system.spawner.bursts.size());
                    rows.emplace_back(buffer);
                } else if (context.kind == ContextKind::Init || context.kind == ContextKind::Update) {
                    const auto& blocks = context.kind == ContextKind::Init ? system.initBlocks : system.updateBlocks;
                    hasBlocks = !blocks.empty();
                    if (blocks.empty()) {
                        rows.emplace_back("(no blocks)");
                    } else {
                        for (const vfx::Block& block : blocks) {
                            rows.push_back((block.enabled ? "" : "[off] ") + block.type);
                        }
                    }
                } else {
                    const vfx::OutputDesc& output = system.outputs[context.output];
                    const char* blend = output.blend == BlendMode::Alpha      ? "alpha"
                                        : output.blend == BlendMode::Additive ? "additive"
                                                                              : "premultiply";
                    rows.emplace_back(std::string("type quad  blend ") + blend);
                    rows.emplace_back(output.lit ? "lit" : "unlit");
                    rows.emplace_back("soft " + std::string(output.softParticles ? "on" : "off"));
                    rows.emplace_back("texture " +
                                      std::string(output.texture.empty() ? "(white)" : output.texture.c_str()));
                }
            }
            const float bodyHeight = node.collapsed ? 0.0f : padding * 2.0f + rowHeight * rows.size();
            node.max = ImVec2(node.min.x + kNodeWidth * zoom, node.headerMax.y + bodyHeight);
            if (!node.collapsed) {
                float y = node.headerMax.y + padding;
                for (std::size_t i = 0; i < rows.size(); ++i) {
                    Row row;
                    row.min = ImVec2(node.min.x + padding, y);
                    row.max = ImVec2(node.max.x - padding, y + rowHeight);
                    row.block = hasBlocks;
                    row.blockIndex = static_cast<std::uint32_t>(i);
                    node.rows.push_back(row);
                    y += rowHeight;
                }
            }
            nodes.push_back(std::move(node));
        }
        return nodes;
    }

    const Node* findNode(const std::vector<Node>& nodes, const ContextRef& context) const {
        const auto found =
            std::find_if(nodes.begin(), nodes.end(), [&](const Node& node) { return node.context == context; });
        return found == nodes.end() ? nullptr : &*found;
    }

    void drawEdges(ImDrawList* drawList, const std::vector<Node>& nodes) const {
        const ImU32 color = IM_COL32(115, 118, 125, 255);
        const vfx::Graph& graph = document->graph();
        for (std::uint32_t system = 0; system < graph.systems.size(); ++system) {
            const Node* spawn = findNode(nodes, ContextRef{system, ContextKind::Spawner, 0});
            const Node* init = findNode(nodes, ContextRef{system, ContextKind::Init, 0});
            const Node* update = findNode(nodes, ContextRef{system, ContextKind::Update, 0});
            if (spawn != nullptr && init != nullptr) {
                drawEdge(drawList, *spawn, *init, color);
            }
            if (init != nullptr && update != nullptr) {
                drawEdge(drawList, *init, *update, color);
            }
            if (update != nullptr) {
                for (std::uint32_t output = 0; output < graph.systems[system].outputs.size(); ++output) {
                    const Node* out = findNode(nodes, ContextRef{system, ContextKind::Output, output});
                    if (out != nullptr) {
                        drawEdge(drawList, *update, *out, color);
                    }
                }
            }
        }
    }

    void drawEdge(ImDrawList* drawList, const Node& from, const Node& to, ImU32 color) const {
        const ImVec2 start((from.min.x + from.max.x) * 0.5f, from.max.y);
        const ImVec2 end((to.min.x + to.max.x) * 0.5f, to.min.y);
        const float mid = (start.y + end.y) * 0.5f;
        drawList->AddBezierCubic(start, ImVec2(start.x, mid), ImVec2(end.x, mid), end, color, 2.0f);
        drawList->AddCircleFilled(end, 3.0f, color);
    }

    void drawNode(ImDrawList* drawList, const Node& node) const {
        const bool selected = (selection.kind == SelectionKind::Context || selection.kind == SelectionKind::Block) &&
                              selection.context == node.context;
        const bool hovered = ImGui::IsMouseHoveringRect(node.min, node.max);
        ImU32 border = selected ? IM_COL32(255, 196, 90, 255) : IM_COL32(20, 21, 23, 255);
        if (hovered && !selected) {
            border = IM_COL32(170, 176, 187, 255);
        }
        const float rounding = 5.0f;
        drawList->AddRectFilled(node.min, node.max, IM_COL32(43, 45, 49, 255), rounding);
        drawList->AddRectFilled(node.min, node.headerMax, node.accent, rounding, ImDrawFlags_RoundCornersTop);
        drawList->AddRect(node.min, node.max, border, rounding, 0, selected ? 2.0f : 1.0f);

        const float zoom = (node.max.x - node.min.x) / kNodeWidth;
        const float font = ImGui::GetFontSize() * zoom;
        const float pad = kNodePadding * zoom;
        const vfx::Graph& graph = document->graph();
        const vfx::SystemDesc& system = graph.systems[node.context.system];
        std::string title = system.name + " / " + contextLabel(node.context.kind);
        if (node.context.kind == ContextKind::Output) {
            title += " " + std::to_string(node.context.output);
        }
        textClipped(drawList, ImVec2(node.min.x + pad, node.min.y + 6.0f * zoom), IM_COL32(245, 247, 250, 255), title,
                    font);
        if (node.collapsed) {
            textClipped(drawList, ImVec2(node.max.x - 20.0f * zoom, node.min.y + 6.0f * zoom),
                        IM_COL32(230, 230, 230, 255), "+", font);
            return;
        }
        for (const Row& row : node.rows) {
            if (row.block) {
                const auto& blocks = node.context.kind == ContextKind::Init ? system.initBlocks : system.updateBlocks;
                const bool enabled = row.blockIndex < blocks.size() ? blocks[row.blockIndex].enabled : true;
                if (!enabled) {
                    drawList->AddRectFilled(row.min, row.max, IM_COL32(58, 45, 42, 255));
                }
            }
            const ImVec2 textPos(row.min.x, row.min.y + 2.0f * zoom);
            const std::string label =
                row.block ? getBlockLabel(node.context, row.blockIndex) : getRowLabel(node.context, row.blockIndex);
            textClipped(drawList, textPos, IM_COL32(218, 221, 227, 255), label, font);
        }
    }

    std::string getBlockLabel(const ContextRef& context, std::uint32_t block) const {
        const vfx::Graph& graph = document->graph();
        const vfx::SystemDesc& system = graph.systems[context.system];
        const auto& blocks = context.kind == ContextKind::Init ? system.initBlocks : system.updateBlocks;
        if (block >= blocks.size()) {
            return {};
        }
        return (blocks[block].enabled ? "" : "[off] ") + blocks[block].type;
    }

    std::string getRowLabel(const ContextRef& context, std::uint32_t row) const {
        const vfx::Graph& graph = document->graph();
        const vfx::SystemDesc& system = graph.systems[context.system];
        char buffer[128];
        if (context.kind == ContextKind::Spawner) {
            if (row == 0) {
                std::snprintf(buffer, sizeof(buffer), "rate %.2f", system.spawner.rate);
                return buffer;
            }
            std::snprintf(buffer, sizeof(buffer), "bursts %zu", system.spawner.bursts.size());
            return buffer;
        }
        if (context.kind == ContextKind::Output && context.output < system.outputs.size()) {
            const vfx::OutputDesc& output = system.outputs[context.output];
            switch (row) {
            case 0:
                return std::string("type quad  blend ") + (output.blend == BlendMode::Alpha      ? "alpha"
                                                           : output.blend == BlendMode::Additive ? "additive"
                                                                                                 : "premultiply");
            case 1:
                return output.lit ? "lit" : "unlit";
            case 2:
                return output.softParticles ? "soft on" : "soft off";
            default:
                return "texture " + std::string(output.texture.empty() ? "(white)" : output.texture.c_str());
            }
        }
        return {};
    }

    void handleCanvasInput(const std::vector<Node>& nodes, ImVec2 origin, ImVec2 available,
                           const vfx::editor::ViewLayout& view) {
        (void)origin;
        (void)available;
        const ImGuiIO& io = ImGui::GetIO();
        if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                dragging = false;
            }
            return;
        }

        const Node* hoveredNode = nullptr;
        const Row* hoveredRow = nullptr;
        for (const Node& node : nodes) {
            if (ImGui::IsMouseHoveringRect(node.min, node.max)) {
                hoveredNode = &node;
                for (const Row& row : node.rows) {
                    if (ImGui::IsMouseHoveringRect(row.min, row.max)) {
                        hoveredRow = &row;
                        break;
                    }
                }
                break;
            }
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hoveredRow != nullptr && hoveredNode != nullptr) {
                if (hoveredRow->block) {
                    selection.kind = SelectionKind::Block;
                    selection.context = hoveredNode->context;
                    selection.block = hoveredRow->blockIndex;
                    selection.property.clear();
                    dragging = false;
                    return;
                }
            }
            if (hoveredNode != nullptr) {
                selection.kind = SelectionKind::Context;
                selection.context = hoveredNode->context;
                selection.property.clear();
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                    ImGui::IsMouseHoveringRect(hoveredNode->min, hoveredNode->headerMax)) {
                    document->setNodeCollapsed(hoveredNode->context, !hoveredNode->collapsed);
                    dragging = false;
                } else if (ImGui::IsMouseHoveringRect(hoveredNode->min, hoveredNode->headerMax)) {
                    dragging = true;
                    dragContext = hoveredNode->context;
                    dragStartMouse = io.MousePos;
                    const vfx::editor::NodeLayout* layout = findLayout(hoveredNode->context);
                    dragStartPosition = layout != nullptr ? ImVec2(static_cast<float>(layout->position.x),
                                                                   static_cast<float>(layout->position.y))
                                                          : ImVec2(0.0f, 0.0f);
                }
                return;
            }
            selection = Selection{};
            dragging = false;
        }

        if (dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float zoom = view.zoom > 0.0f ? view.zoom : 1.0f;
            const vfx::Vec2 next{dragStartPosition.x + (io.MousePos.x - dragStartMouse.x) / zoom,
                                 dragStartPosition.y + (io.MousePos.y - dragStartMouse.y) / zoom};
            document->setNodePosition(dragContext, next);
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            dragging = false;
        }
        if (hoveredNode != nullptr) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
    }

    const vfx::editor::NodeLayout* findLayout(const ContextRef& context) const {
        for (const vfx::editor::NodeLayout& node : document->layout()) {
            if (node.context == context) {
                return &node;
            }
        }
        return nullptr;
    }

    void drawInspectorWindow() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float inspectorWidth = std::clamp(viewport->WorkSize.x * 0.30f, 320.0f, 440.0f);
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - inspectorWidth, viewport->WorkPos.y),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(inspectorWidth, viewport->WorkSize.y), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("VFX Inspector")) {
            drawInspector();
        }
        ImGui::End();
    }

    void drawInspector() {
        ImGui::Text("%s", document->graph().name.c_str());
        ImGui::TextDisabled("%s", document->assetPath().string().c_str());
        if (document->layoutStale()) {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.35f, 1.0f), "layout sidecar hash is stale; save to refresh");
        }
        ImGui::Separator();
        drawPlaybackPanel();
        ImGui::Separator();
        drawCompilePanel();
        ImGui::Separator();
        drawRuntimeDiagnostics();
        ImGui::Separator();
        drawPropertyList();
        ImGui::Separator();
        if (selection.kind == SelectionKind::Property) {
            drawPropertyInspector();
        } else if (selection.kind == SelectionKind::None) {
            drawGraphSummary();
        } else {
            drawContextInspector();
        }
        ImGui::Separator();
        drawDiagnostics();
    }

    void drawPropertyList() {
        if (ImGui::CollapsingHeader("Blackboard", ImGuiTreeNodeFlags_DefaultOpen)) {
            const vfx::Graph& graph = document->graph();
            if (graph.properties.empty()) {
                ImGui::TextDisabled("(no exposed properties)");
            }
            for (const vfx::Property& property : graph.properties) {
                ImGui::PushID(property.name.c_str());
                const bool selected = selection.kind == SelectionKind::Property && selection.property == property.name;
                if (ImGui::Selectable((property.name + "  (" + propertyTypeName(property.type) + ")").c_str(),
                                      selected)) {
                    selection.kind = SelectionKind::Property;
                    selection.property = property.name;
                }
                ImGui::PopID();
            }
            ImGui::Separator();
            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputTextWithHint("##new_property", "new property", newPropertyName.data(), newPropertyName.size());
            ImGui::SameLine();
            std::vector<const char*> typeLabels;
            typeLabels.reserve(std::size(kPropertyTypes));
            for (PropertyType type : kPropertyTypes) {
                typeLabels.push_back(propertyTypeName(type));
            }
            ImGui::SetNextItemWidth(110.0f);
            ImGui::Combo("##property_type", &newPropertyType, typeLabels.data(), static_cast<int>(typeLabels.size()));
            ImGui::SameLine();
            if (ImGui::Button("+ Property")) {
                if (document->addProperty(newPropertyName.data(), propertyTypeAt(newPropertyType))) {
                    status = "property added";
                    newPropertyName[0] = '\0';
                } else {
                    status = "add property failed: " + document->lastError();
                }
            }
        }
    }

    void drawGraphSummary() {
        const vfx::Graph& graph = document->graph();
        ImGui::Text("Graph");
        ImGui::Text("Version: %u  Systems: %zu  Properties: %zu", graph.version, graph.systems.size(),
                    graph.properties.size());
        ImGui::Text("Custom attributes: %zu", graph.attributes.size());
        ImGui::TextDisabled("Select a context, block or property to edit it.");
        for (std::size_t i = 0; i < graph.systems.size(); ++i) {
            ImGui::BulletText("%s  capacity=%u  space=%s", graph.systems[i].name.c_str(),
                              graph.systems[i].requestedCapacity, vfx::toString(graph.systems[i].space));
        }
    }

    void drawContextInspector() {
        const vfx::Graph& graph = document->graph();
        if (selection.context.system >= graph.systems.size()) {
            selection = Selection{};
            drawGraphSummary();
            return;
        }
        const vfx::SystemDesc system = graph.systems[selection.context.system];
        ImGui::TextUnformatted("System");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        std::string committedName;
        const std::string nameKey = "system.name:" + std::to_string(selection.context.system);
        if (textField("##system_name", nameKey, system.name, committedName) && committedName != system.name) {
            if (document->renameSystem(selection.context.system, committedName)) {
                status = "system renamed";
            } else {
                status = "rename failed: " + document->lastError();
                textEdits[nameKey] = system.name;
            }
        }
        ImGui::SameLine();
        if (graph.systems.size() > 1) {
            if (ImGui::Button("Remove System")) {
                const std::uint32_t removed = selection.context.system;
                if (document->removeSystem(removed)) {
                    selection = Selection{};
                    selection.kind = SelectionKind::Context;
                    selection.context = ContextRef{removed > 0 ? removed - 1 : 0, ContextKind::Update, 0};
                    clampSelection();
                    resetTextEdits();
                    status = "system removed";
                } else {
                    status = "remove failed: " + document->lastError();
                }
                return;
            }
        }
        ImGui::TextDisabled("Context: %s", contextLabel(selection.context.kind));

        int capacity = static_cast<int>(std::min<std::uint32_t>(
            system.requestedCapacity, static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
        if (ImGui::DragInt("Capacity", &capacity, 8.0f, 1, 1048576, "%d", ImGuiSliderFlags_AlwaysClamp)) {
            document->setSystemCapacity(selection.context.system, static_cast<std::uint32_t>(capacity));
        }
        int space = system.space == SystemSpace::Local ? 1 : 0;
        if (ImGui::Combo("Space", &space, "World\0Local\0")) {
            document->setSystemSpace(selection.context.system, space == 1 ? SystemSpace::Local : SystemSpace::World);
        }
        int sortMode = system.sort.mode == SortMode::Distance ? 1 : system.sort.mode == SortMode::Custom ? 2 : 0;
        if (ImGui::Combo("Sort", &sortMode, "None\0Distance\0Custom\0")) {
            document->setSystemSort(selection.context.system,
                                    sortMode == 1   ? SortMode::Distance
                                    : sortMode == 2 ? SortMode::Custom
                                                    : SortMode::None,
                                    system.sort.order);
        }
        if (system.sort.mode != SortMode::None) {
            int order = system.sort.order == SortOrder::Descending ? 1 : 0;
            if (ImGui::Combo("Sort order", &order, "Ascending\0Descending\0")) {
                document->setSystemSort(selection.context.system, system.sort.mode,
                                        order == 1 ? SortOrder::Descending : SortOrder::Ascending);
            }
        }

        float center[3] = {system.bounds.center.x, system.bounds.center.y, system.bounds.center.z};
        float extents[3] = {system.bounds.extents.x, system.bounds.extents.y, system.bounds.extents.z};
        bool boundsChanged = false;
        boundsChanged = ImGui::DragFloat3("Bounds center", center, 0.1f, 0.0f, 0.0f, "%.3f") || boundsChanged;
        boundsChanged = ImGui::DragFloat3("Bounds extents", extents, 0.1f, 0.001f, std::numeric_limits<float>::max(),
                                          "%.3f", ImGuiSliderFlags_AlwaysClamp) ||
                        boundsChanged;
        if (boundsChanged) {
            document->setSystemBounds(selection.context.system, {center[0], center[1], center[2]},
                                      {extents[0], extents[1], extents[2]});
        }

        float rate = system.spawner.rate;
        if (ImGui::DragFloat("Spawn rate", &rate, 0.5f, 0.0f, 1000000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
            document->setSpawnerRate(selection.context.system, rate);
        }

        if (selection.kind == SelectionKind::Block) {
            drawBlockInspector();
            return;
        }
        if (selection.context.kind == ContextKind::Output) {
            drawOutputInspector();
            return;
        }
        if (selection.context.kind == ContextKind::Spawner) {
            drawSpawnerInspector();
            return;
        }
        drawBlockList(system, selection.context.kind);
    }

    void drawSpawnerInspector() {
        const vfx::Graph& graph = document->graph();
        const vfx::SystemDesc& system = graph.systems[selection.context.system];
        if (system.spawner.bursts.empty()) {
            ImGui::TextDisabled("No bursts.");
            return;
        }
        ImGui::Text("Bursts");
        for (std::size_t i = 0; i < system.spawner.bursts.size(); ++i) {
            const vfx::Burst& burst = system.spawner.bursts[i];
            ImGui::BulletText("#%zu t=%.3f count=%u period=%s repeat=%s", i, burst.time, burst.count,
                              burst.period ? std::to_string(*burst.period).c_str() : "once",
                              burst.repeat ? std::to_string(*burst.repeat).c_str() : "infinite");
        }
    }

    void drawBlockList(const vfx::SystemDesc& system, ContextKind context) {
        const auto& blocks = context == ContextKind::Init ? system.initBlocks : system.updateBlocks;
        ImGui::Separator();
        ImGui::Text("%s blocks (%zu)", context == ContextKind::Init ? "Init" : "Update", blocks.size());
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            const vfx::Block& block = blocks[i];
            ImGui::PushID(static_cast<int>(i));
            bool enabled = block.enabled;
            if (ImGui::Checkbox("##enabled", &enabled)) {
                document->setBlockEnabled(selection.context.system, context, static_cast<std::uint32_t>(i), enabled);
            }
            ImGui::SameLine();
            if (ImGui::Selectable(block.type.c_str())) {
                selection.kind = SelectionKind::Block;
                selection.context = ContextRef{selection.context.system, context, 0};
                selection.block = static_cast<std::uint32_t>(i);
                selection.property.clear();
            }
            ImGui::PopID();
        }

        const std::vector<std::string> types = vfx::editor::blockTypes(context);
        if (types.empty()) {
            return;
        }
        if (std::find(types.begin(), types.end(), blockTypeChoice) == types.end()) {
            blockTypeChoice = types.front();
        }
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::BeginCombo("##block_type", blockTypeChoice.c_str())) {
            for (const std::string& type : types) {
                const bool selected = type == blockTypeChoice;
                if (ImGui::Selectable(type.c_str(), selected)) {
                    blockTypeChoice = type;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Block")) {
            const std::uint32_t newIndex = static_cast<std::uint32_t>(blocks.size());
            if (document->addBlock(selection.context.system, context, blockTypeChoice, vfx::editor::kAppend)) {
                selection.kind = SelectionKind::Block;
                selection.context = ContextRef{selection.context.system, context, 0};
                selection.block = newIndex;
                status = "block added";
                return;
            }
            status = "add block failed: " + document->lastError();
        }
    }

    void drawBlockInspector() {
        const vfx::Graph& graph = document->graph();
        const ContextRef context{selection.context.system, selection.context.kind, 0};
        if (context.system >= graph.systems.size()) {
            selection = Selection{};
            return;
        }
        const vfx::SystemDesc& system = graph.systems[context.system];
        const auto& blocks = context.kind == ContextKind::Init ? system.initBlocks : system.updateBlocks;
        if (selection.block >= blocks.size()) {
            selection = Selection{};
            return;
        }
        const vfx::Block block = blocks[selection.block];
        ImGui::Separator();
        ImGui::Text("Block: %s", block.type.c_str());
        bool enabled = block.enabled;
        if (ImGui::Checkbox("Enabled", &enabled)) {
            document->setBlockEnabled(context.system, context.kind, selection.block, enabled);
        }
        if (ImGui::Button("Move Up") && selection.block > 0) {
            const std::uint32_t from = selection.block;
            if (document->moveBlock(context.system, context.kind, from, from - 1)) {
                selection.block = from - 1;
                status = "block moved";
            } else {
                status = "move failed: " + document->lastError();
            }
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Move Down") && selection.block + 1 < blocks.size()) {
            const std::uint32_t from = selection.block;
            if (document->moveBlock(context.system, context.kind, from, from + 1)) {
                selection.block = from + 1;
                status = "block moved";
            } else {
                status = "move failed: " + document->lastError();
            }
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove Block")) {
            if (document->removeBlock(context.system, context.kind, selection.block)) {
                selection.kind = SelectionKind::Context;
                selection.block = 0;
                status = "block removed";
            } else {
                status = "remove failed: " + document->lastError();
            }
            return;
        }
        ImGui::Separator();
        drawBlockParameters(block, context);
    }

    void drawBlockParameters(const vfx::Block& block, const ContextRef& context) {
        if (block.parameters.kind != vfx::Value::Kind::Object) {
            ImGui::TextWrapped("%s", formatValue(block.parameters).c_str());
            return;
        }
        for (const auto& [name, value] : block.parameters.object) {
            ImGui::PushID(name.c_str());
            drawBlockParameter(name, value, context);
            ImGui::PopID();
        }
    }

    void drawBlockParameter(const std::string& name, const vfx::Value& value, const ContextRef& context) {
        const std::string key = "block.param:" + std::to_string(context.system) + ":" +
                                std::to_string(static_cast<int>(context.kind)) + ":" + std::to_string(selection.block) +
                                ":" + name;
        switch (value.kind) {
        case vfx::Value::Kind::Number: {
            float number = static_cast<float>(value.number);
            if (ImGui::DragFloat(name.c_str(), &number, 0.01f)) {
                document->setBlockParameterNumber(context.system, context.kind, selection.block, name, -1, number);
            }
            break;
        }
        case vfx::Value::Kind::Bool: {
            bool enabled = value.boolean;
            if (ImGui::Checkbox(name.c_str(), &enabled)) {
                document->setBlockParameterBool(context.system, context.kind, selection.block, name, enabled);
            }
            break;
        }
        case vfx::Value::Kind::String: {
            std::string committed;
            ImGui::TextUnformatted(name.c_str());
            if (name == "code") {
                if (textField("##code", key, value.string, committed, 0, true)) {
                    if (!document->setBlockParameterString(context.system, context.kind, selection.block, name,
                                                           committed)) {
                        textEdits[key] = value.string;
                        status = "block parameter failed: " + document->lastError();
                    }
                }
            } else {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(180.0f);
                if (textField(("##" + name).c_str(), key, value.string, committed)) {
                    if (!document->setBlockParameterString(context.system, context.kind, selection.block, name,
                                                           committed)) {
                        textEdits[key] = value.string;
                        status = "block parameter failed: " + document->lastError();
                    }
                }
            }
            break;
        }
        case vfx::Value::Kind::Array: {
            const bool numeric = std::all_of(value.array.begin(), value.array.end(), [](const vfx::Value& item) {
                return item.kind == vfx::Value::Kind::Number;
            });
            if (!numeric || value.array.size() > 4) {
                ImGui::TextWrapped("%s: %s", name.c_str(), formatValue(value).c_str());
                break;
            }
            ImGui::TextUnformatted(name.c_str());
            for (std::size_t component = 0; component < value.array.size(); ++component) {
                ImGui::SameLine();
                ImGui::PushID(static_cast<int>(component));
                ImGui::SetNextItemWidth(64.0f);
                float number = static_cast<float>(value.array[component].number);
                if (ImGui::DragFloat("##component", &number, 0.01f)) {
                    document->setBlockParameterNumber(context.system, context.kind, selection.block, name,
                                                      static_cast<int>(component), number);
                }
                ImGui::PopID();
            }
            break;
        }
        case vfx::Value::Kind::Object:
        case vfx::Value::Kind::Expression:
        case vfx::Value::Kind::Invalid:
            ImGui::TextWrapped("%s: %s", name.c_str(), formatValue(value).c_str());
            break;
        }
    }

    void drawOutputInspector() {
        const vfx::Graph& graph = document->graph();
        const vfx::SystemDesc& system = graph.systems[selection.context.system];
        if (selection.context.output >= system.outputs.size()) {
            selection = Selection{};
            return;
        }
        const vfx::OutputDesc output = system.outputs[selection.context.output];
        ImGui::Separator();
        ImGui::Text("Output %u", selection.context.output);
        bool lit = output.lit;
        if (ImGui::Checkbox("Lit", &lit)) {
            document->setOutputLit(selection.context.system, selection.context.output, lit);
        }
        int blend = output.blend == BlendMode::Alpha ? 0 : output.blend == BlendMode::Additive ? 1 : 2;
        if (ImGui::Combo("Blend", &blend, "Alpha\0Additive\0Premultiply\0")) {
            document->setOutputBlend(selection.context.system, selection.context.output,
                                     blend == 0   ? BlendMode::Alpha
                                     : blend == 1 ? BlendMode::Additive
                                                  : BlendMode::Premultiply);
        }
        bool soft = output.softParticles;
        if (ImGui::Checkbox("Soft particles", &soft)) {
            document->setOutputSoftParticles(selection.context.system, selection.context.output, soft);
        }
        float softDistance = output.softDistance;
        if (ImGui::DragFloat("Soft distance", &softDistance, 0.01f, 0.001f, 1000.0f, "%.3f",
                             ImGuiSliderFlags_AlwaysClamp)) {
            document->setOutputSoftDistance(selection.context.system, selection.context.output, softDistance);
        }
        if (!output.metallicProperty.empty()) {
            ImGui::TextDisabled("metallic property: %s", output.metallicProperty.c_str());
        }
        float metallic = output.metallic;
        if (ImGui::DragFloat("Metallic", &metallic, 0.01f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
            document->setOutputMetallic(selection.context.system, selection.context.output, metallic);
        }
        if (!output.roughnessProperty.empty()) {
            ImGui::TextDisabled("roughness property: %s", output.roughnessProperty.c_str());
        }
        float roughness = output.roughness;
        if (ImGui::DragFloat("Roughness", &roughness, 0.01f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
            document->setOutputRoughness(selection.context.system, selection.context.output, roughness);
        }

        std::vector<std::string> textureNames{std::string()};
        for (const vfx::Property& property : graph.properties) {
            if (property.type == PropertyType::Texture) {
                textureNames.push_back(property.name);
            }
        }
        std::vector<const char*> textureLabels;
        textureLabels.reserve(textureNames.size());
        for (const std::string& name : textureNames) {
            textureLabels.push_back(name.empty() ? "(white)" : name.c_str());
        }
        int textureIndex = 0;
        const auto textureFound = std::find(textureNames.begin(), textureNames.end(), output.texture);
        if (textureFound != textureNames.end()) {
            textureIndex = static_cast<int>(std::distance(textureNames.begin(), textureFound));
        }
        if (ImGui::Combo("Texture", &textureIndex, textureLabels.data(), static_cast<int>(textureLabels.size()))) {
            document->setOutputTexture(selection.context.system, selection.context.output,
                                       textureNames[static_cast<std::size_t>(textureIndex)]);
        }

        ImGui::Separator();
        if (ImGui::Button("Add Output")) {
            const std::uint32_t newIndex = static_cast<std::uint32_t>(system.outputs.size());
            if (document->addOutput(selection.context.system)) {
                selection.context.output = newIndex;
                status = "output added";
            } else {
                status = "add output failed: " + document->lastError();
            }
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove Output")) {
            if (system.outputs.size() <= 1) {
                status = "at least one output is required";
                return;
            }
            if (document->removeOutput(selection.context.system, selection.context.output)) {
                selection.kind = SelectionKind::Context;
                selection.context = ContextRef{selection.context.system, ContextKind::Update, 0};
                status = "output removed";
            } else {
                status = "remove output failed: " + document->lastError();
            }
            return;
        }
    }

    void drawPropertyInspector() {
        const vfx::Graph& graph = document->graph();
        const auto found = std::find_if(graph.properties.begin(), graph.properties.end(),
                                        [&](const vfx::Property& p) { return p.name == selection.property; });
        if (found == graph.properties.end()) {
            selection = Selection{};
            return;
        }
        const vfx::Property property = *found;
        ImGui::Text("Property: %s", property.name.c_str());
        ImGui::TextDisabled("Type: %s", propertyTypeName(property.type));
        if (ImGui::Button("Remove Property")) {
            if (document->removeProperty(property.name)) {
                selection = Selection{};
                resetTextEdits();
                status = "property removed";
            } else {
                status = "remove property failed: " + document->lastError();
            }
            return;
        }
        switch (property.type) {
        case PropertyType::Float: {
            float value = property.defaultFloat[0];
            if (ImGui::DragFloat("Default", &value, 0.01f)) {
                document->setPropertyNumber(property.name, 0, value);
            }
            break;
        }
        case PropertyType::Int: {
            int value = static_cast<int>(property.defaultFloat[0]);
            if (ImGui::DragInt("Default", &value)) {
                document->setPropertyNumber(property.name, 0, value);
            }
            break;
        }
        case PropertyType::Bool: {
            bool value = property.defaultFloat[0] >= 0.5f;
            if (ImGui::Checkbox("Default", &value)) {
                document->setPropertyNumber(property.name, 0, value ? 1.0 : 0.0);
            }
            break;
        }
        case PropertyType::Vector2: {
            float value[2] = {property.defaultFloat[0], property.defaultFloat[1]};
            if (ImGui::DragFloat2("Default", value, 0.01f)) {
                document->setPropertyNumber(property.name, 0, value[0]);
                document->setPropertyNumber(property.name, 1, value[1]);
            }
            break;
        }
        case PropertyType::Vector3: {
            float value[3] = {property.defaultFloat[0], property.defaultFloat[1], property.defaultFloat[2]};
            if (ImGui::DragFloat3("Default", value, 0.01f)) {
                document->setPropertyNumber(property.name, 0, value[0]);
                document->setPropertyNumber(property.name, 1, value[1]);
                document->setPropertyNumber(property.name, 2, value[2]);
            }
            break;
        }
        case PropertyType::Vector4:
        case PropertyType::Color: {
            float value[4] = {property.defaultFloat[0], property.defaultFloat[1], property.defaultFloat[2],
                              property.defaultFloat[3]};
            bool changed = property.type == PropertyType::Color ? ImGui::ColorEdit4("Default", value)
                                                                : ImGui::DragFloat4("Default", value, 0.01f);
            if (changed) {
                for (std::uint32_t component = 0; component < 4; ++component) {
                    document->setPropertyNumber(property.name, component, value[component]);
                }
            }
            break;
        }
        case PropertyType::Texture: {
            std::string committed;
            const std::string key = "property.texture:" + property.name;
            ImGui::TextUnformatted("Default");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220.0f);
            if (textField("##texture_path", key, property.defaultTexture, committed)) {
                if (!document->setPropertyTexture(property.name, committed)) {
                    textEdits[key] = property.defaultTexture;
                    status = "texture update failed: " + document->lastError();
                }
            }
            break;
        }
        case PropertyType::Curve:
            ImGui::Text("Keys: %zu", property.curve.size());
            for (std::size_t i = 0; i < property.curve.size(); ++i) {
                const vfx::CurveKey& key = property.curve[i];
                ImGui::PushID(static_cast<int>(i));
                float time = key.time;
                float value = key.value;
                ImGui::SetNextItemWidth(90.0f);
                const bool timeChanged =
                    ImGui::DragFloat("Time", &time, 0.01f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90.0f);
                const bool valueChanged = ImGui::DragFloat("Value", &value, 0.01f);
                if (timeChanged || valueChanged) {
                    if (!document->setPropertyCurveKey(property.name, static_cast<std::uint32_t>(i), time, value)) {
                        status = "curve key update failed: " + document->lastError();
                    }
                }
                ImGui::SameLine();
                if (property.curve.size() > 1 && ImGui::Button("Remove")) {
                    if (document->removePropertyCurveKey(property.name, static_cast<std::uint32_t>(i))) {
                        ImGui::PopID();
                        return;
                    }
                    status = "curve key remove failed: " + document->lastError();
                }
                ImGui::PopID();
            }
            if (ImGui::Button("+ Curve Key")) {
                if (document->addPropertyCurveKey(property.name)) {
                    status = "curve key added";
                } else {
                    status = "curve key add failed: " + document->lastError();
                }
                return;
            }
            break;
        case PropertyType::Gradient:
            ImGui::Text("Keys: %zu", property.gradient.size());
            for (std::size_t i = 0; i < property.gradient.size(); ++i) {
                const vfx::GradientKey& key = property.gradient[i];
                ImGui::PushID(static_cast<int>(i));
                float time = key.time;
                float color[4] = {key.color.x, key.color.y, key.color.z, key.color.w};
                ImGui::SetNextItemWidth(90.0f);
                const bool timeChanged =
                    ImGui::DragFloat("Time", &time, 0.01f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SameLine();
                const bool colorChanged = ImGui::ColorEdit4("Color", color);
                if (timeChanged || colorChanged) {
                    if (!document->setPropertyGradientKey(property.name, static_cast<std::uint32_t>(i), time,
                                                          vfx::Vec4{color[0], color[1], color[2], color[3]})) {
                        status = "gradient key update failed: " + document->lastError();
                    }
                }
                ImGui::SameLine();
                if (property.gradient.size() > 1 && ImGui::Button("Remove")) {
                    if (document->removePropertyGradientKey(property.name, static_cast<std::uint32_t>(i))) {
                        ImGui::PopID();
                        return;
                    }
                    status = "gradient key remove failed: " + document->lastError();
                }
                ImGui::PopID();
            }
            if (ImGui::Button("+ Gradient Key")) {
                if (document->addPropertyGradientKey(property.name)) {
                    status = "gradient key added";
                } else {
                    status = "gradient key add failed: " + document->lastError();
                }
                return;
            }
            break;
        }
    }

    void drawRuntimeDiagnostics() {
        if (!diagnosticsProvider) {
            return;
        }
        const VfxEditorRuntimeDiagnostics diagnostics = diagnosticsProvider();
        const ImGuiTreeNodeFlags flags =
            diagnostics.active ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
        if (!ImGui::CollapsingHeader("Runtime", flags)) {
            return;
        }
        ImGui::Text("%s", diagnostics.active ? "VFX active" : "VFX inactive");
        ImGui::TextDisabled("effects %u  systems %u  particles %u", diagnostics.effects,
                            diagnostics.systems, diagnostics.particles);
        ImGui::TextDisabled("draws %u  dispatches %u", diagnostics.draws, diagnostics.dispatches);
        ImGui::TextDisabled("sorts %u  events %u", diagnostics.sortPasses, diagnostics.events);
        ImGui::TextDisabled("update %.3f ms", diagnostics.updateMs);
        ImGui::TextDisabled("sim %.3f ms  output %.3f ms", diagnostics.simulationMs,
                            diagnostics.outputMs);
        ImGui::TextDisabled("total %.3f ms", diagnostics.totalMs);
    }

    void drawDiagnostics() {
        if (!document->lastError().empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "Last edit error");
            ImGui::TextWrapped("%s", document->lastError().c_str());
        }
        if (!document->errors().empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.35f, 1.0f), "Validation");
            for (const vfx::ParseError& error : document->errors()) {
                ImGui::TextWrapped("%s", vfx::formatParseError(error).c_str());
            }
        }
        if (!document->layoutError().empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.35f, 1.0f), "Layout");
            ImGui::TextWrapped("%s", document->layoutError().c_str());
        }
    }
};

VfxEditorUI::VfxEditorUI() : m_impl(std::make_unique<Impl>()) {}
VfxEditorUI::~VfxEditorUI() = default;

bool VfxEditorUI::open(const std::filesystem::path& assetPath, std::string& error) {
    m_impl->document = vfx::editor::Document::load(assetPath, error);
    if (!m_impl->document) {
        return false;
    }
    m_impl->selection = {};
    const vfx::Graph& graph = m_impl->document->graph();
    if (!graph.systems.empty()) {
        m_impl->selection.kind = Impl::SelectionKind::Context;
        m_impl->selection.context = ContextRef{0, ContextKind::Update, 0};
    }
    m_impl->status.clear();
    m_impl->dragging = false;
    m_impl->resetTextEdits();
    m_impl->blockTypeChoice.clear();
    m_impl->newPropertyName[0] = '\0';
    m_impl->compileErrors.clear();
    m_impl->compileError.clear();
    m_impl->compileStatus.clear();
    m_impl->playbackError.clear();
    m_impl->compiledBinaryPath.clear();
    m_impl->previewBinaryPath.clear();
    m_impl->focusedCompileError = -1;
    m_impl->compiled = false;
    m_impl->previewing = false;
    m_impl->pendingFocus.reset();
    return true;
}

void VfxEditorUI::setCompileOptions(const VfxEditorCompileOptions& options) {
    m_impl->compileOptions = options;
}

void VfxEditorUI::setReloadCallback(VfxEditorReloadCallback callback) {
    m_impl->reloadCallback = std::move(callback);
}

void VfxEditorUI::setPreviewCallback(VfxEditorPreviewCallback callback) {
    m_impl->previewCallback = std::move(callback);
}

void VfxEditorUI::setPlaybackCallback(VfxEditorPlaybackCallback callback) {
    m_impl->playbackCallback = std::move(callback);
}

void VfxEditorUI::setPlaybackStatusProvider(VfxEditorPlaybackStatusProvider provider) {
    m_impl->playbackStatusProvider = std::move(provider);
}

void VfxEditorUI::setDiagnosticsProvider(VfxEditorDiagnosticsProvider provider) {
    m_impl->diagnosticsProvider = std::move(provider);
}

bool VfxEditorUI::compile(std::string& error) {
    if (!m_impl->valid()) {
        error = "VFX editor has no loaded graph";
        return false;
    }
    return m_impl->compileGraph(error);
}

bool VfxEditorUI::preview(std::string& error) {
    if (!m_impl->valid()) {
        error = "VFX editor has no loaded graph";
        return false;
    }
    return m_impl->previewGraph(error);
}

bool VfxEditorUI::valid() const {
    return m_impl->valid();
}
bool VfxEditorUI::dirty() const {
    return m_impl->dirty();
}
void VfxEditorUI::draw() {
    m_impl->draw();
}

} // namespace hbg

