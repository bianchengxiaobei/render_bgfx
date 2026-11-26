#include "vfx/editor.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct Options {
    fs::path assets;
    fs::path work;
    std::uint32_t expect = 0;
    bool verbose = false;
};

void printUsage() {
    std::cout << "Usage:\n";
    std::cout << "  hbg_vfx_editor_roundtrip --assets <asset-dir> --work <output-dir> [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --expect <count>      require an exact number of .vfx.json assets\n";
    std::cout << "  --verbose             print every asset\n";
}

bool parseArguments(int argc, char** argv, Options& options, int& exitCode) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        const auto value = [&](std::string& out) {
            if (i + 1 >= argc) {
                return false;
            }
            out = argv[++i];
            return true;
        };
        if (argument == "--assets") {
            std::string text;
            if (!value(text)) {
                return false;
            }
            options.assets = text;
        } else if (argument == "--work") {
            std::string text;
            if (!value(text)) {
                return false;
            }
            options.work = text;
        } else if (argument == "--expect") {
            std::string text;
            if (!value(text)) {
                return false;
            }
            try {
                options.expect = static_cast<std::uint32_t>(std::stoul(text));
            } catch (const std::exception&) {
                std::cerr << "editor_roundtrip: invalid asset count: " << text << '\n';
                exitCode = 2;
                return false;
            }
        } else if (argument == "--verbose") {
            options.verbose = true;
        } else if (argument == "--help" || argument == "-h") {
            printUsage();
            exitCode = 0;
            return false;
        } else {
            std::cerr << "editor_roundtrip: unknown argument: " << argument << '\n';
            exitCode = 2;
            return false;
        }
    }
    if (options.assets.empty() || options.work.empty()) {
        return false;
    }
    return true;
}

bool isAsset(const fs::path& path) {
    const std::string filename = path.filename().string();
    constexpr std::string_view suffix = ".vfx.json";
    return filename.size() >= suffix.size() &&
           filename.compare(filename.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<fs::path> collectAssets(const fs::path& root) {
    std::vector<fs::path> assets;
    std::error_code error;
    fs::recursive_directory_iterator iterator(root, error);
    const fs::recursive_directory_iterator end;
    while (!error && iterator != end) {
        if (iterator->is_regular_file() && isAsset(iterator->path())) {
            assets.push_back(iterator->path());
        }
        iterator.increment(error);
    }
    if (error) {
        std::cerr << "editor_roundtrip: failed to scan '" << root.string() << "': " << error.message() << '\n';
        return {};
    }
    std::sort(assets.begin(), assets.end());
    return assets;
}

bool copyAsset(const fs::path& source, const fs::path& root, const fs::path& work, std::string& error) {
    std::error_code code;
    const fs::path relative = fs::relative(source, root, code);
    if (code) {
        error = "failed to compute relative path for '" + source.string() + "': " + code.message();
        return false;
    }
    const fs::path destination = work / relative;
    fs::create_directories(destination.parent_path(), code);
    if (code) {
        error = "failed to create '" + destination.parent_path().string() + "': " + code.message();
        return false;
    }
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing, code);
    if (code) {
        error = "failed to copy '" + source.string() + "': " + code.message();
        return false;
    }

    const fs::path sourceBinary = vfx::editor::defaultBinaryPath(source);
    if (!fs::is_regular_file(sourceBinary, code)) {
        error = "missing baked asset '" + sourceBinary.string() + "'";
        return false;
    }
    const fs::path destinationBinary = vfx::editor::defaultBinaryPath(destination);
    fs::copy_file(sourceBinary, destinationBinary, fs::copy_options::overwrite_existing, code);
    if (code) {
        error = "failed to copy '" + sourceBinary.string() + "': " + code.message();
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    int exitCode = 2;
    if (!parseArguments(argc, argv, options, exitCode)) {
        if (exitCode == 2) {
            printUsage();
        }
        return exitCode;
    }

    const std::vector<fs::path> assets = collectAssets(options.assets);
    if (assets.empty()) {
        std::cerr << "editor_roundtrip: no .vfx.json assets under '" << options.assets.string() << "'\n";
        return 1;
    }
    if (options.expect != 0 && assets.size() != options.expect) {
        std::cerr << "editor_roundtrip: expected " << options.expect << " assets, found " << assets.size() << '\n';
        return 1;
    }

    std::error_code code;
    fs::create_directories(options.work, code);
    if (code) {
        std::cerr << "editor_roundtrip: failed to create '" << options.work.string() << "': " << code.message() << '\n';
        return 1;
    }

    for (const fs::path& source : assets) {
        std::string error;
        if (!copyAsset(source, options.assets, options.work, error)) {
            std::cerr << "editor_roundtrip: " << source.string() << ": " << error << '\n';
            return 1;
        }

        const fs::path destination = options.work / fs::relative(source, options.assets, code);
        if (code) {
            std::cerr << "editor_roundtrip: " << source.string() << ": " << code.message() << '\n';
            return 1;
        }

        const auto document = vfx::editor::Document::load(destination, error);
        if (document == nullptr) {
            std::cerr << "editor_roundtrip: load failed for '" << source.string() << "': " << error << '\n';
            return 1;
        }
        const std::uint64_t beforeHash = vfx::graphHash(document->graph());
        const std::string beforeJson = document->serialize();

        if (!document->save(error)) {
            std::cerr << "editor_roundtrip: save failed for '" << source.string() << "': " << error << '\n';
            return 1;
        }
        if (!document->saveLayout(error)) {
            std::cerr << "editor_roundtrip: layout save failed for '" << source.string() << "': " << error << '\n';
            return 1;
        }

        const auto reloaded = vfx::editor::Document::load(destination, error);
        if (reloaded == nullptr) {
            std::cerr << "editor_roundtrip: reload failed for '" << source.string() << "': " << error << '\n';
            return 1;
        }
        if (reloaded->serialize() != beforeJson) {
            std::cerr << "editor_roundtrip: serialized graph changed for '" << source.string() << "'\n";
            return 1;
        }
        if (vfx::graphHash(reloaded->graph()) != beforeHash) {
            std::cerr << "editor_roundtrip: graph hash changed for '" << source.string() << "'\n";
            return 1;
        }
        if (reloaded->layoutStale() || !reloaded->layoutError().empty()) {
            std::cerr << "editor_roundtrip: layout did not reload for '" << source.string() << "'\n";
            return 1;
        }
        if (options.verbose) {
            std::cout << "editor_roundtrip: " << source.string() << " hash=" << beforeHash << '\n';
        }
    }

    std::cout << "editor_roundtrip: checked " << assets.size() << " assets\n";
    return 0;
}
