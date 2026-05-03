#include "YahooClient.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <sstream>

namespace td {

namespace {

constexpr const char* kQuoteUrl    = "https://query1.finance.yahoo.com/v7/finance/quote";
constexpr const char* kChartUrlFmt = "https://query1.finance.yahoo.com/v8/finance/chart/";

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
        q.has_price = extract_number(entry, "regularMarketPrice", q.price);
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

} // namespace td
