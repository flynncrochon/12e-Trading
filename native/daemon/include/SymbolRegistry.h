#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace td {

// Symbols the daemon publishes ticks for. For now this is a fixed seed list
// matching what the renderer expects to see. Subscribe/unsubscribe over the
// control channel will mutate this later.
class SymbolRegistry {
public:
    struct Entry {
        std::uint32_t id;
        std::string   ticker;
        double        seed_price;
    };

    static SymbolRegistry& instance();

    SymbolRegistry(const SymbolRegistry&) = delete;
    SymbolRegistry& operator=(const SymbolRegistry&) = delete;
    SymbolRegistry(SymbolRegistry&&) = delete;
    SymbolRegistry& operator=(SymbolRegistry&&) = delete;

    const std::vector<Entry>& entries() const noexcept { return entries_; }
    std::size_t size() const noexcept { return entries_.size(); }

    // Returns nullptr if id is out of range.
    const Entry* find(std::uint32_t id) const noexcept;

    static void reset_for_tests();

private:
    SymbolRegistry();
    ~SymbolRegistry() = default;

    std::vector<Entry> entries_;
};

} // namespace td
