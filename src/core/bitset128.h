#pragma once

#include <cstddef>
#include <cstdint>

namespace hbg {

class BitSet128 {
  public:
    static constexpr std::size_t kBitCount = 128;
    static constexpr std::size_t kWordCount = 2;

    constexpr BitSet128() = default;
    constexpr BitSet128(uint64_t low, uint64_t high) : m_words{low, high} {}

    constexpr bool test(std::size_t index) const {
        return index < kBitCount && ((m_words[index >> 6] >> (index & 63)) & uint64_t{1}) != 0;
    }

    constexpr void set(std::size_t index, bool value = true) {
        if (index >= kBitCount) {
            return;
        }
        const uint64_t mask = uint64_t{1} << (index & 63);
        if (value) {
            m_words[index >> 6] |= mask;
        } else {
            m_words[index >> 6] &= ~mask;
        }
    }

    constexpr void reset(std::size_t index) { set(index, false); }
    constexpr void reset() {
        m_words[0] = 0;
        m_words[1] = 0;
    }

    constexpr bool any() const { return m_words[0] != 0 || m_words[1] != 0; }
    constexpr bool none() const { return !any(); }

    constexpr std::size_t count() const {
        std::size_t result = 0;
        for (uint64_t word : m_words) {
            while (word != 0) {
                word &= word - 1;
                ++result;
            }
        }
        return result;
    }

    constexpr uint64_t low() const { return m_words[0]; }
    constexpr uint64_t high() const { return m_words[1]; }

    friend constexpr BitSet128 operator&(const BitSet128& lhs, const BitSet128& rhs) {
        return BitSet128{lhs.m_words[0] & rhs.m_words[0], lhs.m_words[1] & rhs.m_words[1]};
    }

    friend constexpr BitSet128 operator|(const BitSet128& lhs, const BitSet128& rhs) {
        return BitSet128{lhs.m_words[0] | rhs.m_words[0], lhs.m_words[1] | rhs.m_words[1]};
    }

    friend constexpr BitSet128 operator~(const BitSet128& value) {
        return BitSet128{~value.m_words[0], ~value.m_words[1]};
    }

    friend constexpr bool operator==(const BitSet128& lhs, const BitSet128& rhs) {
        return lhs.m_words[0] == rhs.m_words[0] && lhs.m_words[1] == rhs.m_words[1];
    }

    friend constexpr bool operator!=(const BitSet128& lhs, const BitSet128& rhs) { return !(lhs == rhs); }

  private:
    uint64_t m_words[kWordCount]{};
};

} // namespace hbg
