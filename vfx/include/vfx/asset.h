#pragma once

#include "vfx/graph.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace vfx {

enum class ErrorLocationKind : std::uint8_t {
    None,
    Json,
    Hlsl,
};

struct ParseError {
    std::string path;
    std::string message;
    std::string source;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    ErrorLocationKind location = ErrorLocationKind::None;

    bool hasLocation() const noexcept { return line != 0; }
};

struct ParseResult {
    std::unique_ptr<Graph> graph;
    std::vector<ParseError> errors;

    bool ok() const noexcept { return graph != nullptr && errors.empty(); }
};

enum class GraphParseMode : std::uint8_t {
    FlattenSubgraphs,
    PreserveSubgraphs,
};

ParseResult parseGraphJson(std::string_view json, std::string_view sourceName = {},
                           GraphParseMode mode = GraphParseMode::FlattenSubgraphs);
ParseResult loadGraphFile(const std::filesystem::path& path);
std::uint64_t graphHash(const Graph& graph) noexcept;
std::string formatParseError(const ParseError& error);
bool locateJsonPath(std::string_view json, std::string_view path, std::uint32_t& line,
                    std::uint32_t& column);

} // namespace vfx
