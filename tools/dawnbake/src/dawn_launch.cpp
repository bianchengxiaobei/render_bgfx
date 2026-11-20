#include "hbg/dawn/dawn_launch.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <limits>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace hbg::dawn {
namespace {

bool isHexGuid(const std::string& value) {
    if (value.size() != 32u) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

std::string lowerGuid(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

uint64_t elapsedMillis(std::chrono::steady_clock::time_point start) {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

std::string readLogTail(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }
    const std::streamoff size = file.tellg();
    constexpr std::streamoff tailSize = 8192;
    const std::streamoff offset = size > tailSize ? size - tailSize : 0;
    file.seekg(offset, std::ios::beg);
    std::string text(static_cast<size_t>(size - offset), '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(file.gcount()));
    return text;
}

#ifdef _WIN32

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(),
                        length);
    return result;
}

std::wstring quoteArgument(const std::wstring& value) {
    if (value.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return value;
    }

    std::wstring result;
    result.push_back(L'"');
    for (auto it = value.begin();; ++it) {
        std::size_t backslashes = 0;
        while (it != value.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == value.end()) {
            result.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'"');
        } else {
            result.append(backslashes, L'\\');
            result.push_back(*it);
        }
    }
    result.push_back(L'"');
    return result;
}

std::wstring commandLine(const std::filesystem::path& executable, const std::vector<std::string>& arguments) {
    std::wstring line = quoteArgument(executable.wstring());
    for (const std::string& argument : arguments) {
        line.push_back(L' ');
        line += quoteArgument(utf8ToWide(argument));
    }
    return line;
}

std::string windowsError(const char* what) {
    const DWORD code = GetLastError();
    return std::string(what) + ": " + std::error_code(static_cast<int>(code), std::system_category()).message();
}

#endif

} // namespace

JobWorkspace::~JobWorkspace() {
    if (created && !keep && !path.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
}

bool prepareJobWorkspace(const std::filesystem::path& requested, const std::string& guid, bool keep,
                         JobWorkspace* workspace, std::string* error) {
    if (workspace == nullptr) {
        if (error != nullptr) {
            *error = "job workspace output is null";
        }
        return false;
    }
    if (guid.empty()) {
        if (error != nullptr) {
            *error = "job workspace GUID is empty";
        }
        return false;
    }

    std::error_code ec;
    std::filesystem::path path = requested;
    if (path.empty()) {
        const std::filesystem::path root = std::filesystem::temp_directory_path(ec);
        if (ec) {
            if (error != nullptr) {
                *error = "cannot resolve temporary directory: " + ec.message();
            }
            return false;
        }
        path = root / "hbg_dawnbake" / "jobs" / guid;
    }

    path = std::filesystem::absolute(path, ec);
    if (ec) {
        if (error != nullptr) {
            *error = "cannot resolve job workspace: " + ec.message();
        }
        return false;
    }
    path = path.lexically_normal();

    const bool existed = std::filesystem::exists(path, ec);
    if (ec) {
        if (error != nullptr) {
            *error = "cannot inspect job workspace: " + ec.message();
        }
        return false;
    }
    if (existed && !std::filesystem::is_directory(path, ec)) {
        if (error != nullptr) {
            *error = "job workspace is not a directory: " + pathToUtf8(path);
        }
        return false;
    }
    if (!existed) {
        std::filesystem::create_directories(path, ec);
        if (ec) {
            if (error != nullptr) {
                *error = "cannot create job workspace: " + ec.message();
            }
            return false;
        }
    }

    workspace->path = path;
    workspace->created = !existed;
    workspace->keep = keep;
    return true;
}

bool buildDawnArguments(const std::filesystem::path& jobDirectory, const std::string& guid,
                        std::vector<std::string>* arguments, std::string* error) {
    if (arguments == nullptr) {
        if (error != nullptr) {
            *error = "arguments output is null";
        }
        return false;
    }
    if (!isHexGuid(guid)) {
        if (error != nullptr) {
            *error = "guid must be exactly 32 hex characters";
        }
        return false;
    }
    if (jobDirectory.empty()) {
        if (error != nullptr) {
            *error = "job directory is empty";
        }
        return false;
    }

    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(jobDirectory, ec);
    if (ec) {
        if (error != nullptr) {
            *error = "cannot resolve job directory: " + ec.message();
        }
        return false;
    }
    absolute = absolute.lexically_normal();

    std::wstring directory = absolute.wstring();
    while (!directory.empty() && (directory.back() == L'\\' || directory.back() == L'/')) {
        directory.pop_back();
    }
    directory.push_back(L'\\');

    arguments->clear();
    arguments->push_back("-local");
    arguments->push_back("-localdirectory");
    arguments->push_back(pathToUtf8(std::filesystem::path(directory)));
    arguments->push_back(lowerGuid(guid));
    return true;
}

ProcessResult runProcess(const ProcessOptions& options) {
    ProcessResult result;
    result.logPath = options.logPath;
    const auto start = std::chrono::steady_clock::now();

#ifndef _WIN32
    result.error = "process launching is only implemented on Windows";
    result.elapsedMs = elapsedMillis(start);
    return result;
#else
    std::error_code ec;
    if (options.executable.empty() || !std::filesystem::is_regular_file(options.executable, ec)) {
        result.error = "executable not found: " + pathToUtf8(options.executable);
        result.elapsedMs = elapsedMillis(start);
        return result;
    }

    const std::filesystem::path executable = std::filesystem::absolute(options.executable, ec);
    if (ec) {
        result.error = "cannot resolve executable: " + ec.message();
        result.elapsedMs = elapsedMillis(start);
        return result;
    }

    const std::filesystem::path workingDirectory =
        options.workingDirectory.empty() ? executable.parent_path() : options.workingDirectory;
    if (!std::filesystem::is_directory(workingDirectory, ec)) {
        result.error = "working directory not found: " + pathToUtf8(workingDirectory);
        result.elapsedMs = elapsedMillis(start);
        return result;
    }

    HANDLE logHandle = INVALID_HANDLE_VALUE;
    HANDLE nullHandle = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    if (!options.logPath.empty()) {
        if (!options.logPath.parent_path().empty()) {
            std::filesystem::create_directories(options.logPath.parent_path(), ec);
            if (ec) {
                result.error = "cannot create log directory: " + ec.message();
                result.elapsedMs = elapsedMillis(start);
                return result;
            }
        }
        logHandle = CreateFileW(options.logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (logHandle == INVALID_HANDLE_VALUE) {
            result.error = windowsError("cannot create log file");
            result.elapsedMs = elapsedMillis(start);
            return result;
        }
        nullHandle =
            CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
        if (nullHandle == INVALID_HANDLE_VALUE) {
            CloseHandle(logHandle);
            result.error = windowsError("cannot open NUL device");
            result.elapsedMs = elapsedMillis(start);
            return result;
        }
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (logHandle != INVALID_HANDLE_VALUE) {
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdInput = nullHandle;
        startup.hStdOutput = logHandle;
        startup.hStdError = logHandle;
    }

    PROCESS_INFORMATION process{};
    std::wstring line = commandLine(executable, options.arguments);
    const BOOL created =
        CreateProcessW(executable.c_str(), line.data(), nullptr, nullptr, logHandle != INVALID_HANDLE_VALUE,
                       CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, workingDirectory.c_str(), &startup, &process);
    if (!created) {
        result.error = windowsError("CreateProcessW failed");
        if (logHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(logHandle);
        }
        if (nullHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(nullHandle);
        }
        result.elapsedMs = elapsedMillis(start);
        return result;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
            !AssignProcessToJobObject(job, process.hProcess)) {
            CloseHandle(job);
            job = nullptr;
        }
    }

    if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        result.error = windowsError("ResumeThread failed");
        TerminateProcess(process.hProcess, 1);
    }

    if (result.error.empty()) {
        const bool hasTimeout = options.timeout.count() > 0;
        const int64_t timeoutMs = hasTimeout ? options.timeout.count() : 0;
        const int64_t pollMs = std::max<int64_t>(1, options.pollInterval.count());
        for (;;) {
            int64_t sliceMs = pollMs;
            if (hasTimeout) {
                const int64_t elapsed = static_cast<int64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start)
                        .count());
                if (elapsed >= timeoutMs) {
                    result.timedOut = true;
                    result.status = ProcessStatus::TimedOut;
                    break;
                }
                sliceMs = std::min(sliceMs, timeoutMs - elapsed);
            }

            const DWORD wait =
                WaitForSingleObject(process.hProcess, static_cast<DWORD>(std::min<int64_t>(sliceMs, MAXDWORD)));
            if (options.onPoll) {
                options.onPoll();
            }
            if (wait == WAIT_OBJECT_0) {
                result.status = ProcessStatus::Completed;
                break;
            }
            if (wait == WAIT_TIMEOUT) {
                continue;
            }
            result.error = windowsError("WaitForSingleObject failed");
            break;
        }

        if (result.timedOut || !result.error.empty()) {
            if (job != nullptr) {
                TerminateJobObject(job, 1);
            } else {
                TerminateProcess(process.hProcess, 1);
            }
            WaitForSingleObject(process.hProcess, 5000);
        }
    }

    DWORD exitCode = 0;
    if (GetExitCodeProcess(process.hProcess, &exitCode)) {
        result.exitCode = exitCode;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (job != nullptr) {
        CloseHandle(job);
    }
    if (logHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(logHandle);
    }
    if (nullHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(nullHandle);
    }

    result.elapsedMs = elapsedMillis(start);
    if (!options.logPath.empty()) {
        result.logTail = readLogTail(options.logPath);
    }
    return result;
#endif
}

const char* dawnBakeErrorName(DawnBakeError error) {
    switch (error) {
    case DawnBakeError::None:
        return "NONE";
    case DawnBakeError::JobCanceled:
        return "JOB_CANCELED";
    case DawnBakeError::HiveStartupFailure:
        return "HIVE_STARTUP_FAILURE";
    case DawnBakeError::HiveConnectionLost:
        return "HIVE_CONNECTION_LOST";
    case DawnBakeError::JobStartFailure:
        return "JOB_START_FAILURE";
    case DawnBakeError::CheckSceneFailure:
        return "CHECK_SCENE_FAILURE";
    case DawnBakeError::JobKilled:
        return "JOB_KILLED";
    case DawnBakeError::JobFailure:
        return "JOB_FAILURE";
    case DawnBakeError::ExportSceneFailure:
        return "EXPORT_SCENE_FAILURE";
    case DawnBakeError::ExportJobFailure:
        return "EXPORT_JOB_FAILURE";
    case DawnBakeError::TaskAddFailure:
        return "TASK_ADD_FAILURE";
    case DawnBakeError::TaskFailure:
        return "TASK_FAILURE";
    case DawnBakeError::TaskRejected:
        return "TASK_REJECTED";
    case DawnBakeError::ResultImportFailure:
        return "RESULT_IMPORT_FAILURE";
    case DawnBakeError::ResultEncodingFailure:
        return "RESULT_ENCODING_FAILURE";
    case DawnBakeError::ResultApplyFailure:
        return "RESULT_APPLY_FAILURE";
    case DawnBakeError::ResultSaveFailure:
        return "RESULT_SAVE_FAILURE";
    case DawnBakeError::ResultConvertFailure:
        return "RESULT_CONVERT_FAILURE";
    case DawnBakeError::Exception:
        return "EXCEPTION";
    case DawnBakeError::Unknown:
        return "UNKNOWN";
    }
    return "UNKNOWN";
}

namespace {

bool prepareDawnProcess(const DawnLaunchOptions& options, ProcessOptions* process, ProcessResult* result) {
    result->logPath = options.logPath;
    const auto start = std::chrono::steady_clock::now();

    if (!isHexGuid(options.guid)) {
        result->error = "guid must be exactly 32 hex characters";
        result->elapsedMs = elapsedMillis(start);
        return false;
    }
    if (options.dawnExecutable.empty()) {
        result->error = "Dawn executable is empty";
        result->elapsedMs = elapsedMillis(start);
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(options.dawnExecutable, ec)) {
        result->error = "Dawn executable not found: " + pathToUtf8(options.dawnExecutable);
        result->elapsedMs = elapsedMillis(start);
        return false;
    }
    if (!std::filesystem::is_directory(options.jobDirectory, ec)) {
        result->error = "job directory not found: " + pathToUtf8(options.jobDirectory);
        result->elapsedMs = elapsedMillis(start);
        return false;
    }

    const std::string guid = lowerGuid(options.guid);
    const std::filesystem::path scene = options.jobDirectory / (guid + ".scene");
    const std::filesystem::path jobs = options.jobDirectory / (guid + ".jobs");
    if (!std::filesystem::is_regular_file(scene, ec)) {
        result->error = "Dawn local scene not found: " + pathToUtf8(scene);
        result->elapsedMs = elapsedMillis(start);
        return false;
    }
    if (!std::filesystem::is_regular_file(jobs, ec)) {
        result->error = "Dawn local jobs not found: " + pathToUtf8(jobs);
        result->elapsedMs = elapsedMillis(start);
        return false;
    }

    std::vector<std::string> arguments;
    if (!buildDawnArguments(options.jobDirectory, guid, &arguments, &result->error)) {
        result->elapsedMs = elapsedMillis(start);
        return false;
    }

    process->executable = options.dawnExecutable;
    process->arguments = std::move(arguments);
    process->workingDirectory = std::filesystem::absolute(options.dawnExecutable, ec).parent_path();
    if (ec) {
        result->error = "cannot resolve Dawn working directory: " + ec.message();
        result->elapsedMs = elapsedMillis(start);
        return false;
    }
    process->logPath = options.logPath.empty() ? options.jobDirectory / "Dawn.log" : options.logPath;
    process->timeout = options.timeout;
    result->logPath = process->logPath;
    return true;
}

struct ResultBaseline {
    std::filesystem::path path;
    bool existed = false;
    uintmax_t size = 0;
    std::filesystem::file_time_type writeTime{};
};

ResultBaseline baselineFor(const std::filesystem::path& path) {
    ResultBaseline baseline;
    baseline.path = path;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return baseline;
    }
    baseline.existed = true;
    baseline.size = std::filesystem::file_size(path, ec);
    if (ec) {
        baseline.size = 0;
    }
    baseline.writeTime = std::filesystem::last_write_time(path, ec);
    return baseline;
}

bool resultReady(const ResultBaseline& baseline) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(baseline.path, ec)) {
        return false;
    }
    const uintmax_t size = std::filesystem::file_size(baseline.path, ec);
    if (ec || size == 0) {
        return false;
    }
    if (!baseline.existed || size != baseline.size) {
        return true;
    }
    const auto writeTime = std::filesystem::last_write_time(baseline.path, ec);
    return !ec && writeTime != baseline.writeTime;
}

DawnBakeError mapRunError(const ProcessResult& process, bool missingResults) {
    if (process.status == ProcessStatus::TimedOut) {
        return DawnBakeError::JobKilled;
    }
    if (process.status == ProcessStatus::Failed) {
        if (process.error.find("scene") != std::string::npos || process.error.find("jobs") != std::string::npos ||
            process.error.find("guid") != std::string::npos) {
            return DawnBakeError::CheckSceneFailure;
        }
        return DawnBakeError::HiveStartupFailure;
    }
    if (process.exitCode != 0u) {
        return DawnBakeError::JobFailure;
    }
    if (missingResults) {
        return DawnBakeError::ResultImportFailure;
    }
    return DawnBakeError::None;
}

} // namespace

ProcessResult runDawn(const DawnLaunchOptions& options) {
    ProcessOptions process;
    ProcessResult result;
    if (!prepareDawnProcess(options, &process, &result)) {
        return result;
    }
    return runProcess(process);
}

DawnRunResult runDawnJob(const DawnLaunchOptions& options) {
    DawnRunResult result;
    ProcessOptions process;
    if (!prepareDawnProcess(options, &process, &result.process)) {
        result.expectedResults = options.expectedResults;
        result.missingResults = options.expectedResults;
        result.error = mapRunError(result.process, true);
        return result;
    }

    std::vector<ResultBaseline> baselines;
    baselines.reserve(options.expectedResults.size());
    for (const std::filesystem::path& expected : options.expectedResults) {
        const std::filesystem::path path = expected.is_absolute() ? expected : options.jobDirectory / expected;
        baselines.push_back(baselineFor(path));
        result.expectedResults.push_back(path);
    }

    std::vector<bool> found(baselines.size(), false);
    std::size_t reported = std::numeric_limits<std::size_t>::max();
    const auto report = [&options, &result, &reported] {
        const std::size_t completed = result.resultFiles.size();
        if (options.onProgress && completed != reported) {
            reported = completed;
            options.onProgress(completed, result.expectedResults.size());
        }
    };
    const auto capture = [&baselines, &found, &result, &report] {
        for (std::size_t index = 0; index < baselines.size(); ++index) {
            if (!found[index] && resultReady(baselines[index])) {
                found[index] = true;
                result.resultFiles.push_back(baselines[index].path);
            }
        }
        report();
    };

    report();
    process.onPoll = capture;
    process.pollInterval = std::chrono::milliseconds(100);
    result.process = runProcess(process);
    capture();

    bool allFound = found.empty() || std::all_of(found.begin(), found.end(), [](bool value) { return value; });
    if (result.process.status == ProcessStatus::Completed && !allFound && options.resultGrace.count() > 0) {
        const auto deadline = std::chrono::steady_clock::now() + options.resultGrace;
        while (std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            capture();
            allFound = std::all_of(found.begin(), found.end(), [](bool value) { return value; });
            if (allFound) {
                break;
            }
        }
    }

    for (std::size_t index = 0; index < baselines.size(); ++index) {
        if (!found[index]) {
            result.missingResults.push_back(baselines[index].path);
        }
    }
    report();
    result.error = mapRunError(result.process, !result.missingResults.empty());
    return result;
}

} // namespace hbg::dawn
