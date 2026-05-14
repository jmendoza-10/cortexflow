#pragma once

#include <chrono>

namespace cortexflow {

// Monotonic clock interface injected into the runtime. The runtime stores a
// reference passed at construction; future subsystems (timer service) consult
// it via `runtime.clock()`. Two concrete implementations ship in the core:
// `SteadyClock` for production, `ManualClock` for tests.
//
// `now()` returns a duration since an implementation-defined epoch. Values are
// monotonic — consecutive reads never decrease.
class Clock {
public:
    using duration = std::chrono::nanoseconds;

    virtual duration now() const noexcept = 0;

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
class ManualClock final : public Clock {
public:
    ManualClock() = default;
    explicit ManualClock(duration initial) noexcept : now_(initial) {}

    duration now() const noexcept override;

    void advance(duration delta);

private:
    duration now_{duration::zero()};
};

} // namespace cortexflow
