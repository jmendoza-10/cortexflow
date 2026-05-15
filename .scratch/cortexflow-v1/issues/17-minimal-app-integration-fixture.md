# `examples/minimal_app` + integration fixture + CI build

Status: ready-for-human
PRD: `docs/prd.md` — Documentation / testing; user stories 57, 58

## What to build

A minimal composed system at `examples/minimal_app/` that serves two purposes simultaneously: a working reference for onboarding engineers (composition, modules, flowchart, cache, timer) and the **integration fixture** for end-to-end test scenarios.

Shape: two modules, one cache key, one flow with two states, one timer. Concretely (suggested, refine in implementation): a "Producer" module that owns a cache key (a counter), and a "Consumer" module that subscribes to it and runs a flow — `Idle` waits for a `KeyChanged`, transitions to `Processing` which arms a timer that fires a `Done` message back to `Producer`.

CI builds the example from day one against `CORTEXFLOW_TARGET=host` and `posix`. Integration tests drive scenarios via `run_one()` + `ManualClock` against this exact composition.

## Acceptance criteria

- [ ] `examples/minimal_app/` directory with composition, two modules, one cache key, one flow, one timer
- [ ] CMake build target `minimal_app` built in CI under both `host` and `posix`
- [ ] At least 3 integration tests under `tests/integration/` driving scenarios against the minimal app
- [ ] README in `examples/minimal_app/` annotating each piece for an onboarding reader
- [ ] `main.cpp` is the canonical three-line form: `app.start(); app.run(); app.shutdown();`
- [ ] No `#ifdef`s in the example app's module code

## Blocked by

- `06-runtime-skeleton-lifecycle.md`
- `09-cache-slots-fanout.md`
- `12-timer-service.md`
- `15-flow-lifecycle.md`

## Comments

### 2026-05-15 — from sandcastle agent

**What I built**

- `examples/minimal_app/` with the composition (`app.hpp`/`app.cpp`),
  `Producer`/`Consumer` modules under `modules/`, one cache key (`Counter`),
  one two-state flow (`Idle` / `Processing`), one timer (armed in
  Processing's locals).
- `main.cpp` is the canonical three-line lifecycle (`start(); run();
  shutdown();`).
- The composition is split into `minimal_app_lib` (STATIC) and `minimal_app`
  (executable) so the integration test links the same object code the
  binary uses.
- `add_subdirectory(examples/minimal_app)` lives at top-level
  `CMakeLists.txt` (unconditional, so CI builds the example for both
  `host` and `posix` from day one).
- `tests/integration/test_minimal_app.cpp` carries five scenarios driven by
  `ManualClock::advance` + `run_one()`: start-time wakeup, single-cycle
  round-trip, 10-cycle invariant (`counter == acks + 1`), 50-iteration
  state-locals leak check, and a compile-time composition-shape assertion.
- `examples/minimal_app/README.md` walks an onboarding reader through every
  piece, includes the message-flow diagram, and links to the framework
  headers worth reading next.
- Extended the existing `no_target_ifdefs` CI guard in `tests/CMakeLists.txt`
  to also grep `examples/minimal_app/` — keeps the example honest.

**Things to note for review**

- I had to reorder `modules/consumer.hpp` so that `Idle` and `Processing`
  are defined *before* the `Consumer` class. With them forward-declared,
  `Flow<Consumer, StateList<Idle, Processing>>::kLocalsBufferSize` was
  silently sized to the empty-fallback type (1 byte), and the first
  transition wrote `Idle::Locals` (with a `Subscription`) into a 1-byte
  buffer — the integration test failed with `subscriber_count == 0`
  before this fix. The README's "House rules" section flags this for
  future authors. Worth deciding whether `cortexflow::Flow` should
  static-assert against incomplete state-tag types to catch this at
  compile time.
- The runnable `minimal_app` binary blocks in `app.run()` once the
  Consumer transitions into Processing — `SteadyClock` has no advance
  event, so the timer never fires. Comment in `main.cpp` and a callout
  in the README explain this. Tests cover the moving parts via
  `ManualClock`.
- Cross-module access to `cache()` / `timers()` goes through a single
  static pointer published by `App`'s constructor (see
  `app.hpp::detail::g_runtime`). That's the simplest way to give state
  handlers (which are static functions) and `Producer.on(Bump)` a path
  to the runtime; the App's RAII makes the lifetime obvious. If you'd
  rather see explicit dependency injection here, this is the spot to
  refactor.
- One small test-harness change: `REQUIRE` had to become `CHECK` — the
  project compiles with `-fno-exceptions` and doctest's `REQUIRE`
  needs them.
- Verified locally: `host` and `posix` builds with `-DCORTEXFLOW_BUILD_TESTS=ON`,
  both with and without `-DCORTEXFLOW_TRACE_LEVEL=FULL`, both gcc and
  clang, all 20 ctest cases pass.
