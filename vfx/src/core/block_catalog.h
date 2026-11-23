#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace vfx {

struct Graph;

namespace detail {

struct FieldSpec {
    const char* name;
    bool required;
};

struct BlockSpec {
    std::string_view type;
    std::span<const FieldSpec> fields;
    std::string_view defaults;
};

std::span<const BlockSpec> initBlockSpecs() noexcept;
std::span<const BlockSpec> updateBlockSpecs() noexcept;
const BlockSpec* findBlockSpec(std::string_view type, bool update) noexcept;

std::string defaultBlockJson(std::string_view type, bool update, const Graph& graph, std::uint32_t system,
                             std::string& error);

} // namespace detail
} // namespace vfx
