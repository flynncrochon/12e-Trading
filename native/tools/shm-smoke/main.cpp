// Standalone consumer for the shared-memory ring. Run after starting the
// daemon to verify the producer side is healthy without involving Electron.
//
//   $ ./market-data-service                # in one terminal
//   $ ./shm-smoke                          # in another
//
// Prints aggregate ticks/sec every second until 5 seconds have elapsed or
// 1000 ticks have been read, whichever comes first.

#include "ShmLayout.h"
#include "ShmRegion.h"
#include "ShmRingBuffer.h"
#include "Tick.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

int main() {
    using namespace td;
    using namespace td::shm;
    using clock = std::chrono::steady_clock;

    Region region;
    try {
        region = Region::open_existing(kShmName, kRegionSize);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "shm-smoke: open_existing failed: %s\n", e.what());
        std::fprintf(stderr, "shm-smoke: is the daemon running?\n");
        return 1;
    }

    auto* header = static_cast<Header*>(region.data());
    if (header->magic != kMagic || header->version != kVersion) {
        std::fprintf(stderr, "shm-smoke: header mismatch (magic=0x%08x version=%u)\n",
                     header->magic, header->version);
        return 2;
    }

    RingReader reader{header, slots_of(region.data())};

    constexpr std::size_t kBatch = 256;
    Tick buf[kBatch];

    std::uint64_t total = 0;
    std::uint64_t last_seq = 0;
    bool          have_last = false;
    std::uint64_t gaps = 0;

    const auto t_start = clock::now();
    auto t_last_print = t_start;
    std::uint64_t total_at_last_print = 0;

    while (true) {
        const std::size_t n = reader.pop_batch(buf, kBatch);
        for (std::size_t i = 0; i < n; ++i) {
            const Tick& t = buf[i];
            if (have_last && t.seq != last_seq + 1) {
                gaps += (t.seq - last_seq - 1);
            }
            last_seq = t.seq;
            have_last = true;
        }
        total += n;

        const auto now = clock::now();
        if (now - t_last_print >= std::chrono::seconds(1)) {
            const double dt = std::chrono::duration<double>(now - t_last_print).count();
            const double rate = (total - total_at_last_print) / dt;
            std::printf("shm-smoke: total=%llu rate=%.1f/sec gaps=%llu\n",
                        static_cast<unsigned long long>(total),
                        rate,
                        static_cast<unsigned long long>(gaps));
            std::fflush(stdout);
            t_last_print = now;
            total_at_last_print = total;
        }

        if (now - t_start >= std::chrono::seconds(5) || total >= 1000) {
            break;
        }

        if (n == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    const auto elapsed = std::chrono::duration<double>(clock::now() - t_start).count();
    std::printf("shm-smoke: read %llu ticks in %.2fs (%.1f/sec, %llu gaps)\n",
                static_cast<unsigned long long>(total), elapsed,
                total / (elapsed > 0 ? elapsed : 1),
                static_cast<unsigned long long>(gaps));
    return 0;
}
