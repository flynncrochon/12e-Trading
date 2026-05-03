#include "YahooAuth.h"

#include <algorithm>

namespace td {

namespace {

constexpr const char* kConsentUrl = "https://fc.yahoo.com/";
constexpr const char* kCrumbUrl   = "https://query1.finance.yahoo.com/v1/test/getcrumb";

// fc.yahoo.com returns a 4xx in the steady state — what matters is the
// Set-Cookie header on the response, which curl's cookie jar will capture
// regardless of status. So a transport failure is fatal here, but a 4xx is
// fine.
std::string trim(std::string s) {
    auto not_space = [](unsigned char c) { return c > 0x20; };
    auto first     = std::find_if(s.begin(), s.end(), not_space);
    auto last      = std::find_if(s.rbegin(), s.rend(), not_space).base();
    if (first >= last) return {};
    return std::string(first, last);
}

} // namespace

YahooCrumb YahooAuth::fetch() {
    YahooCrumb out;

    // Step 1: seed consent cookies. We don't care about status or body; we
    // only need the Set-Cookie side-effect into the http client's jar.
    auto consent = http_.get(kConsentUrl);
    if (!consent.transport_ok()) {
        out.error = "consent: " + consent.error;
        return out;
    }

    // Step 2: fetch the crumb. Body is the crumb itself, plain text, no JSON.
    auto crumb_resp = http_.get(kCrumbUrl);
    if (!crumb_resp.transport_ok()) {
        out.error = "getcrumb: " + crumb_resp.error;
        return out;
    }
    if (crumb_resp.status_code != 200) {
        out.error = "getcrumb: HTTP " + std::to_string(crumb_resp.status_code);
        return out;
    }

    out.crumb = trim(std::move(crumb_resp.body));
    if (out.crumb.empty()) {
        out.error = "getcrumb: empty crumb body";
    }
    return out;
}

} // namespace td
