#include "vfx/asset.h"
#include "vfx/attribute_layout.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: hbg_vfx_attributes_dump <input.vfx.json> <output.sc>\n";
        return 2;
    }

    const vfx::ParseResult result = vfx::loadGraphFile(argv[1]);
    if (!result.ok()) {
        for (const vfx::ParseError& error : result.errors) {
            std::cerr << (error.path.empty() ? "<root>" : error.path) << ": " << error.message << "\n";
        }
        return 1;
    }

    std::uint32_t capacity = 64u;
    if (!result.graph->systems.empty()) {
        capacity = result.graph->systems.front().requestedCapacity;
    }
    const vfx::AttributeSet set = vfx::defaultAttributeSet(*result.graph);
    const vfx::AttributeLayout layout = vfx::buildAttributeLayout(set, capacity);

    std::ofstream output(argv[2], std::ios::binary);
    if (!output.good()) {
        std::cerr << "failed to open output: " << argv[2] << "\n";
        return 1;
    }
    output << vfx::generateAttributeShader(layout);
    if (!output.good()) {
        std::cerr << "failed to write output: " << argv[2] << "\n";
        return 1;
    }
    return 0;
}
