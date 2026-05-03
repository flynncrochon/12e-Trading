#pragma once

#include "Tick.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace td::shm {

// Versioned name. Bump the suffix whenever Tick or Header changes shape.
#ifdef _WIN32
inline constexpr const char* kShmName = "Local\\12eTrading-ticks-v1";
#else
inline constexpr const char* kShmName = "/12eTrading-ticks-v1";
#endif

inline constexpr uint32_t kMagic = 0x31324554u;   // '1' '2' 'E' 'T' little-endian
inline constexpr uint32_t kVersion = 1u;
inline constexpr std::size_t kRingCapacity = 1u << 14;   // 16384 slots
inline constexpr std::size_t kRingMask = kRingCapacity - 1;

static_assert((kRingCapacity & kRingMask) == 0,
              "Ring capacity must be a power of two for the mask trick");

inline constexpr std::size_t kCacheLine = 64;

// Header layout in shared memory. head and tail live on their own cache
// lines so the producer and consumer don't ping-pong the same line.
struct alignas(kCacheLine) Header {
    uint32_t magic;
    uint32_t version;
    uint32_t capacity;
    uint32_t tick_size;
    char     _pad_meta[kCacheLine - 16];

    alignas(kCacheLine) std::atomic<uint64_t> head;   // next index to write
    char     _pad_head[kCacheLine - sizeof(std::atomic<uint64_t>)];

    alignas(kCacheLine) std::atomic<uint64_t> tail;   // next index to read
    char     _pad_tail[kCacheLine - sizeof(std::atomic<uint64_t>)];
};

static_assert(std::is_standard_layout<Header>::value,
              "Header must be standard-layout to live in shared memory");
static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "std::atomic<uint64_t> must be lock-free on this platform");

inline constexpr std::size_t kHeaderSize = sizeof(Header);
inline constexpr std::size_t kRegionSize = kHeaderSize + sizeof(Tick) * kRingCapacity;

// Returns a pointer to the first Tick slot, given the start of the region.
inline Tick* slots_of(void* region) noexcept {
    return reinterpret_cast<Tick*>(static_cast<char*>(region) + kHeaderSize);
}

inline const Tick* slots_of(const void* region) noexcept {
    return reinterpret_cast<const Tick*>(static_cast<const char*>(region) + kHeaderSize);
}

} // namespace td::shm
