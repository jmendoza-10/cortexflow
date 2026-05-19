// Copyright 2026 The CortexFlow Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

// POSIX platform backend — typedef-swap surface (PRD §Platform portability,
// user stories 44-45, 48; issue 16). Selected by `cmake/targets/posix.cmake`
// adding `platform/posix/` to the public include path. Module / composition
// code includes `<cortexflow/platform.hpp>` and resolves to whichever backend
// the build target chose.
//
// Each backend exposes the same three names (`Allocator`, `TimerBackend`,
// `TraceSink`) so a composition that names `platform::Allocator` etc. compiles
// unchanged across targets.

#include <cortexflow/clock.hpp>
#include <cortexflow/messaging.hpp>

namespace cortexflow {
namespace platform {

// Allocator: POSIX backend uses `posix_memalign` + `pthread_mutex_t` directly
// rather than the C++ stdlib wrappers used on the generic host target.
// Functionally equivalent — the choice demonstrates that the surface is the
// same while the underlying primitives can differ per target.
using Allocator = HeapAllocator;

// TimerBackend: monotonic clock consulted by `TimerService`. POSIX backend
// reads `clock_gettime(CLOCK_MONOTONIC)` directly (architecture §9.3).
using TimerBackend = SteadyClock;

// TraceSink: POSIX default emits one structured single-line record per call
// via `write(STDERR_FILENO, ...)` — signal-safe and AS-safe, unlike the
// host backend's `fprintf` path.
struct PosixTraceSink {};
using TraceSink = PosixTraceSink;

} // namespace platform
} // namespace cortexflow
