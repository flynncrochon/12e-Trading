#include "MockFeed.h"

#include "Config.h"
#include "SymbolRegistry.h"

#include <chrono>
#include <cmath>

namespace td {

MockFeed::MockFeed() : rng_(0xC0FFEE12u) {}

MockFeed& MockFeed::instance() {
    static MockFeed inst;
    return inst;
}

void MockFeed::start() {
    if (started_) return;
    paths_.clear();

    const auto& entries = SymbolRegistry::instance().entries();
    const double sigma = Config::instance().mock_volatility();
    paths_.reserve(entries.size());
    for (const auto& e : entries) {
        paths_.push_back(Path{e.seed_price, /*drift*/ 0.05, sigma});
    }
    started_ = true;
}

void MockFeed::stop() {
    started_ = false;
}

Tick MockFeed::step(std::uint32_t symbol_index, double dt_seconds, std::uint64_t seq_now,
                    std::uint64_t ts_ns_now) {
    Path& p = paths_[symbol_index];

    // dS = S * (mu * dt + sigma * sqrt(dt) * Z)
    std::normal_distribution<double> z{0.0, 1.0};
    const double zv = z(rng_);
    const double sqrt_dt = std::sqrt(dt_seconds);
    const double increment = p.drift * dt_seconds + p.volatility * sqrt_dt * zv;
    p.price = p.price * std::exp(increment);
    if (p.price < 0.01) p.price = 0.01;

    std::uniform_int_distribution<int> vol_dist{50, 1500};

    Tick t{};
    t.symbol_id = symbol_index;
    t.volume    = static_cast<std::uint32_t>(vol_dist(rng_));
    t.price     = p.price;
    t.ts_ns     = ts_ns_now;
    t.seq       = seq_now;
    return t;
}

void MockFeed::reset_for_tests() {
    auto& self = instance();
    self.paths_.clear();
    self.rng_.seed(0xC0FFEE12u);
    self.started_ = false;
}

} // namespace td
