#pragma once

#include "ShmRegion.h"
#include "ShmRingBuffer.h"

#include <atomic>
#include <thread>

namespace td {

// Owns the shared-memory region, the ring writer, and the producer thread.
// The producer thread runs the YahooFeed poll loop: it issues a quote request
// every `quote_poll_interval()` and applies a backoff schedule on failure.
class MarketDataService {
public:
    static MarketDataService& instance();

    MarketDataService(const MarketDataService&) = delete;
    MarketDataService& operator=(const MarketDataService&) = delete;
    MarketDataService(MarketDataService&&) = delete;
    MarketDataService& operator=(MarketDataService&&) = delete;

    void start();
    void stop();
    bool running() const noexcept { return running_.load(std::memory_order_acquire); }

    std::uint64_t produced() const noexcept { return produced_.load(std::memory_order_relaxed); }
    std::uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

    static void reset_for_tests();

private:
    MarketDataService() = default;
    ~MarketDataService() = default;

    void run_yahoo_loop();

    shm::Region        region_;
    shm::RingWriter    writer_;
    std::thread        thread_;
    std::atomic<bool>  running_{false};
    std::atomic<bool>  stop_requested_{false};
    std::atomic<std::uint64_t> produced_{0};
    std::atomic<std::uint64_t> dropped_{0};
};

} // namespace td
