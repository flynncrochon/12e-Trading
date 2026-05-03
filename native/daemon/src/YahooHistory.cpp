#include "YahooHistory.h"

#include "Logger.h"
#include "SymbolRegistry.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace td {

namespace {

std::int64_t now_unix_seconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

YahooHistory::YahooHistory(EventChannel& channel,
                           int           backfill_lookback_days,
                           int           summary_lookback_days,
                           int           month_ago_days)
    : channel_(channel),
      backfill_days_(backfill_lookback_days),
      summary_days_(summary_lookback_days),
      month_ago_days_(month_ago_days) {}

void YahooHistory::start() {
    if (running_.load(std::memory_order_acquire)) return;
    stop_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { run(); });
}

void YahooHistory::stop_and_join() {
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    running_.store(false, std::memory_order_release);
}

void YahooHistory::run() {
    Logger::instance().info("YahooHistory: starting backfill + summary");
    do_backfill();
    if (stop_.load(std::memory_order_acquire)) return;
    do_summary();
    Logger::instance().info("YahooHistory: done");
    running_.store(false, std::memory_order_release);
}

void YahooHistory::do_backfill() {
    const auto& syms    = SymbolRegistry::instance().entries();
    const auto  now_s   = now_unix_seconds();
    const auto  period1 = now_s - static_cast<std::int64_t>(backfill_days_) * 86'400;

    std::size_t total_points = 0;
    for (const auto& s : syms) {
        if (stop_.load(std::memory_order_acquire)) return;

        auto r = client_.chart(s.ticker, period1, "1m");
        if (!r.ok()) {
            std::ostringstream os;
            os << "history: " << s.ticker << ": " << r.error;
            Logger::instance().warn(os.str());
            continue;
        }

        nlohmann::json msg;
        msg["type"]      = "history:backfill";
        msg["symbol_id"] = s.id;
        auto& pts        = msg["points"] = nlohmann::json::array();
        for (const auto& p : r.points) {
            if (!p.has_close) continue;
            pts.push_back({{"t", p.t_ms}, {"price", p.close}});
        }

        if (!channel_.send_json(msg.dump())) {
            std::ostringstream os;
            os << "history: send failed: " << channel_.last_error();
            Logger::instance().error(os.str());
            return;
        }

        total_points += pts.size();
        std::ostringstream os;
        os << "history: " << s.ticker << ": sent " << pts.size() << " points";
        Logger::instance().info(os.str());
    }

    std::ostringstream os;
    os << "history: backfill complete, " << total_points << " total points";
    Logger::instance().info(os.str());
}

void YahooHistory::do_summary() {
    const auto& syms    = SymbolRegistry::instance().entries();
    const auto  now_s   = now_unix_seconds();
    const auto  period1 = now_s - static_cast<std::int64_t>(summary_days_) * 86'400;
    const auto  target_ms =
        static_cast<std::int64_t>(now_s - static_cast<std::int64_t>(month_ago_days_) * 86'400) * 1000;

    std::size_t emitted = 0;
    for (const auto& s : syms) {
        if (stop_.load(std::memory_order_acquire)) return;

        auto r = client_.chart(s.ticker, period1, "1d");
        if (!r.ok()) {
            std::ostringstream os;
            os << "summary: " << s.ticker << ": " << r.error;
            Logger::instance().warn(os.str());
            continue;
        }

        // Pick the daily close whose timestamp is nearest the target.
        const ChartPoint* best          = nullptr;
        std::int64_t      best_distance = std::numeric_limits<std::int64_t>::max();
        for (const auto& p : r.points) {
            if (!p.has_close) continue;
            const std::int64_t dist = std::llabs(p.t_ms - target_ms);
            if (dist < best_distance) {
                best          = &p;
                best_distance = dist;
            }
        }
        if (!best) continue;

        nlohmann::json msg;
        msg["type"]            = "summary:update";
        msg["symbol_id"]       = s.id;
        msg["month_ago_price"] = best->close;
        msg["month_ago_t"]     = best->t_ms;

        if (!channel_.send_json(msg.dump())) {
            std::ostringstream os;
            os << "summary: send failed: " << channel_.last_error();
            Logger::instance().error(os.str());
            return;
        }
        emitted += 1;
    }

    std::ostringstream os;
    os << "summary: complete, " << emitted << " summaries emitted";
    Logger::instance().info(os.str());
}

} // namespace td
