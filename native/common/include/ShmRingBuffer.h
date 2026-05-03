#pragma once

#include "ShmLayout.h"
#include "Tick.h"

#include <atomic>
#include <cstddef>

namespace td::shm {

// SPSC lock-free ring over shared memory. Indices are 64-bit free-running
// counters; the slot index is `idx & kRingMask`. Empty when head == tail;
// full when head - tail == capacity.

class RingWriter {
public:
    RingWriter() = default;

    RingWriter(Header* header, Tick* slots) noexcept : header_(header), slots_(slots) {}

    // Returns false if the ring is full. The producer is expected to drop or
    // backpressure on its side; callers MUST NOT spin here forever.
    bool try_push(const Tick& t) noexcept {
        const auto head = header_->head.load(std::memory_order_relaxed);
        const auto tail = header_->tail.load(std::memory_order_acquire);
        if (head - tail >= kRingCapacity) {
            return false;
        }
        slots_[head & kRingMask] = t;
        header_->head.store(head + 1, std::memory_order_release);
        return true;
    }

    bool valid() const noexcept { return header_ != nullptr; }

private:
    Header* header_ = nullptr;
    Tick*   slots_  = nullptr;
};

class RingReader {
public:
    RingReader() = default;

    RingReader(Header* header, Tick* slots) noexcept : header_(header), slots_(slots) {}

    bool try_pop(Tick& out) noexcept {
        const auto tail = header_->tail.load(std::memory_order_relaxed);
        const auto head = header_->head.load(std::memory_order_acquire);
        if (tail == head) {
            return false;
        }
        out = slots_[tail & kRingMask];
        header_->tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Drains up to max_n ticks into out. Returns the number actually read.
    std::size_t pop_batch(Tick* out, std::size_t max_n) noexcept {
        const auto tail = header_->tail.load(std::memory_order_relaxed);
        const auto head = header_->head.load(std::memory_order_acquire);
        const std::size_t available = static_cast<std::size_t>(head - tail);
        const std::size_t n = available < max_n ? available : max_n;
        for (std::size_t i = 0; i < n; ++i) {
            out[i] = slots_[(tail + i) & kRingMask];
        }
        header_->tail.store(tail + n, std::memory_order_release);
        return n;
    }

    std::size_t available() const noexcept {
        const auto tail = header_->tail.load(std::memory_order_relaxed);
        const auto head = header_->head.load(std::memory_order_acquire);
        return static_cast<std::size_t>(head - tail);
    }

    bool valid() const noexcept { return header_ != nullptr; }

private:
    Header* header_ = nullptr;
    Tick*   slots_  = nullptr;
};

// Initialize the header in a freshly-created region. Idempotent: if the magic
// is already correct, no-op (multiple opens of the same region are fine).
inline void init_header(void* region) noexcept {
    auto* h = static_cast<Header*>(region);
    if (h->magic == kMagic && h->version == kVersion) {
        return;
    }
    h->magic = kMagic;
    h->version = kVersion;
    h->capacity = static_cast<uint32_t>(kRingCapacity);
    h->tick_size = static_cast<uint32_t>(sizeof(Tick));
    h->head.store(0, std::memory_order_relaxed);
    h->tail.store(0, std::memory_order_relaxed);
}

} // namespace td::shm
