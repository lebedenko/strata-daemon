#pragma once

#include <format>
#include <iostream>
#include <string>
#include <string_view>

namespace strata::log {

enum class Level {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

void init(bool verbose = false);
void setLevel(Level level);
Level getLevel();
bool isSystemd();

void logMessage(Level level, std::string_view message);

template <typename... Args> void debug(std::format_string<Args...> fmt, Args &&...args) {
    if (getLevel() <= Level::Debug) {
        logMessage(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
    }
}

template <typename... Args> void info(std::format_string<Args...> fmt, Args &&...args) {
    if (getLevel() <= Level::Info) {
        logMessage(Level::Info, std::format(fmt, std::forward<Args>(args)...));
    }
}

template <typename... Args> void warn(std::format_string<Args...> fmt, Args &&...args) {
    if (getLevel() <= Level::Warn) {
        logMessage(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
    }
}

template <typename... Args> void error(std::format_string<Args...> fmt, Args &&...args) {
    if (getLevel() <= Level::Error) {
        logMessage(Level::Error, std::format(fmt, std::forward<Args>(args)...));
    }
}

} // namespace strata::log

#define LOG_DEBUG(...) ::strata::log::debug(__VA_ARGS__)
#define LOG_INFO(...) ::strata::log::info(__VA_ARGS__)
#define LOG_WARN(...) ::strata::log::warn(__VA_ARGS__)
#define LOG_ERROR(...) ::strata::log::error(__VA_ARGS__)
