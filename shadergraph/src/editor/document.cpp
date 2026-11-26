#include "sg/editor.h"

#include "editor_internal.h"

#include "sg/validate.h"

#include <algorithm>
#include <set>
#include <utility>

namespace sg::editor {
namespace {


std::string saveErrorMessage(const std::string& prefix, const std::string& detail) {
    return detail.empty() ? prefix : prefix + ": " + detail;
}

} // namespace

Document::Document() : m_impl(std::make_unique<DocumentState>()) {}

Document::~Document() = default;

std::unique_ptr<Document> Document::open(const std::filesystem::path& graphPath,
                                         const NodeRegistry& registry, std::string& error) {
    GraphLoadResult loaded = loadGraph(graphPath, registry);
    if (!loaded.ok) {
        error = saveErrorMessage("cannot load graph", loaded.error);
        return nullptr;
    }

    std::unique_ptr<Document> document(new Document());
    DocumentState& state = *document->m_impl;
    state.assetPath = graphPath;
    state.layoutPath = defaultLayoutPath(graphPath);
    state.registry = registry;
    state.graph = std::move(loaded.graph);
    state.diagnostics = std::move(loaded.diagnostics);
    state.revision = 1;

    std::error_code fileError;
    if (std::filesystem::is_regular_file(state.layoutPath, fileError)) {
        LayoutLoadResult layout = loadLayout(state.layoutPath);
        if (layout.ok) {
            state.layout = std::move(layout.layout);
            applyLayoutPositions(state);
            addMissingLayoutEntries(state);
        } else {
            state.layoutError = layout.error.empty() ? "invalid layout sidecar" : layout.error;
            syncLayoutFromGraph(state);
        }
    } else {
        syncLayoutFromGraph(state);
    }
    assignFallbackNodePositions(state);
    state.layout.revision = state.revision;
    validateState(state);
    error.clear();
    return document;
}

const std::filesystem::path& Document::assetPath() const {
    return m_impl->assetPath;
}

const std::filesystem::path& Document::layoutPath() const {
    return m_impl->layoutPath;
}

const Graph& Document::graph() const {
    return m_impl->graph;
}

const GraphLayout& Document::layout() const {
    return m_impl->layout;
}

const NodeRegistry& Document::registry() const {
    return m_impl->registry;
}

const std::vector<Diagnostic>& Document::diagnostics() const {
    return m_impl->diagnostics;
}

const std::string& Document::layoutError() const {
    return m_impl->layoutError;
}

std::uint64_t Document::revision() const {
    return m_impl->revision;
}

bool Document::dirty() const {
    return m_impl->structuralDirty || m_impl->layoutDirty;
}

bool Document::canUndo() const {
    return !m_impl->history.empty();
}

bool Document::canRedo() const {
    return !m_impl->redo.empty();
}

std::string Document::graphJson(bool pretty) const {
    SerializeOptions options;
    options.pretty = pretty;
    options.includePositions = true;
    options.includeGeneratorVersion = true;
    return serialize(m_impl->graph, options);
}

std::string Document::layoutJson() const {
    return serializeLayout(m_impl->layout);
}

std::string Document::diagnosticsJson() const {
    return serializeDiagnostics(m_impl->diagnostics);
}

std::string Document::commandHistoryJson() const {
    Json result;
    result["undo"] = m_impl->history.size();
    result["redo"] = m_impl->redo.size();
    return result.dump();
}

CommandResult Document::applyCommands(std::string_view requestJson) {
    CommandResult result;
    if (!sg::editor::applyCommands(*m_impl, requestJson, result)) {
        return result;
    }
    return result;
}

bool Document::undo(std::string& error) {
    DocumentState& state = *m_impl;
    if (state.history.empty()) {
        error = "nothing to undo";
        return false;
    }
    Snapshot current;
    current.graph = state.graph;
    current.layout = state.layout;
    state.redo.push_back(std::move(current));
    Snapshot previous = std::move(state.history.back());
    state.history.pop_back();
    state.graph = std::move(previous.graph);
    state.layout = std::move(previous.layout);
    ++state.revision;
    state.layout.revision = state.revision;
    state.structuralDirty = true;
    state.layoutDirty = true;
    validateState(state);
    error.clear();
    return true;
}

bool Document::redo(std::string& error) {
    DocumentState& state = *m_impl;
    if (state.redo.empty()) {
        error = "nothing to redo";
        return false;
    }
    Snapshot current;
    current.graph = state.graph;
    current.layout = state.layout;
    state.history.push_back(std::move(current));
    Snapshot next = std::move(state.redo.back());
    state.redo.pop_back();
    state.graph = std::move(next.graph);
    state.layout = std::move(next.layout);
    ++state.revision;
    state.layout.revision = state.revision;
    state.structuralDirty = true;
    state.layoutDirty = true;
    validateState(state);
    error.clear();
    return true;
}

bool Document::setLayout(std::string_view layoutJson, std::string& error) {
    LayoutLoadResult parsed = deserializeLayout(layoutJson);
    if (!parsed.ok) {
        error = parsed.error.empty() ? "invalid layout" : parsed.error;
        return false;
    }
    m_impl->layout = std::move(parsed.layout);
    addMissingLayoutEntries(*m_impl);
    applyLayoutPositions(*m_impl);
    m_impl->layout.revision = ++m_impl->revision;
    m_impl->layoutDirty = true;
    validateState(*m_impl);
    error.clear();
    return true;
}

bool Document::save(std::string& error) {
    DocumentState& state = *m_impl;
    state.layout.revision = state.revision;
    std::string writeError;
    if (!saveGraph(state.assetPath, state.graph, {}, &writeError)) {
        error = saveErrorMessage("cannot save graph", writeError);
        state.lastError = error;
        return false;
    }
    state.structuralDirty = false;
    if (!saveLayout(state.layoutPath, state.layout, &writeError)) {
        state.layoutError = saveErrorMessage("cannot save layout", writeError);
    } else {
        state.layoutError.clear();
        state.layoutDirty = false;
    }
    state.lastError.clear();
    error.clear();
    return true;
}

bool Document::saveAs(const std::filesystem::path& graphPath, std::string& error) {
    DocumentState& state = *m_impl;
    const std::filesystem::path previousAsset = state.assetPath;
    const std::filesystem::path previousLayout = state.layoutPath;
    state.assetPath = graphPath;
    state.layoutPath = defaultLayoutPath(graphPath);
    if (!save(error)) {
        state.assetPath = previousAsset;
        state.layoutPath = previousLayout;
        return false;
    }
    validateState(state);
    return true;
}

} // namespace sg::editor
