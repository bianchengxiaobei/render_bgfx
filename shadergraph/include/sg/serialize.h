#pragma once

#include "sg/graph.h"
#include "sg/registry.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sg {

struct SerializeOptions {
    bool pretty = true;
    bool includePositions = true;
    bool includeGeneratorVersion = true;
};

struct LoadOptions {
    bool allowUnknownFields = true;
    bool warnUnknownFields = true;
};

struct GraphLoadResult {
    bool ok = false;
    Graph graph;
    std::vector<Diagnostic> diagnostics;
    std::string error;
};

std::string serialize(const Graph& graph, const SerializeOptions& options = {});
std::string serializeValueJson(const Value& value);
GraphLoadResult deserialize(std::string_view json, const NodeRegistry& registry, const LoadOptions& options = {});
std::uint64_t contentHash(const Graph& graph);

bool saveGraph(const std::filesystem::path& path, const Graph& graph, const SerializeOptions& options = {},
               std::string* error = nullptr);
GraphLoadResult loadGraph(const std::filesystem::path& path, const NodeRegistry& registry,
                          const LoadOptions& options = {});

struct NodeLayout {
    NodeId id = kInvalidNode;
    Vec2 position;
    bool collapsed = false;
};

struct GroupLayout {
    int id = 0;
    Vec2 position;
    Vec2 size;
    bool collapsed = false;
};

struct NoteLayout {
    int id = 0;
    Vec2 position;
    Vec2 size;
};

struct GraphLayout {
    int schemaVersion = 1;
    std::uint64_t revision = 0;
    Vec2 pan;
    float zoom = 1.0f;
    std::vector<NodeLayout> nodes;
    std::vector<GroupLayout> groups;
    std::vector<NoteLayout> notes;
};

struct LayoutLoadResult {
    bool ok = false;
    GraphLayout layout;
    std::vector<Diagnostic> diagnostics;
    std::string error;
};

std::string serializeLayout(const GraphLayout& layout);
LayoutLoadResult deserializeLayout(std::string_view json);
bool saveLayout(const std::filesystem::path& path, const GraphLayout& layout, std::string* error = nullptr);
LayoutLoadResult loadLayout(const std::filesystem::path& path);

} // namespace sg
