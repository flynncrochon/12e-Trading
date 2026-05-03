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

    // Exposed for diagnostics; empty until the first successful auth.
    const std::string& crumb() const noexcept { return crumb_; }

private:
    HttpClient& http_;
    YahooAuth   auth_;
    std::string crumb_;
};

} // namespace td
