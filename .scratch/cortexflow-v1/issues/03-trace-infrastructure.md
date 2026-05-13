# Trace infrastructure: six levels, compile-time selection, pluggable sink

Status: ready-for-agent
PRD: `docs/prd.md` — Trace subsystem; user stories 17, 18, 19, 56

## What to build

A six-level trace hierarchy (`OFF` / `ERROR` / `WARN` / `INFO` / `DISPATCH` / `FULL`) selected at compile time via a build-flag-driven constant. Trace points use `if constexpr` against that constant so calls above the selected level compile to no-ops the optimizer drops. A pluggable sink interface receives formatted trace records; the default host sink writes to stderr. The CMake flag `FRAMEWORK_TRACE_LEVEL` selects the level.

CI runs every test with `FRAMEWORK_TRACE_LEVEL=FULL` so that all trace points stay syntactically valid regardless of the default build level. Default coverage by level: `DISPATCH` traces each dispatched envelope (`from → to`, type name); `FULL` adds cache writes, state transitions, and timer arm/cancel/fire.

## Acceptance criteria

- [ ] Six-level enum and a compile-time `kTraceLevel` constant driven by the CMake flag
- [ ] `TRACE_*(level, ...)` macros use `if constexpr (level <= kTraceLevel)`; below-threshold calls expand to nothing
- [ ] Pluggable sink interface; default host sink to stderr; one-line format documented in code comments (timestamp, level, kind, from, to, type name, key fields)
- [ ] `FRAMEWORK_TRACE_LEVEL={OFF,ERROR,WARN,INFO,DISPATCH,FULL}` CMake option wired up
- [ ] CI job runs the full test suite with `TRACE_FULL` enabled
- [ ] Unit test that `TRACE_FULL(...)` at level `OFF` produces no symbol references in optimized output

## Blocked by

None — can start immediately.
