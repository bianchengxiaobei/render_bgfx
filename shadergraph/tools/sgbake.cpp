#include "sg/compile.h"
#include "sg/node_library.h"
#include "sg/serialize.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Options {
    std::filesystem::path input;
    std::filesystem::path output;
    std::filesystem::path checkDirectory;
    std::filesystem::path root;
    std::filesystem::path shaderc;
    std::filesystem::path bgfxSource;
    std::filesystem::path cache;
    std::vector<sg::Backend> backends{sg::Backend::Dxbc, sg::Backend::Spirv};
    sg::VariantPolicy variants = sg::VariantPolicy::Used;
    bool check = false;
    bool keepHlsl = false;
    bool verbose = false;
    int maxVariants = 64;
};

void usage() {
    std::cout << "Usage:\n";
    std::cout << "  sgbake --input <graph.sgraph.json> --output <graph.sgb> [options]\n";
    std::cout << "  sgbake --check <asset-dir> [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --backend <list>   dxbc,spirv (default: dxbc,spirv)\n";
    std::cout << "  --variants <mode>  used|all (default: used)\n";
    std::cout << "  --root <path>      repository root\n";
    std::cout << "  --shaderc <path>   bgfx shaderc executable\n";
    std::cout << "  --bgfx-src <path>  bgfx source root\n";
    std::cout << "  --cache <path>     generated shader cache\n";
    std::cout << "  --keep-hlsl        include generated HLSL in the asset\n";
    std::cout << "  --verbose          print per-asset progress\n";
    std::cout << "  --max-variants N   variant cap (default: 64)\n";
}

bool parseBackends(const std::string& text, std::vector<sg::Backend>& backends) {
    backends.clear();
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t end = text.find(',', begin);
        const std::string token = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (token == "dxbc") {
            backends.push_back(sg::Backend::Dxbc);
        } else if (token == "spirv") {
            backends.push_back(sg::Backend::Spirv);
        } else if (!token.empty()) {
            return false;
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return !backends.empty();
}

bool parseArguments(int argc, char** argv, Options& options, int& exitCode) {
    options.root = std::filesystem::path(SG_ROOT);
    options.shaderc = std::filesystem::path(SG_SHADERC_DEFAULT);
    options.bgfxSource = std::filesystem::path(SG_BGFX_SOURCE_DEFAULT);
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        const auto value = [&](std::filesystem::path& target) {
            if (index + 1 >= argc) {
                return false;
            }
            target = argv[++index];
            return true;
        };
        const auto equals = [&](const char* prefix, std::filesystem::path& target) {
            const std::string stem(prefix);
            if (!argument.starts_with(stem)) {
                return false;
            }
            target = argument.substr(stem.size());
            return true;
        };
        if (argument == "--input" || equals("--input=", options.input)) {
            if (argument == "--input" && !value(options.input)) return false;
        } else if (argument == "--output" || equals("--output=", options.output)) {
            if (argument == "--output" && !value(options.output)) return false;
        } else if (argument == "--root" || equals("--root=", options.root)) {
            if (argument == "--root" && !value(options.root)) return false;
        } else if (argument == "--shaderc" || equals("--shaderc=", options.shaderc)) {
            if (argument == "--shaderc" && !value(options.shaderc)) return false;
        } else if (argument == "--bgfx-src" || equals("--bgfx-src=", options.bgfxSource)) {
            if (argument == "--bgfx-src" && !value(options.bgfxSource)) return false;
        } else if (argument == "--cache" || equals("--cache=", options.cache)) {
            if (argument == "--cache" && !value(options.cache)) return false;
        } else if (argument == "--backend" || argument.starts_with("--backend=")) {
            std::string text;
            if (argument.starts_with("--backend=")) {
                text = argument.substr(std::string("--backend=").size());
            } else {
                if (index + 1 >= argc) return false;
                text = argv[++index];
            }
            if (!parseBackends(text, options.backends)) {
                std::cerr << "sgbake: invalid backend list: " << text << "\n";
                exitCode = 2;
                return false;
            }
        } else if (argument == "--variants" || argument.starts_with("--variants=")) {
            std::string text;
            if (argument.starts_with("--variants=")) {
                text = argument.substr(std::string("--variants=").size());
            } else {
                if (index + 1 >= argc) return false;
                text = argv[++index];
            }
            if (text == "all") {
                options.variants = sg::VariantPolicy::All;
            } else if (text == "used") {
                options.variants = sg::VariantPolicy::Used;
            } else {
                std::cerr << "sgbake: invalid variant mode: " << text << "\n";
                exitCode = 2;
                return false;
            }
        } else if (argument == "--check" || argument.starts_with("--check=")) {
            options.check = true;
            if (argument.starts_with("--check=")) {
                options.checkDirectory = argument.substr(std::string("--check=").size());
            } else if (index + 1 < argc && argv[index + 1][0] != '-') {
                options.checkDirectory = argv[++index];
            }
        } else if (argument == "--keep-hlsl") {
            options.keepHlsl = true;
        } else if (argument == "--verbose") {
            options.verbose = true;
        } else if (argument == "--max-variants" || argument.starts_with("--max-variants=")) {
            std::string text;
            if (argument.starts_with("--max-variants=")) {
                text = argument.substr(std::string("--max-variants=").size());
            } else {
                if (index + 1 >= argc) return false;
                text = argv[++index];
            }
            options.maxVariants = std::stoi(text);
        } else if (argument == "--help" || argument == "-h") {
            usage();
            exitCode = 0;
            return false;
        } else {
            std::cerr << "sgbake: unknown argument: " << argument << "\n";
            exitCode = 2;
            return false;
        }
    }
    return true;
}

std::filesystem::path bakedPathFor(const std::filesystem::path& input) {
    std::string name = input.filename().string();
    constexpr std::string_view suffix = ".sgraph.json";
    if (name.size() > suffix.size() &&
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        name.resize(name.size() - suffix.size());
        name += ".sgb";
    } else {
        name = input.stem().string() + ".sgb";
    }
    return input.parent_path() / name;
}

bool readFile(const std::filesystem::path& path, std::vector<std::uint8_t>& data) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0) {
        return false;
    }
    file.seekg(0, std::ios::beg);
    data.resize(static_cast<std::size_t>(size));
    if (!data.empty()) {
        file.read(reinterpret_cast<char*>(data.data()), size);
    }
    return static_cast<bool>(file) || file.eof();
}

bool writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return false;
        }
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(file);
}

void printDiagnostics(const sg::CompileResult& result) {
    for (const sg::Diagnostic& diagnostic : result.diagnostics) {
        std::cerr << diagnostic.code << ": " << diagnostic.message;
        if (diagnostic.node != sg::kInvalidNode) {
            std::cerr << " (node " << diagnostic.node;
            if (diagnostic.slot != sg::kInvalidSlot) {
                std::cerr << ", slot " << diagnostic.slot;
            }
            std::cerr << ")";
        }
        if (!diagnostic.pass.empty()) {
            std::cerr << " [" << diagnostic.pass;
            if (!diagnostic.variant.empty()) {
                std::cerr << "/" << diagnostic.variant;
            }
            std::cerr << "]";
        }
        std::cerr << "\n";
    }
}

bool compileOne(const std::filesystem::path& input, const std::filesystem::path& output,
                const Options& options, const sg::NodeRegistry& registry, bool check) {
    sg::GraphLoadResult loaded = sg::loadGraph(input, registry);
    if (!loaded.ok) {
        std::cerr << "sgbake: failed to load " << input.string() << ": " << loaded.error << "\n";
        for (const sg::Diagnostic& diagnostic : loaded.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << "\n";
        }
        return false;
    }
    sg::CompileOptions compileOptions;
    compileOptions.rootDirectory = options.root;
    compileOptions.shaderc = options.shaderc;
    compileOptions.bgfxSource = options.bgfxSource;
    compileOptions.cacheDirectory = options.cache;
    compileOptions.backends = options.backends;
    compileOptions.variants = options.variants;
    compileOptions.keepHlsl = options.keepHlsl;
    compileOptions.maxVariants = options.maxVariants;
    sg::CompileResult result = sg::compile(loaded.graph, registry, compileOptions);
    if (!result.ok) {
        std::cerr << "sgbake: failed to compile " << input.string() << ": " << result.error << "\n";
        printDiagnostics(result);
        return false;
    }
    if (check) {
        std::vector<std::uint8_t> existing;
        if (!readFile(output, existing)) {
            std::cerr << "sgbake: missing baked asset: " << output.string() << "\n";
            return false;
        }
        if (existing != result.binary) {
            std::cerr << "sgbake: baked asset differs: " << output.string() << "\n";
            return false;
        }
        if (options.verbose) {
            std::cout << "sgbake: checked " << output.string() << "\n";
        }
        return true;
    }
    if (!writeFile(output, result.binary)) {
        std::cerr << "sgbake: failed to write " << output.string() << "\n";
        return false;
    }
    if (options.verbose) {
        std::cout << "sgbake: baked " << output.string() << " (" << result.binary.size()
                  << " bytes, " << result.stats.shaders << " shaders)\n";
    }
    return true;
}

bool bakeDirectory(const Options& options, const sg::NodeRegistry& registry) {
    std::error_code error;
    if (!std::filesystem::is_directory(options.checkDirectory, error)) {
        std::cerr << "sgbake: not a directory: " << options.checkDirectory.string() << "\n";
        return false;
    }
    std::vector<std::filesystem::path> inputs;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(options.checkDirectory)) {
        if (entry.is_regular_file() && entry.path().filename().string().ends_with(".sgraph.json")) {
            inputs.push_back(entry.path());
        }
    }
    std::sort(inputs.begin(), inputs.end());
    if (inputs.empty()) {
        std::cerr << "sgbake: no .sgraph.json assets under " << options.checkDirectory.string() << "\n";
        return false;
    }
    bool ok = true;
    for (const auto& input : inputs) {
        if (!compileOne(input, bakedPathFor(input), options, registry, true)) {
            ok = false;
        }
    }
    if (ok && !options.verbose) {
        std::cout << "sgbake: checked " << inputs.size() << " assets\n";
    }
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    int exitCode = 1;
    if (!parseArguments(argc, argv, options, exitCode)) {
        return exitCode;
    }
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    if (options.check) {
        if (options.checkDirectory.empty()) {
            usage();
            return 2;
        }
        return bakeDirectory(options, registry) ? 0 : 1;
    }
    if (options.input.empty() || options.output.empty()) {
        usage();
        return 2;
    }
    return compileOne(options.input, options.output, options, registry, false) ? 0 : 1;
}
