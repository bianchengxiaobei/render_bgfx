#include "compile_internal.h"

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

namespace sg::detail {
namespace {

std::string quote(const std::filesystem::path& path) {
    return "\"" + path.string() + "\"";
}

bool readFile(const std::filesystem::path& path, std::string& value) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    value = stream.str();
    return true;
}

bool writeKey(const std::filesystem::path& output, const std::string& key) {
    std::ofstream file(output.string() + ".key", std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << key;
    return static_cast<bool>(file);
}

bool keyMatches(const std::filesystem::path& output, const std::string& key) {
    std::string existing;
    return readFile(output.string() + ".key", existing) && existing == key;
}

} // namespace

ShadercResult compileShader(const ShadercOptions& options, const std::string& sourcePath,
                            const std::string& outputPath, const std::string& stage,
                            Backend backend, const std::vector<std::string>& defines,
                            const std::string& hashKey) {
    ShadercResult result;
    const std::filesystem::path output(outputPath);
    std::error_code directoryError;
    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path(), directoryError);
    }
    std::string fullKey = hashKey + "|" + stage + "|" + std::to_string(static_cast<int>(backend)) +
                          "|";
    for (const std::string& define : defines) {
        fullKey += define;
        fullKey.push_back(';');
    }
    if (std::filesystem::exists(output) && std::filesystem::file_size(output) > 0 &&
        keyMatches(output, fullKey)) {
        result.ok = true;
        result.cached = true;
        result.data.resize(static_cast<std::size_t>(std::filesystem::file_size(output)));
        std::ifstream file(output, std::ios::binary);
        file.read(reinterpret_cast<char*>(result.data.data()),
                  static_cast<std::streamsize>(result.data.size()));
        return result;
    }

    const std::filesystem::path logPath = output.string() + ".log";
    std::ostringstream command;
#ifdef _WIN32
    command << "call ";
#endif
    command << quote(options.shaderc) << " -f " << quote(sourcePath) << " -o " << quote(output)
            << " --type " << stage;
    command << " -i " << quote(options.cache);
    command << " -i " << quote(options.root / "shaders");
    command << " -i " << quote(options.root / "shaders/include");
    command << " -i " << quote(options.bgfxSource / "src");
    command << " --varyingdef " << quote(options.root / "shaders/shadergraph/varying_graph.def.sc");
    if (backend == Backend::Dxbc) {
        command << " --platform windows -p s_5_0";
    } else {
        command << " --platform linux -p spirv -O 3 --Werror";
    }
    if (!defines.empty()) {
        command << " --define \"";
        for (std::size_t index = 0; index < defines.size(); ++index) {
            if (index != 0) {
                command << ";";
            }
            command << defines[index];
        }
        command << "\"";
    }
    command << " > " << quote(logPath) << " 2>&1";

    int exitCode = 1;
    std::string log;
    for (int attempt = 0; attempt < 2; ++attempt) {
        exitCode = std::system(command.str().c_str());
        log.clear();
        readFile(logPath, log);
        std::filesystem::remove(logPath);
        const bool hasOutput = std::filesystem::exists(output) &&
                               std::filesystem::file_size(output) > 0;
        if (exitCode == 0 && hasOutput) {
            break;
        }
        std::filesystem::remove(output);
        if (attempt == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
    if (exitCode != 0) {
        result.error = "shaderc failed";
        result.compilerOutput = "command: " + command.str() + "\n" +
                                log.substr(0, std::min<std::size_t>(log.size(), 8192));
        std::filesystem::remove(output);
        return result;
    }
    if (!std::filesystem::exists(output) || std::filesystem::file_size(output) == 0) {
        result.error = "shaderc produced an empty output";
        result.compilerOutput = log.substr(0, std::min<std::size_t>(log.size(), 8192));
        std::filesystem::remove(output);
        return result;
    }
    writeKey(output, fullKey);
    result.ok = true;
    result.data.resize(static_cast<std::size_t>(std::filesystem::file_size(output)));
    std::ifstream file(output, std::ios::binary);
    file.read(reinterpret_cast<char*>(result.data.data()),
              static_cast<std::streamsize>(result.data.size()));
    return result;
}

} // namespace sg::detail
