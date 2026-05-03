#pragma once

#include "EventChannel.h"
#include "HttpClient.h"
#include "YahooClient.h"

#include <atomic>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace td {

// One-shot market-wide negative-news scan. Runs in a background thread:
//
//   1. Query Yahoo's `day_losers` predefined screener (US + AU regions) to
//      identify the stocks that have been negatively impacted today —
//      *without* limiting the universe to the user's watchlist.
//   2. Fetch a daily chart for each benchmark (SPY for US tickers, ^AXJO
//      for ASX tickers) covering the news lookback window. Cached.
//   3. For each loser ticker, fetch its daily chart and recent news. For
//      each news item, find the close most recently before the headline
//      (pre) and the first close after it (post) for both the symbol and
//      its benchmark, then emit adjusted_pct = stock_pct - benchmark_pct
//      via the event channel.
//
// Items we can't score (missing pre/post close, older than the chart window)
// are dropped silently — the renderer only ever sees fully scored entries.
class NewsService {
public:
    NewsService(EventChannel& channel,
                int           lookback_days,
                int           news_per_symbol,
                int           losers_per_region);

    NewsService(const NewsService&)            = delete;
    NewsService& operator=(const NewsService&) = delete;

    void start();         // launches the background thread
    void stop_and_join(); // signals exit and joins; safe to call repeatedly

    bool running() const noexcept { return running_.load(std::memory_order_acquire); }

    // Internal helper exposed publicly so file-scope helpers in the cpp can
    // construct it; users of the class never need to touch this directly.
    struct DailySeries {
        // Closes sorted by timestamp ascending. Empty if the chart fetch
        // failed or returned no points.
        std::vector<std::pair<std::int64_t, double>> points;

        bool close_before(std::int64_t t_ms, double& out) const;
        bool close_after (std::int64_t t_ms, double& out) const;
    };

private:
    struct LoserStock {
        std::string ticker;
        std::string short_name;
        double      change_pct;     // today's % change from the screener
    };

    void run();

    // Pulls day_losers for one region and appends to `out`.
    void collect_losers(std::string_view region, std::vector<LoserStock>& out);

    // Lazily fetch + cache a benchmark's daily series. Returns nullptr only
    // on hard fetch failure (transport, parse). An empty series is still
    // returned as a valid pointer so we don't keep retrying.
    const DailySeries* benchmark_series(const std::string& bench_ticker,
                                        std::int64_t       period1_seconds);

    // Maps a ticker to the benchmark we score against.
    static std::string benchmark_for(const std::string& ticker);

    EventChannel& channel_;
    int           lookback_days_;
    int           news_per_symbol_;
    int           losers_per_region_;

    HttpClient        http_;
    YahooClient       client_{http_};
    std::thread       thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};

    // Filled on demand inside run() — same thread, so no synchronisation.
    std::unordered_map<std::string, DailySeries> benchmark_cache_;
};

} // namespace td
