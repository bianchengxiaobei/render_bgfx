#pragma once

#include "vfx/graph.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vfx {

struct CustomHlslBlock {
    std::string code;
    std::vector<std::string> reads;
    std::vector<std::string> writes;
};

struct CustomHlslError {
    std::string path;
    std::string message;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

bool readCustomHlslBlock(const Block& block, const Graph& graph, std::string_view path,
                         CustomHlslBlock& out, CustomHlslError& error);
std::string formatCustomHlslError(const CustomHlslError& error);

} // namespace vfx
