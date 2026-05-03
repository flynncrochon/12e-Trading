#include "Config.h"
#include "Logger.h"
#include "MarketDataService.h"
#include "MockFeed.h"
#include "ShmLayout.h"
#include "SymbolRegistry.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <sstream>
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

} // namespace

int main() {
    using namespace td;

    install_signal_handlers();
    Logger::instance().info("market-data-service: starting");

    // Touch each singleton in order. Construction is lazy; doing it here
    // makes the order explicit and guarantees Logger/Config/Symbols exist
    // before anyone else grabs them.
    (void) Config::instance();
    (void) SymbolRegistry::instance();
    (void) MockFeed::instance();

    {
        std::ostringstream os;
        os << "market-data-service: " << SymbolRegistry::instance().size() << " symbols, "
           << Config::instance().ticks_per_second() << " ticks/sec";
        Logger::instance().info(os.str());
    }

    MarketDataService::instance().start();

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
    MarketDataService::instance().stop();
    Logger::instance().info("market-data-service: bye");
    return 0;
}
