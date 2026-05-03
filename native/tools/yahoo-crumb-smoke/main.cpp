// Standalone smoke test for the Yahoo crumb auth flow. Performs the
// fc.yahoo.com → getcrumb handshake once and prints the crumb to stdout so we
// can verify the C++ HTTP path works end-to-end before plugging it into the
// rest of the daemon.
//
//   $ ./yahoo-crumb-smoke
//   crumb: aBcDeFg.HiJ
//
// Exit code 0 on success, 1 on any failure (network, status, or empty crumb).

#include "HttpClient.h"
#include "YahooAuth.h"

#include <cstdio>

int main() {
    td::HttpClient http;
    td::YahooAuth  auth(http);

    const auto result = auth.fetch();
    if (!result.ok()) {
        std::fprintf(stderr, "yahoo-crumb-smoke: %s\n", result.error.c_str());
        return 1;
    }

    std::printf("crumb: %s\n", result.crumb.c_str());
    return 0;
}
