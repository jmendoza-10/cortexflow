#pragma once

// Host platform backend — typedef-swap surface (PRD §Platform portability,
// user stories 44-45, 48; issue 16). This header is selected by
// `cmake/targets/host.cmake` adding `platform/host/` to the public include
// path. Module / composition code includes `<cortexflow/platform.hpp>` and
// resolves to whichever backend the build target chose.
//
// Each backend exposes the same three names (`Allocator`, `TimerBackend`,
// `TraceSink`) so a composition that names `platform::Allocator` etc. compiles
// unchanged across targets. Adding a new platform means writing a new backend
// tree and a target file — no core changes (architecture §15, PRD US 48).

#include <cortexflow/clock.hpp>
#include <cortexflow/messaging.hpp>

namespace cortexflow {
namespace platform {

// Allocator: returns a `MessageAllocator` subclass. Host backend uses the
// C++ global `::operator new` / `::operator delete` family guarded by a
// `std::mutex` (architecture §6.5).
using Allocator = HeapAllocator;

// TimerBackend: the monotonic clock the runtime's `TimerService` consults.
// Host backend uses `std::chrono::steady_clock`. ManualClock is provided
// alongside in core for tests regardless of target.
using TimerBackend = SteadyClock;

// TraceSink: tag struct documenting which sink implementation is linked.
// The actual dispatch goes through the weak `platform_trace_sink` symbol
// (architecture §12.3); the typedef gives compositions and contributor docs
// a stable name to reference. Default: fprintf(stderr, ...) with timestamps.
struct HostTraceSink {};
using TraceSink = HostTraceSink;

} // namespace platform
} // namespace cortexflow
