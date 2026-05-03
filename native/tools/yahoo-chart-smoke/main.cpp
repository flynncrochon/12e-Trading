// Standalone smoke test for the Yahoo chart endpoint. Fetches one symbol's
// 1-minute chart for the last 7 days and prints summary stats so we can
// verify the v8 chart parser before plugging it into the daemon's history
// backfill flow.
//
//   $ ./yahoo-chart-smoke                 # AAPL, 7d, 1m
//   $ ./yahoo-chart-smoke MSFT 14 1m      # MSFT, 14d, 1m
//   $ ./yahoo-chart-smoke BHP.AX 35 1d    # BHP.AX, 35d, 1d

#include "HttpClient.h"
#include "YahooClient.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    std::string symbol   = argc > 1 ? argv[1] : "AAPL";
    int         days     = argc > 2 ? std::atoi(argv[2]) : 7;
    std::string interval = argc > 3 ? argv[3] : "1m";

    const auto         now      = std::chrono::system_clock::now();
    const std::int64_t now_secs =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    const std::int64_t period1 = now_secs - static_cast<std::int64_t>(days) * 86'400;

    td::HttpClient  http;
    td::YahooClient client(http);

    const auto r = client.chart(symbol, period1, interval);
    if (!r.ok()) {
        std::fprintf(stderr, "yahoo-chart-smoke: %s\n", r.error.c_str());
        return 1;
    }

    std::size_t closed = 0;
    double      first_close = 0.0;
    double      last_close  = 0.0;
    std::int64_t first_t = 0;
    std::int64_t last_t  = 0;
    for (const auto& p : r.points) {
        if (!p.has_close) continue;
        if (closed == 0) {
            first_close = p.close;
            first_t     = p.t_ms;
        }
        last_close = p.close;
        last_t     = p.t_ms;
        ++closed;
    }

    std::printf("symbol=%s interval=%s days=%d points=%zu (with close=%zu)\n",
                symbol.c_str(), interval.c_str(), days, r.points.size(), closed);
    if (closed > 0) {
        std::printf("first: t_ms=%lld close=%.4f\n",
                    static_cast<long long>(first_t), first_close);
        std::printf("last:  t_ms=%lld close=%.4f\n",
                    static_cast<long long>(last_t), last_close);
    }
    return 0;
}
