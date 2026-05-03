#include "YahooClient.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <string_view>

namespace td {

namespace {

constexpr const char* kQuoteUrl    = "https://query1.finance.yahoo.com/v7/finance/quote";
constexpr const char* kChartUrlFmt = "https://query1.finance.yahoo.com/v8/finance/chart/";
constexpr const char* kSearchUrl   = "https://query1.finance.yahoo.com/v1/finance/search";
constexpr const char* kScreenerUrl = "https://query1.finance.yahoo.com/v1/finance/screener/predefined/saved";
constexpr const char* kRssUrl      = "https://feeds.finance.yahoo.com/rss/2.0/headline";

// CDATA-wrapped tag content from RSS. Returns the inside without "<![CDATA["
// / "]]>" markers if present, otherwise returns input unchanged.
std::string strip_cdata(std::string s) {
    constexpr std::string_view kStart = "<![CDATA[";
    constexpr std::string_view kEnd   = "]]>";
    auto p1 = s.find(kStart);
    if (p1 == std::string::npos) return s;
    s.erase(p1, kStart.size());
    auto p2 = s.find(kEnd, p1);
    if (p2 != std::string::npos) s.erase(p2, kEnd.size());
    return s;
}

// Pulls the inner text of <tag>...</tag> (or the first such, starting at
// search_pos) from `xml`. Updates search_pos to point past the closing
// tag so callers can iterate. Returns empty if the tag isn't found.
std::string extract_first_tag(const std::string& xml,
                              std::string_view   tag,
                              std::size_t        search_pos = 0) {
    std::string open_prefix = "<";
    open_prefix += tag;
    std::string close_tag = "</";
    close_tag += tag;
    close_tag += ">";

    auto p1 = xml.find(open_prefix, search_pos);
    if (p1 == std::string::npos) return "";

    // Skip the opening tag — may have attributes (e.g. <link rel="x">).
    auto p1_end = xml.find('>', p1);
    if (p1_end == std::string::npos) return "";
    ++p1_end;

    auto p2 = xml.find(close_tag, p1_end);
    if (p2 == std::string::npos) return "";

    return strip_cdata(xml.substr(p1_end, p2 - p1_end));
}

// Parses an RFC 822 date ("Wed, 01 May 2026 12:34:56 +0000") to unix
// seconds. Returns 0 on parse failure. Treats the timestamp as UTC; the
// timezone field is ignored because Yahoo's feed always emits +0000/GMT.
std::int64_t parse_rfc822(const std::string& s) {
    auto comma = s.find(',');
    auto rest  = (comma == std::string::npos) ? s : s.substr(comma + 1);
    while (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);

    int  day = 0, year = 0, hour = 0, minute = 0, sec = 0;
    char month_str[4] = {0};
    if (std::sscanf(rest.c_str(), "%d %3s %d %d:%d:%d",
                    &day, month_str, &year, &hour, &minute, &sec) < 6) {
        return 0;
    }

    static const std::pair<std::string_view, int> kMonths[] = {
        {"Jan", 1}, {"Feb", 2}, {"Mar", 3}, {"Apr", 4},
        {"May", 5}, {"Jun", 6}, {"Jul", 7}, {"Aug", 8},
        {"Sep", 9}, {"Oct", 10}, {"Nov", 11}, {"Dec", 12},
    };
    int month = 0;
    for (const auto& [name, m] : kMonths) {
        if (name == month_str) { month = m; break; }
    }
    if (month == 0) return 0;

    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = minute;
    t.tm_sec  = sec;

#ifdef _WIN32
    return static_cast<std::int64_t>(_mkgmtime(&t));
#else
    return static_cast<std::int64_t>(timegm(&t));
#endif
}

std::string join_symbols(const std::vector<std::string>& symbols) {
    // Yahoo accepts comma-separated symbols verbatim (no per-symbol escaping
    // needed for the tickers we care about: A-Z, digits, dots).
    std::string out;
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        if (i) out.push_back(',');
        out += symbols[i];
    }
    return out;
}

// nlohmann's value() falls back to default on missing-key, but a present-but-
// null field still throws on get<T>(). These helpers normalise that into
// "absent" by checking is_number() / is_string() first.
bool extract_number(const nlohmann::json& obj, const char* key, double& out) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_number()) return false;
    out = it->get<double>();
    return true;
}

bool extract_int64(const nlohmann::json& obj, const char* key, std::int64_t& out) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_number()) return false;
    out = it->get<std::int64_t>();
    return true;
}

YahooQuoteResult parse_quote_body(const std::string& body) {
    YahooQuoteResult out;
    nlohmann::json   j;
    try {
        j = nlohmann::json::parse(body);
    } catch (const std::exception& e) {
        out.error = std::string{"json parse: "} + e.what();
        return out;
    }

    auto qr_it = j.find("quoteResponse");
    if (qr_it == j.end() || !qr_it->is_object()) {
        out.error = "missing quoteResponse object";
        return out;
    }

    auto err_it = qr_it->find("error");
    if (err_it != qr_it->end() && !err_it->is_null()) {
        out.error = "yahoo error: " + err_it->dump();
        return out;
    }

    auto result_it = qr_it->find("result");
    if (result_it == qr_it->end() || !result_it->is_array()) {
        out.error = "missing quoteResponse.result array";
        return out;
    }

    out.quotes.reserve(result_it->size());
    for (const auto& entry : *result_it) {
        YahooQuote q;
        if (auto sym = entry.find("symbol"); sym != entry.end() && sym->is_string()) {
            q.symbol = sym->get<std::string>();
        }
        if (q.symbol.empty()) continue; // unusable
        q.has_price      = extract_number(entry, "regularMarketPrice", q.price);
        q.has_change_pct = extract_number(entry, "regularMarketChangePercent", q.change_pct);
        extract_int64(entry, "regularMarketVolume", q.volume);
        out.quotes.push_back(std::move(q));
    }
    return out;
}

} // namespace

YahooQuoteResult YahooClient::quote(const std::vector<std::string>& symbols) {
    if (symbols.empty()) return {};

    for (int attempt = 0; attempt < 2; ++attempt) {
        if (crumb_.empty()) {
            auto a = auth_.fetch();
            if (!a.ok()) {
                YahooQuoteResult err;
                err.error = "auth: " + a.error;
                return err;
            }
            crumb_ = a.crumb;
        }

        std::string url = kQuoteUrl;
        url += "?symbols=";
        url += join_symbols(symbols);
        url += "&crumb=";
        url += http_.escape(crumb_);

        auto resp = http_.get(url, {"Accept: application/json"});
        if (!resp.transport_ok()) {
            YahooQuoteResult err;
            err.error = "transport: " + resp.error;
            return err;
        }

        // Crumb expired or rotated. Drop it and re-auth on the next pass.
        if (resp.status_code == 401 && attempt == 0) {
            crumb_.clear();
            continue;
        }

        if (!resp.ok()) {
            std::ostringstream os;
            os << "HTTP " << resp.status_code;
            YahooQuoteResult err;
            err.error = os.str();
            return err;
        }

        return parse_quote_body(resp.body);
    }

    YahooQuoteResult err;
    err.error = "auth: 401 after refresh";
    return err;
}

ChartResult YahooClient::chart(const std::string&  symbol,
                               std::int64_t        period1_seconds,
                               std::string_view    interval) {
    ChartResult out;

    // Yahoo requires period2 to be set explicitly now; omitting it is
    // interpreted as -1 and the endpoint returns 400.
    const auto         now      = std::chrono::system_clock::now();
    const std::int64_t now_secs =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    std::string url = kChartUrlFmt;
    url += http_.escape(symbol);
    url += "?period1=";
    url += std::to_string(period1_seconds);
    url += "&period2=";
    url += std::to_string(now_secs);
    url += "&interval=";
    url += std::string(interval);

    auto resp = http_.get(url, {"Accept: application/json"});
    if (!resp.transport_ok()) {
        out.error = "transport: " + resp.error;
        return out;
    }
    if (!resp.ok()) {
        out.error = "HTTP " + std::to_string(resp.status_code);
        return out;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(resp.body);
    } catch (const std::exception& e) {
        out.error = std::string{"json parse: "} + e.what();
        return out;
    }

    auto chart_it = j.find("chart");
    if (chart_it == j.end() || !chart_it->is_object()) {
        out.error = "missing chart object";
        return out;
    }

    auto err_it = chart_it->find("error");
    if (err_it != chart_it->end() && !err_it->is_null()) {
        out.error = "yahoo error: " + err_it->dump();
        return out;
    }

    auto result_it = chart_it->find("result");
    if (result_it == chart_it->end() || !result_it->is_array() || result_it->empty()) {
        out.error = "missing chart.result";
        return out;
    }

    const auto& first = (*result_it)[0];

    auto ts_it = first.find("timestamp");
    if (ts_it == first.end() || !ts_it->is_array()) {
        // No data points (Yahoo returns this for symbols outside trading
        // hours sometimes). Treat as empty success.
        return out;
    }

    auto ind_it = first.find("indicators");
    if (ind_it == first.end()) return out;
    auto quote_arr = ind_it->find("quote");
    if (quote_arr == ind_it->end() || !quote_arr->is_array() || quote_arr->empty()) return out;

    const auto& q       = (*quote_arr)[0];
    auto        close_it = q.find("close");
    if (close_it == q.end() || !close_it->is_array()) return out;

    const std::size_t n = std::min(ts_it->size(), close_it->size());
    out.points.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        ChartPoint p;
        p.t_ms = static_cast<std::int64_t>((*ts_it)[i].get<std::int64_t>()) * 1000;
        if ((*close_it)[i].is_number()) {
            p.close     = (*close_it)[i].get<double>();
            p.has_close = true;
        }
        out.points.push_back(p);
    }
    return out;
}

ScreenerResult YahooClient::screener(std::string_view scr_id,
                                     std::string_view region,
                                     int              count) {
    ScreenerResult out;
    if (count <= 0) count = 50;

    for (int attempt = 0; attempt < 2; ++attempt) {
        if (crumb_.empty()) {
            auto a = auth_.fetch();
            if (!a.ok()) {
                out.error = "auth: " + a.error;
                return out;
            }
            crumb_ = a.crumb;
        }

        std::string url = kScreenerUrl;
        url += "?formatted=false&lang=en-US&region=";
        url += http_.escape(std::string(region));
        url += "&scrIds=";
        url += http_.escape(std::string(scr_id));
        url += "&count=";
        url += std::to_string(count);
        url += "&corsDomain=finance.yahoo.com&crumb=";
        url += http_.escape(crumb_);

        auto resp = http_.get(url, {"Accept: application/json"});
        if (!resp.transport_ok()) {
            out.error = "transport: " + resp.error;
            return out;
        }

        if (resp.status_code == 401 && attempt == 0) {
            crumb_.clear();
            continue;
        }

        if (!resp.ok()) {
            out.error = "HTTP " + std::to_string(resp.status_code);
            return out;
        }

        nlohmann::json j;
        try {
            j = nlohmann::json::parse(resp.body);
        } catch (const std::exception& e) {
            out.error = std::string{"json parse: "} + e.what();
            return out;
        }

        auto fin_it = j.find("finance");
        if (fin_it == j.end() || !fin_it->is_object()) {
            out.error = "missing finance object";
            return out;
        }

        auto err_it = fin_it->find("error");
        if (err_it != fin_it->end() && !err_it->is_null()) {
            out.error = "yahoo error: " + err_it->dump();
            return out;
        }

        auto result_it = fin_it->find("result");
        if (result_it == fin_it->end() || !result_it->is_array() || result_it->empty()) {
            // No groups returned — treat as empty success.
            return out;
        }

        const auto& group = (*result_it)[0];
        auto        quotes_it = group.find("quotes");
        if (quotes_it == group.end() || !quotes_it->is_array()) {
            return out;
        }

        out.quotes.reserve(quotes_it->size());
        for (const auto& q : *quotes_it) {
            ScreenerQuote sq;
            if (auto it = q.find("symbol"); it != q.end() && it->is_string()) {
                sq.symbol = it->get<std::string>();
            }
            if (sq.symbol.empty()) continue;
            if (auto it = q.find("shortName"); it != q.end() && it->is_string()) {
                sq.short_name = it->get<std::string>();
            }
            extract_number(q, "regularMarketPrice", sq.price);
            sq.has_change_pct = extract_number(q, "regularMarketChangePercent", sq.change_pct);
            out.quotes.push_back(std::move(sq));
        }
        return out;
    }

    out.error = "auth: 401 after refresh";
    return out;
}

YahooNewsResult YahooClient::news(const std::string& symbol, int count) {
    YahooNewsResult out;

    if (count <= 0) count = 10;

    std::string url = kSearchUrl;
    url += "?q=";
    url += http_.escape(symbol);
    url += "&newsCount=";
    url += std::to_string(count);
    url += "&quotesCount=0&enableFuzzyQuery=false&enableNavLinks=false";

    auto resp = http_.get(url, {"Accept: application/json"});
    if (!resp.transport_ok()) {
        out.error = "transport: " + resp.error;
        return out;
    }
    if (!resp.ok()) {
        out.error = "HTTP " + std::to_string(resp.status_code);
        return out;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(resp.body);
    } catch (const std::exception& e) {
        out.error = std::string{"json parse: "} + e.what();
        return out;
    }

    auto news_it = j.find("news");
    if (news_it == j.end() || !news_it->is_array()) {
        // No news for this symbol — treat as empty success.
        return out;
    }

    out.items.reserve(news_it->size());
    for (const auto& entry : *news_it) {
        YahooNewsItem item;

        if (auto it = entry.find("uuid"); it != entry.end() && it->is_string()) {
            item.id = it->get<std::string>();
        }
        if (auto it = entry.find("title"); it != entry.end() && it->is_string()) {
            item.title = it->get<std::string>();
        }
        if (auto it = entry.find("publisher"); it != entry.end() && it->is_string()) {
            item.publisher = it->get<std::string>();
        }
        if (auto it = entry.find("link"); it != entry.end() && it->is_string()) {
            item.link = it->get<std::string>();
        }
        if (auto it = entry.find("providerPublishTime");
            it != entry.end() && it->is_number()) {
            item.published_t_ms = it->get<std::int64_t>() * 1000;
        }

        // Drop items we can't display or correlate.
        if (item.title.empty() || item.published_t_ms == 0) continue;
        out.items.push_back(std::move(item));
    }
    return out;
}

YahooNewsResult YahooClient::news_rss(const std::string& symbol,
                                      std::string_view   region,
                                      std::string_view   lang,
                                      int                count) {
    YahooNewsResult out;
    if (count <= 0) count = 10;

    std::string url = kRssUrl;
    url += "?s=";
    url += http_.escape(symbol);
    url += "&region=";
    url += http_.escape(std::string(region));
    url += "&lang=";
    url += http_.escape(std::string(lang));

    auto resp = http_.get(url, {"Accept: application/rss+xml,application/xml,text/xml"});
    if (!resp.transport_ok()) {
        out.error = "transport: " + resp.error;
        return out;
    }
    if (!resp.ok()) {
        out.error = "HTTP " + std::to_string(resp.status_code);
        return out;
    }

    constexpr std::string_view kItemOpen  = "<item>";
    constexpr std::string_view kItemClose = "</item>";

    std::size_t pos       = 0;
    int         collected = 0;
    while (collected < count) {
        auto p1 = resp.body.find(kItemOpen, pos);
        if (p1 == std::string::npos) break;
        auto p2 = resp.body.find(kItemClose, p1);
        if (p2 == std::string::npos) break;

        const std::string item_xml =
            resp.body.substr(p1, p2 + kItemClose.size() - p1);
        pos = p2 + kItemClose.size();

        const std::string title   = extract_first_tag(item_xml, "title");
        const std::string pubdate = extract_first_tag(item_xml, "pubDate");
        const std::string link    = extract_first_tag(item_xml, "link");
        const std::string guid    = extract_first_tag(item_xml, "guid");

        if (title.empty()) continue;

        YahooNewsItem item;
        item.title          = title;
        item.link           = link;
        item.id             = guid.empty() ? link : guid;
        item.published_t_ms = parse_rfc822(pubdate) * 1000;
        // RSS feed has no publisher field; leave blank.
        item.publisher = "";

        if (item.id.empty() || item.published_t_ms == 0) continue;
        out.items.push_back(std::move(item));
        ++collected;
    }
    return out;
}

} // namespace td
