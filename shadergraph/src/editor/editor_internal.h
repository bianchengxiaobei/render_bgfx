#pragma once

#include "sg/editor.h"
#include "sg/registry.h"
#include "sg/serialize.h"

#include "../core/value_json.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace sg::editor {

struct Snapshot {
    Graph graph;
    GraphLayout layout;
};

struct DocumentState {
    std::filesystem::path assetPath;
    std::filesystem::path layoutPath;
    NodeRegistry registry;
    Graph graph;
    GraphLayout layout;
    std::vector<Diagnostic> diagnostics;
    std::vector<Snapshot> history;
    std::vector<Snapshot> redo;
    std::uint64_t revision = 1;
    bool structuralDirty = false;
    bool layoutDirty = false;
    std::string layoutError;
    std::string lastError;
};

using Json = nlohmann::ordered_json;

bool applyCommands(DocumentState& state, std::string_view requestJson, CommandResult& result);
void assignFallbackNodePositions(DocumentState& state);
void addMissingLayoutEntries(DocumentState& state);
void applyLayoutPositions(DocumentState& state);
void syncLayoutFromGraph(DocumentState& state);
bool validateState(DocumentState& state);

std::string nodeKind(const Graph& graph);
std::string serializeDiagnostics(const std::vector<Diagnostic>& diagnostics);

} // namespace sg::editor
