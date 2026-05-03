#pragma once

#include "HttpClient.h"
#include "YahooAuth.h"

#include <cstdint>
#include <string>
#include <vector>

namespace td {

struct YahooQuote {
    std::string   symbol;
    double        price     = 0.0;
    std::int64_t  volume    = 0;
    bool          has_price = false; // regularMarketPrice was present and non-null
};

struct YahooQuoteResult {
    std::vector<YahooQuote> quotes;
    std::string             error; // non-empty on failure (transport, status, parse, or auth)

    bool ok() const noexcept { return error.empty(); }
};

struct ChartPoint {
    std::int64_t t_ms      = 0;     // unix milliseconds (matches TS Date.getTime())
    double       close     = 0.0;
    bool         has_close = false; // false if Yahoo returned null for this slot
};

struct ChartResult {
    std::vector<ChartPoint> points;
    std::string             error;

    bool ok() const noexcept { return error.empty(); }
};

struct YahooNewsItem {
    std::string  id;              // uuid (for de-dup across symbols)
    std::string  title;
    std::string  publisher;
    std::string  link;
    std::int64_t published_t_ms = 0; // providerPublishTime * 1000
};

struct YahooNewsResult {
    std::vector<YahooNewsItem> items;
    std::string                error;

    bool ok() const noexcept { return error.empty(); }
};

struct ScreenerQuote {
    std::string symbol;
    std::string short_name;
    double      price          = 0.0;
    double      change_pct     = 0.0; // regularMarketChangePercent (e.g. -8.5)
    bool        has_change_pct = false;
};

struct ScreenerResult {
    std::vector<ScreenerQuote> quotes;
    std::string                error;

    bool ok() const noexcept { return error.empty(); }
};

// Calls Yahoo Finance's quote endpoint and parses the response. Owns a crumb
// token transparently — the first quote() call performs the auth handshake,
// and a 401 transparently re-fetches the crumb and retries once.
//
// Holds an HttpClient by reference so its cookie jar persists; callers
// typically construct one HttpClient + one YahooClient and reuse them.
class YahooClient {
public:
    explicit YahooClient(HttpClient& http) noexcept : http_(http), auth_(http) {}

    YahooClient(const YahooClient&)            = delete;
    YahooClient& operator=(const YahooClient&) = delete;

    YahooQuoteResult quote(const std::vector<std::string>& symbols);

    // Calls /v8/finance/chart for one symbol. interval is "1m", "1d", etc.
    // (anything Yahoo accepts). period1_seconds is unix-time of the start of
    // the lookback window; period2 defaults to now. Crumb is not required
    // for the chart endpoint, so this is callable without prior auth.
    ChartResult chart(const std::string&  symbol,
                      std::int64_t        period1_seconds,
                      std::string_view    interval);

    // Calls /v1/finance/search for one symbol and returns up to `count`
    // recent news items. No crumb required. Items missing a title or
    // providerPublishTime are dropped silently.
    YahooNewsResult news(const std::string& symbol, int count = 10);

    // Calls Yahoo's predefined-screener endpoint and returns the matching
    // quotes. Common scr_id values: "day_losers", "day_gainers",
    // "most_actives". `region` is e.g. "US"; `count` caps at 100 server-side.
    // Requires a crumb — the same one-shot 401 retry path as quote().
    ScreenerResult screener(std::string_view scr_id,
                            std::string_view region,
                            int              count);

    // Exposed for diagnostics; empty until the first successful auth.
    const std::string& crumb() const noexcept { return crumb_; }

private:
    HttpClient& http_;
    YahooAuth   auth_;
    std::string crumb_;
};

} // namespace td
