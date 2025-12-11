#pragma once

#include <atomic>
#include <iostream>
#include <mutex>
#include <string_view>

namespace mx4hyprland {

enum class LogLevel : int {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3
};

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void set_level(LogLevel level) {
        level_.store(static_cast<int>(level), std::memory_order_relaxed);
    }

    void set_level(std::string_view level_str) {
        if (level_str == "debug") {
            set_level(LogLevel::Debug);
        } else if (level_str == "info") {
            set_level(LogLevel::Info);
        } else if (level_str == "warning") {
            set_level(LogLevel::Warning);
        } else if (level_str == "error") {
            set_level(LogLevel::Error);
        }
    }

    [[nodiscard]] bool should_log(LogLevel level) const {
        return static_cast<int>(level) >= level_.load(std::memory_order_relaxed);
    }

    template<typename... Args>
    void log(LogLevel level, std::string_view prefix, Args&&... args) {
        if (!should_log(level)) {
            return;
        }
        std::lock_guard lock(mutex_);
        std::cerr << prefix;
        ((std::cerr << std::forward<Args>(args)), ...);
        std::cerr << '\n';
    }

    template<typename... Args>
    void debug(Args&&... args) {
        log(LogLevel::Debug, "[DEBUG] ", std::forward<Args>(args)...);
    }

    template<typename... Args>
    void info(Args&&... args) {
        log(LogLevel::Info, "[INFO] ", std::forward<Args>(args)...);
    }

    template<typename... Args>
    void warning(Args&&... args) {
        log(LogLevel::Warning, "[WARNING] ", std::forward<Args>(args)...);
    }

    template<typename... Args>
    void error(Args&&... args) {
        log(LogLevel::Error, "[ERROR] ", std::forward<Args>(args)...);
    }

private:
    Logger() = default;
    std::atomic<int> level_{static_cast<int>(LogLevel::Info)};
    std::mutex mutex_;
};

inline Logger& logger() {
    return Logger::instance();
}

}
