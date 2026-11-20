#pragma once

#include "vfx/graph.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vfx {

inline constexpr std::uint32_t kAttributeCapacityAlignment = 64u;

std::uint32_t alignAttributeCapacity(std::uint32_t requestedCapacity) noexcept;

struct AttributeSet {
    std::vector<AttributeDesc> attributes;

    bool add(const AttributeDesc& attribute);
    const AttributeDesc* find(std::string_view name) const noexcept;
};

AttributeSet defaultAttributeSet(const Graph& graph);

struct AttributeLayoutEntry {
    AttributeDesc attribute;
    std::uint32_t offsetWords = 0;
};

struct AttributeLayout {
    std::uint32_t requestedCapacity = 0;
    std::uint32_t capacity = 0;
    std::uint32_t bucketOffsetWords = 0;
    std::uint32_t strideWords = 0;
    std::vector<AttributeLayoutEntry> attributes;

    std::uint32_t bufferSizeWords() const noexcept;
    std::uint32_t bufferSizeBytes() const noexcept;
    const AttributeLayoutEntry* find(std::string_view name) const noexcept;
    std::uint32_t elementWordOffset(const AttributeLayoutEntry& entry,
                                    std::uint32_t particleIndex) const noexcept;
};

using AttributeWords = std::array<std::uint32_t, 4>;

AttributeWords attributeDefaultWords(const AttributeDesc& attribute) noexcept;
AttributeLayout buildAttributeLayout(const AttributeSet& set, std::uint32_t requestedCapacity);

bool readAttribute(const AttributeLayout& layout, const AttributeLayoutEntry& entry,
                   std::span<const std::uint32_t> buffer, std::uint32_t particleIndex,
                   AttributeWords& out) noexcept;
bool writeAttribute(const AttributeLayout& layout, const AttributeLayoutEntry& entry,
                    std::span<std::uint32_t> buffer, std::uint32_t particleIndex,
                    const AttributeWords& value) noexcept;

std::string generateAttributeShader(const AttributeLayout& layout);

} // namespace vfx
