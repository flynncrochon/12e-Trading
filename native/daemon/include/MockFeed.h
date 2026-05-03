#pragma once

#include "Tick.h"

#include <cstdint>
#include <random>
#include <vector>

namespace td {

// Geometric-Brownian-motion price generator. One independent path per symbol,
// seeded from the SymbolRegistry. Produces a tick for the symbol with the
// next-due timestamp on each call.
class MockFeed {
public:
    static MockFeed& instance();

    MockFeed(const MockFeed&) = delete;
    MockFeed& operator=(const MockFeed&) = delete;
    MockFeed(MockFeed&&) = delete;
    MockFeed& operator=(MockFeed&&) = delete;

    void start();   // initialize per-symbol state from the registry
    void stop();

    // Produce one tick for symbol_index (0 .. symbols-1) using `dt_seconds`
    // as the elapsed simulated time since the previous tick for that symbol.
    Tick step(std::uint32_t symbol_index, double dt_seconds, std::uint64_t seq_now,
              std::uint64_t ts_ns_now);

    std::size_t symbol_count() const noexcept { return paths_.size(); }

    static void reset_for_tests();

private:
    MockFeed();
    ~MockFeed() = default;

    struct Path {
        double price;
        double drift;        // mu (annualized)
        double volatility;   // sigma (annualized)
    };

    std::vector<Path> paths_;
    std::mt19937_64   rng_;
    bool              started_ = false;
};

} // namespace td
