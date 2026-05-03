#include "MarketDataService.h"

#include "Config.h"
#include "Logger.h"
#include "MockFeed.h"
#include "ShmLayout.h"
#include "SymbolRegistry.h"

#include <chrono>
#include <sstream>
#include <thread>

namespace td {

MarketDataService& MarketDataService::instance() {
    static MarketDataService inst;
    return inst;
}

void MarketDataService::start() {
    if (running_.load(std::memory_order_acquire)) return;

    region_ = shm::Region::create_or_open(shm::kShmName, shm::kRegionSize);
    if (!region_.valid()) {
        Logger::instance().error("MarketDataService: failed to create shared memory region");
        return;
    }
    shm::init_header(region_.data());
    auto* header = static_cast<shm::Header*>(region_.data());
    writer_ = shm::RingWriter{header, shm::slots_of(region_.data())};

    {
        std::ostringstream os;
        os << "MarketDataService: shm region '" << shm::kShmName
           << "' size=" << shm::kRegionSize
           << " ring_capacity=" << shm::kRingCapacity;
        Logger::instance().info(os.str());
    }

    MockFeed::instance().start();

    stop_requested_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { run_loop(); });
}

void MarketDataService::stop() {
    stop_requested_.store(true, std::memory_order_release);
    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false, std::memory_order_release);
    MockFeed::instance().stop();
    region_ = shm::Region{};
    Logger::instance().info("MarketDataService: stopped");
}

void MarketDataService::reset_for_tests() {
    auto& self = instance();
    if (self.running_.load(std::memory_order_acquire)) {
        self.stop();
    }
    self.produced_.store(0, std::memory_order_relaxed);
    self.dropped_.store(0, std::memory_order_relaxed);
}

void MarketDataService::run_loop() {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();

    auto& cfg = Config::instance();
    auto& feed = MockFeed::instance();
    auto& syms = SymbolRegistry::instance();

    const std::size_t n_symbols = syms.size();
    const auto slice = cfg.loop_slice();   // microseconds per (per-symbol) tick cycle
    const double dt_seconds_step =
        std::chrono::duration<double>(slice).count();

    std::uint64_t seq = 0;

    while (!stop_requested_.load(std::memory_order_acquire)) {
        const auto cycle_start = clock::now();

        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(n_symbols); ++i) {
            const auto now = clock::now();
            const auto ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - t0).count();
            Tick t = feed.step(i, dt_seconds_step, ++seq, static_cast<std::uint64_t>(ts_ns));
            if (writer_.try_push(t)) {
                produced_.fetch_add(1, std::memory_order_relaxed);
            } else {
                dropped_.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Sleep until the next cycle. If we overran (slow consumer or hiccup),
        // skip ahead rather than catching up — the consumer doesn't need a perfect cadence.
        const auto next = cycle_start + slice;
        const auto now  = clock::now();
        if (now < next) {
            std::this_thread::sleep_for(next - now);
        }
    }
}

} // namespace td
