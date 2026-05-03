#include "Config.h"

namespace td {

Config& Config::instance() {
    static Config inst;
    return inst;
}

std::chrono::milliseconds Config::quote_backoff_for(int consecutive_failures) const noexcept {
    using namespace std::chrono_literals;
    static constexpr std::chrono::milliseconds kSteps[] = {5'000ms, 15'000ms, 60'000ms, 300'000ms};
    if (consecutive_failures <= 0) return 0ms;
    const std::size_t idx =
        static_cast<std::size_t>(consecutive_failures - 1) >= std::size(kSteps)
            ? std::size(kSteps) - 1
            : static_cast<std::size_t>(consecutive_failures - 1);
    return kSteps[idx];
}

void Config::reset_for_tests() {
    auto& self = instance();
    self.quote_poll_interval_    = std::chrono::minutes{10};
    self.history_backfill_days_  = 7;
    self.summary_lookback_days_  = 35;
    self.month_ago_days_         = 30;
    self.news_lookback_days_     = 90;
    self.news_per_symbol_        = 50;
    self.losers_per_region_      = 30;
}

} // namespace td
