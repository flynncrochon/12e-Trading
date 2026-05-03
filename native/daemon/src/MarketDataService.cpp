#include "MarketDataService.h"

#include "Config.h"
#include "Logger.h"
#include "MockFeed.h"
#include "ShmLayout.h"
#include "SymbolRegistry.h"
#include "YahooFeed.h"

#include <algorithm>
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

    feed_kind_ = Config::instance().feed_kind();

    {
        std::ostringstream os;
        os << "MarketDataService: shm region '" << shm::kShmName
           << "' size=" << shm::kRegionSize
           << " ring_capacity=" << shm::kRingCapacity
           << " feed=" << (feed_kind_ == FeedKind::Yahoo ? "yahoo" : "mock");
        Logger::instance().info(os.str());
    }

    if (feed_kind_ == FeedKind::Mock) {
        MockFeed::instance().start();
    } else {
        YahooFeed::instance().start();
    }

    stop_requested_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] {
        if (feed_kind_ == FeedKind::Mock) {
            run_mock_loop();
        } else {
            run_yahoo_loop();
        }
    });
}

void MarketDataService::stop() {
    stop_requested_.store(true, std::memory_order_release);
    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false, std::memory_order_release);
    if (feed_kind_ == FeedKind::Mock) {
        MockFeed::instance().stop();
    } else {
        YahooFeed::instance().stop();
    }
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

void MarketDataService::run_mock_loop() {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();

    auto& cfg  = Config::instance();
    auto& feed = MockFeed::instance();
    auto& syms = SymbolRegistry::instance();

    const std::size_t n_symbols = syms.size();
    const auto        slice     = cfg.loop_slice();
    const double      dt_seconds_step = std::chrono::duration<double>(slice).count();

    std::uint64_t seq = 0;

    while (!stop_requested_.load(std::memory_order_acquire)) {
        const auto cycle_start = clock::now();

        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(n_symbols); ++i) {
            const auto now = clock::now();
            const auto ts_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(now - t0).count();
            Tick t = feed.step(i, dt_seconds_step, ++seq, static_cast<std::uint64_t>(ts_ns));
            if (writer_.try_push(t)) {
                produced_.fetch_add(1, std::memory_order_relaxed);
            } else {
                dropped_.fetch_add(1, std::memory_order_relaxed);
            }
        }

        const auto next = cycle_start + slice;
        const auto now  = clock::now();
        if (now < next) {
            std::this_thread::sleep_for(next - now);
        }
    }
}

void MarketDataService::run_yahoo_loop() {
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();

    auto& cfg  = Config::instance();
    auto& feed = YahooFeed::instance();

    int        consecutive_failures = 0;
    auto       next_poll            = clock::now(); // fire immediately on first iteration
    const auto check_slice          = std::chrono::milliseconds{250};

    while (!stop_requested_.load(std::memory_order_acquire)) {
        const auto now = clock::now();
        if (now < next_poll) {
            // Wait in small slices so a stop request gets noticed within ~250ms,
            // even though the natural cadence is 10 minutes.
            std::this_thread::sleep_for(std::min<clock::duration>(next_poll - now, check_slice));
            continue;
        }

        const auto ts_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - t0).count();
        const auto ticks = feed.poll(static_cast<std::uint64_t>(ts_ns));

        if (!feed.last_error().empty()) {
            consecutive_failures += 1;
            const auto backoff = cfg.quote_backoff_for(consecutive_failures);
            std::ostringstream os;
            os << "YahooFeed: poll failed (n=" << consecutive_failures << "): "
               << feed.last_error()
               << " — retry in " << backoff.count() << "ms";
            Logger::instance().warn(os.str());
            next_poll = clock::now() + backoff;
            continue;
        }

        consecutive_failures = 0;
        for (const auto& t : ticks) {
            if (writer_.try_push(t)) {
                produced_.fetch_add(1, std::memory_order_relaxed);
            } else {
                dropped_.fetch_add(1, std::memory_order_relaxed);
            }
        }

        {
            std::ostringstream os;
            os << "YahooFeed: poll ok, " << ticks.size() << " ticks";
            Logger::instance().info(os.str());
        }
        next_poll = clock::now() + cfg.quote_poll_interval();
    }
}

} // namespace td
