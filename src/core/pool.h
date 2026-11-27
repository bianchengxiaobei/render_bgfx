#pragma once

#include "core/assert.h"
#include "core/handle.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace hbg {

template <typename Tag, typename T>
class Pool {
  public:
    Handle<Tag> add(T&& value) { return emplace(std::move(value)); }

    template <typename... Args>
    Handle<Tag> emplace(Args&&... args) {
        uint32_t index = 0;
        if (!m_free.empty()) {
            index = m_free.back();
            m_free.pop_back();
        } else {
            HBG_ASSERT(m_slots.size() < static_cast<std::size_t>(Handle<Tag>::kInvalid));
            index = static_cast<uint32_t>(m_slots.size());
            m_slots.emplace_back();
        }

        Slot& slot = m_slots[index];
        slot.alive = true;
        slot.value.emplace(std::forward<Args>(args)...);
        ++m_size;

        return Handle<Tag>{index, slot.generation};
    }

    T* get(Handle<Tag> handle) {
        if (!isLive(handle)) {
            return nullptr;
        }
        return &*m_slots[handle.index].value;
    }

    const T* get(Handle<Tag> handle) const {
        if (!isLive(handle)) {
            return nullptr;
        }
        return &*m_slots[handle.index].value;
    }

    bool contains(Handle<Tag> handle) const { return isLive(handle); }

    bool remove(Handle<Tag> handle) {
        if (!isLive(handle)) {
            return false;
        }

        Slot& slot = m_slots[handle.index];
        slot.alive = false;
        slot.value.reset();
        slot.generation = nextGeneration(slot.generation);
        m_free.push_back(handle.index);
        --m_size;
        return true;
    }

    std::size_t size() const noexcept { return m_size; }
    bool empty() const noexcept { return m_size == 0; }

    template <typename Fn>
    void forEach(Fn&& fn) {
        for (std::size_t index = 0; index < m_slots.size(); ++index) {
            Slot& slot = m_slots[index];
            if (slot.alive && slot.value.has_value()) {
                fn(Handle<Tag>{static_cast<uint32_t>(index), slot.generation}, *slot.value);
            }
        }
    }

    template <typename Fn>
    void forEach(Fn&& fn) const {
        for (std::size_t index = 0; index < m_slots.size(); ++index) {
            const Slot& slot = m_slots[index];
            if (slot.alive && slot.value.has_value()) {
                fn(Handle<Tag>{static_cast<uint32_t>(index), slot.generation}, *slot.value);
            }
        }
    }

  private:
    struct Slot {
        uint32_t generation = 1;
        bool alive = false;
        std::optional<T> value;
    };

    static uint32_t nextGeneration(uint32_t generation) noexcept {
        ++generation;
        return generation == 0 ? 1 : generation;
    }

    bool isLive(Handle<Tag> handle) const {
        if (!handle.valid() || static_cast<std::size_t>(handle.index) >= m_slots.size()) {
            return false;
        }
        const Slot& slot = m_slots[handle.index];
        return slot.alive && slot.generation == handle.generation && slot.value.has_value();
    }

    std::vector<Slot> m_slots;
    std::vector<uint32_t> m_free;
    std::size_t m_size = 0;
};

} // namespace hbg
