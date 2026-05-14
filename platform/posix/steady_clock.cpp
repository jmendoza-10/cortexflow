// POSIX backend: `SteadyClock::now()` reads `clock_gettime(CLOCK_MONOTONIC)`
// directly. Architecture §9.3 names this as the canonical POSIX path; on
// glibc/musl `std::chrono::steady_clock` resolves to the same syscall so the
// observable behaviour matches the host backend. The choice demonstrates the
// typedef-swap mechanism.

#include <cortexflow/clock.hpp>

#include <time.h>

namespace cortexflow {

Clock::duration SteadyClock::now() const noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return std::chrono::seconds{ts.tv_sec} +
           std::chrono::nanoseconds{ts.tv_nsec};
}

} // namespace cortexflow
