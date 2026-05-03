#include "Config.h"

namespace td {

Config& Config::instance() {
    static Config inst;
    return inst;
}

std::chrono::microseconds Config::loop_slice() const noexcept {
    if (ticks_per_second_ == 0) {
        return std::chrono::microseconds{100'000};
    }
    return std::chrono::microseconds{1'000'000 / ticks_per_second_};
}

void Config::reset_for_tests() {
    auto& self = instance();
    self.ticks_per_second_ = 10;
    self.mock_volatility_  = 0.20;
}

} // namespace td
