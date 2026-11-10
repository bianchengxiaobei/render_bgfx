#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace hbg {

template <typename Tag>
struct Handle {
    static constexpr uint32_t kInvalid = UINT32_MAX;

    uint32_t index = kInvalid;
    uint32_t generation = 0;

    constexpr bool valid() const noexcept { return index != kInvalid; }
    constexpr explicit operator bool() const noexcept { return valid(); }

    friend constexpr bool operator==(const Handle&, const Handle&) = default;
};

struct HandleHash {
    template <typename Tag>
    std::size_t operator()(Handle<Tag> handle) const noexcept {
        const uint64_t value = (static_cast<uint64_t>(handle.generation) << 32) | handle.index;
        return std::hash<uint64_t>{}(value);
    }
};

} // namespace hbg
