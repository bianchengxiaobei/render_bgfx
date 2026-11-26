#include "sgb.h"

#include <cstring>
#include <fstream>
#include <limits>

namespace sg::spike {
namespace {

constexpr std::uint32_t kMetaSection = 1;
constexpr std::uint32_t kPassesSection = 6;
constexpr std::uint32_t kBlobsSection = 7;

std::uint16_t readU16(const std::uint8_t* data) {
    std::uint16_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

std::uint32_t readU32(const std::uint8_t* data) {
    std::uint32_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

std::uint64_t readU64(const std::uint8_t* data) {
    std::uint64_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

bool rangeValid(std::uint64_t offset, std::uint64_t size, std::uint64_t total) {
    return offset <= total && size <= total - offset;
}

} // namespace

bool Asset::load(const std::string& path, std::string& error) {
    m_bytes.clear();
    m_entries.clear();
    m_blobsOffset = 0;
    m_contentHash = 0;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "failed to open '" + path + "'";
        return false;
    }
    const std::streamsize fileSize = file.tellg();
    if (fileSize < 28) {
        error = "file is too small to be SGB1";
        return false;
    }
    file.seekg(0, std::ios::beg);
    m_bytes.resize(static_cast<std::size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(m_bytes.data()), fileSize)) {
        error = "failed to read '" + path + "'";
        return false;
    }

    const std::uint8_t* data = m_bytes.data();
    const std::uint64_t total = m_bytes.size();
    if (std::memcmp(data, "SGB1", 4) != 0) {
        error = "bad magic (expected SGB1)";
        return false;
    }
    const std::uint16_t version = readU16(data + 4);
    if (version != 1) {
        error = "unsupported SGB format version " + std::to_string(version);
        return false;
    }
    m_contentHash = readU64(data + 8);
    const std::uint32_t sectionCount = readU32(data + 24);
    if (sectionCount == 0 || sectionCount > 32) {
        error = "invalid section count " + std::to_string(sectionCount);
        return false;
    }
    const std::uint64_t tableBytes = static_cast<std::uint64_t>(sectionCount) * 20;
    if (!rangeValid(28, tableBytes, total)) {
        error = "section table is out of range";
        return false;
    }

    bool foundBlobs = false;
    std::uint64_t blobsOffset = 0;
    std::uint64_t blobsSize = 0;
    for (std::uint32_t index = 0; index < sectionCount; ++index) {
        const std::uint8_t* entry = data + 28 + static_cast<std::size_t>(index) * 20;
        const std::uint32_t kind = readU32(entry);
        const std::uint64_t offset = readU64(entry + 4);
        const std::uint64_t size = readU64(entry + 12);
        if (!rangeValid(offset, size, total)) {
            error = "section " + std::to_string(kind) + " is out of range";
            return false;
        }
        if (kind == kBlobsSection) {
            foundBlobs = true;
            blobsOffset = offset;
            blobsSize = size;
        } else if (kind != kMetaSection && kind != kPassesSection) {
            error = "unexpected section kind " + std::to_string(kind);
            return false;
        }
    }
    if (!foundBlobs || blobsSize < 4) {
        error = "missing BLOBS section";
        return false;
    }

    const std::uint8_t* blobs = data + blobsOffset;
    const std::uint32_t blobCount = readU32(blobs);
    if (blobCount == 0 || blobCount > 32) {
        error = "invalid blob count " + std::to_string(blobCount);
        return false;
    }
    const std::uint64_t blobTableBytes = 4 + static_cast<std::uint64_t>(blobCount) * 12;
    if (blobTableBytes > blobsSize) {
        error = "blob table is out of range";
        return false;
    }
    for (std::uint32_t index = 0; index < blobCount; ++index) {
        const std::uint8_t* entry = blobs + 4 + static_cast<std::size_t>(index) * 12;
        Entry parsed;
        parsed.stage = static_cast<ShaderStage>(entry[0]);
        parsed.backend = static_cast<ShaderBackend>(entry[1]);
        parsed.offset = readU32(entry + 4);
        parsed.size = readU32(entry + 8);
        if (parsed.stage > ShaderStage::Compute || parsed.backend > ShaderBackend::Spirv) {
            error = "invalid blob stage/backend";
            return false;
        }
        if (parsed.offset < blobTableBytes || parsed.size == 0 ||
            !rangeValid(parsed.offset, parsed.size, blobsSize)) {
            error = "blob is out of range";
            return false;
        }
        m_entries.push_back(parsed);
    }
    m_blobsOffset = static_cast<std::uint32_t>(blobsOffset);
    return true;
}

bool Asset::find(ShaderStage stage, ShaderBackend backend, ShaderBlob& blob) const {
    for (const Entry& entry : m_entries) {
        if (entry.stage != stage || entry.backend != backend) {
            continue;
        }
        blob.stage = entry.stage;
        blob.backend = entry.backend;
        blob.data = m_bytes.data() + m_blobsOffset + entry.offset;
        blob.size = entry.size;
        return true;
    }
    return false;
}

} // namespace sg::spike
