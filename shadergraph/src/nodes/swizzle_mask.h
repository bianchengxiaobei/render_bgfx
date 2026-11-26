#pragma once

#include "sg/types.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace sg {

struct SwizzleMask {
    bool valid = false;
    std::string mask;
    int inputCount = 1;
    int outputCount = 1;
};

inline SwizzleMask parseSwizzleMask(std::string_view text) {
    SwizzleMask result;
    for (char character : text) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
        switch (character) {
        case 'r':
            character = 'x';
            break;
        case 'g':
            character = 'y';
            break;
        case 'b':
            character = 'z';
            break;
        case 'a':
            character = 'w';
            break;
        default:
            break;
        }
        if (character != 'x' && character != 'y' && character != 'z' && character != 'w') {
            return result;
        }
        result.mask.push_back(character);
    }
    if (result.mask.empty() || result.mask.size() > 4) {
        return result;
    }
    result.valid = true;
    result.outputCount = static_cast<int>(result.mask.size());
    const std::string channels = "xyzw";
    for (char character : result.mask) {
        const std::size_t index = channels.find(character);
        result.inputCount = std::max(result.inputCount, static_cast<int>(index) + 1);
    }
    return result;
}

inline ValueType swizzleVectorType(int count) {
    switch (count) {
    case 1:
        return ValueType::Float;
    case 2:
        return ValueType::Vector2;
    case 3:
        return ValueType::Vector3;
    default:
        return ValueType::Vector4;
    }
}

} // namespace sg
