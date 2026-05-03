#pragma once

#include <chrono>

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

    // News: how far back to pull headlines, and how many per symbol.
    int news_lookback_days() const noexcept { return news_lookback_days_; }
    void set_news_lookback_days(int v) noexcept { news_lookback_days_ = v; }

    int news_per_symbol() const noexcept { return news_per_symbol_; }
    void set_news_per_symbol(int v) noexcept { news_per_symbol_ = v; }

    // News: how many losers per region to scan (US + AU). Yahoo's screener
    // caps at 100; ~30/region keeps the per-startup HTTP fan-out manageable.
    int losers_per_region() const noexcept { return losers_per_region_; }
    void set_losers_per_region(int v) noexcept { losers_per_region_ = v; }

    static void reset_for_tests();

private:
    Config() = default;
    ~Config() = default;

    std::chrono::milliseconds quote_poll_interval_    = std::chrono::minutes{10};
    int                       history_backfill_days_  = 7;    // matches TS LOOKBACK_DAYS
    int                       summary_lookback_days_  = 35;
    int                       month_ago_days_         = 30;
    // News retention window: the daemon fetches everything inside this range
    // and the renderer applies a finer user-chosen filter on top, so the
    // daemon should be generous here. Yahoo's search endpoint caps at ~50.
    int                       news_lookback_days_     = 90;
    int                       news_per_symbol_        = 50;
    int                       losers_per_region_      = 30;
};

} // namespace td
