#pragma once

#include "sg/graph.h"
#include "sg/registry.h"
#include "sg/serialize.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sg::editor {

struct DocumentState;

struct CommandResult {
    bool ok = false;
    std::string code = "SG_BAD_REQUEST";
    std::string message;
    int applied = 0;
    std::vector<Diagnostic> diagnostics;
    std::string dataJson = "{}";
};

std::filesystem::path defaultLayoutPath(const std::filesystem::path& graphPath);
std::filesystem::path defaultBakePath(const std::filesystem::path& graphPath);
std::string diagnosticsJson(const std::vector<Diagnostic>& diagnostics);

class Document {
  public:
    static std::unique_ptr<Document> open(const std::filesystem::path& graphPath,
                                          const NodeRegistry& registry, std::string& error);
    ~Document();

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    const std::filesystem::path& assetPath() const;
    const std::filesystem::path& layoutPath() const;
    const Graph& graph() const;
    const GraphLayout& layout() const;
    const NodeRegistry& registry() const;
    const std::vector<Diagnostic>& diagnostics() const;
    const std::string& layoutError() const;

    std::uint64_t revision() const;
    bool dirty() const;
    bool canUndo() const;
    bool canRedo() const;

    std::string graphJson(bool pretty = true) const;
    std::string layoutJson() const;
    std::string diagnosticsJson() const;
    std::string commandHistoryJson() const;

    CommandResult applyCommands(std::string_view requestJson);
    bool undo(std::string& error);
    bool redo(std::string& error);

    bool setLayout(std::string_view layoutJson, std::string& error);

    bool save(std::string& error);
    bool saveAs(const std::filesystem::path& graphPath, std::string& error);

  private:
    Document();
    std::unique_ptr<DocumentState> m_impl;
};

} // namespace sg::editor
