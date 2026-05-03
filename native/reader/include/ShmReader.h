#pragma once

#include "ShmRegion.h"
#include "ShmRingBuffer.h"
#include "Tick.h"

#include <cstddef>

namespace td {

// Opens an existing shared-memory ring written by the market-data-service.
// Used by the N-API binding loaded inside Electron's main process.
class ShmReader {
public:
    ShmReader();
    ~ShmReader() = default;

    bool open();
    void close();
    bool is_open() const noexcept { return region_.valid(); }

    // Drains up to max_n ticks into out. Returns the number actually read.
    std::size_t poll(Tick* out, std::size_t max_n);

private:
    shm::Region     region_;
    shm::RingReader reader_;
};

} // namespace td
