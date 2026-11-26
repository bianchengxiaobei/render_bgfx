#pragma once

#include "sg/types.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sg {

struct CustomFunctionSlotSpec {
    std::string name;
    ValueType type = ValueType::Float;
};

inline std::string_view trimCustomFunctionText(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' ||
                             text.front() == '\r' || text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                             text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

inline std::vector<CustomFunctionSlotSpec> parseCustomFunctionSlots(std::string_view text) {
    std::vector<CustomFunctionSlotSpec> result;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t end = text.find(';', begin);
        const std::string_view entry =
            trimCustomFunctionText(text.substr(begin, end == std::string_view::npos
                                                          ? std::string_view::npos
                                                          : end - begin));
        if (!entry.empty()) {
            const std::size_t separator = entry.find('|');
            const std::string_view name = trimCustomFunctionText(
                separator == std::string_view::npos ? entry : entry.substr(0, separator));
            if (!name.empty()) {
                CustomFunctionSlotSpec slot;
                slot.name = std::string(name);
                if (separator != std::string_view::npos) {
                    const std::string_view type =
                        trimCustomFunctionText(entry.substr(separator + 1));
                    if (const auto parsed = valueTypeFromString(type)) {
                        slot.type = *parsed;
                    }
                }
                result.push_back(std::move(slot));
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    return result;
}

} // namespace sg
