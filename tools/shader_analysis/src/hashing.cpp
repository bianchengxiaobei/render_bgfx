#include "hbg/analysis/hashing.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>
#include <vector>

namespace hbg::analysis {
namespace {

constexpr std::array<uint32_t, 64> kRoundConstants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

uint32_t rotateRight(uint32_t value, uint32_t bits) {
    return (value >> bits) | (value << (32u - bits));
}

class Sha256 {
public:
    void update(const uint8_t* data, size_t size) {
        if (size == 0) {
            return;
        }
        m_bitCount += static_cast<uint64_t>(size) * 8u;
        while (size > 0) {
            const size_t take = std::min(size, m_buffer.size() - m_bufferSize);
            std::memcpy(m_buffer.data() + m_bufferSize, data, take);
            m_bufferSize += take;
            data += take;
            size -= take;
            if (m_bufferSize == m_buffer.size()) {
                transform(m_buffer.data());
                m_bufferSize = 0;
            }
        }
    }

    std::string finish() {
        const uint64_t bitCount = m_bitCount;
        m_buffer[m_bufferSize++] = 0x80u;
        if (m_bufferSize > 56) {
            while (m_bufferSize < m_buffer.size()) {
                m_buffer[m_bufferSize++] = 0u;
            }
            transform(m_buffer.data());
            m_bufferSize = 0;
        }
        while (m_bufferSize < 56) {
            m_buffer[m_bufferSize++] = 0u;
        }
        for (size_t i = 0; i < 8; ++i) {
            m_buffer[56 + i] = static_cast<uint8_t>((bitCount >> (56u - 8u * i)) & 0xffu);
        }
        transform(m_buffer.data());
        m_bufferSize = 0;

        static constexpr char kHex[] = "0123456789abcdef";
        std::string result;
        result.reserve(64);
        for (const uint32_t word : m_state) {
            for (int shift = 28; shift >= 0; shift -= 4) {
                result.push_back(kHex[(word >> static_cast<uint32_t>(shift)) & 0x0fu]);
            }
        }
        return result;
    }

private:
    void transform(const uint8_t* block) {
        std::array<uint32_t, 64> words{};
        for (size_t i = 0; i < 16; ++i) {
            words[i] = (static_cast<uint32_t>(block[i * 4]) << 24u) |
                       (static_cast<uint32_t>(block[i * 4 + 1]) << 16u) |
                       (static_cast<uint32_t>(block[i * 4 + 2]) << 8u) |
                       static_cast<uint32_t>(block[i * 4 + 3]);
        }
        for (size_t i = 16; i < words.size(); ++i) {
            const uint32_t s0 = rotateRight(words[i - 15], 7u) ^
                                rotateRight(words[i - 15], 18u) ^ (words[i - 15] >> 3u);
            const uint32_t s1 = rotateRight(words[i - 2], 17u) ^
                                rotateRight(words[i - 2], 19u) ^ (words[i - 2] >> 10u);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }

        uint32_t a = m_state[0];
        uint32_t b = m_state[1];
        uint32_t c = m_state[2];
        uint32_t d = m_state[3];
        uint32_t e = m_state[4];
        uint32_t f = m_state[5];
        uint32_t g = m_state[6];
        uint32_t h = m_state[7];

        for (size_t i = 0; i < words.size(); ++i) {
            const uint32_t s1 = rotateRight(e, 6u) ^ rotateRight(e, 11u) ^ rotateRight(e, 25u);
            const uint32_t choose = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + s1 + choose + kRoundConstants[i] + words[i];
            const uint32_t s0 = rotateRight(a, 2u) ^ rotateRight(a, 13u) ^ rotateRight(a, 22u);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + majority;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        m_state[0] += a;
        m_state[1] += b;
        m_state[2] += c;
        m_state[3] += d;
        m_state[4] += e;
        m_state[5] += f;
        m_state[6] += g;
        m_state[7] += h;
    }

    std::array<uint32_t, 8> m_state = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    uint64_t m_bitCount = 0;
    std::array<uint8_t, 64> m_buffer{};
    size_t m_bufferSize = 0;
};

} // namespace

std::string sha256Hex(std::string_view bytes) {
    Sha256 sha;
    sha.update(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
    return sha.finish();
}

std::string sha256File(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open file for SHA-256: " + path.string());
    }
    Sha256 sha;
    std::array<char, 65536> buffer{};
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = stream.gcount();
        if (count > 0) {
            sha.update(reinterpret_cast<const uint8_t*>(buffer.data()),
                       static_cast<size_t>(count));
        }
    }
    if (!stream.eof()) {
        throw std::runtime_error("cannot read file for SHA-256: " + path.string());
    }
    return sha.finish();
}

std::vector<std::string> canonicalDefines(const std::vector<std::string>& defines) {
    std::vector<std::string> result = defines;
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::string joinCanonicalDefines(const std::vector<std::string>& canonical) {
    std::string result;
    for (size_t i = 0; i < canonical.size(); ++i) {
        if (i != 0) {
            result.push_back('\n');
        }
        result += canonical[i];
    }
    return result;
}

std::string makeVariantId(std::string_view source, std::string_view stage,
                          std::string_view backend, const std::vector<std::string>& defines) {
    const std::vector<std::string> canonical = canonicalDefines(defines);
    std::string key;
    key.reserve(source.size() + stage.size() + backend.size() + 3);
    key += source;
    key.push_back('\n');
    key += stage;
    key.push_back('\n');
    key += backend;
    key.push_back('\n');
    key += joinCanonicalDefines(canonical);
    return sha256Hex(key).substr(0, 12);
}

std::string defineName(std::string_view define) {
    const size_t separator = define.find('=');
    if (separator == std::string_view::npos) {
        return std::string(define);
    }
    return std::string(define.substr(0, separator));
}

std::vector<std::string> keywordNames(const std::vector<std::string>& defines) {
    std::set<std::string> names;
    for (const std::string& define : defines) {
        const std::string name = defineName(define);
        if (!name.empty()) {
            names.insert(name);
        }
    }
    return std::vector<std::string>(names.begin(), names.end());
}

} // namespace hbg::analysis
