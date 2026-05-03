#include "YahooFeed.h"

#include "SymbolRegistry.h"

namespace td {

YahooFeed& YahooFeed::instance() {
    static YahooFeed inst;
    return inst;
}

void YahooFeed::start() {
    last_error_.clear();
    prior_volume_.clear();
    seq_     = 0;
    started_ = true;
}

void YahooFeed::stop() {
    started_ = false;
}

std::vector<Tick> YahooFeed::poll(std::uint64_t ts_ns_now) {
    std::vector<Tick> out;

    const auto& entries = SymbolRegistry::instance().entries();
    if (entries.empty()) {
        last_error_.clear();
        return out;
    }

    std::vector<std::string> tickers;
    tickers.reserve(entries.size());
    for (const auto& e : entries) tickers.push_back(e.ticker);

    auto result = client_.quote(tickers);
    if (!result.ok()) {
        last_error_ = result.error;
        return out;
    }

    last_error_.clear();
    out.reserve(result.quotes.size());
    for (const auto& q : result.quotes) {
        if (!q.has_price) continue;

        const auto* sym = SymbolRegistry::instance().find_by_ticker(q.symbol);
        if (!sym) continue; // Yahoo returned a ticker we don't track

        // First sighting emits volume=0 to match TS semantics. Subsequent
        // polls report the positive delta (clamped at zero across day rollover
        // in case Yahoo resets regularMarketVolume mid-session).
        std::int64_t volume_delta = 0;
        auto         prior_it     = prior_volume_.find(sym->id);
        if (prior_it != prior_volume_.end()) {
            const std::int64_t delta = q.volume - prior_it->second;
            volume_delta             = delta > 0 ? delta : 0;
        }
        prior_volume_[sym->id] = q.volume;

        Tick t{};
        t.symbol_id = sym->id;
        t.volume    = static_cast<std::uint32_t>(
            volume_delta > static_cast<std::int64_t>(UINT32_MAX) ? UINT32_MAX : volume_delta);
        t.price     = q.price;
        t.ts_ns     = ts_ns_now;
        t.seq       = ++seq_;
        out.push_back(t);
    }
    return out;
}

void YahooFeed::reset_for_tests() {
    auto& self = instance();
    self.prior_volume_.clear();
    self.seq_     = 0;
    self.last_error_.clear();
    self.started_ = false;
}

} // namespace td
