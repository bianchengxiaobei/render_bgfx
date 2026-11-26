#include "hbg/analysis/compile_command.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace hbg::analysis {
namespace {

std::string joinDefines(const std::vector<std::string>& defines) {
    std::string result;
    for (size_t i = 0; i < defines.size(); ++i) {
        if (i != 0) {
            result.push_back(';');
        }
        result += defines[i];
    }
    return result;
}

bool needsQuotes(std::string_view value) {
    if (value.empty()) {
        return true;
    }
    return value.find_first_of(" \t\"&|<>();") != std::string_view::npos;
}

std::filesystem::path appendComponents(const std::filesystem::path& base,
                                       const std::filesystem::path& relative) {
    std::filesystem::path result = base;
    for (const std::filesystem::path& part : relative) {
        result /= part;
    }
    return result;
}

} // namespace

CompileCommand buildCompileCommand(const VariantManifest& manifest, const CompileTask& task,
                                   const std::filesystem::path& root,
                                   const std::filesystem::path& bgfxSrc,
                                   const std::filesystem::path& shaderc,
                                   const CompileCommandOptions& options) {
    if (shaderc.empty()) {
        throw std::runtime_error("shaderc path is required");
    }
    const auto backend = manifest.backends.find(task.backend);
    if (backend == manifest.backends.end()) {
        throw std::runtime_error("unknown backend '" + task.backend + "'");
    }

    CompileCommand command;
    command.source = appendComponents(root, task.source).string();
    command.output = options.outputPath.has_value()
                         ? options.outputPath->string()
                         : appendComponents(root / "shaders", task.output).string();

    command.args.push_back(shaderc.string());
    command.args.push_back("-f");
    command.args.push_back(command.source);
    command.args.push_back("-o");
    command.args.push_back(command.output);
    command.args.push_back("--type");
    command.args.push_back(task.stage);

    for (const std::filesystem::path& includeDir :
         resolveIncludeDirs(manifest, root, bgfxSrc)) {
        command.args.push_back("-i");
        command.args.push_back(includeDir.string());
    }

    command.args.push_back("--platform");
    command.args.push_back(backend->second.platform);
    command.args.push_back("-p");
    command.args.push_back(backend->second.profile);

    if (!(task.backend == "dxbc" && task.d3dSkipOptimization)) {
        command.args.push_back("-O");
        command.args.push_back(std::to_string(task.optimization));
        command.args.push_back("--Werror");
    }

    if (options.debugInfo && task.backend == "spirv") {
        command.args.push_back("--debug");
    }

    if (task.stage != "compute") {
        const std::filesystem::path varyingDef =
            task.varyingDef.has_value()
                ? appendComponents(root, *task.varyingDef)
                : appendComponents(root, "shaders/include/varying.def.sc");
        command.args.push_back("--varyingdef");
        command.args.push_back(varyingDef.string());
    }

    if (!task.defines.empty()) {
        command.args.push_back("--define");
        command.args.push_back(joinDefines(task.defines));
    }

    if (options.disasm && task.backend == "dxbc") {
        command.args.push_back("--disasm");
    }
    if (options.depends) {
        command.args.push_back("--depends");
    }

    return command;
}

std::string formatCommandLine(const std::vector<std::string>& args) {
    std::string result;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            result.push_back(' ');
        }
        if (!needsQuotes(args[i])) {
            result += args[i];
            continue;
        }
        result.push_back('"');
        for (const char character : args[i]) {
            if (character == '"') {
                result.push_back('\\');
            }
            result.push_back(character);
        }
        result.push_back('"');
    }
    return result;
}

} // namespace hbg::analysis
