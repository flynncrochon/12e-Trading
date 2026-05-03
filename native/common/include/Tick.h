#pragma once

#include <cstdint>
#include <type_traits>

namespace td {

// Wire-format tick struct. Lives in shared memory, so it must be a POD
// with stable layout. Order chosen to avoid implicit padding on x86-64.
struct Tick {
    uint32_t symbol_id;   // index into SymbolRegistry's symbol table
    uint32_t volume;      // shares traded for this print
    double   price;       // last trade price
    uint64_t ts_ns;       // monotonic nanoseconds since the daemon started
    uint64_t seq;         // monotonically increasing per-tick sequence
};

static_assert(std::is_trivially_copyable<Tick>::value,
              "Tick must be trivially copyable to live in shared memory");
static_assert(sizeof(Tick) == 32, "Tick layout drift — bump kShmName version");
static_assert(alignof(Tick) == 8, "Tick alignment drift");

} // namespace td
