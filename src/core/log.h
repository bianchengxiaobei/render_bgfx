#pragma once

#include <format>
#include <string_view>
#include <utility>

namespace hbg::log {

enum class Level { Debug, Info, Warn, Error, Fatal };

void init();
void shutdown();
void setLevel(Level level);
Level level();
void write(Level level, std::string_view message);
[[noreturn]] void fatal(std::string_view message);
std::string_view toString(Level level);

template <typename... Args> inline void debug(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args> inline void info(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args> inline void warn(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args> inline void error(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args> [[noreturn]] inline void fatal(std::format_string<Args...> fmt, Args&&... args) {
    fatal(std::format(fmt, std::forward<Args>(args)...));
}

} // namespace hbg::log

#define HBG_LOGD(...) ::hbg::log::debug(__VA_ARGS__)
#define HBG_LOGI(...) ::hbg::log::info(__VA_ARGS__)
#define HBG_LOGW(...) ::hbg::log::warn(__VA_ARGS__)
#define HBG_LOGE(...) ::hbg::log::error(__VA_ARGS__)
#define HBG_LOGF(...) ::hbg::log::fatal(__VA_ARGS__)
