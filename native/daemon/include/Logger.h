#pragma once

#include <mutex>
#include <string>
#include <string_view>

namespace td {

// Minimal thread-safe logger. Writes ISO-ish timestamped lines to stderr.
// Keeping this dependency-free for the first iteration; swap in spdlog once
// the build pipeline is proven end-to-end.
class Logger {
public:
    enum class Level { Trace, Debug, Info, Warn, Error };

    static Logger& instance();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    void set_level(Level lvl) noexcept;
    Level level() const noexcept;

    void log(Level lvl, std::string_view msg);

    void info(std::string_view msg)  { log(Level::Info, msg); }
    void warn(std::string_view msg)  { log(Level::Warn, msg); }
    void error(std::string_view msg) { log(Level::Error, msg); }
    void debug(std::string_view msg) { log(Level::Debug, msg); }

    static const char* level_name(Level lvl) noexcept;

    static void reset_for_tests();

private:
    Logger() = default;
    ~Logger() = default;

    std::mutex mtx_;
    Level      level_ = Level::Info;
};

} // namespace td
