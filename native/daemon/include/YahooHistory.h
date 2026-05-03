#pragma once

#include "EventChannel.h"
#include "HttpClient.h"
#include "YahooClient.h"

#include <atomic>
#include <thread>

namespace td {

// One-shot history backfill + month-ago summary fetcher. Runs in a background
// thread, drives YahooClient::chart() across the full SymbolRegistry, and
// pushes results to the EventChannel as they arrive (one frame per symbol).
//
// Sequential per symbol — at 16 symbols and a few hundred ms per chart call,
// the total wall time is on the order of seconds. Concurrency would help but
// adds thread-safety surface to YahooClient/HttpClient that isn't worth it
// for a one-shot startup task.
class YahooHistory {
public:
    YahooHistory(EventChannel& channel,
                 int           backfill_lookback_days,
                 int           summary_lookback_days,
                 int           month_ago_days);

    YahooHistory(const YahooHistory&)            = delete;
    YahooHistory& operator=(const YahooHistory&) = delete;

    void start();           // launches the background thread
    void stop_and_join();   // signals exit and joins; safe to call repeatedly

    bool running() const noexcept { return running_.load(std::memory_order_acquire); }

private:
    void run();
    void do_backfill();
    void do_summary();

    EventChannel& channel_;
    int           backfill_days_;
    int           summary_days_;
    int           month_ago_days_;

    HttpClient        http_;
    YahooClient       client_{http_};
    std::thread       thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
};

} // namespace td
