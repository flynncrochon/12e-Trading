#include "NewsService.h"

#include "Logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <sstream>
#include <unordered_set>

namespace td {

namespace {

std::int64_t now_unix_seconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

NewsService::DailySeries to_series(const ChartResult& r) {
    NewsService::DailySeries s;
    s.points.reserve(r.points.size());
    for (const auto& p : r.points) {
        if (!p.has_close) continue;
        s.points.emplace_back(p.t_ms, p.close);
    }
    std::sort(s.points.begin(), s.points.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return s;
}

// Top 50-ish ASX names by market cap. The list is intentionally manual and
// hardcoded — Yahoo's day_losers screener returns nothing for region=AU,
// so we fetch quotes for these tickers in one call and rank by today's
// change ourselves. Re-broaden later if we need deeper loser coverage.
constexpr const char* kAsxUniverse[] = {
    "BHP.AX", "CBA.AX", "CSL.AX", "NAB.AX", "WBC.AX", "ANZ.AX", "FMG.AX", "RIO.AX",
    "WES.AX", "MQG.AX", "TLS.AX", "WOW.AX", "GMG.AX", "TCL.AX", "WTC.AX", "ALL.AX",
    "QBE.AX", "STO.AX", "REA.AX", "SCG.AX", "WDS.AX", "JHX.AX", "COL.AX", "S32.AX",
    "AMC.AX", "RMD.AX", "SUN.AX", "ORG.AX", "IAG.AX", "QAN.AX", "ASX.AX", "MIN.AX",
    "PLS.AX", "LYC.AX", "AGL.AX", "MGR.AX", "VCX.AX", "TPG.AX", "DXS.AX", "ALD.AX",
    "CPU.AX", "EVN.AX", "AZJ.AX", "REH.AX", "BSL.AX", "ORI.AX", "SOL.AX", "NHF.AX",
    "SHL.AX", "NST.AX",
};

} // namespace

bool NewsService::DailySeries::close_before(std::int64_t t_ms, double& out) const {
    auto it = std::lower_bound(
        points.begin(), points.end(), t_ms,
        [](const auto& p, std::int64_t v) { return p.first < v; });
    if (it == points.begin()) return false;
    --it;
    out = it->second;
    return true;
}

bool NewsService::DailySeries::close_after(std::int64_t t_ms, double& out) const {
    auto it = std::upper_bound(
        points.begin(), points.end(), t_ms,
        [](std::int64_t v, const auto& p) { return v < p.first; });
    if (it == points.end()) return false;
    out = it->second;
    return true;
}

NewsService::NewsService(EventChannel& channel,
                         int           lookback_days,
                         int           news_per_symbol,
                         int           losers_per_region)
    : channel_(channel),
      lookback_days_(lookback_days),
      news_per_symbol_(news_per_symbol),
      losers_per_region_(losers_per_region) {}

void NewsService::start() {
    if (running_.load(std::memory_order_acquire)) return;
    stop_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { run(); });
}

void NewsService::stop_and_join() {
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    running_.store(false, std::memory_order_release);
}

std::vector<std::string> NewsService::benchmark_chain_for(const std::string& ticker) {
    // ASX tickers end in ".AX" — score against the S&P/ASX 200 index, with
    // STW (SPDR S&P/ASX 200) and IOZ (iShares Core S&P/ASX 200) as ETF
    // fallbacks in case the index quote is unavailable on Yahoo. US tickers
    // score against SPY only.
    if (ticker.size() >= 3 && ticker.compare(ticker.size() - 3, 3, ".AX") == 0) {
        return {"^AXJO", "STW.AX", "IOZ.AX"};
    }
    return {"SPY"};
}

const NewsService::DailySeries*
NewsService::benchmark_series(const std::string& bench_ticker,
                              std::int64_t       period1_seconds) {
    auto it = benchmark_cache_.find(bench_ticker);
    if (it != benchmark_cache_.end()) return &it->second;

    auto r = client_.chart(bench_ticker, period1_seconds, "1d");
    if (!r.ok()) {
        std::ostringstream os;
        os << "news: benchmark " << bench_ticker << ": " << r.error;
        Logger::instance().warn(os.str());
        return &benchmark_cache_.emplace(bench_ticker, DailySeries{}).first->second;
    }
    return &benchmark_cache_.emplace(bench_ticker, to_series(r)).first->second;
}

void NewsService::collect_losers(std::string_view region, std::vector<LoserStock>& out) {
    auto r = client_.screener("day_losers", region, losers_per_region_);
    if (!r.ok()) {
        std::ostringstream os;
        os << "news: screener day_losers " << region << ": " << r.error;
        Logger::instance().warn(os.str());
        return;
    }
    out.reserve(out.size() + r.quotes.size());
    for (const auto& q : r.quotes) {
        if (!q.has_change_pct) continue;
        if (q.change_pct >= 0.0) continue;
        out.push_back({q.symbol, q.short_name, q.change_pct});
    }
}

void NewsService::collect_asx_losers(std::vector<LoserStock>& out) {
    std::vector<std::string> tickers;
    tickers.reserve(std::size(kAsxUniverse));
    for (const auto* t : kAsxUniverse) tickers.emplace_back(t);

    auto r = client_.quote(tickers);
    if (!r.ok()) {
        std::ostringstream os;
        os << "news: ASX quote failed: " << r.error;
        Logger::instance().warn(os.str());
        return;
    }

    // Pull just the negatives, then take the worst N.
    std::vector<LoserStock> ranked;
    ranked.reserve(r.quotes.size());
    for (const auto& q : r.quotes) {
        if (!q.has_change_pct || q.change_pct >= 0.0) continue;
        ranked.push_back({q.symbol, /*short_name=*/"", q.change_pct});
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const LoserStock& a, const LoserStock& b) {
                  return a.change_pct < b.change_pct;
              });
    if (static_cast<int>(ranked.size()) > losers_per_region_) {
        ranked.resize(losers_per_region_);
    }

    {
        std::ostringstream os;
        os << "news: ASX losers from quote rank: " << ranked.size();
        Logger::instance().info(os.str());
    }

    out.insert(out.end(), ranked.begin(), ranked.end());
}

void NewsService::run() {
    Logger::instance().info("NewsService: starting (market-wide losers)");

    const auto now_s   = now_unix_seconds();
    const auto period1 = now_s - static_cast<std::int64_t>(lookback_days_ + 5) * 86'400;
    const auto cutoff_ms =
        static_cast<std::int64_t>(now_s - static_cast<std::int64_t>(lookback_days_) * 86'400) * 1000;

    std::vector<LoserStock> losers;
    collect_losers("US", losers);
    if (stop_.load(std::memory_order_acquire)) { running_.store(false); return; }
    collect_asx_losers(losers);
    if (stop_.load(std::memory_order_acquire)) { running_.store(false); return; }

    // De-dup by ticker — Yahoo occasionally surfaces dual-listed names in
    // both regional pools.
    std::unordered_set<std::string> seen;
    losers.erase(
        std::remove_if(losers.begin(), losers.end(),
                       [&](const LoserStock& s) { return !seen.insert(s.ticker).second; }),
        losers.end());

    {
        std::ostringstream os;
        os << "news: " << losers.size() << " losers across regions";
        Logger::instance().info(os.str());
    }

    std::size_t emitted = 0;

    for (const auto& s : losers) {
        if (stop_.load(std::memory_order_acquire)) break;

        auto chart_r = client_.chart(s.ticker, period1, "1d");
        if (!chart_r.ok()) {
            std::ostringstream os;
            os << "news: chart " << s.ticker << ": " << chart_r.error;
            Logger::instance().warn(os.str());
            continue;
        }
        const DailySeries series = to_series(chart_r);
        if (series.points.empty()) continue;

        const auto         bench_chain  = benchmark_chain_for(s.ticker);
        const DailySeries* bench_series = nullptr;
        std::string        bench;
        for (const auto& candidate : bench_chain) {
            const auto* candidate_series = benchmark_series(candidate, period1);
            if (candidate_series && !candidate_series->points.empty()) {
                bench_series = candidate_series;
                bench        = candidate;
                break;
            }
        }
        if (!bench_series) {
            std::ostringstream os;
            os << "news: " << s.ticker << ": no benchmark in chain (";
            for (std::size_t i = 0; i < bench_chain.size(); ++i) {
                if (i) os << ", ";
                os << bench_chain[i];
            }
            os << ") returned data";
            Logger::instance().warn(os.str());
            continue;
        }

        // Yahoo's /v1/finance/search ignores the `.AX` suffix and returns
        // generic US news for ASX tickers, so route ASX through the RSS
        // feed (which is the only endpoint that respects the suffix).
        const bool is_asx = s.ticker.size() >= 3 &&
                            s.ticker.compare(s.ticker.size() - 3, 3, ".AX") == 0;
        auto news_r = is_asx
                          ? client_.news_rss(s.ticker, "AU", "en-AU", news_per_symbol_)
                          : client_.news(s.ticker, news_per_symbol_);
        if (!news_r.ok()) {
            std::ostringstream os;
            os << "news: feed " << s.ticker << ": " << news_r.error;
            Logger::instance().warn(os.str());
            continue;
        }

        std::size_t sym_emitted = 0;
        for (const auto& item : news_r.items) {
            if (item.published_t_ms < cutoff_ms) continue;

            double pre_stock  = 0.0;
            double post_stock = 0.0;
            if (!series.close_before(item.published_t_ms, pre_stock)) continue;
            if (!series.close_after (item.published_t_ms, post_stock)) continue;
            if (pre_stock <= 0.0) continue;

            double pre_bench  = 0.0;
            double post_bench = 0.0;
            if (!bench_series->close_before(item.published_t_ms, pre_bench)) continue;
            if (!bench_series->close_after (item.published_t_ms, post_bench)) continue;
            if (pre_bench <= 0.0) continue;

            const double stock_pct     = (post_stock  - pre_stock)  / pre_stock  * 100.0;
            const double benchmark_pct = (post_bench  - pre_bench)  / pre_bench  * 100.0;
            const double adjusted_pct  = stock_pct - benchmark_pct;

            nlohmann::json msg;
            msg["type"]              = "news:item";
            msg["ticker"]            = s.ticker;
            msg["short_name"]        = s.short_name;
            msg["news_id"]           = item.id;
            msg["title"]             = item.title;
            msg["publisher"]         = item.publisher;
            msg["link"]              = item.link;
            msg["published_t"]       = item.published_t_ms;
            msg["stock_pct"]         = stock_pct;
            msg["benchmark"]         = bench;
            msg["benchmark_pct"]     = benchmark_pct;
            msg["adjusted_pct"]      = adjusted_pct;
            msg["daily_change_pct"]  = s.change_pct;

            if (!channel_.send_json(msg.dump())) {
                std::ostringstream os;
                os << "news: send failed: " << channel_.last_error();
                Logger::instance().error(os.str());
                running_.store(false, std::memory_order_release);
                return;
            }

            sym_emitted += 1;
            emitted     += 1;
        }

        std::ostringstream os;
        os << "news: " << s.ticker << " (" << s.change_pct << "%): "
           << sym_emitted << " scored items";
        Logger::instance().info(os.str());
    }

    std::ostringstream os;
    os << "NewsService: done, " << emitted << " items emitted";
    Logger::instance().info(os.str());
    running_.store(false, std::memory_order_release);
}

} // namespace td
