#pragma once

#include <chrono>
#include <cstdint>

namespace td {

// Process-wide configuration. Compiled defaults for now; later we'll load
// `config.json` next to the binary if present.
class Config {
public:
    static Config& instance();

    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    Config(Config&&) = delete;
    Config& operator=(Config&&) = delete;

    // Ticks per symbol per second. 10 Hz is plenty for a UI that flickers.
    std::uint32_t ticks_per_second() const noexcept { return ticks_per_second_; }
    void set_ticks_per_second(std::uint32_t v) noexcept { ticks_per_second_ = v; }

    // Per-symbol price volatility for the mock feed (annualized stddev approx).
    double mock_volatility() const noexcept { return mock_volatility_; }
    void set_mock_volatility(double v) noexcept { mock_volatility_ = v; }

    // Sleep slice for the main producer loop.
    std::chrono::microseconds loop_slice() const noexcept;

    static void reset_for_tests();

private:
    Config() = default;
    ~Config() = default;

    std::uint32_t ticks_per_second_ = 10;
    double        mock_volatility_  = 0.20;   // 20% annualized
};

} // namespace td
