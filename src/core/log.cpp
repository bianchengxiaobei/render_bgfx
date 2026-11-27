#include "core/log.h"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace hbg::log {
namespace {

std::mutex g_mutex;
std::atomic<Level> g_level{Level::Info};

std::FILE* streamFor(Level level) {
    return level >= Level::Warn ? stderr : stdout;
}

} // namespace

void init() {
    setLevel(Level::Info);
}

void setLevel(Level level) {
    g_level.store(level, std::memory_order_relaxed);
}

Level level() {
    return g_level.load(std::memory_order_relaxed);
}

void shutdown() {
    std::fflush(stdout);
    std::fflush(stderr);
}

void write(Level level, std::string_view message) {
    if (level < g_level.load(std::memory_order_relaxed)) {
        return;
    }

    const std::string line = std::format("[{}] {}\n", toString(level), message);

    std::lock_guard<std::mutex> lock(g_mutex);
    std::FILE* out = streamFor(level);
    std::fwrite(line.data(), 1, line.size(), out);
    std::fflush(out);

#ifdef _WIN32
    ::OutputDebugStringA(line.c_str());
#endif
}

[[noreturn]] void fatal(std::string_view message) {
    write(Level::Fatal, message);
    std::abort();
}

std::string_view toString(Level level) {
    switch (level) {
    case Level::Debug:
        return "DEBUG";
    case Level::Info:
        return "INFO ";
    case Level::Warn:
        return "WARN ";
    case Level::Error:
        return "ERROR";
    case Level::Fatal:
        return "FATAL";
    }
    return "UNKWN";
}

} // namespace hbg::log
