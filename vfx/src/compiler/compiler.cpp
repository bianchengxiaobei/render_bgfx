#include "binary.h"
#include "codegen.h"

#include "vfx/asset.h"
#include "vfx/compiler.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/wait.h>
#endif

namespace vfx {
namespace {

std::string joinErrors(const std::vector<ParseError>& errors) {
    if (errors.empty()) {
        return "failed to parse graph";
    }
    std::string message;
    for (const ParseError& error : errors) {
        if (!message.empty()) {
            message += '\n';
        }
        message += error.path;
        message += ": ";
        message += error.message;
    }
    return message;
}

std::string quoteArgument(const std::string& value) {
#ifdef _WIN32
    std::string result = "\"";
    for (const char character : value) {
        if (character == '"') {
            result += "\\\"";
        } else {
            result += character;
        }
    }
    result += "\"";
    return result;
#else
    std::string result = "'";
    for (const char character : value) {
        if (character == '\'') {
            result += "'\\''";
        } else {
            result += character;
        }
    }
    result += "'";
    return result;
#endif
}

bool runProcess(const std::string& command, std::string& output, int& exitCode) {
    output.clear();
    exitCode = -1;
#ifdef _WIN32
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &attributes, 0)) {
        return false;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;

    PROCESS_INFORMATION process{};
    std::wstring commandLine(command.begin(), command.end());
    const BOOL created = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(writePipe);
    if (!created) {
        CloseHandle(readPipe);
        return false;
    }
    CloseHandle(process.hThread);

    char buffer[4096];
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
        output.append(buffer, bytesRead);
    }
    CloseHandle(readPipe);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD processExit = 0;
    GetExitCodeProcess(process.hProcess, &processExit);
    CloseHandle(process.hProcess);
    exitCode = static_cast<int>(processExit);
    return true;
#else
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (pipe == nullptr) {
        return false;
    }
    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    const int status = pclose(pipe);
    if (status == -1) {
        return false;
    }
    exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return true;
#endif
}

const char* shaderTypeName(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex:
        return "vertex";
    case ShaderStage::Fragment:
        return "fragment";
    case ShaderStage::Compute:
        return "compute";
    }
    return "compute";
}

bool writeTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(file);
}

bool readBinaryFile(const std::filesystem::path& path, std::vector<std::byte>& out) {
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

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory() {
        if (!path.empty()) {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    }
};

bool runShaderc(const std::filesystem::path& shaderc, const std::filesystem::path& source,
                const std::filesystem::path& output, const std::filesystem::path& varyingDef,
                ShaderStage stage, ShaderBackend backend, const CompileOptions& options,
                std::string& error) {
    std::vector<std::string> arguments;
    arguments.push_back(quoteArgument(shaderc.string()));
    arguments.push_back("-f");
    arguments.push_back(quoteArgument(source.string()));
    arguments.push_back("-o");
    arguments.push_back(quoteArgument(output.string()));
    arguments.push_back("--type");
    arguments.push_back(shaderTypeName(stage));
    arguments.push_back("-i");
    arguments.push_back(quoteArgument((options.bgfxSource / "src").string()));
    if (!options.shaderPack.empty()) {
        arguments.push_back("-i");
        arguments.push_back(quoteArgument(options.shaderPack.string()));
        arguments.push_back("-i");
        arguments.push_back(quoteArgument((options.shaderPack / "include").string()));
    }
    if (stage != ShaderStage::Compute) {
        arguments.push_back("--varyingdef");
        arguments.push_back(quoteArgument(varyingDef.string()));
    }
    if (backend == ShaderBackend::Dxbc) {
        arguments.push_back("--platform");
        arguments.push_back("windows");
        arguments.push_back("-p");
        arguments.push_back("s_5_0");
    } else {
        arguments.push_back("--platform");
        arguments.push_back("linux");
        arguments.push_back("-p");
        arguments.push_back("spirv");
    }
    arguments.push_back("-O");
    arguments.push_back("3");
    arguments.push_back("--Werror");

    std::string command;
    for (const std::string& argument : arguments) {
        if (!command.empty()) {
            command += ' ';
        }
        command += argument;
    }
    std::string log;
    int exitCode = -1;
    if (!runProcess(command, log, exitCode) || exitCode != 0) {
        if (log.size() > 4000) {
            log = log.substr(log.size() - 4000);
        }
        error = "shaderc failed for " + source.filename().string() + " (" + shaderTypeName(stage) +
                ", " + (backend == ShaderBackend::Dxbc ? "dxbc" : "spirv") + ")";
        if (!log.empty()) {
            error += ": " + log;
        }
        error += "\ncommand: " + command;
        return false;
    }
    return true;
}

bool compileSource(const detail::ShaderSource& shader, const CompileOptions& options,
                   const TemporaryDirectory& temp, std::size_t index,
                   std::vector<detail::BinaryBlob>& blobs, std::string& error) {
    const std::filesystem::path sourcePath =
        temp.path / ("shader_" + std::to_string(index) + (shader.stage == ShaderStage::Compute
                                                              ? ".cs.sc"
                                                              : shader.stage == ShaderStage::Vertex ? ".vs.sc"
                                                                                                    : ".fs.sc"));
    if (!writeTextFile(sourcePath, shader.source)) {
        error = "failed to write generated shader " + shader.name;
        return false;
    }
    const std::filesystem::path varyingDef = temp.path / "vfx_generated_varyings.def.sc";
    if (!shader.varyings.empty() && !writeTextFile(varyingDef, shader.varyings)) {
        error = "failed to write generated varying definition for " + shader.name;
        return false;
    }

    std::vector<ShaderBackend> backends = options.backends;
    std::sort(backends.begin(), backends.end(), [](ShaderBackend a, ShaderBackend b) {
        return static_cast<int>(a) < static_cast<int>(b);
    });
    backends.erase(std::unique(backends.begin(), backends.end()), backends.end());
    for (const ShaderBackend backend : backends) {
        const std::string suffix = backend == ShaderBackend::Dxbc ? ".dxbc" : ".spv";
        const std::filesystem::path outputPath =
            temp.path / ("shader_" + std::to_string(index) + suffix);
        if (!runShaderc(options.shaderc, sourcePath, outputPath, varyingDef, shader.stage, backend, options,
                        error)) {
            return false;
        }
        detail::BinaryBlob blob;
        blob.stage = shader.stage;
        blob.backend = backend;
        if (!readBinaryFile(outputPath, blob.data) || blob.data.empty()) {
            error = "shaderc produced no output for " + shader.name;
            return false;
        }
        blobs.push_back(std::move(blob));
    }
    return true;
}

} // namespace

CompileResult compileGraph(std::string_view json, const CompileOptions& options) {
    CompileResult result;
    if (options.shaderc.empty()) {
        result.error = "CompileOptions.shaderc is required";
        return result;
    }
    if (options.bgfxSource.empty()) {
        result.error = "CompileOptions.bgfxSource is required";
        return result;
    }
    if (options.backends.empty()) {
        result.error = "CompileOptions.backends must not be empty";
        return result;
    }
    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(options.shaderc, filesystemError)) {
        result.error = "shaderc executable not found: " + options.shaderc.string();
        return result;
    }
    if (!std::filesystem::is_directory(options.bgfxSource / "src", filesystemError)) {
        result.error = "bgfx source directory not found: " + options.bgfxSource.string();
        return result;
    }

    ParseResult parsed = parseGraphJson(json);
    if (!parsed.ok()) {
        result.error = joinErrors(parsed.errors);
        return result;
    }
    detail::CompiledAsset asset = detail::generatePrograms(std::move(parsed.graph));
    if (!asset.error.empty()) {
        result.error = asset.error;
        return result;
    }
    if (asset.graph == nullptr) {
        result.error = "compiler produced no graph";
        return result;
    }

    TemporaryDirectory temp;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    temp.path = std::filesystem::temp_directory_path() /
                ("vfxbake_" + std::to_string(static_cast<unsigned long long>(stamp)));
    std::filesystem::create_directories(temp.path, filesystemError);
    if (filesystemError) {
        result.error = "failed to create shader temporary directory: " + filesystemError.message();
        return result;
    }

    std::vector<std::vector<detail::BinaryBlob>> kernelBlobs;
    std::map<std::string, std::vector<detail::BinaryBlob>> shaderCache;
    std::size_t shaderIndex = 0;
    for (const detail::CompiledSystem& system : asset.systems) {
        for (const detail::KernelProgram& kernel : system.kernels) {
            const std::string cacheKey = std::to_string(static_cast<int>(kernel.shader.stage)) + "\n" +
                                         kernel.shader.varyings + "\n" + kernel.shader.source;
            const auto cached = shaderCache.find(cacheKey);
            if (cached != shaderCache.end()) {
                kernelBlobs.push_back(cached->second);
                continue;
            }
            std::vector<detail::BinaryBlob> blobs;
            if (!compileSource(kernel.shader, options, temp, shaderIndex++, blobs, result.error)) {
                return result;
            }
            shaderCache.emplace(cacheKey, blobs);
            kernelBlobs.push_back(std::move(blobs));
        }
    }

    std::string binaryError;
    result.binary = detail::writeAssetBinary(asset, kernelBlobs, options, binaryError);
    if (!binaryError.empty()) {
        result.error = std::move(binaryError);
        result.binary.clear();
    }
    return result;
}

CompileResult compileGraphFile(const std::filesystem::path& path, const CompileOptions& options) {
    CompileResult result;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        result.error = "failed to open graph file: " + path.string();
        return result;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    if (!file && !file.eof()) {
        result.error = "failed to read graph file: " + path.string();
        return result;
    }
    return compileGraph(contents.str(), options);
}

} // namespace vfx
