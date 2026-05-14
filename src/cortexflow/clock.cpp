#include <cortexflow/clock.hpp>

#include <cortexflow/assert.hpp>

#include <chrono>

namespace cortexflow {

Clock::duration SteadyClock::now() const noexcept {
    return std::chrono::duration_cast<Clock::duration>(
        std::chrono::steady_clock::now().time_since_epoch());
}

Clock::duration ManualClock::now() const noexcept {
    return now_;
}

void ManualClock::install_advance_handler(
    Clock::AdvanceHandler fn, void* ctx) noexcept {
    advance_handler_ = fn;
    advance_ctx_ = ctx;
}

void ManualClock::advance(Clock::duration delta) {
    CORTEXFLOW_ASSERT(delta.count() >= 0,
        "ManualClock::advance called with negative duration");
    now_ += delta;
    if (advance_handler_) {
        advance_handler_(advance_ctx_);
    }
}

} // namespace cortexflow
