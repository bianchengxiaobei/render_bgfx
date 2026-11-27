#pragma once

#include <array>
#include <cstdint>

namespace hbg::render {

template <typename T> class HistoryBuffer {
  public:
    HistoryBuffer() = default;

    void reset() {
        m_slots = {};
        m_frameCount = 0;
    }

    void reset(const T& value) {
        m_slots[0] = value;
        m_slots[1] = value;
        m_frameCount = 0;
    }

    void push(const T& value) {
        if (m_frameCount == 0) {
            m_slots[0] = value;
            m_slots[1] = value;
        } else {
            m_slots[1] = m_slots[0];
            m_slots[0] = value;
        }
        ++m_frameCount;
    }

    T& current() { return m_slots[0]; }
    const T& current() const { return m_slots[0]; }
    const T& previous() const { return m_slots[1]; }

    bool hasHistory() const { return m_frameCount > 1; }
    uint32_t frameCount() const { return m_frameCount; }

  private:
    std::array<T, 2> m_slots{};
    uint32_t m_frameCount = 0;
};

} // namespace hbg::render
