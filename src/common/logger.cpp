#include "logger.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <unistd.h>

namespace strata::log {

namespace {

Level g_currentLevel = Level::Info;
bool g_isSystemd = false;
bool g_isTty = false;
std::mutex g_logMutex;

std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_buf{};
    localtime_r(&in_time_t, &tm_buf);

    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3)
       << ms.count();
    return ss.str();
}

const char *levelToString(Level l) {
    switch (l) {
    case Level::Debug:
        return "DEBUG";
    case Level::Info:
        return "INFO ";
    case Level::Warn:
        return "WARN ";
    case Level::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

const char *levelColor(Level l) {
    switch (l) {
    case Level::Debug:
        return "\033[36m"; // Cyan
    case Level::Info:
        return "\033[32m"; // Green
    case Level::Warn:
        return "\033[33m"; // Yellow
    case Level::Error:
        return "\033[31m"; // Red
    }
    return "\033[0m";
}

const char *journalPrefix(Level l) {
    switch (l) {
    case Level::Debug:
        return "<7>";
    case Level::Info:
        return "<6>";
    case Level::Warn:
        return "<4>";
    case Level::Error:
        return "<3>";
    }
    return "<6>";
}

} // namespace

void init(bool verbose) {
    const char *invocationId = std::getenv("INVOCATION_ID");
    const char *journalStream = std::getenv("JOURNAL_STREAM");
    g_isSystemd = (invocationId != nullptr) || (journalStream != nullptr);
    g_isTty = isatty(STDERR_FILENO) != 0;

    if (verbose) {
        g_currentLevel = Level::Debug;
        return;
    }

    const char *envLevel = std::getenv("STRATAD_LOG_LEVEL");
    if (envLevel != nullptr) {
        std::string lvl(envLevel);
        for (auto &c : lvl) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (lvl == "debug") {
            g_currentLevel = Level::Debug;
        } else if (lvl == "info") {
            g_currentLevel = Level::Info;
        } else if (lvl == "warn" || lvl == "warning") {
            g_currentLevel = Level::Warn;
        } else if (lvl == "error") {
            g_currentLevel = Level::Error;
        }
    } else {
        g_currentLevel = Level::Info;
    }
}

void setLevel(Level level) {
    g_currentLevel = level;
}

Level getLevel() {
    return g_currentLevel;
}

bool isSystemd() {
    return g_isSystemd;
}

void logMessage(Level level, std::string_view message) {
    std::lock_guard<std::mutex> lock(g_logMutex);

    if (g_isSystemd) {
        // Systemd native stream priority prefix format: <N>message
        std::cerr << journalPrefix(level) << "[" << levelToString(level) << "] " << message << '\n';
        std::cerr.flush();
    } else if (g_isTty) {
        // Terminal colored format
        std::cerr << "\033[90m" << getTimestamp() << "\033[0m " << levelColor(level) << "["
                  << levelToString(level) << "]\033[0m " << message << '\n';
        std::cerr.flush();
    } else {
        // Plain text format
        std::cerr << getTimestamp() << " [" << levelToString(level) << "] " << message << '\n';
        std::cerr.flush();
    }
}

} // namespace strata::log
