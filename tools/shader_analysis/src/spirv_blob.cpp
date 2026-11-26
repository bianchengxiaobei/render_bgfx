#include "hbg/analysis/spirv_blob.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace hbg::analysis {
namespace {

constexpr std::uint32_t kSpirvMagic = 0x07230203u;
constexpr std::size_t kHeaderPrefix = 16;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error("spirv blob: " + message);
}

std::uint16_t readU16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 2 > data.size()) {
        fail("unexpected end of data");
    }
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[offset + 1]) << 8u);
}

std::uint32_t readU32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4 > data.size()) {
        fail("unexpected end of data");
    }
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16u) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24u);
}

bool hasMagic(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return offset + 4 <= data.size() && readU32(data, offset) == kSpirvMagic;
}

struct Tail {
    std::uint8_t attributeCount = 0;
    std::vector<std::uint16_t> attributes;
    std::uint16_t uniformSize = 0;
    std::size_t end = 0;
};

bool parseTail(const std::vector<std::uint8_t>& data, std::size_t start, Tail& tail) {
    if (start > data.size() || data.size() - start < 4) {
        return false;
    }
    if (data[start] != 0) {
        return false;
    }
    const std::uint8_t attributeCount = data[start + 1];
    const std::size_t attributesStart = start + 2;
    const std::size_t attributesEnd = attributesStart + 2u * attributeCount;
    if (attributesEnd + 2 > data.size()) {
        return false;
    }

    Tail parsed;
    parsed.attributeCount = attributeCount;
    parsed.attributes.reserve(attributeCount);
    for (std::uint8_t i = 0; i < attributeCount; ++i) {
        parsed.attributes.push_back(readU16(data, attributesStart + 2u * i));
    }
    parsed.uniformSize = readU16(data, attributesEnd);
    parsed.end = attributesEnd + 2;
    tail = std::move(parsed);
    return tail.end == data.size();
}

bool parseCandidate(const std::vector<std::uint8_t>& data, std::size_t magicOffset,
                    SpirvBlob& blob) {
    if (magicOffset < 4 || !hasMagic(data, magicOffset)) {
        return false;
    }
    const std::uint32_t shaderSize = readU32(data, magicOffset - 4);
    if (shaderSize < 4 || shaderSize % 4 != 0 ||
        magicOffset + shaderSize > data.size()) {
        return false;
    }
    Tail tail;
    if (!parseTail(data, magicOffset + shaderSize, tail)) {
        return false;
    }

    blob.bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(magicOffset),
                      data.begin() + static_cast<std::ptrdiff_t>(magicOffset + shaderSize));
    blob.offset = magicOffset;
    blob.declaredSize = shaderSize;
    blob.attributeCount = tail.attributeCount;
    blob.attributes = std::move(tail.attributes);
    blob.uniformSize = tail.uniformSize;
    return true;
}

bool parseFullHeader(const std::vector<std::uint8_t>& data, SpirvBlob& blob) {
    if (data.size() < kHeaderPrefix + 4) {
        return false;
    }

    std::size_t offset = kHeaderPrefix;
    const std::uint16_t uniformCount = readU16(data, offset);
    offset += 2;
    for (std::uint16_t i = 0; i < uniformCount; ++i) {
        if (offset >= data.size()) {
            return false;
        }
        const std::uint8_t nameSize = data[offset++];
        if (offset + nameSize > data.size()) {
            return false;
        }
        offset += nameSize;
        if (offset + 11 > data.size()) {
            return false;
        }
        offset += 11;
    }

    if (offset + 4 > data.size()) {
        return false;
    }
    const std::uint32_t shaderSize = readU32(data, offset);
    offset += 4;
    if (shaderSize < 4 || shaderSize % 4 != 0 || offset + shaderSize > data.size() ||
        !hasMagic(data, offset)) {
        return false;
    }
    Tail tail;
    if (!parseTail(data, offset + shaderSize, tail)) {
        return false;
    }

    blob.bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(offset),
                      data.begin() + static_cast<std::ptrdiff_t>(offset + shaderSize));
    blob.offset = offset;
    blob.declaredSize = shaderSize;
    blob.attributeCount = tail.attributeCount;
    blob.attributes = std::move(tail.attributes);
    blob.uniformSize = tail.uniformSize;
    return true;
}

} // namespace

SpirvBlob extractSpirvBlob(const std::vector<std::uint8_t>& binary) {
    for (std::size_t offset = 4; offset + 4 <= binary.size(); ++offset) {
        if (!hasMagic(binary, offset)) {
            continue;
        }
        SpirvBlob blob;
        if (parseCandidate(binary, offset, blob)) {
            return blob;
        }
    }

    SpirvBlob blob;
    if (parseFullHeader(binary, blob)) {
        return blob;
    }
    fail("SPIR-V magic/header not found in " + std::to_string(binary.size()) +
         " byte(s)");
}

SpirvBlob extractSpirvBlob(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        fail("cannot open '" + path.string() + "'");
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        fail("cannot read '" + path.string() + "'");
    }
    const std::string text = contents.str();
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    return extractSpirvBlob(bytes);
}

} // namespace hbg::analysis
