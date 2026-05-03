#include "HttpClient.h"

#include <curl/curl.h>

#include <atomic>
#include <mutex>

namespace td {

namespace {

// curl_global_init must be called once per process, before any other curl
// call. We gate it with std::call_once and trust the OS to release the
// thread/socket resources at exit (curl_global_cleanup is not strictly
// required and dropping it avoids ordering hazards in long-lived processes).
std::once_flag g_curl_init_once;

void ensure_global_init() {
    std::call_once(g_curl_init_once, [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    });
}

// Default UA. Yahoo's edge sometimes 4xxs the bare libcurl UA; a plausible
// browser string sidesteps that without us pretending to be Chrome elsewhere.
constexpr const char* kDefaultUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";

std::size_t write_to_string(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto*       out = static_cast<std::string*>(userdata);
    std::size_t n   = size * nmemb;
    out->append(ptr, n);
    return n;
}

} // namespace

HttpClient::HttpClient() {
    ensure_global_init();
    curl_ = curl_easy_init();
}

HttpClient::~HttpClient() {
    if (curl_) {
        curl_easy_cleanup(static_cast<CURL*>(curl_));
        curl_ = nullptr;
    }
}

HttpResponse HttpClient::get(const std::string&              url,
                             const std::vector<std::string>& extra_headers) {
    HttpResponse resp;
    if (!curl_) {
        resp.error = "curl easy handle not initialised";
        return resp;
    }

    auto* curl = static_cast<CURL*>(curl_);
    curl_easy_reset(curl);

    // Cookie engine on, in-memory jar (empty filename = enable without loading).
    // We keep the same handle across get() calls so the jar persists.
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kDefaultUserAgent);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_ms_));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5'000L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); // accept whatever curl built with

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);

    struct curl_slist* hdrs = nullptr;
    for (const auto& h : extra_headers) {
        hdrs = curl_slist_append(hdrs, h.c_str());
    }
    if (hdrs) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    }

    const CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        resp.error = curl_easy_strerror(rc);
    } else {
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        resp.status_code = status;
    }

    if (hdrs) curl_slist_free_all(hdrs);
    return resp;
}

std::string HttpClient::escape(const std::string& s) const {
    if (!curl_) return s;
    auto* curl   = static_cast<CURL*>(curl_);
    char* esc    = curl_easy_escape(curl, s.data(), static_cast<int>(s.size()));
    if (!esc) return s;
    std::string out(esc);
    curl_free(esc);
    return out;
}

} // namespace td
