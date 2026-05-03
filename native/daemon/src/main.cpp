#include "Config.h"
#include "EventChannel.h"
#include "Logger.h"
#include "MarketDataService.h"
#include "NewsService.h"
#include "ShmLayout.h"
#include "SymbolRegistry.h"
#include "YahooHistory.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace {

std::atomic<bool> g_should_quit{false};

void request_shutdown() noexcept {
    g_should_quit.store(true, std::memory_order_release);
}

#ifdef _WIN32
BOOL WINAPI win_console_handler(DWORD type) {
    switch (type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            request_shutdown();
            return TRUE;
        default:
            return FALSE;
    }
}
#else
void posix_signal_handler(int /*sig*/) {
    request_shutdown();
}
#endif

void install_signal_handlers() {
#ifdef _WIN32
    ::SetConsoleCtrlHandler(win_console_handler, TRUE);
#else
    std::signal(SIGINT, posix_signal_handler);
    std::signal(SIGTERM, posix_signal_handler);
#endif
}

// Parses --event-port=N or --event-port N. Any other args are ignored.
std::optional<std::uint16_t> parse_event_port(int argc, char** argv) {
    constexpr std::string_view kFlag = "--event-port";
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == kFlag && i + 1 < argc) {
            const long v = std::strtol(argv[i + 1], nullptr, 10);
            if (v > 0 && v < 65'536) return static_cast<std::uint16_t>(v);
        } else if (arg.starts_with(kFlag) && arg.size() > kFlag.size() && arg[kFlag.size()] == '=') {
            const long v = std::strtol(arg.data() + kFlag.size() + 1, nullptr, 10);
            if (v > 0 && v < 65'536) return static_cast<std::uint16_t>(v);
        }
    }
    return std::nullopt;
}

} // namespace

int main(int argc, char** argv) {
    using namespace td;

    install_signal_handlers();
    Logger::instance().info("market-data-service: starting");

    (void) Config::instance();
    (void) SymbolRegistry::instance();

    {
        std::ostringstream os;
        os << "market-data-service: " << SymbolRegistry::instance().size() << " symbols";
        Logger::instance().info(os.str());
    }

    MarketDataService::instance().start();

    // Optional event channel: if --event-port=N was passed, connect back to
    // the Electron main and run the one-shot history backfill, summary, and
    // news-impact fetch.
    EventChannel                channel;
    std::optional<YahooHistory> history;
    std::optional<NewsService>  news;
    if (auto port = parse_event_port(argc, argv); port.has_value()) {
        std::ostringstream os;
        os << "market-data-service: event channel port=" << *port;
        Logger::instance().info(os.str());

        if (!channel.connect(*port)) {
            std::ostringstream err;
            err << "event channel: " << channel.last_error();
            Logger::instance().warn(err.str());
        } else {
            auto& cfg = Config::instance();
            history.emplace(channel,
                            cfg.history_backfill_days(),
                            cfg.summary_lookback_days(),
                            cfg.month_ago_days());
            history->start();
            news.emplace(channel,
                         cfg.news_lookback_days(),
                         cfg.news_per_symbol(),
                         cfg.losers_per_region());
            news->start();
        }
    } else {
        Logger::instance().info("market-data-service: no --event-port; skipping history+summary+news");
    }

    // Status heartbeat every 5 seconds while the loop runs.
    auto last_log = std::chrono::steady_clock::now();
    while (!g_should_quit.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto now = std::chrono::steady_clock::now();
        if (now - last_log >= std::chrono::seconds(5)) {
            std::ostringstream os;
            os << "market-data-service: produced=" << MarketDataService::instance().produced()
               << " dropped=" << MarketDataService::instance().dropped();
            Logger::instance().info(os.str());
            last_log = now;
        }
    }

    Logger::instance().info("market-data-service: shutdown requested");
    if (news) news->stop_and_join();
    if (history) history->stop_and_join();
    channel.close();
    MarketDataService::instance().stop();
    Logger::instance().info("market-data-service: bye");
    return 0;
}
