#pragma once

#include "HttpClient.h"
#include "Tick.h"
#include "YahooClient.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace td {

// Real-quote feed backed by Yahoo Finance. One poll() call performs one HTTP
// round-trip for the full SymbolRegistry watchlist and produces one Tick per
// symbol that returned a price. Volume is computed as the delta of
// regularMarketVolume vs the prior poll, matching the TypeScript behaviour
// the renderer expects (first poll for each symbol emits volume=0).
class YahooFeed {
public:
    static YahooFeed& instance();

    YahooFeed(const YahooFeed&)            = delete;
    YahooFeed& operator=(const YahooFeed&) = delete;
    YahooFeed(YahooFeed&&)                 = delete;
    YahooFeed& operator=(YahooFeed&&)      = delete;

    void start(); // resets per-poll state; safe to call repeatedly
    void stop();

    // Performs one quote poll. ts_ns_now is stamped into each emitted Tick.
    // On any failure (transport, status, parse), returns an empty vector and
    // last_error() is non-empty. On success, last_error() is cleared.
    std::vector<Tick> poll(std::uint64_t ts_ns_now);

    const std::string& last_error() const noexcept { return last_error_; }

    static void reset_for_tests();

private:
    YahooFeed()  = default;
    ~YahooFeed() = default;

    HttpClient  http_;
    YahooClient client_{http_};

    // symbol_id -> last observed regularMarketVolume, for delta computation.
    std::unordered_map<std::uint32_t, std::int64_t> prior_volume_;

    std::uint64_t seq_     = 0;
    std::string   last_error_;
    bool          started_ = false;
};

} // namespace td
