#pragma once

#include <chrono>
#include <cstdint>

namespace td {

enum class FeedKind {
    Mock,   // synthetic GBM ticks; useful for tests and demos without network
    Yahoo,  // real quotes via HTTPS to query1.finance.yahoo.com
};

// Process-wide configuration. Compiled defaults for now; later we'll load
// `config.json` next to the binary if present.
class Config {
public:
    static Config& instance();

    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    Config(Config&&) = delete;
    Config& operator=(Config&&) = delete;

    // Which feed the MarketDataService should drain into the ring.
    FeedKind feed_kind() const noexcept { return feed_kind_; }
    void set_feed_kind(FeedKind k) noexcept { feed_kind_ = k; }

    // Mock feed: ticks per symbol per second. 10 Hz is plenty for a UI that flickers.
    std::uint32_t ticks_per_second() const noexcept { return ticks_per_second_; }
    void set_ticks_per_second(std::uint32_t v) noexcept { ticks_per_second_ = v; }

    // Per-symbol price volatility for the mock feed (annualized stddev approx).
    double mock_volatility() const noexcept { return mock_volatility_; }
    void set_mock_volatility(double v) noexcept { mock_volatility_ = v; }

    // Sleep slice for the mock feed's producer loop.
    std::chrono::microseconds loop_slice() const noexcept;

    // Yahoo feed: nominal interval between successful quote polls. Matches
    // the prior TypeScript POLL_INTERVAL_MS of 10 minutes.
    std::chrono::milliseconds quote_poll_interval() const noexcept {
        return quote_poll_interval_;
    }
    void set_quote_poll_interval(std::chrono::milliseconds v) noexcept {
        quote_poll_interval_ = v;
    }

    // Yahoo feed: backoff after `consecutive_failures` failed polls. Mirrors
    // the TS BACKOFF_STEPS_MS table; saturates at the last entry.
    std::chrono::milliseconds quote_backoff_for(int consecutive_failures) const noexcept;

    // History backfill window — 1-minute chart fetched at startup.
    int history_backfill_days() const noexcept { return history_backfill_days_; }
    void set_history_backfill_days(int v) noexcept { history_backfill_days_ = v; }

    // Summary lookback window — fetched daily, then we pick the close
    // closest to month_ago_days() for the "% change vs ~1 month ago" widget.
    int summary_lookback_days() const noexcept { return summary_lookback_days_; }
    void set_summary_lookback_days(int v) noexcept { summary_lookback_days_ = v; }

    int month_ago_days() const noexcept { return month_ago_days_; }
    void set_month_ago_days(int v) noexcept { month_ago_days_ = v; }

    static void reset_for_tests();

private:
    Config() = default;
    ~Config() = default;

    FeedKind                  feed_kind_              = FeedKind::Yahoo;
    std::uint32_t             ticks_per_second_       = 10;
    double                    mock_volatility_        = 0.20; // 20% annualized
    std::chrono::milliseconds quote_poll_interval_    = std::chrono::minutes{10};
    int                       history_backfill_days_  = 7;    // matches TS LOOKBACK_DAYS
    int                       summary_lookback_days_  = 35;
    int                       month_ago_days_         = 30;
};

} // namespace td
