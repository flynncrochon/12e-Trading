#include "SymbolRegistry.h"

namespace td {

namespace {

// Must stay in sync with src/main/symbol_registry.ts (SEED_SYMBOLS).
// The renderer maps incoming Tick.symbol_id back to a ticker via that table,
// so any divergence here means ticks for the missing ids are silently dropped.
const SymbolRegistry::Entry kSeed[] = {
    { 0, "AAPL"  },
    { 1, "MSFT"  },
    { 2, "NVDA"  },
    { 3, "TSLA"  },
    { 4, "GOOG"  },
    { 5, "META"  },
    { 6, "AMZN"  },
    { 7, "SPY"   },
    { 8, "BHP.AX"},
    { 9, "CBA.AX"},
    {10, "CSL.AX"},
    {11, "NAB.AX"},
    {12, "WBC.AX"},
    {13, "ANZ.AX"},
    {14, "FMG.AX"},
    {15, "RIO.AX"},
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
