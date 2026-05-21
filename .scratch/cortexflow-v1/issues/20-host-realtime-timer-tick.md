# Host runtime: real-time timer tick so wall-clock-armed timers actually fire

Status: ready-for-agent
PRD: `docs/prd.md` — Time subsystem; user stories 42, 43 (extends the v1 clock work)

## Background — why this matters

Today, the production `SteadyClock` (`platform/host/steady_clock.cpp`,
`platform/posix/steady_clock.cpp`) is a *time-reader* only — `now()` returns
`std::chrono::steady_clock::now()` but `install_advance_handler` is a no-op.
The only path that ever calls `TimerService::fire_due()` is
`ManualClock::advance(...)`, which is test-only. So:

- Integration tests with `ManualClock::advance(kDebounceWindow)` work fine.
- The `examples/button_pipeline/button_pipeline` host binary stalls one
  step into any gesture: the Debouncer transitions to `CoolingDown` and
  arms `DebounceExpired` with `due_ms=5`, but the timer never fires, so
  subsequent `RawTransition` envelopes are ignored by `CoolingDown`'s
  handler forever. Reproduced empirically — see the trace output captured
  in the 2026-05-21 comment on `03-trace-infrastructure.md` (the
  `DISPATCH envelope` lines keep coming with no `timer_fire` or
  `cache_write` to follow).

`include/cortexflow/timer.hpp:104-105` calls this out explicitly:
"Production `SteadyClock` provides no advance event and therefore fires no
timers." That comment is correct but the gap needs filling for the example
binary (and any future host demo) to be useful interactively.

## What to build

A host-side mechanism that periodically — or, better, on next-expiry —
calls `TimerService::fire_due()` against actual wall-clock elapsed time, so
that timers armed via `arm<Target, Msg>` on the production `SteadyClock`
fire on schedule.

### Strongly preferred shape (no new thread)

Modify `Runtime::run` (`include/cortexflow/runtime.hpp:197`) so the
indefinite `cv_.wait(lock, predicate)` becomes a bounded
`cv_.wait_until(lock, deadline, predicate)` where:

- `deadline = now + max(min_tick_interval, next_pending_timer - now)`
- After the wake (whether by predicate-true or by deadline), call the
  clock's advance handler — i.e. `clock_->fire_due_via_handler()` or
  expose `TimerService::fire_due()` to the Runtime directly. Either is
  fine; the existing `fire_due_trampoline` (`timer.hpp:284`) is already
  the agreed indirection point.
- `min_tick_interval` is a small constant (proposed: 1 ms) so that even
  with no pending timer the run loop wakes occasionally and stays
  responsive to `stop()`. Tune-able by host build but not by composition.

This keeps everything single-threaded, preserves the existing CV-wakeup
path for foreign-thread `post`, and adds no synchronization primitives.

### What you'll need to thread

The Runtime currently holds the `Clock*` but does **not** hold a reference
to the `TimerService` directly — the timer service is owned per-module-state
or by the Runtime depending on construction (verify by reading
`runtime.hpp`'s constructor). Pick whichever of these is least invasive:

1. Expose `TimerService::next_due_at() -> std::optional<Clock::duration>` so
   the Runtime can compute the wait deadline. The heap already tracks the
   earliest entry — this is a one-line accessor on the top of the heap.
2. Have the Runtime call the trampoline blind every `min_tick_interval`;
   `fire_due()` is cheap when the heap is empty or no entry is due
   (compare top.due vs now; return if not due).

Option 2 is simpler; option 1 is more efficient and lets the loop sleep
through long quiet periods. Pick option 1 if it's a clean add.

### Don't do this

- **Don't add a separate ticker thread.** The runtime's queue is the
  serialisation point; a thread that called `fire_due` from outside the
  loop would need to either acquire `mutex_` and re-implement dispatch
  semantics, or post-into-self in a way that creates ordering hazards.
  Keep all firing on the run-loop thread.
- **Don't change `ManualClock` semantics.** Tests must continue to drive
  time exclusively via `advance(duration)`. The new path lives entirely
  in `SteadyClock` / `Runtime::run`. A `ManualClock`-backed Runtime should
  still behave identically — the `cv_.wait_until` deadline can be
  computed from `clock_->now()`, but `ManualClock::now()` doesn't tick on
  its own, so a deadline based on `now() + 1ms` will simply never elapse
  under `ManualClock` (which is the correct test behaviour).
- **Don't introduce a real-time scheduler abstraction.** This is a
  one-liner cadence + one accessor, not a new subsystem.

## Acceptance criteria

- [ ] Running `examples/button_pipeline/button_pipeline` interactively
      and typing `d` followed by a space (release) within a few seconds
      causes the Debouncer's `DebounceExpired` timer to fire (visible
      as a `FULL timer_fire button_pipeline::Debouncer ... DebounceExpired`
      line in stderr at `CORTEXFLOW_TRACE_LEVEL=FULL`), the Debouncer
      returns to `Settled`, and a *second* press a second later produces
      a `cache_write` + fanout for the new `pressed=false→true` edge
      (not silently swallowed by `CoolingDown`).
- [ ] Holding `d` for > 500 ms causes `ClickClassifier::LongPressExpired`
      to fire and `UiController::LongPress{}` to land — i.e. a full
      gesture completes on host without any test-only clock plumbing.
- [ ] A click + click within `kDoubleClickWindow` produces
      `UiController::DoubleClick{}` (the `AwaitingSecondClick →
      SecondPressed → Idle` branch completes on the trailing release's
      timer).
- [ ] Existing integration tests (`tests/integration/test_button_pipeline.cpp`,
      `test_button_pipeline_trace.cpp`, all `test_runtime.cpp` /
      `test_cache.cpp` etc.) continue to pass under `ManualClock` with
      no semantic change — `advance(duration)` remains the only way to
      move test time forward.
- [ ] A small unit or integration test for the new path: with a
      production-`SteadyClock` Runtime, arm a 50 ms timer, call `run()`
      on a background thread, and verify the timer fires within
      (say) 200 ms of arm — proving the run loop self-wakes without a
      foreign `post`. Use `std::this_thread::sleep_for` to give the
      run loop time to tick.
- [ ] `Runtime::stop()` continues to wake the loop promptly (the
      `cv_.wait_until` deadline must not delay shutdown more than
      `min_tick_interval`).

## Blocked by

None — depends only on `11-clock-manual-steady.md` and
`12-timer-service.md`, both already landed (status `ready-for-human` /
`merged`).

## Non-goals

- Not introducing per-target real-time backends for FreeRTOS or bare-metal
  here. Those targets have their own timing primitives (RTOS timers,
  SysTick, hardware timer ISRs) and will land in separate slices when
  the framework-side `Clock` interface is exercised on those platforms.
- Not lowering `min_tick_interval` below 1 ms in this slice; sub-ms
  cadence on host is a separate concern (CPU usage, kernel scheduler
  granularity).
- Not adding a "real-time-only" mode to `ManualClock`. They stay
  orthogonal.
