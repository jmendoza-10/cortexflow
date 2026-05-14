# `TimerService` + `Timer` state-local + arm/cancel/expire-to-message

Status: ready-for-human
PRD: `docs/prd.md` — Time subsystem; user stories 40, 41

## What to build

`TimerService` is a runtime-level facility (not a module) accessed via `runtime.timers()`. `Timer` is a value type intended for use as a state-local: construction calls `timers().arm(duration, msg, target)` synchronously (thread-safe, does not require a queue round-trip); destruction calls `cancel`. On expiry, the timer posts the supplied envelope into the main queue — there are no synchronous callbacks; the timer's effect is dispatched on the same thread as all other handlers.

For host targets the backend is a min-heap of due times keyed off `Clock::now()`. `ManualClock::advance(duration)` walks the heap and fires every timer whose due time is now ≤ current time. Timers armed during the firing of others do NOT fire in the same `advance` window (the heap snapshot is taken at the start of `advance`).

## Acceptance criteria

- [ ] `TimerService` accessible via `runtime.timers()`
- [ ] `Timer` value type: construction arms; destruction cancels
- [ ] `arm(duration, msg, target)` enqueues the timer in the backend and returns a handle the `Timer` owns
- [ ] Expiry posts a normal envelope via `runtime.post(...)`; observed via `run_one()`
- [ ] `ManualClock::advance(duration)` fires due timers
- [ ] Advance-window semantics: timers armed during firing of others do not fire in the same `advance`
- [ ] Integration test: state-local `Timer` arms, advances, observes message; destruction during arm cancels cleanly
- [ ] `arm` is callable from a foreign thread (mutex-protected) and from the main thread

## Blocked by

- `11-clock-manual-steady.md`
- `06-runtime-skeleton-lifecycle.md`

## Comments

### 2026-05-14 — from sandcastle agent

Implemented `TimerService` as a runtime-level facility and `Timer` as a
move-only RAII handle. Highlights:

- New header `include/cortexflow/timer.hpp`. `Timer` mirrors the
  `Subscription` pattern: untyped on message/target so it can sit in a
  state-locals struct without leaking template parameters.
- Backend is a `std::vector` heap of `(due, seq, Envelope)` entries, keyed
  off `Clock::now()`. Each `arm` inserts a unique seq into a `live_seqs_`
  set; cancel removes it. `fire_due` pops every entry whose due time has
  arrived and skips any whose seq is no longer live. This handles
  `Timer::cancel()` called after the timer fired without underflowing the
  live count — early versions of the implementation tracked
  `heap_.size() - cancelled_.size()` and tripped on this edge case.
- `Clock` gained a non-pure virtual `install_advance_handler(fn, ctx)`
  with a default no-op body. `ManualClock` overrides it; `TimerService`
  installs `fire_due` as the handler at its own construction. Result:
  `ManualClock::advance(d)` fires due timers synchronously; production
  `SteadyClock` does nothing here (no synthetic advance event in v1).
- Snapshot semantics ("timers armed during firing do NOT fire in the same
  advance window") fall out naturally: `fire_due` holds the mutex across
  the entire heap walk and posts only entries that were in the heap when
  the lock was acquired. The reentrant-arm unit test pins this down.
- Concurrency: every `arm`/`cancel`/`fire_due` call holds a single mutex.
  Envelopes are extracted under the lock and posted after release so the
  runtime queue mutex cannot deadlock against ours. Foreign-thread arm
  tests (50 producers, then 100 main + 100 foreign interleaved) exercise
  this path.
- Lifecycle: `Runtime::start()` binds the post sink on `TimerService`
  before any module `on_start()` so modules may arm timers in their
  start hook. `Runtime::shutdown()` calls `timers_.clear()` after modules
  destruct, dropping any remaining heap entries and unbinding the sink.
- `Timer arm(duration, Envelope)` is the non-templated path; a templated
  `arm<Target>(duration, Msg)` convenience wraps it. Unlike
  `Module::send`, neither version statically checks that `Target` declares
  a handler for `Msg` — the timer service is reachable from contexts
  outside a module (boundary modules, future free-standing handlers), so a
  `Target::Inbox` requirement would be over-tight. The dispatch-time
  invariant ("envelope addressed to module not in ModuleList") still
  guards against forging timers at non-existent modules.

Tests live in `tests/unit/test_timer.cpp` (one binary `test_timer`,
registered via `tests/CMakeLists.txt`). Coverage:

- Type-level move-only / inert default checks for `Timer`.
- Basic arm + ManualClock advance + run_one delivery.
- Zero-delay timer not synchronously posted; multiple-timer due ordering;
  partial-advance only fires the timers whose due times have arrived.
- Cancel via dtor, cancel via explicit `cancel()`, cancel of one of many.
- Move semantics: move-construct, move-from drop is a no-op, move-assign
  cancels the previously-held timer.
- Advance-window semantics under reentrant re-arm.
- Foreign-thread arm and main+foreign interleaving.
- Integration scenario: a `StateLocals` struct holds a Timer; construct
  → advance → observe → destruct branches and the "destruction during
  arm" branch both verified.
- Non-templated `arm(Envelope)` path.
- Negative-duration assert.
- Shutdown drops armed timers; restart sees a clean service.

All 15 ctest cases pass at default and `CORTEXFLOW_TRACE_LEVEL=FULL`.
`CORTEXFLOW_ENABLE_TSAN=ON` builds the binaries fine but every TSan
process — including pre-existing tests — fails immediately with
`personality(ADDR_NO_RANDOMIZE)` because the sandbox forbids it. Not
related to this change; flagging for the human reviewer.

Things to look at:

- The `install_advance_handler` virtual on `Clock` is the API expansion
  that lets `runtime.timers().fire_due()` ride `ManualClock::advance(...)`.
  An alternative is to expose a `TimerService::poll()` and have tests call
  it explicitly after `clock.advance(...)`. I went with the hook because
  the issue explicitly requires `ManualClock::advance(duration)` to fire
  due timers — re-evaluate if that surface feels intrusive.
- `TimerService::arm` does not statically check that `Target` declares a
  handler for `Msg` (see rationale above). If you'd prefer it to mirror
  `Module::send<>`, add a `static_assert` on `detail::tuple_contains_v`.
  I left it permissive to match the runtime's `post(Envelope)` surface.
- `live_seqs_` is an `std::unordered_set<std::size_t>`. For bare-metal
  the pool sizing story will need to be revisited (probably a fixed-size
  freelist sized at composition time, similar to `MaxSubscriptions`). Out
  of scope for this issue but worth noting for the platform port pass.
