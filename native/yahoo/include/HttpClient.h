#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace td {

struct HttpResponse {
    long        status_code = 0;
    std::string body;
    std::string error; // non-empty means a transport-level failure (DNS, TLS, timeout, etc.)

    bool transport_ok() const noexcept { return error.empty(); }
    bool ok() const noexcept { return transport_ok() && status_code >= 200 && status_code < 300; }
};

// Thin libcurl wrapper, GET-only for now. Owns a single curl easy handle with
// an in-memory cookie jar so callers that perform multi-step flows (like the
// Yahoo crumb fetch) get cookie reuse for free.
//
// One instance per logical session. Not thread-safe; callers must serialize
// access to a given HttpClient.
class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&)            = delete;
    HttpClient& operator=(const HttpClient&) = delete;
    HttpClient(HttpClient&&)                 = delete;
    HttpClient& operator=(HttpClient&&)      = delete;

    // GET the given URL. `extra_headers` are full header lines like
    // "Accept: application/json". Response body is captured into the result;
    // a default User-Agent and Accept header are sent unless overridden.
    HttpResponse get(const std::string& url, const std::vector<std::string>& extra_headers = {});

    // Percent-encode a string for use in a URL query value. Wraps
    // curl_easy_escape so callers don't need to deal with curl directly when
    // building URLs (crumbs in particular contain `/`, `+` etc).
    std::string escape(const std::string& s) const;

    void set_timeout_ms(std::int64_t ms) noexcept { timeout_ms_ = ms; }

private:
    void*        curl_       = nullptr; // CURL*
    std::int64_t timeout_ms_ = 10'000;
};

} // namespace td
