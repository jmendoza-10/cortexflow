#pragma once

#include <chrono>

namespace cortexflow {

// Monotonic clock interface injected into the runtime. The runtime stores a
// reference passed at construction; subsystems such as the timer service
// consult it via `runtime.clock()`. Two concrete implementations ship in the
// core: `SteadyClock` for production, `ManualClock` for tests.
//
// `now()` returns a duration since an implementation-defined epoch. Values are
// monotonic — consecutive reads never decrease.
//
// `install_advance_handler` is the hook the timer service uses to learn that
// time has progressed. Production clocks (real wall time) have no synthetic
// advance event and the default no-op override is correct. Test clocks
// (`ManualClock`) override it so a call to `advance()` fans out into the
// timer service synchronously — the test-determinism path that
// `ManualClock::advance(duration)` fires due timers depends on this hook.
class Clock {
public:
    using duration = std::chrono::nanoseconds;
    using AdvanceHandler = void (*)(void* ctx);

    virtual duration now() const noexcept = 0;

    virtual void install_advance_handler(AdvanceHandler /*fn*/,
                                         void* /*ctx*/) noexcept {}

    Clock() = default;
    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) = delete;
    Clock(Clock&&) = delete;
    Clock& operator=(Clock&&) = delete;

protected:
    ~Clock() = default;
};

// Production clock backed by `std::chrono::steady_clock`.
class SteadyClock final : public Clock {
public:
    duration now() const noexcept override;
};

// Test clock. `now()` starts at zero and only changes when `advance()` is
// called. `advance(d)` asserts via `CORTEXFLOW_ASSERT` if `d` is negative.
// After updating `now_`, `advance` invokes the installed advance handler so
// the timer service can fire due timers in the same call.
class ManualClock final : public Clock {
public:
    ManualClock() = default;
    explicit ManualClock(duration initial) noexcept : now_(initial) {}

    duration now() const noexcept override;

    void install_advance_handler(AdvanceHandler fn, void* ctx) noexcept override;

    void advance(duration delta);

private:
    duration now_{duration::zero()};
    AdvanceHandler advance_handler_ = nullptr;
    void* advance_ctx_ = nullptr;
};

} // namespace cortexflow
