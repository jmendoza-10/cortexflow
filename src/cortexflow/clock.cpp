#include <cortexflow/clock.hpp>

#include <cortexflow/assert.hpp>

namespace cortexflow {

// `SteadyClock::now()` lives in the platform backend (`platform/<target>/
// steady_clock.cpp`) — host uses `std::chrono::steady_clock`, POSIX uses
// `clock_gettime(CLOCK_MONOTONIC)` directly (architecture §9.3). Everything
// in this file is target-agnostic.

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
