# `TimerService` + `Timer` state-local + arm/cancel/expire-to-message

Status: ready-for-agent
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
