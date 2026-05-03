#pragma once

#include "HttpClient.h"

#include <string>

namespace td {

struct YahooCrumb {
    std::string crumb; // short opaque token, used as &crumb=... on quote endpoints
    std::string error; // non-empty on failure (transport, status, or empty body)

    bool ok() const noexcept { return error.empty() && !crumb.empty(); }
};

// Performs Yahoo Finance's two-step crumb handshake. Yahoo's quote endpoints
// require both a session cookie (set by hitting fc.yahoo.com) and a crumb
// token (returned by /v1/test/getcrumb). Both must be sent together on
// subsequent API calls.
//
// Holds a reference to the HttpClient so the cookie jar persists into
// downstream calls made through the same client.
class YahooAuth {
public:
    explicit YahooAuth(HttpClient& http) noexcept : http_(http) {}

    YahooAuth(const YahooAuth&)            = delete;
    YahooAuth& operator=(const YahooAuth&) = delete;

    // Hits fc.yahoo.com to seed cookies, then GETs /v1/test/getcrumb. The
    // returned crumb (when ok()) should be appended as &crumb=... to
    // subsequent quote requests.
    YahooCrumb fetch();

private:
    HttpClient& http_;
};

} // namespace td
