# Fault path: `FRAMEWORK_ASSERT` + weak-linked platform handler

Status: ready-for-human
PRD: `docs/prd.md` — Fault subsystem; user stories 49, 50, 51

## What to build

A single `FRAMEWORK_ASSERT(cond, reason)` macro that, on false condition, calls `[[noreturn]] fault(reason)`. `fault(...)` invokes a weak-linked platform handler so bare-metal builds can override behavior (typically: disable interrupts, log, reset). The default host handler aborts after writing the reason and source location through the trace sink.

Every framework-level error is a system invariant violation routed through this one channel — no error codes thread through `send`/`post`/`subscribe` APIs.

## Acceptance criteria

- [ ] `FRAMEWORK_ASSERT(cond, reason)` macro evaluates `cond` exactly once; reason is a `string_view`-compatible literal
- [ ] `[[noreturn]] fault(reason, source_location)` is the single sink for all assertion failures
- [ ] Platform handler is weak-linked; default host handler aborts and writes reason + location
- [ ] Override mechanism is demonstrated by a unit test that supplies an alternate strong symbol and observes it called
- [ ] Unit test that an assert in normal code triggers `fault(...)` and never returns
- [ ] Macro compiles to a single branch in optimized builds (no string allocation on the hot path)

## Blocked by

None — can start immediately.

## Comments

### 2026-05-13 — from sandcastle agent

**Built:**

- `include/framework/assert.hpp` — `FRAMEWORK_ASSERT(cond, reason)` macro and `[[noreturn]] fault()` declaration. The macro expands to a ternary that evaluates `cond` exactly once and compiles to a single conditional branch with no allocations on the hot path.
- `src/framework/fault.cpp` — `fault()` implementation that dispatches to `platform_fault_handler`. The default handler is `__attribute__((weak))` with `extern "C"` linkage; it writes reason + source location to stderr and calls `std::abort()`. Bare-metal builds override by providing a strong symbol with the same signature.
- `tests/unit/test_assert.cpp` — Six tests: true/false paths, single-evaluation for both branches, source location correctness, never-returns verification, and weak-link override demonstration. Override test supplies a strong `platform_fault_handler` that captures args and `longjmp`s back to the test harness.
- Promoted the `framework` CMake target from `INTERFACE` to `STATIC` (first compiled translation unit). All existing tests still pass.

**Notes for reviewer:**

- The default host handler writes directly to stderr rather than through the trace sink (issue 03 is not built yet). When trace infrastructure lands, the default handler should route through it.
- `fault()` ends with `__builtin_unreachable()` after calling the platform handler, since the handler is not marked `[[noreturn]]` (to allow test overrides that `longjmp`). In production the handler must not return.
- `longjmp`-based testing of `[[noreturn]]` functions is technically UB in C++ but is the standard approach without exceptions (`-fno-exceptions`). Works on GCC and Clang, which are the supported compilers.
