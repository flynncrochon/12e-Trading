#include "Logger.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace td {

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::set_level(Level lvl) noexcept {
    level_ = lvl;
}

Logger::Level Logger::level() const noexcept {
    return level_;
}

const char* Logger::level_name(Level lvl) noexcept {
    switch (lvl) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
    }
    return "?";
}

void Logger::reset_for_tests() {
    auto& self = instance();
    std::lock_guard<std::mutex> lk(self.mtx_);
    self.level_ = Level::Info;
}

void Logger::log(Level lvl, std::string_view msg) {
    if (static_cast<int>(lvl) < static_cast<int>(level_)) return;

    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t = system_clock::to_time_t(now);
    const auto us = duration_cast<microseconds>(now.time_since_epoch()) % 1'000'000;

    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif

    std::ostringstream os;
    os << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
       << '.' << std::setw(6) << std::setfill('0') << us.count()
       << " [" << level_name(lvl) << "] "
       << msg << '\n';

    const std::string out = os.str();
    {
        std::lock_guard<std::mutex> lk(mtx_);
        std::fwrite(out.data(), 1, out.size(), stderr);
        std::fflush(stderr);
    }
}

} // namespace td
