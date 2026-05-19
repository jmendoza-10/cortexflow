// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
// Host backend: `SteadyClock::now()` backed by `std::chrono::steady_clock`.
// The class itself is declared in `<cortexflow/clock.hpp>` (core); the POSIX
// backend provides a sibling impl using `clock_gettime(CLOCK_MONOTONIC)`.

#include <cortexflow/clock.hpp>

#include <chrono>

namespace cortexflow {

Clock::duration SteadyClock::now() const noexcept {
    return std::chrono::duration_cast<Clock::duration>(
        std::chrono::steady_clock::now().time_since_epoch());
}

} // namespace cortexflow
