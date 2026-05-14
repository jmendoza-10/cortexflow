# Clock interface + `SteadyClock` + `ManualClock` + runtime injection

Status: ready-for-human
PRD: `docs/prd.md` — Time subsystem; user stories 42, 43

## What to build

The `Clock` interface (`now()` returning a monotonic duration type) and two implementations: `SteadyClock` wrapping `std::chrono::steady_clock` for production, and `ManualClock` for tests. The clock is injected at runtime construction so a single composition can swap clocks per build / per test.

`ManualClock::advance(duration)` moves time forward and (in the next slice) fires due timers as posted messages. In this slice the clock stands alone — `advance` is callable, `now()` returns the advanced time, no timer integration yet.

## Acceptance criteria

- [ ] `Clock` abstract interface with `now()`
- [ ] `SteadyClock` implementation backed by `std::chrono::steady_clock`
- [ ] `ManualClock` implementation; `advance(duration)` moves `now()` forward
- [ ] Runtime constructor accepts a clock reference and stores it for later subsystems
- [ ] `ManualClock::now()` is deterministic across reads with no intervening `advance`
- [ ] Asserting on backward-advance (negative duration) via `CORTEXFLOW_ASSERT`
- [ ] Unit tests for `ManualClock` determinism and `SteadyClock` monotonicity

## Blocked by

- `02-cortexflow-assert-fault-path.md`

## Comments

Built (2026-05-14):

- `include/cortexflow/clock.hpp` declares `Clock` as an abstract base with a
  protected default destructor (matches the `MessageAllocator` pattern; the
  runtime only ever holds a reference, never deletes through `Clock*`).
  `Clock::duration` is `std::chrono::nanoseconds` so the same return type
  works for steady-clock conversions and for `ManualClock`'s zero-based
  bookkeeping. Copy / move are deleted on the base — clocks are non-owning
  references throughout the runtime.
- `SteadyClock::now()` is `std::chrono::duration_cast<nanoseconds>(steady_clock::now().time_since_epoch())`.
- `ManualClock` stores a single `duration`; `advance(delta)` calls
  `CORTEXFLOW_ASSERT(delta.count() >= 0, …)` before incrementing. On a
  failing assert the clock does not move — the test verifies that.
- Runtime now has two constructors:
  - `Runtime()` — default-constructs and binds to a process-wide
    `SteadyClock` (function-local static, never destroyed) so the existing
    `Runtime app; app.start();` test pattern keeps working without changes.
  - `explicit Runtime(Clock&)` — stores the supplied reference for later
    subsystems. `runtime.clock()` exposes it; it is valid for the entire
    Runtime lifetime, not gated on `start()`/`shutdown()` so subsystems
    constructed eagerly can already read it.
- Unit tests (`tests/unit/test_clock.cpp`):
  - ManualClock starts at zero; consecutive reads without `advance` agree.
  - `advance` accumulates; zero is a no-op; an initial-offset constructor
    is honoured.
  - Negative-delta `advance` faults through the `platform_fault_handler`
    override (setjmp/longjmp pattern lifted from `test_assert.cpp`) and
    leaves `now()` unchanged.
  - SteadyClock monotonicity over 10k reads + a sleep-spanning forward
    progress check.
  - Runtime constructor stores the injected clock and `clock()` returns
    it (pointer-equality via the abstract base) before, during, and after
    start()/shutdown(). A second case verifies the default constructor
    yields a working SteadyClock.

Notes for the reviewer:

- I kept the default no-arg `Runtime()` constructor on purpose: every
  existing test (`test_runtime`, `test_cache`, etc.) uses it, and rewriting
  them was out of scope. If you'd prefer the constructor to be
  unconditionally `explicit Runtime(Clock&)`, holler — it's a mechanical
  rename across the test suite.
- `Clock::duration` is `std::chrono::nanoseconds` rather than
  `steady_clock::duration` so the type is identical on every platform and
  can be written into headers without dragging in `<chrono>`'s
  implementation-defined tick type. If the timer service in issue 12
  needs a different precision, this is the place to change.
- The test composition uses a tiny `DummyModule` (empty `Inbox`) rather
  than `ModuleList<>`. Instantiating `Runtime<ModuleList<>, …>` exposes a
  pre-existing `-Wunused-but-set-variable` warning in `dispatch_one` (the
  `try_one` lambda is unused when the fold-expression is empty). I chose
  not to scope-creep into fixing that warning here.
- No timer integration yet — `ManualClock::advance` only moves `now()`.
  Issue 12 picks up the timer service that consumes this clock.

— from sandcastle agent
