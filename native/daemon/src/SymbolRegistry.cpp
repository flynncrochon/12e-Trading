#include "SymbolRegistry.h"

namespace td {

namespace {

const SymbolRegistry::Entry kSeed[] = {
    {0, "AAPL", 192.50},
    {1, "MSFT", 415.20},
    {2, "NVDA", 875.00},
    {3, "TSLA", 248.30},
    {4, "GOOG", 168.75},
    {5, "META", 502.10},
    {6, "AMZN", 185.40},
    {7, "SPY",  525.60},
};

} // namespace

SymbolRegistry::SymbolRegistry() {
    entries_.reserve(sizeof(kSeed) / sizeof(kSeed[0]));
    for (const auto& e : kSeed) {
        entries_.push_back(e);
    }
}

SymbolRegistry& SymbolRegistry::instance() {
    static SymbolRegistry inst;
    return inst;
}

const SymbolRegistry::Entry* SymbolRegistry::find(std::uint32_t id) const noexcept {
    for (const auto& e : entries_) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

void SymbolRegistry::reset_for_tests() {
    auto& self = instance();
    self.entries_.clear();
    for (const auto& e : kSeed) {
        self.entries_.push_back(e);
    }
}

} // namespace td
