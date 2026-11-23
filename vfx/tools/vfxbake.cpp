#include "vfx/compiler.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::filesystem::path input;
    std::filesystem::path output;
    std::filesystem::path checkDirectory;
    std::filesystem::path shaderPack;
    std::filesystem::path shaderc;
    std::filesystem::path bgfxSource;
    std::vector<vfx::ShaderBackend> backends{vfx::ShaderBackend::Dxbc, vfx::ShaderBackend::Spirv};
    bool check = false;
    bool verbose = false;
    bool batch = false;
};

void printUsage() {
    std::cout << "Usage:\n";
    std::cout << "  vfxbake --input <effect.vfx.json> --output <effect.vfxb> [options]\n";
    std::cout << "  vfxbake --check <asset-dir> [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --shaderc <path>      bgfx shaderc executable\n";
    std::cout << "  --bgfx-src <path>     bgfx source root containing src/bgfx_compute.sh\n";
    std::cout << "  --shader-pack <path>  optional shader include root\n";
    std::cout << "  --backend <list>      dxbc,spirv (default: dxbc,spirv)\n";
    std::cout << "  --check               compare instead of writing input/output\n";
    std::cout << "  --verbose             print generated file names\n";
}

bool parseBackends(const std::string& text, std::vector<vfx::ShaderBackend>& backends) {
    backends.clear();
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t end = text.find(',', begin);
        const std::string token = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (token == "dxbc") {
            backends.push_back(vfx::ShaderBackend::Dxbc);
        } else if (token == "spirv") {
            backends.push_back(vfx::ShaderBackend::Spirv);
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
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        const auto value = [&](std::filesystem::path& out) {
            if (i + 1 >= argc) {
                return false;
            }
            out = argv[++i];
            return true;
        };
        if (argument == "--input") {
            if (!value(options.input)) {
                return false;
            }
        } else if (argument == "--output") {
            if (!value(options.output)) {
                return false;
            }
        } else if (argument == "--shader-pack") {
            if (!value(options.shaderPack)) {
                return false;
            }
        } else if (argument == "--shaderc") {
            if (!value(options.shaderc)) {
                return false;
            }
        } else if (argument == "--bgfx-src") {
            if (!value(options.bgfxSource)) {
                return false;
            }
        } else if (argument == "--backend") {
            std::string text;
            if (i + 1 >= argc) {
                return false;
            }
            text = argv[++i];
            if (!parseBackends(text, options.backends)) {
                std::cerr << "vfxbake: invalid backend list: " << text << '\n';
                exitCode = 2;
                return false;
            }
        } else if (argument == "--check") {
            options.check = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                options.checkDirectory = argv[++i];
                options.batch = true;
            }
        } else if (argument == "--verbose") {
            options.verbose = true;
        } else if (argument == "--help" || argument == "-h") {
            printUsage();
            exitCode = 0;
            return false;
        } else {
            std::cerr << "vfxbake: unknown argument: " << argument << '\n';
            exitCode = 2;
            return false;
        }
    }
    return true;
}

bool readFile(const std::filesystem::path& path, std::vector<std::byte>& out) {
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
    out.resize(static_cast<std::size_t>(size));
    if (!out.empty()) {
        file.read(reinterpret_cast<char*>(out.data()), size);
    }
    return static_cast<bool>(file) || file.eof();
}

bool writeFile(const std::filesystem::path& path, const std::vector<std::byte>& data) {
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

std::filesystem::path binaryPathFor(const std::filesystem::path& jsonPath) {
    const std::string filename = jsonPath.filename().string();
    constexpr std::string_view suffix = ".vfx.json";
    if (filename.size() > suffix.size() &&
        filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0) {
        return jsonPath.parent_path() / (filename.substr(0, filename.size() - suffix.size()) + ".vfxb");
    }
    return jsonPath.parent_path() / (jsonPath.stem().string() + ".vfxb");
}

vfx::CompileOptions makeCompileOptions(const Options& options) {
    vfx::CompileOptions compile;
    compile.shaderPack = options.shaderPack;
    compile.shaderc = options.shaderc;
    compile.bgfxSource = options.bgfxSource;
    compile.backends = options.backends;
    return compile;
}

bool bakeOne(const std::filesystem::path& input, const std::filesystem::path& output,
             const Options& options, bool check) {
    vfx::CompileResult result = vfx::compileGraphFile(input, makeCompileOptions(options));
    if (!result.ok()) {
        std::cerr << "vfxbake: " << result.error << '\n';
        return false;
    }
    if (check) {
        std::vector<std::byte> existing;
        if (!readFile(output, existing)) {
            std::cerr << "vfxbake: missing baked asset: " << output.string() << '\n';
            return false;
        }
        if (existing != result.binary) {
            std::cerr << "vfxbake: baked asset differs: " << output.string() << '\n';
            return false;
        }
        if (options.verbose) {
            std::cout << "vfxbake: checked " << output.string() << '\n';
        }
        return true;
    }
    if (!writeFile(output, result.binary)) {
        std::cerr << "vfxbake: failed to write " << output.string() << '\n';
        return false;
    }
    if (options.verbose) {
        std::cout << "vfxbake: baked " << output.string() << " (" << result.binary.size() << " bytes)\n";
    }
    return true;
}

bool bakeDirectory(const Options& options) {
    std::error_code error;
    if (!std::filesystem::is_directory(options.checkDirectory, error)) {
        std::cerr << "vfxbake: not a directory: " << options.checkDirectory.string() << '\n';
        return false;
    }
    std::vector<std::filesystem::path> assets;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(options.checkDirectory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string filename = entry.path().filename().string();
        if (filename.size() >= 9 && filename.compare(filename.size() - 9, 9, ".vfx.json") == 0) {
            assets.push_back(entry.path());
        }
    }
    std::sort(assets.begin(), assets.end());
    if (assets.empty()) {
        std::cerr << "vfxbake: no .vfx.json assets under " << options.checkDirectory.string() << '\n';
        return false;
    }
    bool ok = true;
    for (const std::filesystem::path& asset : assets) {
        if (!bakeOne(asset, binaryPathFor(asset), options, true)) {
            ok = false;
        }
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
    if (options.batch) {
        return bakeDirectory(options) ? 0 : 1;
    }
    if (options.input.empty() || options.output.empty()) {
        printUsage();
        return 2;
    }
    return bakeOne(options.input, options.output, options, options.check) ? 0 : 1;
}
