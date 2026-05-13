# Clock interface + `SteadyClock` + `ManualClock` + runtime injection

Status: ready-for-agent
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
