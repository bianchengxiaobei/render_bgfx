#include "hbg/dawn/dawn_gzip.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

#ifdef HBG_DAWN_HAVE_ZLIB
#include <zlib.h>
#endif

namespace hbg::dawn {
namespace {

void appendLe16(std::vector<uint8_t>& bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value & 0xFFu));
    bytes.push_back(static_cast<uint8_t>((value >> 8u) & 0xFFu));
}

void appendLe32(std::vector<uint8_t>& bytes, uint32_t value) {
    for (uint32_t index = 0; index < 4; ++index) {
        bytes.push_back(static_cast<uint8_t>((value >> (index * 8u)) & 0xFFu));
    }
}

uint16_t readLe16(std::span<const uint8_t> bytes, std::size_t offset) {
    if (offset + 2 > bytes.size()) {
        throw std::runtime_error("truncated gzip stream");
    }
    return static_cast<uint16_t>(bytes[offset]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8u);
}

uint32_t readLe32(std::span<const uint8_t> bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) {
        throw std::runtime_error("truncated gzip stream");
    }
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8u) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16u) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24u);
}

uint32_t crc32(std::span<const uint8_t> bytes) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t value : bytes) {
        crc ^= value;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1u) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

}  // namespace

std::vector<uint8_t> gzipCompressStored(std::span<const uint8_t> data) {
    std::vector<uint8_t> result;
    result.reserve(data.size() + data.size() / 65535u * 5u + 32u);

    result.push_back(0x1Fu);
    result.push_back(0x8Bu);
    result.push_back(0x08u);
    result.push_back(0x00u);
    appendLe32(result, 0u);
    result.push_back(0x00u);
    result.push_back(0xFFu);

    if (data.empty()) {
        result.push_back(0x01u);
        appendLe16(result, 0u);
        appendLe16(result, 0xFFFFu);
    } else {
        std::size_t offset = 0;
        while (offset < data.size()) {
            const std::size_t blockSize = std::min<std::size_t>(65535u, data.size() - offset);
            const bool finalBlock = offset + blockSize == data.size();
            result.push_back(finalBlock ? 0x01u : 0x00u);
            const auto length = static_cast<uint16_t>(blockSize);
            appendLe16(result, length);
            appendLe16(result, static_cast<uint16_t>(~length));
            result.insert(result.end(), data.begin() + static_cast<std::ptrdiff_t>(offset),
                          data.begin() + static_cast<std::ptrdiff_t>(offset + blockSize));
            offset += blockSize;
        }
    }

    appendLe32(result, crc32(data));
    appendLe32(result, static_cast<uint32_t>(data.size() & 0xFFFFFFFFu));
    return result;
}

std::vector<uint8_t> gzipDecompressStored(std::span<const uint8_t> data) {
    if (data.size() < 18) {
        throw std::runtime_error("gzip stream is too short");
    }
    if (data[0] != 0x1Fu || data[1] != 0x8Bu || data[2] != 0x08u) {
        throw std::runtime_error("invalid gzip header");
    }
    const uint8_t flags = data[3];
    std::size_t offset = 10;

    if ((flags & 0x04u) != 0u) {
        const uint16_t extraLength = readLe16(data, offset);
        offset += 2u + extraLength;
    }
    if ((flags & 0x08u) != 0u) {
        while (offset < data.size() && data[offset] != 0u) {
            ++offset;
        }
        ++offset;
    }
    if ((flags & 0x10u) != 0u) {
        while (offset < data.size() && data[offset] != 0u) {
            ++offset;
        }
        ++offset;
    }
    if ((flags & 0x02u) != 0u) {
        offset += 2u;
    }

    std::vector<uint8_t> result;
    bool finalBlock = false;
    while (!finalBlock) {
        if (offset >= data.size()) {
            throw std::runtime_error("truncated gzip deflate block");
        }
        const uint8_t header = data[offset++];
        finalBlock = (header & 0x01u) != 0u;
        const uint8_t blockType = static_cast<uint8_t>((header >> 1u) & 0x03u);
        if (blockType != 0u) {
            throw std::runtime_error("only stored gzip blocks are supported");
        }
        const uint16_t length = readLe16(data, offset);
        offset += 2u;
        const uint16_t inverseLength = readLe16(data, offset);
        offset += 2u;
        if (static_cast<uint16_t>(~length) != inverseLength) {
            throw std::runtime_error("invalid gzip stored block length");
        }
        if (offset + length > data.size()) {
            throw std::runtime_error("truncated gzip stored block payload");
        }
        result.insert(result.end(), data.begin() + static_cast<std::ptrdiff_t>(offset),
                      data.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;
    }

    if (offset + 8u > data.size()) {
        throw std::runtime_error("truncated gzip trailer");
    }
    const uint32_t expectedCrc = readLe32(data, offset);
    const uint32_t expectedSize = readLe32(data, offset + 4u);
    if (crc32(result) != expectedCrc) {
        throw std::runtime_error("gzip crc mismatch");
    }
    if (static_cast<uint32_t>(result.size() & 0xFFFFFFFFu) != expectedSize) {
        throw std::runtime_error("gzip size mismatch");
    }
    return result;
}

std::vector<uint8_t> gzipDecompress(std::span<const uint8_t> data) {
#ifdef HBG_DAWN_HAVE_ZLIB
    if (data.size() > static_cast<std::size_t>(std::numeric_limits<uInt>::max())) {
        throw std::runtime_error("gzip stream is too large for zlib");
    }

    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(data.data());
    stream.avail_in = static_cast<uInt>(data.size());
    if (inflateInit2(&stream, MAX_WBITS + 16) != Z_OK) {
        throw std::runtime_error("zlib inflateInit2 failed");
    }

    std::vector<uint8_t> result;
    std::array<uint8_t, 64 * 1024> buffer{};
    int status = Z_OK;
    while (status != Z_STREAM_END) {
        stream.next_out = buffer.data();
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END) {
            const std::string message = stream.msg != nullptr ? stream.msg : "unknown zlib error";
            inflateEnd(&stream);
            throw std::runtime_error("gzip inflate failed: " + message);
        }
        const std::size_t produced = buffer.size() - stream.avail_out;
        result.insert(result.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(produced));
    }
    inflateEnd(&stream);
    return result;
#else
    return gzipDecompressStored(data);
#endif
}

void writeGzipFile(const std::filesystem::path& path, std::span<const uint8_t> data) {
    const std::vector<uint8_t> compressed = gzipCompressStored(data);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("failed to open gzip file: " + path.string());
    }
    stream.write(reinterpret_cast<const char*>(compressed.data()),
                 static_cast<std::streamsize>(compressed.size()));
    if (!stream) {
        throw std::runtime_error("failed to write gzip file: " + path.string());
    }
}

}  // namespace hbg::dawn
