#pragma once

#include "vfx/graph.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace vfx {

struct ParseError {
    std::string path;
    std::string message;
};

struct ParseResult {
    std::unique_ptr<Graph> graph;
    std::vector<ParseError> errors;

    bool ok() const noexcept { return graph != nullptr && errors.empty(); }
};

ParseResult parseGraphJson(std::string_view json, std::string_view sourceName = {});
ParseResult loadGraphFile(const std::filesystem::path& path);
std::uint64_t graphHash(const Graph& graph) noexcept;

} // namespace vfx
