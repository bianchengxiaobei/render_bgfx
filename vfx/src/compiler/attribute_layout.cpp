#include "vfx/attribute_layout.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string_view>

namespace vfx {
namespace {

constexpr std::array<char, 4> kComponents = {'x', 'y', 'z', 'w'};

std::string_view hlslType(AttributeType type) {
    switch (type) {
    case AttributeType::Float:
        return "float";
    case AttributeType::Float2:
        return "vec2";
    case AttributeType::Float3:
        return "vec3";
    case AttributeType::Float4:
        return "vec4";
    case AttributeType::UInt:
        return "uint";
    case AttributeType::Int:
        return "int";
    case AttributeType::Bool:
        return "bool";
    }
    return "uint";
}

std::string loadExpression(const AttributeLayoutEntry& entry, std::uint32_t baseOffset) {
    const auto count = attributeComponentCount(entry.attribute.type);
    std::string expression;
    for (std::uint8_t i = 0; i < count; ++i) {
        if (!expression.empty()) {
            expression += ", ";
        }
        const std::uint32_t word = baseOffset + entry.offsetWords + i;
        switch (entry.attribute.type) {
        case AttributeType::Float:
        case AttributeType::Float2:
        case AttributeType::Float3:
        case AttributeType::Float4:
            expression += "rawLoadFloat(s_vfxAttributes, base + " + std::to_string(word) + "u)";
            break;
        case AttributeType::UInt:
            expression += "rawLoadUint(s_vfxAttributes, base + " + std::to_string(word) + "u)";
            break;
        case AttributeType::Int:
            expression += "int(rawLoadUint(s_vfxAttributes, base + " + std::to_string(word) + "u))";
            break;
        case AttributeType::Bool:
            expression += "(rawLoadUint(s_vfxAttributes, base + " + std::to_string(word) + "u) != 0u)";
            break;
        }
    }
    if (count <= 1) {
        return expression;
    }
    return std::string(hlslType(entry.attribute.type)) + "(" + expression + ")";
}

std::string storeExpression(const AttributeLayoutEntry& entry, std::uint32_t baseOffset,
                            std::string_view valueName) {
    const auto count = attributeComponentCount(entry.attribute.type);
    std::string expression;
    for (std::uint8_t i = 0; i < count; ++i) {
        if (!expression.empty()) {
            expression += "\n        ";
        }
        const std::uint32_t word = baseOffset + entry.offsetWords + i;
        const std::string component =
            count == 1 ? std::string(valueName)
                       : std::string(valueName) + "." + std::string(1, kComponents[i]);
        switch (entry.attribute.type) {
        case AttributeType::Float:
        case AttributeType::Float2:
        case AttributeType::Float3:
        case AttributeType::Float4:
            expression += "rawStoreFloat(s_vfxAttributes, base + " + std::to_string(word) +
                          "u, " + component + ");";
            break;
        case AttributeType::UInt:
            expression += "rawStoreUint(s_vfxAttributes, base + " + std::to_string(word) +
                          "u, " + component + ");";
            break;
        case AttributeType::Int:
            expression += "rawStoreUint(s_vfxAttributes, base + " + std::to_string(word) +
                          "u, uint(" + component + "));";
            break;
        case AttributeType::Bool:
            expression += "rawStoreUint(s_vfxAttributes, base + " + std::to_string(word) +
                          "u, " + component + " ? 1u : 0u);";
            break;
        }
    }
    return expression;
}

} // namespace

std::uint32_t alignAttributeCapacity(std::uint32_t requestedCapacity) noexcept {
    const std::uint64_t requested = std::max<std::uint32_t>(requestedCapacity, 1u);
    const std::uint64_t rounded =
        ((requested + kAttributeCapacityAlignment - 1u) / kAttributeCapacityAlignment) *
        kAttributeCapacityAlignment;
    return static_cast<std::uint32_t>(
        std::min<std::uint64_t>(rounded, std::numeric_limits<std::uint32_t>::max()));
}

bool AttributeSet::add(const AttributeDesc& attribute) {
    if (find(attribute.name) != nullptr || findBuiltInAttribute(attribute.name) != nullptr) {
        return false;
    }
    attributes.push_back(attribute);
    return true;
}

const AttributeDesc* AttributeSet::find(std::string_view name) const noexcept {
    const auto found = std::find_if(attributes.begin(), attributes.end(),
                                    [name](const AttributeDesc& attribute) {
                                        return attribute.name == name;
                                    });
    return found == attributes.end() ? nullptr : &*found;
}

AttributeSet defaultAttributeSet(const Graph& graph) {
    AttributeSet set;
    set.attributes.reserve(builtInAttributes().size() + graph.attributes.size());
    for (const AttributeDesc& attribute : builtInAttributes()) {
        set.attributes.push_back(attribute);
    }
    for (const AttributeDesc& attribute : graph.attributes) {
        set.attributes.push_back(attribute);
    }
    return set;
}

std::uint32_t AttributeLayout::bufferSizeWords() const noexcept {
    const std::uint64_t size = static_cast<std::uint64_t>(bucketOffsetWords) +
                               static_cast<std::uint64_t>(capacity) * strideWords;
    return size > std::numeric_limits<std::uint32_t>::max()
               ? std::numeric_limits<std::uint32_t>::max()
               : static_cast<std::uint32_t>(size);
}

std::uint32_t AttributeLayout::bufferSizeBytes() const noexcept {
    return bufferSizeWords() * 4u;
}

const AttributeLayoutEntry* AttributeLayout::find(std::string_view name) const noexcept {
    const auto found = std::find_if(attributes.begin(), attributes.end(),
                                    [name](const AttributeLayoutEntry& entry) {
                                        return entry.attribute.name == name;
                                    });
    return found == attributes.end() ? nullptr : &*found;
}

std::uint32_t AttributeLayout::elementWordOffset(const AttributeLayoutEntry& entry,
                                                 std::uint32_t particleIndex) const noexcept {
    return bucketOffsetWords + entry.offsetWords + particleIndex * strideWords;
}

AttributeWords attributeDefaultWords(const AttributeDesc& attribute) noexcept {
    AttributeWords words{};
    const std::uint8_t count = attributeComponentCount(attribute.type);
    switch (attribute.type) {
    case AttributeType::Float:
    case AttributeType::Float2:
    case AttributeType::Float3:
    case AttributeType::Float4:
        for (std::uint8_t i = 0; i < count; ++i) {
            words[i] = std::bit_cast<std::uint32_t>(attribute.defaultFloat[i]);
        }
        break;
    case AttributeType::UInt:
    case AttributeType::Int:
        for (std::uint8_t i = 0; i < count; ++i) {
            words[i] = attribute.defaultUint[i];
        }
        break;
    case AttributeType::Bool:
        words[0] = attribute.defaultUint[0] != 0u ? 1u : 0u;
        break;
    }
    return words;
}

AttributeLayout buildAttributeLayout(const AttributeSet& set, std::uint32_t requestedCapacity) {
    AttributeLayout layout;
    layout.requestedCapacity = requestedCapacity;
    layout.capacity = alignAttributeCapacity(requestedCapacity);
    layout.bucketOffsetWords = 0;

    std::uint32_t offset = 0;
    layout.attributes.reserve(set.attributes.size());
    for (const AttributeDesc& attribute : set.attributes) {
        const std::uint8_t count = attributeComponentCount(attribute.type);
        if (count == 0) {
            continue;
        }
        layout.attributes.push_back(AttributeLayoutEntry{attribute, offset});
        offset += count;
    }
    layout.strideWords = offset;
    return layout;
}

bool readAttribute(const AttributeLayout& layout, const AttributeLayoutEntry& entry,
                   std::span<const std::uint32_t> buffer, std::uint32_t particleIndex,
                   AttributeWords& out) noexcept {
    const std::uint8_t count = attributeComponentCount(entry.attribute.type);
    const std::uint64_t offset = layout.elementWordOffset(entry, particleIndex);
    if (offset + count > buffer.size()) {
        return false;
    }
    out = {};
    for (std::uint8_t i = 0; i < count; ++i) {
        out[i] = buffer[static_cast<std::size_t>(offset + i)];
    }
    return true;
}

bool writeAttribute(const AttributeLayout& layout, const AttributeLayoutEntry& entry,
                    std::span<std::uint32_t> buffer, std::uint32_t particleIndex,
                    const AttributeWords& value) noexcept {
    const std::uint8_t count = attributeComponentCount(entry.attribute.type);
    const std::uint64_t offset = layout.elementWordOffset(entry, particleIndex);
    if (offset + count > buffer.size()) {
        return false;
    }
    for (std::uint8_t i = 0; i < count; ++i) {
        buffer[static_cast<std::size_t>(offset + i)] = value[i];
    }
    return true;
}

std::string generateAttributeShader(const AttributeLayout& layout) {
    std::ostringstream out;
    out << "#ifndef VFX_ATTRIBUTES_GENERATED_SC\n";
    out << "#define VFX_ATTRIBUTES_GENERATED_SC\n\n";
    out << "#include \"bgfx_compute.sh\"\n\n";
    out << "#ifndef VFX_ATTRIBUTE_BUFFER_STAGE\n";
    out << "#define VFX_ATTRIBUTE_BUFFER_STAGE 0\n";
    out << "#endif\n\n";
    out << "#if defined(VFX_ATTRIBUTE_BUFFER_WRITE)\n";
    out << "BUFFER_RAW_RW(s_vfxAttributes, VFX_ATTRIBUTE_BUFFER_STAGE);\n";
    out << "#else\n";
    out << "BUFFER_RAW_RO(s_vfxAttributes, VFX_ATTRIBUTE_BUFFER_STAGE);\n";
    out << "#endif\n\n";
    out << "#define VFX_ATTRIBUTE_COUNT " << layout.attributes.size() << "u\n";
    out << "#define VFX_ATTRIBUTE_STRIDE " << layout.strideWords << "u\n";
    out << "#define VFX_ATTRIBUTE_STRIDE_BYTES " << (layout.strideWords * 4u) << "u\n";
    out << "#define VFX_ATTRIBUTE_CAPACITY " << layout.capacity << "u\n";
    for (const AttributeLayoutEntry& entry : layout.attributes) {
        out << "#define VFX_ATTRIBUTE_OFFSET_" << entry.attribute.name << " "
            << entry.offsetWords << "u\n";
        out << "#define VFX_ATTRIBUTE_BYTE_OFFSET_" << entry.attribute.name << " "
            << (entry.offsetWords * 4u) << "u\n";
    }
    out << "\nstruct VfxAttributes\n{\n";
    out << "    uint base;\n\n";
    out << "    void Init(uint particleIndex)\n    {\n";
    out << "        base = particleIndex * VFX_ATTRIBUTE_STRIDE;\n";
    out << "    }\n";
    for (const AttributeLayoutEntry& entry : layout.attributes) {
        out << "\n    " << hlslType(entry.attribute.type) << " Load_" << entry.attribute.name
            << "()\n    {\n";
        out << "        return " << loadExpression(entry, 0) << ";\n";
        out << "    }\n\n";
        out << "#if defined(VFX_ATTRIBUTE_BUFFER_WRITE)\n";
        out << "    void Store_" << entry.attribute.name << "("
            << hlslType(entry.attribute.type) << " value)\n    {\n";
        out << "        " << storeExpression(entry, 0, "value") << "\n";
        out << "    }\n";
        out << "#endif\n";
    }
    out << "};\n\n";
    out << "#endif\n";
    return out.str();
}

} // namespace vfx
