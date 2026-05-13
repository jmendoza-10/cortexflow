# `examples/minimal_app` + integration fixture + CI build

Status: ready-for-agent
PRD: `docs/prd.md` — Documentation / testing; user stories 57, 58

## What to build

A minimal composed system at `examples/minimal_app/` that serves two purposes simultaneously: a working reference for onboarding engineers (composition, modules, flowchart, cache, timer) and the **integration fixture** for end-to-end test scenarios.

Shape: two modules, one cache key, one flow with two states, one timer. Concretely (suggested, refine in implementation): a "Producer" module that owns a cache key (a counter), and a "Consumer" module that subscribes to it and runs a flow — `Idle` waits for a `KeyChanged`, transitions to `Processing` which arms a timer that fires a `Done` message back to `Producer`.

CI builds the example from day one against `FRAMEWORK_TARGET=host` and `posix`. Integration tests drive scenarios via `run_one()` + `ManualClock` against this exact composition.

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
