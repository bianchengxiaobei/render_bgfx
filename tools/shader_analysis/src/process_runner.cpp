#include "hbg/analysis/process_runner.h"

#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>

#include <system_error>
#endif

namespace hbg::analysis {

#if defined(_WIN32)
namespace {

std::wstring utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        throw std::runtime_error("process argument is not valid UTF-8");
    }
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), length);
    return result;
}

std::wstring quoteArgument(const std::wstring& value) {
    if (value.empty()) {
        return L"\"\"";
    }
    if (value.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return value;
    }

    std::wstring result;
    result.push_back(L'"');
    size_t backslashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

std::wstring buildCommandLine(const std::filesystem::path& executable,
                              const std::vector<std::string>& arguments) {
    std::wstring commandLine = quoteArgument(executable.wstring());
    for (const std::string& argument : arguments) {
        commandLine.push_back(L' ');
        commandLine += quoteArgument(utf8ToWide(argument));
    }
    return commandLine;
}

std::string readPipe(HANDLE pipe) {
    std::string result;
    char buffer[4096];
    for (;;) {
        DWORD count = 0;
        if (!ReadFile(pipe, buffer, sizeof(buffer), &count, nullptr) || count == 0) {
            break;
        }
        result.append(buffer, count);
    }
    return result;
}

std::string windowsError(DWORD code) {
    return std::system_category().message(static_cast<int>(code));
}

} // namespace

ProcessResult runProcess(const std::filesystem::path& executable,
                         const std::vector<std::string>& arguments,
                         const std::filesystem::path& workingDirectory,
                         int timeoutMs,
                         const std::atomic<bool>* cancelFlag) {
    ProcessResult result;
    const ULONGLONG started = GetTickCount64();

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    HANDLE stderrRead = nullptr;
    HANDLE stderrWrite = nullptr;
    if (!CreatePipe(&stdoutRead, &stdoutWrite, &security, 0) ||
        !CreatePipe(&stderrRead, &stderrWrite, &security, 0)) {
        result.launchError = "failed to create process pipes: " +
                             windowsError(GetLastError());
        if (stdoutRead != nullptr) {
            CloseHandle(stdoutRead);
            CloseHandle(stdoutWrite);
        }
        if (stderrRead != nullptr) {
            CloseHandle(stderrRead);
            CloseHandle(stderrWrite);
        }
        result.durationMs = static_cast<std::int64_t>(GetTickCount64() - started);
        return result;
    }
    SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = stdoutWrite;
    startup.hStdError = stderrWrite;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION process{};
    std::wstring commandLine = buildCommandLine(executable, arguments);
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');
    const DWORD flags = CREATE_NO_WINDOW;
    const std::wstring cwd = workingDirectory.empty() ? std::wstring{}
                                                      : workingDirectory.wstring();
    if (!CreateProcessW(executable.wstring().c_str(), mutableCommandLine.data(), nullptr,
                        nullptr, TRUE, flags, nullptr, cwd.empty() ? nullptr : cwd.c_str(),
                        &startup, &process)) {
        result.launchError = "failed to launch " + executable.string() + ": " +
                             windowsError(GetLastError());
        CloseHandle(stdoutRead);
        CloseHandle(stdoutWrite);
        CloseHandle(stderrRead);
        CloseHandle(stderrWrite);
        result.durationMs = static_cast<std::int64_t>(GetTickCount64() - started);
        return result;
    }
    result.launched = true;

    CloseHandle(stdoutWrite);
    CloseHandle(stderrWrite);

    std::thread stdoutThread([&result, stdoutRead]() {
        result.stdoutText = readPipe(stdoutRead);
    });
    std::thread stderrThread([&result, stderrRead]() {
        result.stderrText = readPipe(stderrRead);
    });

    for (;;) {
        const DWORD wait = WaitForSingleObject(process.hProcess, 50);
        if (wait == WAIT_OBJECT_0) {
            break;
        }
        if (wait != WAIT_TIMEOUT) {
            result.launchError = "failed to wait for process: " +
                                 windowsError(GetLastError());
            TerminateProcess(process.hProcess, 1);
            break;
        }
        if (cancelFlag != nullptr && cancelFlag->load()) {
            result.cancelled = true;
            TerminateProcess(process.hProcess, 1);
            break;
        }
        if (timeoutMs > 0 &&
            static_cast<int64_t>(GetTickCount64() - started) >= timeoutMs) {
            result.timedOut = true;
            TerminateProcess(process.hProcess, 1);
            break;
        }
    }

    if (result.timedOut || result.cancelled) {
        WaitForSingleObject(process.hProcess, INFINITE);
    }

    stdoutThread.join();
    stderrThread.join();
    CloseHandle(stdoutRead);
    CloseHandle(stderrRead);

    DWORD exitCode = 0;
    if (GetExitCodeProcess(process.hProcess, &exitCode)) {
        result.exitCode = static_cast<int>(exitCode);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    result.durationMs = static_cast<std::int64_t>(GetTickCount64() - started);
    return result;
}

#else

ProcessResult runProcess(const std::filesystem::path& executable,
                         const std::vector<std::string>& arguments,
                         const std::filesystem::path&,
                         int,
                         const std::atomic<bool>*) {
    ProcessResult result;
    result.launchError = "process runner is only implemented on Windows: " +
                         executable.string() + " (" +
                         std::to_string(arguments.size()) + " argument(s))";
    return result;
}

#endif

} // namespace hbg::analysis
