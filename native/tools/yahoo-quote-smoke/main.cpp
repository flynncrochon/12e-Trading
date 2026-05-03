// Standalone smoke test for the Yahoo quote endpoint. Fetches a small list
// of symbols (defaulting to AAPL/MSFT/GOOG/TSLA) through YahooClient and
// prints the parsed quotes one per line. Confirms the crumb auth + JSON
// parsing work end-to-end before plugging YahooClient into the daemon.
//
//   $ ./yahoo-quote-smoke
//   AAPL  $189.84   vol=42738612
//   MSFT  $415.20   vol=15203411
//   ...
//   $ ./yahoo-quote-smoke BHP.AX CBA.AX

#include "HttpClient.h"
#include "YahooClient.h"

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::vector<std::string> symbols;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) symbols.emplace_back(argv[i]);
    } else {
        symbols = {"AAPL", "MSFT", "GOOG", "TSLA"};
    }

    td::HttpClient  http;
    td::YahooClient client(http);

    const auto result = client.quote(symbols);
    if (!result.ok()) {
        std::fprintf(stderr, "yahoo-quote-smoke: %s\n", result.error.c_str());
        return 1;
    }

    for (const auto& q : result.quotes) {
        if (q.has_price) {
            std::printf("%-8s $%-10.4f vol=%lld\n",
                        q.symbol.c_str(),
                        q.price,
                        static_cast<long long>(q.volume));
        } else {
            std::printf("%-8s (no price)  vol=%lld\n",
                        q.symbol.c_str(),
                        static_cast<long long>(q.volume));
        }
    }
    return 0;
}
