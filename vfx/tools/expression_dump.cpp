#include "vfx/asset.h"
#include "vfx/expression.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

std::string child(std::string_view base, std::string_view name) {
    if (base.empty()) {
        return std::string(name);
    }
    return std::string(base) + "." + std::string(name);
}

std::string element(std::string_view base, std::size_t index) {
    return std::string(base) + "[" + std::to_string(index) + "]";
}

struct ExpressionCollector {
    explicit ExpressionCollector(const vfx::ExpressionContext& context) : context(context) {}

    void collect(const vfx::Value& value, const std::string& path) {
        if (!errors.empty()) {
            return;
        }
        if (value.kind == vfx::Value::Kind::Expression) {
            const vfx::CompiledExpression compiled = vfx::compileExpression(value, context, path);
            if (!compiled.ok()) {
                if (!compiled.errors.empty()) {
                    errors.push_back(compiled.errors.front());
                } else {
                    errors.push_back(vfx::ExpressionError{path, "failed to compile expression"});
                }
                return;
            }
            const vfx::ExpressionNode& root = *compiled.root;
            const std::string name = "vfxExpr" + std::to_string(lines.size());
            lines.push_back("    const " + std::string(vfx::toString(root.type)) + " " + name + " = " +
                            root.hlsl + ";");
            if (root.type == vfx::ExprType::Bool) {
                lines.push_back("    result += " + name + " ? 1.0 : 0.0;");
            } else if (root.type == vfx::ExprType::UInt || root.type == vfx::ExprType::Int) {
                lines.push_back("    result += float(" + name + ");");
            } else if (vfx::expressionComponentCount(root.type) > 1) {
                lines.push_back("    result += dot(" + name + ", " + name + ");");
            } else {
                lines.push_back("    result += " + name + ";");
            }
            return;
        }
        if (value.kind == vfx::Value::Kind::Array) {
            for (std::size_t i = 0; i < value.array.size() && errors.empty(); ++i) {
                collect(value.array[i], path + "[" + std::to_string(i) + "]");
            }
        } else if (value.kind == vfx::Value::Kind::Object) {
            for (const auto& [name, member] : value.object) {
                if (!errors.empty()) {
                    break;
                }
                collect(member, child(path, name));
            }
        }
    }

    void collectBlocks(const std::vector<vfx::Block>& blocks, const std::string& path) {
        for (std::size_t i = 0; i < blocks.size() && errors.empty(); ++i) {
            const vfx::Block& block = blocks[i];
            if (block.parameters.kind != vfx::Value::Kind::Object) {
                continue;
            }
            const std::string blockPath = element(path, i);
            for (const auto& [name, value] : block.parameters.object) {
                if (!errors.empty()) {
                    break;
                }
                collect(value, child(blockPath, name));
            }
        }
    }

    const vfx::ExpressionContext& context;
    std::vector<std::string> lines;
    std::vector<vfx::ExpressionError> errors;
};

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: hbg_vfx_expression_dump <input.vfx.json> <output.sc>\n";
        return 2;
    }

    const vfx::ParseResult result = vfx::loadGraphFile(argv[1]);
    if (!result.ok()) {
        for (const vfx::ParseError& error : result.errors) {
            std::cerr << (error.path.empty() ? "<root>" : error.path) << ": " << error.message << "\n";
        }
        return 1;
    }

    const vfx::ExpressionContext context(*result.graph);
    ExpressionCollector collector(context);
    for (std::size_t i = 0; i < result.graph->systems.size() && collector.errors.empty(); ++i) {
        const vfx::SystemDesc& system = result.graph->systems[i];
        const std::string systemPath = element("systems", i);
        collector.collectBlocks(system.initBlocks, child(child(systemPath, "init"), "blocks"));
        collector.collectBlocks(system.updateBlocks, child(child(systemPath, "update"), "blocks"));
    }
    if (!collector.errors.empty()) {
        for (const vfx::ExpressionError& error : collector.errors) {
            std::cerr << (error.path.empty() ? "<root>" : error.path) << ": " << error.message << "\n";
        }
        return 1;
    }

    std::ofstream output(argv[2], std::ios::binary);
    if (!output.good()) {
        std::cerr << "failed to open output: " << argv[2] << "\n";
        return 1;
    }
    output << "void vfxTestExpressions(VfxAttributes attributes, inout float result)\n{\n";
    for (const std::string& line : collector.lines) {
        output << line << "\n";
    }
    output << "}\n";
    if (!output.good()) {
        std::cerr << "failed to write output: " << argv[2] << "\n";
        return 1;
    }
    return 0;
}
