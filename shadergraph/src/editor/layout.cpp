#include "editor_internal.h"

#include "sg/validate.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace sg::editor {
namespace {

bool endsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

std::filesystem::path replaceSuffix(const std::filesystem::path& path, std::string_view from,
                                    std::string_view to) {
    std::string text = path.string();
    if (endsWith(text, from)) {
        text.resize(text.size() - from.size());
        text += to;
        return std::filesystem::path(text);
    }
    return path;
}

} // namespace

std::filesystem::path defaultLayoutPath(const std::filesystem::path& graphPath) {
    if (endsWith(graphPath.filename().string(), ".sgraph.json")) {
        return replaceSuffix(graphPath, ".sgraph.json", ".sgraph.editor.json");
    }
    if (endsWith(graphPath.filename().string(), ".ssubgraph.json")) {
        return replaceSuffix(graphPath, ".ssubgraph.json", ".ssubgraph.editor.json");
    }
    return std::filesystem::path(graphPath.string() + ".editor.json");
}

std::filesystem::path defaultBakePath(const std::filesystem::path& graphPath) {
    if (endsWith(graphPath.filename().string(), ".sgraph.json")) {
        return replaceSuffix(graphPath, ".sgraph.json", ".sgb");
    }
    if (endsWith(graphPath.filename().string(), ".ssubgraph.json")) {
        return replaceSuffix(graphPath, ".ssubgraph.json", ".sgb");
    }
    return graphPath.parent_path() / (graphPath.stem().string() + ".sgb");
}

void assignFallbackNodePositions(DocumentState& state) {
    if (state.graph.nodes.size() < 2) {
        return;
    }
    const Vec2 first = state.graph.nodes.front().position;
    const bool allSame = std::all_of(state.graph.nodes.begin(), state.graph.nodes.end(),
                                     [&](const NodeInstance& node) {
                                         return node.position.x == first.x && node.position.y == first.y;
                                     });
    if (!allSame) {
        return;
    }

    std::map<NodeId, int> depth;
    std::function<int(NodeId, std::set<NodeId>&)> depthOf = [&](NodeId id, std::set<NodeId>& visiting) {
        if (const auto found = depth.find(id); found != depth.end()) {
            return found->second;
        }
        if (!visiting.insert(id).second) {
            return 0;
        }
        int result = 0;
        for (const Edge& edge : state.graph.edges) {
            if (edge.toNode == id) {
                result = std::max(result, depthOf(edge.fromNode, visiting) + 1);
            }
        }
        visiting.erase(id);
        depth[id] = result;
        return result;
    };

    std::set<NodeId> visiting;
    for (const NodeInstance& node : state.graph.nodes) {
        depthOf(node.id, visiting);
    }

    std::map<int, int> rows;
    for (NodeInstance& node : state.graph.nodes) {
        const int column = depth[node.id];
        const int row = rows[column]++;
        node.position = {80.0f + static_cast<float>(column) * 290.0f,
                         80.0f + static_cast<float>(row) * 150.0f};
        const auto layoutNode = std::find_if(state.layout.nodes.begin(), state.layout.nodes.end(),
                                             [id = node.id](const NodeLayout& item) {
                                                 return item.id == id;
                                             });
        if (layoutNode != state.layout.nodes.end()) {
            layoutNode->position = node.position;
        }
    }
}

void addMissingLayoutEntries(DocumentState& state) {
    std::set<NodeId> nodes;
    for (const NodeLayout& node : state.layout.nodes) {
        nodes.insert(node.id);
    }
    for (const NodeInstance& node : state.graph.nodes) {
        if (nodes.insert(node.id).second) {
            state.layout.nodes.push_back({node.id, node.position, false});
        }
    }
    std::sort(state.layout.nodes.begin(), state.layout.nodes.end(),
              [](const NodeLayout& lhs, const NodeLayout& rhs) { return lhs.id < rhs.id; });

    std::set<int> groups;
    for (const GroupLayout& group : state.layout.groups) {
        groups.insert(group.id);
    }
    for (const Group& group : state.graph.groups) {
        if (groups.insert(group.id).second) {
            state.layout.groups.push_back({group.id, group.position, group.size, false});
        }
    }
    std::sort(state.layout.groups.begin(), state.layout.groups.end(),
              [](const GroupLayout& lhs, const GroupLayout& rhs) { return lhs.id < rhs.id; });

    std::set<int> notes;
    for (const NoteLayout& note : state.layout.notes) {
        notes.insert(note.id);
    }
    for (const Note& note : state.graph.notes) {
        if (notes.insert(note.id).second) {
            state.layout.notes.push_back({note.id, note.position, note.size});
        }
    }
    std::sort(state.layout.notes.begin(), state.layout.notes.end(),
              [](const NoteLayout& lhs, const NoteLayout& rhs) { return lhs.id < rhs.id; });
}

void applyLayoutPositions(DocumentState& state) {
    for (const NodeLayout& layoutNode : state.layout.nodes) {
        if (NodeInstance* node = state.graph.findNode(layoutNode.id); node != nullptr) {
            node->position = layoutNode.position;
        }
    }
    for (const GroupLayout& layoutGroup : state.layout.groups) {
        const auto found = std::find_if(state.graph.groups.begin(), state.graph.groups.end(),
                                        [&](const Group& group) { return group.id == layoutGroup.id; });
        if (found != state.graph.groups.end()) {
            found->position = layoutGroup.position;
            found->size = layoutGroup.size;
        }
    }
    for (const NoteLayout& layoutNote : state.layout.notes) {
        const auto found = std::find_if(state.graph.notes.begin(), state.graph.notes.end(),
                                        [&](const Note& note) { return note.id == layoutNote.id; });
        if (found != state.graph.notes.end()) {
            found->position = layoutNote.position;
            found->size = layoutNote.size;
        }
    }
}

void syncLayoutFromGraph(DocumentState& state) {
    state.layout.nodes.clear();
    state.layout.nodes.reserve(state.graph.nodes.size());
    for (const NodeInstance& node : state.graph.nodes) {
        state.layout.nodes.push_back({node.id, node.position, false});
    }
    std::sort(state.layout.nodes.begin(), state.layout.nodes.end(),
              [](const NodeLayout& lhs, const NodeLayout& rhs) { return lhs.id < rhs.id; });

    state.layout.groups.clear();
    state.layout.groups.reserve(state.graph.groups.size());
    for (const Group& group : state.graph.groups) {
        state.layout.groups.push_back({group.id, group.position, group.size, false});
    }
    std::sort(state.layout.groups.begin(), state.layout.groups.end(),
              [](const GroupLayout& lhs, const GroupLayout& rhs) { return lhs.id < rhs.id; });

    state.layout.notes.clear();
    state.layout.notes.reserve(state.graph.notes.size());
    for (const Note& note : state.graph.notes) {
        state.layout.notes.push_back({note.id, note.position, note.size});
    }
    std::sort(state.layout.notes.begin(), state.layout.notes.end(),
              [](const NoteLayout& lhs, const NoteLayout& rhs) { return lhs.id < rhs.id; });
    state.layout.revision = state.revision;
}

bool validateState(DocumentState& state) {
    ValidationResult result = validate(state.graph, state.registry, true);
    state.diagnostics = std::move(result.diagnostics);
    return result.ok && !hasError(state.diagnostics);
}

std::string nodeKind(const Graph& graph) {
    return graph.kind == GraphKind::SubGraph ? "subgraph" : "graph";
}

std::string diagnosticsJson(const std::vector<Diagnostic>& diagnostics) {
    return serializeDiagnostics(diagnostics);
}

std::string serializeDiagnostics(const std::vector<Diagnostic>& diagnostics) {
    Json result = Json::array();
    for (const Diagnostic& diagnostic : diagnostics) {
        Json item;
        item["severity"] = toString(diagnostic.severity);
        item["code"] = diagnostic.code;
        item["message"] = diagnostic.message;
        if (diagnostic.node != kInvalidNode) {
            item["nodeId"] = diagnostic.node;
        }
        if (diagnostic.slot != kInvalidSlot) {
            item["slotId"] = diagnostic.slot;
        }
        if (!diagnostic.pass.empty()) {
            item["pass"] = diagnostic.pass;
        }
        if (!diagnostic.variant.empty()) {
            item["variant"] = diagnostic.variant;
        }
        result.push_back(std::move(item));
    }
    return result.dump();
}

} // namespace sg::editor
