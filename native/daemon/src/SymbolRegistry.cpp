#include "SymbolRegistry.h"

namespace td {

namespace {

// Must stay in sync with src/main/symbol_registry.ts (SEED_SYMBOLS).
// The renderer maps incoming Tick.symbol_id back to a ticker via that table,
// so any divergence here means ticks for the missing ids are silently dropped.
const SymbolRegistry::Entry kSeed[] = {
    { 0, "AAPL",   192.50},
    { 1, "MSFT",   415.20},
    { 2, "NVDA",   875.00},
    { 3, "TSLA",   248.30},
    { 4, "GOOG",   168.75},
    { 5, "META",   502.10},
    { 6, "AMZN",   185.40},
    { 7, "SPY",    525.60},
    { 8, "BHP.AX",  44.20},
    { 9, "CBA.AX", 113.00},
    {10, "CSL.AX", 290.00},
    {11, "NAB.AX",  35.50},
    {12, "WBC.AX",  27.80},
    {13, "ANZ.AX",  29.40},
    {14, "FMG.AX",  21.50},
    {15, "RIO.AX", 124.00},
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

const SymbolRegistry::Entry* SymbolRegistry::find_by_ticker(std::string_view ticker) const noexcept {
    for (const auto& e : entries_) {
        if (e.ticker == ticker) return &e;
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
